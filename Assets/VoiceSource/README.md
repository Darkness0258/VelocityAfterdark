# Afterdark development voice assets

`VO_ArrivalBroadcast.wav` is a synthetic authoring pass for the opening arrival sequence. The dialogue is original game text; the synthetic voice is temporary development audio and is not a contracted actor performance. Review the voice service terms and clear the asset before commercial distribution. The game keeps subtitles active if the imported SoundWave is absent.

Regenerate from the project root with:

```powershell
python -m pip install -r Scripts/Audio/requirements.txt
python Scripts/Audio/generate_voiceover.py --line arrival_broadcast
```

Then run the Unreal content bootstrap to import the WAV into `/Game/Velocity/Audio/Voice`.
