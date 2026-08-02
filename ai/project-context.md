# VLC-whisper AI Project Context

## Mission

Build offline, real-time speech captions inside VLC for local media. The first deliverable is Windows 10/11 x64, one pinned VLC 3.x build, English local files, tiny.en CPU model, no GUI, no seeking. Playback reliability and privacy outrank caption completeness.

## Non-negotiables

- Author project code in C17. Third-party whisper.cpp is pinned and unmodified; call its public C API from the worker.
- The VLC module must be native C and is version-coupled to the selected VLC build. Do not invent a stable VLC plugin SDK/ABI.
- Never perform inference, pipe I/O, blocking wait, or unbounded allocation in VLC's audio callback.
- No network traffic at runtime: no HTTP/TCP listener, cloud service, telemetry, auto-update, or automatic model download.
- Audio/transcripts are in-memory only in MVP. Logs must not contain PCM, transcript text, full local paths, or authentication tokens.
- Windows IPC is authenticated current-user-only message-mode named pipe; Linux abstraction is Unix `SOCK_SEQPACKET`.
- Use media PTS in signed 64-bit microseconds; never synchronize captions using wall clock.
- Bounded queues: if overloaded, drop old audio and record it; never slow VLC.
- Seek/rate/title/source discontinuity is unsupported MVP behavior: clear captions and disable the caption session gracefully while VLC playback continues.

## Repository map

```text
/docs                 Product and engineering source of truth
/ai                   Instructions for AI contributors
/plugin               Native C VLC integration (capture, IPC client, presenter)
/worker               C worker host; links pinned whisper.cpp
/protocol             Frame/message definitions and golden fixtures
/models               Local GGML model files and offline manifest.json
/third_party          Pinned dependencies, preferably git submodules
/cmake                Toolchains, presets, helper modules
/tests                Unit, protocol, integration, e2e fixtures/harnesses
/scripts              Reproducible developer and CI commands
```

## Commands convention

Do not guess commands. Keep canonical commands in root `README.md` and `CMakePresets.json`, then update this file when introduced. Intended pattern:

```sh
cmake --preset windows-x64-release
cmake --build --preset windows-x64-release
ctest --preset windows-x64-release
```

Cross-compilation proves artifact creation, not VLC compatibility; run Windows integration tests against the pinned VLC installation.

## Design rules

- Prefer small explicit state machines, ownership documented in headers, fixed upper bounds, and error codes over implicit global behavior.
- All IPC parsers validate length before allocation, UTF-8 before render/log, protocol version, session ID, sequence, PTS range, and payload-specific invariants.
- Maintain plugin/worker protocol compatibility tests for every message change. Breaking framing/semantics requires a major protocol bump.
- Add a test with every bug fix. Avoid broad refactors combined with behavior changes.
- Treat captions as untrusted worker output: escape/sanitize for the chosen VLC renderer and cap length.

## Current open questions

1. Which exact VLC module interfaces reliably capture decoded PCM and render timed generated text on the pinned Windows VLC?
2. Can the supported distribution be out-of-tree, or must it be a minimal patch against a pinned VLC source build?
3. What is the measured reference-PC baseline for tiny.en and the intended 8s/2s windowing?

Resolve these with narrow proof-of-concept spikes and document results in `docs/decisions.md`; do not build speculative GUI/settings features first.
