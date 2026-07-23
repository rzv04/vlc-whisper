# Source Layout

## Purpose

This document makes the VLC-whisper implementation layout explicit. It is binding for contributors and AI agents unless an Architecture Decision Record (ADR) changes it.

The codebase is an ensemble: a native C17 VLC integration module, an isolated local transcription worker, and a shared C17 IPC protocol library. The plugin must not link Whisper or perform inference. The worker is the only component that links the pinned `whisper.cpp` dependency.

## Ownership Rules

| Area | Owns | Must not own |
|---|---|---|
| `plugin/` | VLC lifecycle, audio capture, bounded queues, worker supervision, caption presentation | Whisper inference, VAD decisions, persistent transcripts |
| `worker/` | IPC session handling, VAD, audio windows, Whisper inference, final caption segments | VLC callbacks, subtitle internals |
| `protocol/` | Versioned frames, encoding, decoding, validation, transport abstraction | VLC or Whisper APIs, application policy |
| `tests/` | Automated verification, fixtures, manual E2E procedure | Production implementation logic |

All project-authored source is C17. The pinned `whisper.cpp` dependency may contain C/C++, but project-owned plugin code remains C.

## Repository Tree

**The repository tree, file roles and naming are orientative and may wildly change; in that case, the AI agent/developer must update this document.**

```text
vlc-whisper/
├── plugin/
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── vw_plugin.h
│   │   ├── vw_session.h
│   │   ├── vw_audio_capture.h
│   │   ├── vw_caption_presenter.h
│   │   ├── vw_worker_client.h
│   │   ├── vw_queue.h
│   │   ├── vw_log.h
│   │   └── vw_platform.h
│   └── src/
│       ├── vlc_whisper_module.c
│       ├── vw_session.c
│       ├── vw_audio_capture.c
│       ├── vw_caption_presenter.c
│       ├── vw_worker_client_win32.c
│       ├── vw_queue.c
│       ├── vw_log.c
│       └── vw_platform_win32.c
├── worker/
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── vw_worker.h
│   │   ├── vw_whisper_engine.h
│   │   ├── vw_vad.h
│   │   ├── vw_segment_builder.h
│   │   ├── vw_audio_buffer.h
│   │   └── vw_worker_config.h
│   └── src/
│       ├── main.c
│       ├── vw_worker.c
│       ├── vw_whisper_engine.c
│       ├── vw_vad.c
│       ├── vw_segment_builder.c
│       ├── vw_audio_buffer.c
│       └── vw_worker_config.c
├── protocol/
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── vw_protocol.h
│   │   ├── vw_protocol_types.h
│   │   ├── vw_protocol_codec.h
│   │   └── vw_ipc_transport.h
│   └── src/
│       ├── vw_protocol_codec.c
│       ├── vw_protocol_validate.c
│       ├── vw_ipc_pipe_win32.c
│       └── vw_ipc_socket_linux.c
├── tests/
│   ├── CMakeLists.txt
│   ├── unit/
│   │   ├── test_protocol_codec.c
│   │   ├── test_protocol_validate.c
│   │   ├── test_queue.c
│   │   ├── test_segment_builder.c
│   │   └── test_caption_timing.c
│   ├── integration/
│   │   ├── test_worker_ipc.c
│   │   └── test_worker_lifecycle.c
│   ├── e2e/
│   │   └── test_local_video_playback.md
│   └── fixtures/
│       ├── spoken_english_16khz.wav
│       └── expected_segments.json
├── cmake/
│   ├── toolchains/mingw-w64-x86_64.cmake
│   ├── FindWhisperCpp.cmake
│   └── CompilerWarnings.cmake
├── third_party/whisper.cpp/
├── docs/source-layout.md
├── ai/project-context.md
├── ai/task-template.md
├── CMakeLists.txt
├── CMakePresets.json
├── AGENTS.md
└── README.md
```

