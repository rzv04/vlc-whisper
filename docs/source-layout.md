# Source Layout

## Purpose

This document makes the VLC-whisper implementation layout explicit. It is binding for contributors and AI agents unless an Architecture Decision Record (ADR) changes it.

The codebase is an ensemble: a native C17 VLC integration module, an isolated local transcription worker, and a shared C17 IPC protocol library. The plugin must not link Whisper or perform inference. The worker is the only component that links the pinned `whisper.cpp` dependency.

## Ownership Rules

| Area        | Owns                                                                                   | Must not own                                             |
| ----------- | -------------------------------------------------------------------------------------- | -------------------------------------------------------- |
| `plugin/`   | VLC lifecycle, audio capture, bounded queues, worker supervision, caption presentation, aggregate per-session benchmark reports | Whisper inference, VAD decisions, persistent transcripts |
| `worker/`   | IPC session handling, VAD, audio windows, Whisper inference, final caption segments, inference timing | VLC callbacks, subtitle internals                        |
| `protocol/` | Versioned frames, encoding, decoding, validation, transport abstraction                | VLC or Whisper APIs, application policy                  |
| `models/`   | Local GGML whisper model binary storage and model manifest validation                  | Model downloading over network at runtime                |
| `tests/`    | Automated verification, fixtures, manual E2E procedure                                 | Production implementation logic                          |

All project-authored source is C17. The pinned `whisper.cpp` dependency may contain C/C++, but project-owned plugin code remains C.

## Repository Tree

The repository tree is subject to change.

