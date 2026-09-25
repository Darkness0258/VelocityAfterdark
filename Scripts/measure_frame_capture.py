"""Summarize real Unreal CSV frame times; this does not certify a performance gate."""

import argparse
import csv
import json
import math
from pathlib import Path


def percentile(values, fraction):
    ordered = sorted(values)
    position = (len(ordered) - 1) * fraction
    low = math.floor(position)
    high = math.ceil(position)
    return ordered[low] + (ordered[high] - ordered[low]) * (position - low)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--warmup-seconds", type=float, default=10.0)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if not math.isfinite(args.warmup_seconds) or args.warmup_seconds < 0:
        parser.error("warmup must be finite and nonnegative")
    csv.field_size_limit(100_000_000)  # Unreal's EVENTS record can exceed the default.
    frames, elapsed, discarded = [], 0.0, 0
    timings = {key: [] for key in ("GameThreadTime", "RenderThreadTime", "RHIThreadTime", "GPUTime")}
    gpu_memory = []
    with args.capture.open(encoding="utf-8-sig", newline="") as source:
        reader = csv.DictReader(source)
        if not reader.fieldnames or "FrameTime" not in reader.fieldnames:
            parser.error("CSV has no FrameTime column; use a rendered Unreal CSV capture")
        for row in reader:
            try:
                duration = float(row["FrameTime"])
            except (ValueError, TypeError, KeyError):
                continue  # Unreal also writes event and trailing metadata records.
            if not math.isfinite(duration) or duration <= 0:
                continue
            elapsed += duration / 1000
            if elapsed < args.warmup_seconds:
                discarded += 1
                continue
            frames.append(duration)
            for key, samples in timings.items():
                try:
                    value = float(row.get(key, ""))
                except (ValueError, TypeError):
                    continue
                if math.isfinite(value) and value >= 0:
                    samples.append(value)
            try:
                used_mb = float(row.get("GPUMem/LocalUsedMB", ""))
                if math.isfinite(used_mb) and used_mb >= 0:
                    gpu_memory.append(used_mb)
            except (ValueError, TypeError):
                pass
    if not frames:
        parser.error("No measured frames after warmup; increase capture length")
    report = {
        "capture": str(args.capture),
        "scope": "Rendered frame-time sample; capture route/hardware must be recorded separately.",
        "warmupSeconds": args.warmup_seconds,
        "discardedFrames": discarded,
        "measuredFrames": len(frames),
        "measuredSeconds": round(sum(frames) / 1000, 3),
        "frameTimeMs": {
            "median": round(percentile(frames, .5), 3),
            "p95": round(percentile(frames, .95), 3),
            "p99": round(percentile(frames, .99), 3),
            "worst": round(max(frames), 3),
        },
        "averageFps": round(1000 * len(frames) / sum(frames), 2),
        "framesOver33_33ms": sum(x > 1000 / 30 for x in frames),
        "reportedTimingsMs": {
            key: {"median": round(percentile(values, .5), 3), "p95": round(percentile(values, .95), 3)}
            for key, values in timings.items() if values and max(values) > 0
        },
        "reportedLocalGpuMemoryPeakMB": round(max(gpu_memory), 3) if gpu_memory else None,
    }
    rendered = json.dumps(report, indent=2)
    print(rendered)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
