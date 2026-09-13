# Product Brief

VLC-Whisper adds generated captions to desktop VLC without uploading audio for transcription. A native VLC plugin captures decoded PCM and presents captions; an isolated local worker performs source decoding, VAD, Whisper inference, model provisioning, and optional finalized-text translation.

## Product priorities

1. **Playback safety:** caption failures may disable captions; they must not stall, glitch, or crash media playback.
2. **Truthful timing/state:** seek, pause/resume, media swap, EOF, overload, and worker failure must not produce stale or plausible-but-wrong captions.
3. **Local transcription:** PCM remains on-device. No cloud transcription or telemetry.
4. **Explicit network use:** model downloads require user action; translation is opt-in and sends finalized text only.
5. **Bounded behavior:** queues, retries, network deadlines, and worker restart policy are bounded.

## Current user flow

1. Install VLC-Whisper into a compatible VLC 3.x installation.
2. Play local media, network VoD, or a supported live/non-seekable source.
3. The plugin captures timestamped PCM without blocking VLC and starts an authenticated local worker session.
4. The worker transcribes locally and returns finalized timed cues; VLC renders them as generated captions.
5. Settings can select backend/model/language/threads and optionally enable translation or explicit model downloads.
6. Pause, seek, media replacement, end, and worker failure reset/clear the relevant caption epoch without interrupting playback.

## Privacy boundary

- PCM/audio never leaves the machine.
- Model downloads are worker-owned, user-initiated, HTTPS, catalog-limited, and SHA-256 verified.
- Translation is disabled by default; only finalized subtitle text is sent to Google Translate web endpoints under a bounded deadline.
- No network listener, cloud transcription, telemetry, remote logging, or automatic model download.
- Diagnostics must not contain tokens, credentials, PCM, or subtitle bodies.

## Engineering scope

Project-authored runtime code is C17. The plugin is coupled to a pinned VLC build/API surface; do not assume a stable generic VLC plugin ABI. The worker alone links pinned `whisper.cpp`. Linux is a supported development/build target; Windows x64 is the primary packaged target.

Future feature order is governed by `roadmap.md`. New UI/providers/subtitle workflows must preserve the same playback, lifecycle, privacy, and timeline invariants rather than bypassing the worker/plugin ownership model.