```text
vlc-whisper/
├── plugin/                                    # Native C17 VLC integration module
│   ├── CMakeLists.txt                         # Builds libvlc_whisper_plugin library
│   ├── include/
│   │   ├── vw_plugin.h                        # Core module structs, capabilities, and setup declarations
│   │   ├── vw_session.h                       # Playback session lifecycle & discontinuity state machine
│   │   ├── vw_audio_capture.h                 # Decoded PCM extraction & monotonic media PTS assignment
│   │   ├── vw_caption_presenter.h             # Translates transcript segments into VLC SPU/OSD caption cues with 1.0s reading floor
│   │   ├── vw_benchmark.h                     # Per-session benchmark counters, latency samples, and report lifecycle
│   │   ├── vw_worker_client.h                 # Authenticated IPC client, worker process supervisor
│   │   ├── vw_queue.h                         # Bounded realtime-safe SPSC audio queue declarations
│   │   └── vw_platform.h                      # OS abstraction: CSPRNG, timing, process spawning
│   └── src/
│       ├── vw_whisper_module.c               # Entry point: VLC module descriptor, open/close hooks
│       ├── vw_session.c                       # Session lifecycle logic (start, pause, resume, seek reset)
│       ├── vw_audio_capture.c                 # Audio callback handler & PCM format normalization
│       ├── vw_caption_presenter.c             # Schedules and renders timed text captions via SPU with 1.0s floor & OSD fallback
│       ├── vw_benchmark.c                     # Bounded per-session metrics and private temporary text report writer
│       ├── vw_worker_client.c                 # Worker process launcher, IPC client & HELLO handshake
│       ├── vw_queue.c                         # Non-blocking lock-free SPSC queue implementation
│       ├── vw_platform_win32.c                # Windows: paths, BCrypt CSPRNG, process spawn, timing
│       └── vw_platform_linux.c                # Linux/Unix: random bytes, posix_spawn, timing
├── worker/                                    # Standalone local transcription worker application
│   ├── CMakeLists.txt                         # Builds vlc-whisper-worker executable and links whisper.cpp
│   ├── include/
│   │   ├── vw_worker.h                        # Main worker event loop and IPC message dispatcher
│   │   ├── vw_source_decoder.h                # Native audio/video source file demuxer interface
│   │   ├── vw_worker_queue.h                  # Bounded frame queue types and ownership contract
│   │   ├── vw_whisper_engine.h                # C wrapper around whisper.cpp: segment-level timing & no_speech_prob accessors
│   │   ├── vw_vad.h                           # Silero VAD GGML context management, chunk finding & RMS Energy fallback
│   │   ├── vw_hallucination_filter.h          # Non-speech sound tag and isolated punctuation filter
│   │   ├── vw_segment_builder.h               # Final-subtitles dedup (no expansion/revision), timed segments
│   │   ├── vw_audio_buffer.h                  # Rolling PCM ring buffer & window extraction
│   │   ├── vw_worker_config.h                 # Model/VAD path resolution, --vad-model precedence & compatibility discovery
│   │   ├── vw_sha256.h                        # Streaming SHA-256 for download verification
│   │   ├── vw_model_catalog.h                 # Committed catalog (7 models, pinned sha256/bytes)
│   │   ├── vw_model_download.h                # Download engine: thread, single-flight, abort, progress
│   │   └── vw_translate.h                     # Keyless 3-tier Google Translate fallback engine
│   ├── src/
│   │   ├── main.c                             # Worker executable entry point: CLI parsing & signal handling
│   │   ├── vw_worker.c                        # Worker IPC state machine, look-ahead decoding & message loop
│   │   ├── vw_source_decoder_mf.c             # Windows Media Foundation native audio source demuxer
│   │   ├── vw_source_decoder_ffmpeg.c         # Linux FFmpeg native audio source demuxer
│   │   ├── vw_worker_queue.c                  # Bounded worker frame queue (reader -> main loop handoff)
│   │   ├── vw_whisper_engine.c                # Model load/unload, whisper_full inference, confidence & segment accessors
│   │   ├── vw_vad.c                           # Silero GGML VAD integration, chunk boundary finding & RMS energy fallback
│   │   ├── vw_hallucination_filter.c          # Sound descriptor tag stripping and isolated punctuation filter
│   │   ├── vw_segment_builder.c               # Segment dedup (final subtitles), queue growth
│   │   ├── vw_audio_buffer.c                  # PCM sample accumulation & 8s windowing
│   │   ├── vw_worker_config.c                 # Configuration setup plus post-model-resolution VAD discovery
│   │   ├── vw_sha256.c                        # Streaming SHA-256 implementation
│   │   ├── vw_model_download.c                # WinHTTP/curl download, ownership lock, diagnostics, .part → verify → atomic rename
│   │   └── vw_translate.c                     # 3-tier keyless Google Translate fallback engine (Web RPC, GTX, Mobile scrape)
│   └── third_party/                           # Pinned external C/C++ dependencies
│       ├── vlc-3.0.23/                        # Pinned VLC header SDK headers
│       └── whisper.cpp/                       # Pinned whisper.cpp C/C++ inference engine
├── protocol/                                  # Shared C17 IPC protocol, logging & framing library
│   ├── CMakeLists.txt                         # Builds vw_protocol library
│   ├── include/
│   │   ├── vw_protocol.h                      # High-level protocol encoder/decoder API
│   │   ├── vw_protocol_types.h                # Binary message headers, magic bytes, and struct definitions
│   │   ├── vw_protocol_util.h                 # Compiler-safe saturating 64-bit integer arithmetic helpers
│   │   ├── vw_protocol_codec.h                # Serialization/deserialization helpers
│   │   ├── vw_ipc_transport.h                 # Platform transport abstraction (Named Pipe / Unix Domain Socket)
│   │   └── vw_log.h                           # Privacy-safe variadic diagnostic logging API
│   └── src/
│   │   ├── vw_protocol_codec.c                # Binary frame pack & unpack implementations
│   │   ├── vw_protocol_validate.c             # Frame bounds checking, magic verification & UTF-8 validation
│   │   ├── vw_ipc_pipe_win32.c                # Windows Named Pipe server/client transport implementation
│   │   ├── vw_ipc_socket_linux.c              # Linux Unix Domain Socket transport implementation
│   │   └── vw_log.c                           # Privacy-safe variadic logger & customizable sink implementation
├── models/                                    # Offline local GGML model storage & manifests
│   ├── vw_download_vad_model.sh               # POSIX helper to download Silero VAD GGML weights
│   ├── vw_download_vad_model.cmd              # Windows helper to download Silero VAD GGML weights
│   ├── ggml-tiny.bin                           # Bundled multilingual tiny weights file (git-ignored binary)
│   ├── ggml-silero-vad.bin                    # Silero VAD GGML weights file (git-ignored binary)
│   └── manifest.json                          # Offline manifest (SHA-256 integrity, RAM bounds)
├── tests/                                     # Verification suites, fixtures, and E2E procedures
│   ├── CMakeLists.txt                         # Builds unit and integration test executables
│   ├── include/
│   │   └── vw_test.h                          # Common test helper macros (e.g., EXPECT, EXPECT_EQ_STR)
│   ├── unit/                                  # Isolated component tests
│   │   ├── test_protocol_codec.c              # Serialization & frame encoding unit tests
│   │   ├── test_protocol_validate.c           # Malformed payload & boundary validation tests
│   │   ├── test_protocol_util.c               # Saturating arithmetic boundary and overflow unit tests
│   │   ├── test_source_decoder.c              # Media Foundation / FFmpeg native source demuxer tests
│   │   ├── test_queue.c                       # Lock-free SPSC queue concurrency & overflow tests
│   │   ├── test_audio_capture.c               # PCM normalization & chunking tests
│   │   ├── test_audio_buffer.c                # PCM ring buffer float32 conversion & overflow tests
│   │   ├── test_whisper_engine.c              # whisper.cpp model init, determinism & decoding parameter tests
│   │   ├── test_vad.c                         # Silero GGML VAD, chunk boundary & RMS Energy fallback tests
│   │   ├── test_hallucination_filter.c        # Non-speech tag & isolated punctuation filter tests
│   │   ├── test_segment_builder.c             # Segment overlap & deduplication unit tests
│   │   ├── test_caption_timing.c              # pts_us timestamp arithmetic and formatting tests
│   │   ├── test_benchmark.c                   # Per-session metric and temporary report tests
│   │   ├── test_caption_presenter.c           # Caption cue conversion, reading floor & rate scaling tests
│   │   ├── test_platform.c                    # Platform abstraction (RNG, time, spawn) tests
│   │   ├── vw_test_worker_client.c            # Worker IPC client API (start/send/stop/shutdown) tests
│   │   ├── test_worker_config.c               # Worker CLI plus CWD-independent model/VAD directory resolution tests
│   │   └── test_model_download.c              # Model download: sha256 vectors, catalog, progress, retry tests
│   ├── integration/                           # Sub-system IPC and process tests
│   │   ├── test_worker_ipc.c                  # Full IPC handshake & message exchange test
│   │   └── test_worker_lifecycle.c            # Worker startup, crash recovery & shutdown test
│   ├── e2e/                                   # End-to-end playback test procedures
│   │   └── test_local_video_playback.md       # Manual test protocol for live VLC playback
│   └── fixtures/                              # Test fixtures & expected outputs
│       ├── spoken_english_16khz.wav           # Deterministic 16kHz audio sample
│       └── expected_segments.json             # Reference transcript segments & timestamps
├── samples/                                   # Standalone demo snippets & verification utilities
│   ├── CMakeLists.txt                         # Dynamically builds snippet executables
│   ├── audio/                                 # Sample audio test files (output.wav, harvard.wav)
│   └── snippets/                              # Standalone C17 sample code files
│       └── vw_sample_whisper_pcm.c            # 16kHz WAV reader, float resampler & Whisper runner
├── cmake/                                     # Build system configurations & toolchains
│   ├── vw_packaging.cmake                     # CPack release archive & NSIS installer target definitions
│   ├── vw_installer.nsi.in                   # Templated NSIS script for standalone Windows installer
│   └── toolchains/
│       └── windows-x64-mingw.cmake            # MinGW cross-compilation CMake toolchain configuration
├── docs/                                      # Project specifications, ADRs & architectural design docs
├── ai/                                        # Internal AI/agent workspace context & logs
├── CMakeLists.txt                             # Root CMake build configuration
├── CMakePresets.json                          # Native and cross-compilation build presets
├── LICENSE                                    # Root MIT License
├── THIRD_PARTY_NOTICES.md                     # Legal notices and third-party open-source attributions
├── AGENTS.md                                  # Coding standards, architectural invariants & privacy rules
└── README.md                                  # Project overview, build instructions & developer guide
```

