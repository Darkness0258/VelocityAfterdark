"""Generate local authoring WAVs for the Afterdark data-driven dialogue catalog.

Synthesized files are development voice assets. Keep the line text original and
clear the generated voice's terms before any commercial release; use contracted
performers for final character performances.
"""

from __future__ import annotations

import argparse
import asyncio
import json
import shutil
import subprocess
import tempfile
from pathlib import Path

import edge_tts


ROOT = Path(__file__).resolve().parents[2]
CATALOG = ROOT / "Content/Data/Audio/voice_lines.json"
OUTPUT = ROOT / "Assets/VoiceSource"


async def synthesize(line: dict, ffmpeg: str) -> Path:
    OUTPUT.mkdir(parents=True, exist_ok=True)
    output_path = OUTPUT / f"{line['asset']}.wav"
    with tempfile.TemporaryDirectory(prefix="afterdark-voice-") as temporary:
        source_path = Path(temporary) / f"{line['asset']}.mp3"
        communication = edge_tts.Communicate(
            text=line["text"],
            voice=line["voice"],
            rate=line.get("rate", "+0%"),
            pitch=line.get("pitch", "+0Hz"),
        )
        await communication.save(str(source_path))
        if not source_path.is_file() or source_path.stat().st_size < 2048:
            raise RuntimeError(f"Speech service returned an empty response for {line['id']}.")

        filters = [
            "highpass=f=130",
            "lowpass=f=5600",
            "acompressor=threshold=-19dB:ratio=2.2:attack=6:release=90",
            "equalizer=f=2600:t=q:w=0.8:g=1.4",
            "loudnorm=I=-18:TP=-1.5:LRA=7",
        ]
        if not line.get("radioFilter", False):
            filters = ["acompressor=threshold=-18dB:ratio=2:attack=8:release=100", "loudnorm=I=-18:TP=-1.5:LRA=7"]
        subprocess.run(
            [
                ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-i", str(source_path),
                "-af", ",".join(filters), "-ar", "24000", "-ac", "1", "-c:a", "pcm_s16le",
                str(output_path),
            ],
            check=True,
        )
    if output_path.stat().st_size < 12000:
        raise RuntimeError(f"Generated WAV is unexpectedly short or empty: {output_path}")
    return output_path


async def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--line", help="Only generate the matching dialogue line ID")
    args = parser.parse_args()

    catalog = json.loads(CATALOG.read_text(encoding="utf-8"))
    if catalog.get("schemaVersion") != 1 or not isinstance(catalog.get("lines"), list):
        raise RuntimeError("Unsupported voice_lines.json schema.")
    lines = catalog["lines"]
    if args.line:
        lines = [line for line in lines if line.get("id") == args.line]
        if not lines:
            raise RuntimeError(f"No voice line matches ID {args.line!r}.")
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        raise RuntimeError("ffmpeg is required to produce the 24 kHz PCM WAV assets.")
    for line in lines:
        for required in ("id", "asset", "voice", "text"):
            if not isinstance(line.get(required), str) or not line[required].strip():
                raise RuntimeError(f"Voice line is missing required field {required!r}: {line}")
        path = await synthesize(line, ffmpeg)
        print(f"Generated {line['id']}: {path.relative_to(ROOT)} ({path.stat().st_size:,} bytes)")


if __name__ == "__main__":
    asyncio.run(main())
