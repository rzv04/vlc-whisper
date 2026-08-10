# Test Strategy

## Quality principle

The primary invariant is: **captioning must never harm playback**. A transcription error is recoverable; a VLC crash, audio glitch, deadlock, uncontrolled memory growth, or privacy leak is a release blocker.

## Test layers

| Layer              | Scope                | Examples                                                                                |
| ------------------ | -------------------- | --------------------------------------------------------------------------------------- |
| Unit               | Pure C logic         | Ring buffer, PTS arithmetic/overflow, session state, UTF-8 validation, dedupe           |
| Protocol           | Binary compatibility | Golden frames, unknown fields/types, max lengths, token failure, sequence/stale session |
| Worker integration | Local inference      | Fixed PCM fixtures, VAD boundaries, model missing/hash mismatch, deterministic settings |
| VLC integration    | Module behavior      | Load/unload, PCM capture format, display scheduling, pause/end/stop                     |
| End-to-end         | Pinned Windows VLC   | Install, local video, visible captions, worker crash, seek rejection, uninstall         |
| Performance        | Reference machines   | Real-time factor, p50/p95 caption latency, CPU/RAM, queue drops                         |
| Security/privacy   | Local boundary       | Pipe ACLs, random name/token, no listener, no remote traffic, log redaction             |

## Code Coverage

Code coverage instrumentation (`--coverage`) is configured for native Linux builds to ensure project-authored C17 logic is thoroughly exercised.

- **Target Matrix**: Coverage generation and reporting run natively on Linux (`linux-x64-coverage`). Code coverage is not executed for Windows MinGW cross-builds or macOS.
- **Invariant Rules**: Third-party libraries (`worker/third_party/whisper.cpp`, `ggml`), VLC SDK headers, and test suite code are strictly **excluded** from coverage calculations (via `gcovr` `--exclude` flags). ZERO modifications are permitted to third-party/VLC codebase.
- **Reporting**: HTML and CLI reports are generated using `gcovr` after running the automated `ctest` suite.

## Fixtures

Keep legal, small, versioned fixtures: synthetic tones/silence, public-domain or licensed English speech with known transcript/timestamps, short local MP4/MKV containers, malformed frames, and controlled PTS discontinuities. Never commit proprietary films, user audio, production model binaries, or personal transcripts.

Golden expected text should tolerate model-version variance only through explicit normalization policy. Pin model hash and whisper.cpp commit for exact regression tests; if either changes, review differences intentionally rather than silently re-baselining.

## Required cases

- Start local English media, captions appear after bounded warm-up, and final captions have valid ordered PTS.
- Pause stops AUDIO forwarding and clears partial state; resume does not reuse a stale worker session.
- End/stop clears captions and closes worker cleanly.
- User seeks, changes rate, replaces media, or creates non-monotonic PTS: generated captions clear, VLC keeps playing, a single diagnostic appears, no crash.
- Worker absent, wrong version, invalid token, model missing/corrupt, pipe disconnect, bad payload, invalid UTF-8, or worker nonzero exit: safe disable, no playback impact.
- Sustained slow inference: queue stays bounded, old audio is dropped by policy, memory stays bounded, and drop counter rises.
- Existing subtitle track and VLC-whisper behavior follow the documented coexistence policy.

### Automated failure-path coverage