## Models Directory Layout

The `models/` directory serves as the local offline store for GGML model files and manifests:

- **Local Model Files (`models/*.bin`)**: Bundled or user-provisioned GGML weights (the bundled default is
  `ggml-tiny.bin`; additional catalog models are downloaded into the per-user directory). Binary model files are
  git-ignored.
- **Model Manifest (`models/manifest.json`)**: Declares supported models, expected SHA-256 hashes, language scope (`en`), and disk/RAM footprint bounds per ADR-007.

## Plugin Files

| File                     | Responsibility                                                            |
| ------------------------ | ------------------------------------------------------------------------- |
| `vw_whisper_module.c`   | VLC module registration, activation/deactivation, module setup; since 14c also hosts the sender thread (SPSC drain + worker frame drain, 5/20 ms cadence), model-path discovery, and worker-scoped model-download orchestration |
| `vw_session.c`           | Caption session state: start, pause, resume, stop, discontinuity, failure |
| `vw_audio_capture.c`     | Receive/normalize PCM and associate monotonic media PTS                   |
| `vw_queue.c`             | Bounded audio producer-consumer queue and overload/drop policy            |
| `vw_worker_client.c`     | Launch worker, IPC connect, HELLO handshake, send/receive, cleanup        |
| `vw_caption_presenter.c` | VLC caption SPU rendering with OSD fallback and look-ahead scheduling; owns a separate wall-clock SPU channel for model-download progress that survives pause/seek blanking |
| `vw_benchmark.c`        | Bounded aggregate session metrics, live PTS-to-monotonic latency samples, and unique temporary key/value report snapshots |
| `vw_log.c`               | Opt-in privacy-safe diagnostics; disabled by default and never logs PCM/transcripts |
| `vw_platform_win32.c`    | Windows: paths, handles, BCrypt CSPRNG, process spawn, timing helpers     |
| `vw_platform_linux.c`    | Linux/Unix: random bytes, posix_spawn, timing helpers                     |

