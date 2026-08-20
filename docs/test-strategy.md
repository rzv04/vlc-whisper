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
- `tests/unit/vw_test_worker_client.c` (16): fake server now expects `PAUSE` (USER_PAUSE) then `RESUME` (USER_RESUME) control frames between AUDIO and STOP, verifying the client pause/resume API and that the session stays active through both.
- `tests/integration/test_worker_lifecycle.c` (16): model-gated section sends PAUSE/RESUME mid-stream before STOP/SHUTDOWN, proving the worker survives both controls with exit 0.
- `tests/unit/vw_test_worker_client.c` (17): fake server decodes the `STOP` payload and asserts `reason == VW_CTRL_REASON_SEEK_DISCONTINUITY`.
- `tests/integration/test_worker_lifecycle.c` (17): model-gated section runs STOP(SEEK_DISCONTINUITY) → START (new session_id, new PTS epoch) → AUDIO → STOP → SHUTDOWN on one connection, proving the worker accepts the restart cycle and drops stale pre-seek audio (exit 0).
- `tests/unit/test_worker_config.c` (17a): `--backend auto|gpu|cpu` and `--gpu-device` parsing (success + bad value/negative/dangling), backend defaults (AUTO, device 0). Engine tests force `VW_WORKER_BACKEND_CPU` for determinism. Build matrix smoke (manual): default preset links Vulkan; `*-cpu` preset emits `vlc-whisper-worker-cpu` with no Vulkan import; Windows GPU build requires `VW_VULKAN_SDK` pointing at the SDK root (imports `vulkan-1.dll`).
- `tests/unit/test_caption_presenter.c` (17b): SPU subpicture channel registration (`vout_RegisterSubpictureChannel`), subpicture creation and ownership handover (`vout_PutSubpicture`), bottom-center alignment, timeline date mapping with fallback clamping, SPU registration failure fallback to `vout_OSDText`, and dual-channel flushing (`vout_FlushSubpictureChannel`) on blank/clear.
- `tests/unit/test_protocol_codec.c` & `test_protocol_validate.c` (17c): Protocol v1.1 serialization, deserialization, and schema validation for `VW_CAPABILITY_SOURCE_MODE`, `source_url` in `vw_msg_start_t`, and `VW_MSG_POSITION` (`vw_msg_position_t`).
- `tests/unit/test_source_decoder.c` (17c): Media Foundation (`vw_source_decoder_mf.c`) and FFmpeg (`vw_source_decoder_ffmpeg.c`) native audio demuxer tests: container format detection, 16kHz mono S16LE extraction, stream timestamp calculation, seeking (`IMFSourceReader::SetCurrentPosition` / `av_seek_frame`), and EOF handling.
- `tests/unit/test_caption_presenter.c` (17c): Look-ahead future timestamp SPU scheduling: maps future segment media PTS relative to `input_time_us` into future OSD date domain (`mdate() + lead_us`), verifying zero perceived caption display latency.
- `tests/unit/test_protocol_util.c` (17d): 64-bit saturating addition and subtraction (`vw_saturating_add_i64` / `vw_saturating_sub_i64`) boundary verification across `INT64_MAX`, `INT64_MIN`, overflow/underflow clamping, and sign permutations.
- `tests/unit/test_protocol_codec.c` & `test_protocol_validate.c` (17d): Protocol v1.2 serialization and schema validation: 1-byte `source_active` payload in `VW_MSG_STARTED`, legal media-range validation for `VW_MSG_POSITION` (`current_pts_us` and `input_time_us` bounded in `[-10s, 10 years]`), finite positive playback rate (`isfinite` and in `(0, 16]`), and strict position flag bitmask validation (`VW_POSITION_FLAG_SEEK | VW_POSITION_FLAG_PAUSED`).
- `tests/unit/test_caption_presenter.c` (17d): SPU subpicture channel persistence across repeated blanking flushes, rate-scaled lead clamping, and pause transition blanking.
- `tests/integration/test_worker_lifecycle.c` (17d): Fire-and-forget `VW_MSG_POSITION` seek repositioning without session teardown, 15-frame rapid scrub burst coalescing (<50ms), and in-session media swap across distinct session IDs.
- `tests/unit/test_whisper_engine.c` (17d.1): `vw_whisper_segment_t` struct, `vw_whisper_engine_get_segment_count`, `vw_whisper_engine_get_segment` accessor bounds checking (NULL engine, negative index, index overflow, NULL out_seg), microsecond scaling (`10000LL`), and monotonic $t_0 \le t_1$.
- `tests/unit/test_segment_builder.c` (17d.1): multi-phrase push per window, cross-hop deduplication with committed history ring buffer persistence across `pop()`, silence gap preservation (0.6s), `is_final = true` flag validation, and `vw_segment_builder_clear()` reset.
- `tests/unit/test_caption_presenter.c` (17d.1): discrete SPU phrase scheduling with non-overlapping subpictures and silence interval screen blanking (0.6s gap).
- `tests/unit/test_hallucination_filter.c` (17e.1): non-speech sound tag filtering (`[Music]`, `(applause)`, `♪`, `♫`, etc.), isolated punctuation filtering (`...`, `---`, `! ! !` with zero alphanumeric characters), and preservation of legitimate dialogue and sentence punctuation.
- `tests/unit/test_vad.c` (17e.1): pure silence, low-level ambient static (<0.005 RMS), speech tone/wave RMS energy detection, NULL context fallback, partial window sample counts, NULL safety for reset/free, model-gated Silero VAD GGML inference on real 16kHz speech fixtures (`jfk.wav`), and Strategy C VAD-guided non-overlapping audio chunk boundary finding (`vw_vad_find_chunk_boundary`) across pure silence, natural dialogue pauses, continuous speech clamping, and EOF tails.
- `tests/unit/test_whisper_engine.c` (17e.1): `no_speech_prob` float extraction via `whisper_full_get_segment_no_speech_prob` and verification that $P(\text{no\_speech}) \in [0.0, 1.0]$.
- `tests/unit/test_worker_config.c` (17e.1): `--vad-model <path>` CLI option parsing, unknown option rejection, and `vw_worker_config_autodiscover_vad` searching alongside `--model` and worker binary.

## Performance contract

Define the reference machine before claiming “real time”: CPU model/core count, RAM, Windows build, VLC build, model hash, worker flags, and fixture. Record:

- Real-time factor = inference processing time divided by audio duration; target steady-state below 1.0 for tiny.en on reference hardware.
- End-to-caption latency: target p95 below 5 seconds under the selected 8-second/2-second default windowing, measured from segment end PTS to display scheduling.
- No unbounded queue; backlog hard limit 8 seconds (16 × 512 ms chunks); zero intentional playback stalls.

These targets are engineering gates, not a guarantee for every PC or noisy source.

## CI gates

Every merge: format check, C compilation with warnings-as-errors, unit/protocol tests, sanitizer build where target permits, dependency/license scan, and Windows cross-build. Nightly: fuzz corpus, worker integration with pinned model fixture, reproducibility/hash check, and Windows VM VLC smoke test when infrastructure is available.

Release requires all gates green, manual local-file acceptance on clean Windows, documented known failures, protocol/version manifest, model hash verification, and review of diagnostics to ensure no PCM/transcript/path leakage.