- `tests/unit/test_platform.c`: NULL/zero-size RNG rejection, NULL executable/argv spawn rejection, non-existent executable spawn failure, time monotonicity and wall-clock sanity, and POSIX `vw_platform_terminate_process` fully reaping a spawned child (no zombie).
- `tests/unit/test_audio_buffer.c`: float32 ring buffer creation, S16LE conversion, PTS indexing, drain, clear, ring buffer overflow drop-oldest accounting, and exact 62.5 µs/sample PTS advancement (no drift).
- `tests/unit/test_whisper_engine.c`: invalid model path initialization failure (NULL), model file presence check, and model-gated skip (exit 77). Under Valgrind/memcheck the heavy model-gated section is also skipped (exit 77): loading the 77MB model plus multi-threaded whisper inference is impractically slow under Valgrind and whisper's GPU-less Vulkan fallback emits false-positive `close(-1)` warnings, so the memcheck gate remains fast and clean.
- `tests/integration/test_worker_lifecycle.c`: wrong-token HELLO rejection (worker exits 1), first-frame-not-HELLO rejection (worker exits 1), client NULL-arg validation (NULL endpoint/token), connect failure with no listener.
- `tests/integration/test_worker_ipc.c`: `START` with an unsupported sample rate rejected with an `E_AUDIO_FORMAT` error reply; clean `SHUTDOWN` exit.
- `tests/unit/test_worker_config.c`: worker CLI arg parsing — valid `--token`/`--pipe`/`--model` success, and startup failure paths returning exit code 2 (bad `--token` length, non-hex `--token`, unknown option, dangling `--token`, NULL config).
- `tests/unit/vw_test_worker_client.c`: client-API session state machine (`vw_worker_client_start_session`, `vw_worker_client_send_audio`, `vw_worker_client_stop_session`, `vw_worker_client_shutdown`), transport receive timeout (`vw_ipc_receive_timeout`), and protocol framing verification against an in-process mock server.
- `tests/unit/test_worker_queue.c` (14c): bounded worker frame queue FIFO order with mixed types, payload ownership transfer, full-queue eviction dropping only the oldest `AUDIO` frame while control frames survive, `dropped_audio_us` equal to the decoded `duration_us` sum, zero-payload frames, and destroy freeing queued payloads (valgrind).
- `tests/unit/vw_test_worker_client.c` (14c receive-frame block): `vw_worker_client_receive_frame` decodes `CAPTION_SEGMENT`/`STATUS`/`ERROR` in order, drains and skips an unknown `PAUSE` frame, times out with 0 against a silent server (transport stays usable), and returns -1 at EOF; segment text is copied into caller-owned storage.
- `tests/integration/test_worker_lifecycle.c` (14c additions): worker with zeroed `model_path` rejects `START` through the client API (E_MODEL_MISSING error path); model-gated section (when `models/ggml-tiny.en.bin` exists and not under Valgrind) streams four 512 ms silence chunks through `STARTED`/`AUDIO`/`STOP`/`SHUTDOWN` and exits 0.
- `tests/integration/test_worker_ipc.c` (14c): unchanged asserts re-run against the worker reader-thread split, proving the split preserves lifecycle semantics.
- `tests/unit/test_caption_presenter.c` (15): presenter display/show_segment/clear against VLC symbol stubs (NULL-filter standalone mode). Step 15 wiring itself is module-internal (sender-thread dispatch to OSD) and is verified by live-VLC acceptance: captions appear ~8s+ behind audio due to the batch 8s-window inference geometry (documented in the plan); automated suite is regression-only for this path.

## Performance contract

Define the reference machine before claiming “real time”: CPU model/core count, RAM, Windows build, VLC build, model hash, worker flags, and fixture. Record:

- Real-time factor = inference processing time divided by audio duration; target steady-state below 1.0 for tiny.en on reference hardware.
- End-to-caption latency: target p95 below 5 seconds under the selected 8-second/2-second default windowing, measured from segment end PTS to display scheduling.
- No unbounded queue; backlog hard limit 8 seconds (16 × 512 ms chunks); zero intentional playback stalls.

These targets are engineering gates, not a guarantee for every PC or noisy source.

## CI gates

Every merge: format check, C compilation with warnings-as-errors, unit/protocol tests, sanitizer build where target permits, dependency/license scan, and Windows cross-build. Nightly: fuzz corpus, worker integration with pinned model fixture, reproducibility/hash check, and Windows VM VLC smoke test when infrastructure is available.

Release requires all gates green, manual local-file acceptance on clean Windows, documented known failures, protocol/version manifest, model hash verification, and review of diagnostics to ensure no PCM/transcript/path leakage.