The VLC audio callback may only do bounded non-blocking work. It must never wait for the worker, infer, do blocking IPC, or allocate unbounded memory.

## Worker Files

| File                   | Responsibility                                                     |
| ---------------------- | ------------------------------------------------------------------ |
| `main.c`               | Parse arguments, initialize, run, and return meaningful exit codes |
| `vw_worker.c`          | IPC event loop, protocol dispatch, look-ahead decoding & state     |
| `vw_source_decoder_mf.c` | Windows Media Foundation native audio demuxer & resampler        |
| `vw_source_decoder_ffmpeg.c` | Linux FFmpeg native audio demuxer & resampler                |
| `vw_whisper_engine.c`  | Whisper C API adapter: model load/unload, inference timing, per-segment accessors |
| `vw_vad.c`             | Voice-activity detection state and window decisions                |
| `vw_audio_buffer.c`    | PCM accumulation, window extraction, overlap                       |
| `vw_segment_builder.c` | Ordered timed segments, final-subtitles dedup, dynamic queue growth |
| `vw_worker_config.c`   | Validate model path, resolve downloaded relative paths, initial `en` language, and safe defaults |
| `vw_sha256.h`          | Streaming SHA-256 computation for model download verification      |
| `vw_sha256.c`          | Incremental SHA-256 implementation, streaming hash while writing .part |
| `vw_model_catalog.h`   | Committed model catalog (7 models, pinned sha256/bytes, Hugging Face URLs) |
| `vw_model_download.h`  | Download engine interface: dedicated thread, interprocess ownership lock, single-flight, abort, progress snapshot |
| `vw_model_download.c`  | WinHTTP/curl download, per-model lock, .part → sha256 verify → atomic rename into per-user dir; abort and worker-death cleanup |
| `vw_translate.h`       | 3-tier keyless Google Translate fallback engine interface, parser contracts, and constants |
| `vw_translate.c`       | HTTP client (WinHTTP/curl), URL encode, HTML unescape, Web RPC (MkEWBc), GTX, and Mobile scrape endpoints |
| `test_model_download.c` | SHA-256 NIST vectors, catalog lookup, pct math, local success, abort cleanup, same-destination locking, and retry-then-fail paths |
| `test_translate.c`     | URL encoding, HTML unescaping, Web RPC JSON parser, GTX array parser, and Mobile scrape parser verification |

The builder exposes `vw_segment_builder_push_hypothesis` (whole-phrase final-subtitles dedup: exact,
fragment, and expanded superstring hypotheses are dropped; queue grows dynamically; history commits after a
successful enqueue). See ADR-018 and `docs/plans/phrase_timing_segmentation_plan.md` step 17d.1.

The shipped default supports multilingual CPU inference using `tiny`, one local playback session, and final-only segments.

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
    VW_MSG_SHUTDOWN = 11,
    VW_MSG_STARTED = 12,
    VW_MSG_POSITION = 13
} vw_message_type_t;
```

Frames carry protocol version, message type, session ID, payload size, and payload. All audio/caption timing uses media PTS in microseconds. See `docs/api-contracts.md` for the authoritative wire contract.

Payload structs are constructed at call sites with C99 designated initializers (fields not listed are zero-filled).

## Tests

- `tests/unit/`: codec/validation, queue policy, audio capture, segment building, caption timing, platform abstraction.
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