## Plugin Files

| File | Responsibility |
|---|---|
| `vlc_whisper_module.c` | VLC module registration, activation/deactivation, module setup |
| `vw_session.c` | Caption session state: start, pause, resume, stop, discontinuity, failure |
| `vw_audio_capture.c` | Receive/normalize PCM and associate monotonic media PTS |
| `vw_queue.c` | Bounded audio producer-consumer queue and overload/drop policy |
| `vw_worker_client_win32.c` | Launch worker, named-pipe connection, handshake, send/receive, cleanup |
| `vw_caption_presenter.c` | Convert final worker segments into VLC caption cues |
| `vw_log.c` | Privacy-safe diagnostics; never log PCM/transcript by default |
| `vw_platform_win32.c` | Windows paths, handles, randomness, cleanup, timing helpers |

The VLC audio callback may only do bounded non-blocking work. It must never wait for the worker, infer, do blocking IPC, or allocate unbounded memory.

## Worker Files

| File | Responsibility |
|---|---|
| `main.c` | Parse arguments, initialize, run, and return meaningful exit codes |
| `vw_worker.c` | IPC event loop, protocol dispatch, worker state transitions |
| `vw_whisper_engine.c` | Whisper C API adapter, model load/unload, inference calls |
| `vw_vad.c` | Voice-activity detection state and window decisions |
| `vw_audio_buffer.c` | PCM accumulation, window extraction, overlap |
| `vw_segment_builder.c` | Ordered timed segments and overlap deduplication |
| `vw_worker_config.c` | Validate model path, initial `en` language, and safe defaults |

MVP supports CPU inference using `tiny.en`, one local playback session, and final-only segments.

## Protocol Files

Both plugin and worker build `protocol/`; it is the only location for framing, message identifiers, versioning, limits, validation, and transport interfaces.

```c
typedef enum vw_message_type {
    VW_MSG_HELLO = 1,
    VW_MSG_HELLO_ACK = 2,
    VW_MSG_START_SESSION = 3,
    VW_MSG_AUDIO_PCM = 4,
    VW_MSG_PAUSE = 5,
    VW_MSG_RESUME = 6,
    VW_MSG_STOP_SESSION = 7,
    VW_MSG_CAPTION_SEGMENT = 8,
    VW_MSG_STATUS = 9,
    VW_MSG_ERROR = 10,
    VW_MSG_SHUTDOWN = 11
} vw_message_type_t;
```

Frames carry protocol version, message type, session ID, payload size, and payload. All audio/caption timing uses media PTS in microseconds. See `docs/api-contracts.md` for the authoritative wire contract.

## Tests

- `tests/unit/`: codec/validation, queue policy, segment building, caption timing.
- `tests/integration/`: worker process, pipe handshake, lifecycle/errors/cleanup.
- `tests/e2e/`: repeatable manual test of the pinned VLC build and local English video.
- `tests/fixtures/`: legal, small, deterministic offline input data only.

## Implementation Order

1. Root CMake project, Windows cross-build preset, C17 warnings, pinned `whisper.cpp`.
2. Protocol headers, codec, validation, and unit tests.
3. No-inference worker: handshake and lifecycle commands.
4. Plugin worker launch, named-pipe client, and status logging.
5. Bounded PCM capture and timestamped `AUDIO` frames.
6. VAD, windows, and `tiny.en` inference.
7. Final caption delivery and VLC presentation.
8. Pause/resume and graceful discontinuity: clear captions, end caption session, preserve VLC playback.

## Change Rules

Before adding a source file, record its owner, one responsibility, public API/header where applicable, tests, callback/thread context, and whether it alters IPC, lifecycle, privacy, or platform assumptions.

Changes to accepted decisions, protocol framing, privacy rules, or the plugin/worker boundary require an ADR update first. Do not silently add cloud communication, transcript persistence, authored C++, unbounded queues, or seeking support.
