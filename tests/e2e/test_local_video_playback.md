# Manual E2E Verification Procedure: Local Video Playback

## Pre-conditions
- Compatible Windows 10/11 x64 target machine.
- Pinned VLC 3.x installation.
- Built `vlc_whisper_plugin.dll` in VLC plugin directory.
- `vlc-whisper-worker.exe` and `models/ggml-tiny.en.bin` provisioned locally.

## Steps
1. Open local English MP4/MKV video in VLC.
2. Verify caption session starts automatically.
3. Verify timed captions render accurately without video jitter or audio glitches.
4. Pause and resume media; verify caption timing aligns correctly.
5. Seek forward or backward; verify captions clear and caption session disables gracefully without crashing VLC.
