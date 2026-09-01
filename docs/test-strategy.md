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
| Performance        | Reference machines   | Real-time factor, p50/p95 caption latency, CPU/RAM, queue drops, per-session report      |
| Security/privacy   | Local boundary       | Pipe ACLs, random name/token, no listener, no remote traffic, log redaction             |

## Code Coverage

Code coverage instrumentation (`--coverage`) is configured for native Linux builds to ensure project-authored C17 logic is thoroughly exercised.

- **Target Matrix**: Coverage generation and reporting run natively on Linux (`linux-x64-coverage`). Code coverage is not executed for Windows MinGW cross-builds or macOS.
- **Invariant Rules**: Third-party libraries (`worker/third_party/whisper.cpp`, `ggml`), VLC SDK headers, and test suite code are strictly **excluded** from coverage calculations (via `gcovr` `--exclude` flags). ZERO modifications are permitted to third-party/VLC codebase.
- **Reporting**: HTML and CLI reports are generated using `gcovr` after running the automated `ctest` suite.

## Fixtures

Keep legal, small, versioned fixtures: synthetic tones/silence, public-domain or licensed English speech with known transcript/timestamps, short local MP4/MKV containers, malformed frames, and controlled PTS discontinuities. Never commit proprietary films, user audio, production model binaries, or personal transcripts.

Golden expected text should tolerate model-version variance only through explicit normalization policy. Pin model hash and whisper.cpp commit for exact regression tests; if either changes, review differences intentionally rather than silently re-baselining.

Step 20 benchmark reports are aggregate, local, and key/value formatted. They include session duration, audio chunks and duration sent, worker frames and captions received, captions sent to the presenter, plugin-filtered captions (paused, stale, or presenter-rejected), segment audio duration/text bytes, cumulative `whisper_full()` time, real-time factor (`inference / input audio`), processing speed ratio (`input audio / inference`), first sent-caption elapsed time, and live-mode utterance latency min/p50/p95/max. Latency is sampled only when live system-date PTS can be mapped to monotonic time; source-mode media timestamps are not mixed with that clock. Speed metrics exclude post-filtering. Reports are created with unique temporary names, flushed during active playback, finalized on normal teardown, and contain no transcript, PCM, URL, token, or telemetry.

## Required cases

- Start local English media, captions appear after bounded warm-up, and final captions have valid ordered PTS.
- Pause stops AUDIO forwarding and clears partial state; resume does not reuse a stale worker session.
- End/stop clears captions and closes worker cleanly.
- User seeks, changes rate, replaces media, or creates non-monotonic PTS: generated captions clear, VLC keeps playing, a single diagnostic appears, no crash. Every accepted live or source seek creates a fresh caption-session ID so buffered pre-seek source and translated cues are stale by construction.
- Worker absent, wrong version, invalid token, model missing/corrupt, pipe disconnect, bad payload, invalid UTF-8, or worker nonzero exit: safe disable, no playback impact.
- Sustained slow inference: queue stays bounded, old audio is dropped by policy, memory stays bounded, and drop counter rises.
- Existing subtitle track and VLC-whisper behavior follow the documented coexistence policy.
- Lua settings acceptance: opening the single dialog reports bundled/per-user model presence; `tiny.en` and `base.en`
  force `en` on Apply while the full language list remains visible; existing files offer re-download without blocking
  VLC; language choices persist after closing and reopening the dialog.

### Automated failure-path coverage

- `tests/unit/test_platform.c`: NULL/zero-size RNG rejection, NULL executable/argv spawn rejection, non-existent executable spawn failure, time monotonicity and wall-clock sanity, and POSIX `vw_platform_terminate_process` fully reaping a spawned child (no zombie). Windows VLC acceptance additionally confirms worker launches use hidden console/window flags.
- `tests/unit/test_audio_buffer.c`: float32 ring buffer creation, S16LE conversion, PTS indexing, drain, clear, ring buffer overflow drop-oldest accounting, and exact 62.5 µs/sample PTS advancement (no drift). `tests/unit/test_audio_capture.c` additionally verifies continuous 44.1 kHz → 16 kHz resampling across callback-block boundaries without phase reset.
- `tests/unit/test_whisper_engine.c`: invalid model path initialization failure (NULL), model file presence check, and model-gated skip (exit 77). Under Valgrind/memcheck the heavy model-gated section is also skipped (exit 77): loading the 77MB model plus multi-threaded whisper inference is impractically slow under Valgrind and whisper's GPU-less Vulkan fallback emits false-positive `close(-1)` warnings, so the memcheck gate remains fast and clean.
- `tests/unit/test_worker_config.c`: concrete Whisper language validation rejects `auto` and unsupported codes; valid language selections remain accepted.
- `tests/unit/test_caption_presenter.c`: adjacent cues preserve the one-second floor by allowing overlap when clipping would make the preceding cue unreadable.
- `tests/unit/test_translate.c`: one global 800 ms deadline is consumed across fallback tiers rather than reset for each request.
- `tests/integration/test_worker_lifecycle.c`: wrong-token HELLO rejection (worker exits 1), first-frame-not-HELLO rejection (worker exits 1), client NULL-arg validation (NULL endpoint/token), connect failure with no listener.
- `tests/integration/test_worker_ipc.c`: `START` with an unsupported sample rate rejected with an `E_AUDIO_FORMAT` error reply; clean `SHUTDOWN` exit.
- `tests/unit/test_worker_config.c`: worker CLI arg parsing — valid `--token`/`--pipe`/`--model` success, and startup failure paths returning exit code 2 (bad `--token` length, non-hex `--token`, unknown option, dangling `--token`, NULL config).
- `tests/unit/test_worker_config.c`: logging disabled by default, `--enable-logging`, and `--log-file` implying enabled diagnostics.
- `tests/unit/vw_test_worker_client.c`: client-API session state machine (`vw_worker_client_start_session`, `vw_worker_client_send_audio`, `vw_worker_client_stop_session`, `vw_worker_client_shutdown`), transport receive timeout (`vw_ipc_receive_timeout`), and protocol framing verification against an in-process mock server.
- `tests/unit/test_worker_client_seek_epoch.c`: source-mode seek lifecycle regression using a fake worker with `SOURCE_MODE` and `TRANSLATION`. A backward seek (100s → 30s, translation disabled) and a modest forward seek (30s → 40s, translation enabled) must each emit `STOP(SEEK_DISCONTINUITY)` followed by `START` with a fresh random session ID, preserve model/source identity, reapply translation settings, and send pacing `POSITION` for the new epoch. The fixture then injects a completed source cue and a completed translated cue from the previous session at PTS values that the old lower-bound heuristic would accept; both retain the previous ID while the subsequent valid cue carries the current ID, matching the plugin's stale-session rejection invariant.
- `tests/unit/vw_test_worker_client.c`: worker-scoped `MODEL_CTRL` is accepted before `START_SESSION` with a zero session ID, proving model provisioning remains available when the selected model initially rejects caption startup.
- `tests/unit/test_worker_queue.c` (14c): bounded worker frame queue FIFO order with mixed types, payload ownership transfer, full-queue eviction dropping only the oldest `AUDIO` frame while control frames survive, `dropped_audio_us` equal to the decoded `duration_us` sum, zero-payload frames, and destroy freeing queued payloads (valgrind).
- `tests/unit/vw_test_worker_client.c` (14c receive-frame block): `vw_worker_client_receive_frame` decodes `CAPTION_SEGMENT`/`STATUS`/`ERROR` in order, drains and skips an unknown `PAUSE` frame, times out with 0 against a silent server (transport stays usable), and returns -1 at EOF; segment text is copied into caller-owned storage.
- `tests/integration/test_worker_lifecycle.c` (14c additions): worker with zeroed `model_path` rejects `START` through the client API (E_MODEL_MISSING error path); model-gated section (when `models/ggml-tiny.en.bin` exists and not under Valgrind) streams four 512 ms silence chunks through `STARTED`/`AUDIO`/`STOP`/`SHUTDOWN` and exits 0.
- `tests/integration/test_worker_ipc.c` (14c): unchanged asserts re-run against the worker reader-thread split, proving the split preserves lifecycle semantics.
- `tests/unit/test_caption_presenter.c` (15): presenter display/show_segment/clear against VLC symbol stubs (NULL-filter standalone mode). Step 15 wiring itself is module-internal (sender-thread dispatch to OSD) and is verified by live-VLC acceptance: captions appear ~8s+ behind audio due to the batch 8s-window inference geometry (documented in the plan); automated suite is regression-only for this path.
- `tests/unit/vw_test_worker_client.c` (16): fake server now expects `PAUSE` (USER_PAUSE) then `RESUME` (USER_RESUME) control frames between AUDIO and STOP, verifying the client pause/resume API and that the session stays active through both.
- `tests/integration/test_worker_lifecycle.c` (16): model-gated section sends PAUSE/RESUME mid-stream before STOP/SHUTDOWN, proving the worker survives both controls with exit 0.
- `tests/unit/vw_test_worker_client.c` (17): fake server decodes the `STOP` payload and asserts `reason == VW_CTRL_REASON_SEEK_DISCONTINUITY`.
- `tests/integration/test_worker_lifecycle.c` (17): model-gated section runs STOP(SEEK_DISCONTINUITY) → START (new session_id, new PTS epoch) → AUDIO → STOP → SHUTDOWN on one connection, proving the worker accepts the restart cycle and drops stale pre-seek audio (exit 0).
- `tests/unit/test_worker_config.c` (17a): `--backend auto|gpu|cpu` and `--gpu-device` parsing (success + bad value/negative/dangling), backend defaults (AUTO, device 0). Engine tests force `VW_WORKER_BACKEND_CPU` for determinism. Build matrix smoke (manual): the production `windows-x64-release` preset requires Vulkan and must fail configure if the SDK/`glslc` cannot be resolved; `*-cpu` presets emit `vlc-whisper-worker-cpu` with no Vulkan import; development GPU presets may retain their explicit fallback behavior.
- `tests/unit/test_caption_presenter.c` (17b): SPU subpicture channel registration (`vout_RegisterSubpictureChannel`), subpicture creation and ownership handover (`vout_PutSubpicture`), bottom-center alignment, timeline date mapping with fallback clamping, SPU registration failure fallback to `vout_OSDText`, and dual-channel flushing (`vout_FlushSubpictureChannel`) on blank/clear.
- `tests/unit/test_protocol_codec.c` & `test_protocol_validate.c` (17c): Protocol v1.1 serialization, deserialization, and schema validation for `VW_CAPABILITY_SOURCE_MODE`, `source_url` in `vw_msg_start_t`, and `VW_MSG_POSITION` (`vw_msg_position_t`).
- `tests/unit/test_source_decoder.c` (17c): Media Foundation (`vw_source_decoder_mf.c`) and FFmpeg (`vw_source_decoder_ffmpeg.c`) native audio demuxer tests: container format detection, 16kHz mono S16LE extraction, stream timestamp calculation, seeking (`IMFSourceReader::SetCurrentPosition` / `av_seek_frame`), and EOF handling.
- `tests/unit/test_caption_presenter.c` (17c): Look-ahead future timestamp SPU scheduling maps future segment media PTS relative to `input_time_us` into the future OSD date domain (`mdate() + lead_us`), including valid media position zero; live-network regression coverage verifies that an inference-late system-date cue starts immediately at `mdate()` and queues caption-channel flush before replacement, while source look-ahead rendering never flushes its future cue queue.
- `tests/unit/test_protocol_util.c` (17d): 64-bit saturating addition and subtraction (`vw_saturating_add_i64` / `vw_saturating_sub_i64`) boundary verification across `INT64_MAX`, `INT64_MIN`, overflow/underflow clamping, and sign permutations.
- `tests/unit/test_protocol_codec.c` & `test_protocol_validate.c`: Protocol v1.6 serialization and schema validation: correlated 16-byte session ID plus `source_active` in `VW_MSG_STARTED` (with legacy one-byte decode), legal media-range validation for `VW_MSG_POSITION` (`current_pts_us` and `input_time_us` bounded in `[-10s, 10 years]`), finite positive playback rate (`isfinite` and in `(0, 16]`), and strict position flag bitmask validation (`VW_POSITION_FLAG_SEEK | VW_POSITION_FLAG_PAUSED`).
- `tests/unit/test_caption_presenter.c` (17d): SPU subpicture channel persistence across repeated blanking flushes, rate-scaled lead clamping, and pause transition blanking.
- `tests/unit/test_caption_presenter.c`: model-download progress uses a separate wall-clock SPU channel, survives caption blanking, and flushes explicitly on abort/teardown.
- `tests/unit/test_model_download.c`: local-file verified download, SHA-256 vectors, catalog lookup, progress math, abort cleanup, and same-destination interprocess single-flight locking.
- `tests/unit/test_protocol_codec.c` and `test_protocol_validate.c`: zero-total terminal `FAILED` model progress is accepted while zero-total active progress remains rejected.
- Model-download regression coverage must preserve the pending catalog-id correlation across the worker's initial
  `MODEL_PROGRESS(IDLE)` snapshot, then accept `DONE` as the activation trigger; `FAILED`, abort, worker death, and
  transport teardown are the terminal clearing paths. Windows manual coverage additionally checks the per-user final
  path and the worker temp log because WinHTTP and VLC Lua config timing are unavailable in the native Linux suite.
- `tests/integration/test_worker_lifecycle.c` (17d): Fire-and-forget `VW_MSG_POSITION` seek repositioning remains covered as the worker's direct protocol operation, 15-frame rapid scrub burst coalescing (<50ms), and in-session media swap across distinct session IDs. The VLC source-mode client policy is separately covered by `test_worker_client_seek_epoch` and uses fresh `STOP`/`START` epochs rather than relying on same-session seek for visible captions.
- `tests/unit/test_whisper_engine.c` (17d.1): `vw_whisper_segment_t` struct, `vw_whisper_engine_get_segment_count`, `vw_whisper_engine_get_segment` accessor bounds checking (NULL engine, negative index, index overflow, NULL out_seg), microsecond scaling (`10000LL`), and monotonic $t_0 \le t_1$.
- `tests/unit/test_segment_builder.c` (17d.1): multi-phrase push per window, cross-hop deduplication with committed history ring buffer persistence across `pop()`, silence gap preservation (0.6s), `is_final = true` flag validation, and `vw_segment_builder_clear()` reset.
- `tests/unit/test_caption_presenter.c` (17d.1): discrete SPU phrase scheduling with non-overlapping subpictures and silence interval screen blanking (0.6s gap).
- `tests/unit/test_hallucination_filter.c` (17e.1): non-speech sound tag filtering (`[Music]`, `(applause)`, `♪`, `♫`, etc.), isolated punctuation filtering (`...`, `---`, `! ! !` with zero alphanumeric characters), and preservation of legitimate dialogue and sentence punctuation.
- `tests/unit/test_vad.c` (17e.1): pure silence, low-level ambient static (<0.005 RMS), speech tone/wave RMS energy detection, NULL context fallback, partial window sample counts, NULL safety for reset/free, model-gated Silero VAD GGML inference on real 16kHz speech fixtures (`jfk.wav`), and Strategy C VAD-guided non-overlapping audio chunk boundary finding (`vw_vad_find_chunk_boundary`) across pure silence, natural dialogue pauses, continuous speech clamping, and EOF tails.
- `tests/unit/test_whisper_engine.c` (17e.1): `no_speech_prob` float extraction via `whisper_full_get_segment_no_speech_prob` and verification that $P(\text{no\_speech}) \in [0.0, 1.0]$.
- `tests/unit/test_worker_config.c` (17e.1): `--vad-model <path>` CLI parsing and precedence, unknown option rejection, and VAD discovery beside the resolved model, under `--model-dir`, beside the worker executable in its adjacent `models/` directory, and through legacy working-directory fallbacks.
- `tests/unit/test_worker_config.c`: relative selected model paths resolve first from `--model-dir`, then from the worker executable's adjacent `models/` directory, independently of the worker launch directory; sibling VAD resolution remains covered separately.
- `tests/unit/test_caption_presenter.c` (17e.2): `VW_CAPTION_MIN_DISPLAY_DURATION_US` (1.0s) display floor enforcement on sub-second cues, wall-clock floor scaling across variable playback rates ($0.5\times$, $1.0\times$, $2.0\times$), long speech duration preservation ($> 1.0\text{s}$), and OSD fallback minimum floor.
- `tests/unit/test_whisper_engine.c` (17e.2): deterministic greedy decoding verification (identical PCM buffer transcribes to identical segment counts, timestamps, and UTF-8 text on repeat passes) and bounded decoding parameters (`temperature = 0.0f`, `temperature_inc = 0.2f`, `entropy_thold = 2.40f`, `no_context = true`, `suppress_nst = true`).
- `tests/unit/test_benchmark.c` (20, 21b): bounded metric accounting, negative live look-ahead latency retention, safe ratios, report snapshots/finalization, translation request/success/tier distribution counters, timeout tracking, and aggregate status updates.
- `tests/unit/test_translate.c` (21b): RFC 3986 percent URL encoding, HTML entity unescaping, Web RPC (`MkEWBc`) envelope response parsing, legacy GTX JSON array parsing, mobile web scrape HTML parsing, timeout budgeting (800ms), and 3-tier fallback tier constant validation.
- `tests/unit/test_caption_presenter.c` (21b): dual-line translated subtitle formatting (`<source>\n<translated>`) and single-line translation-only presentation via VLC SPU subpicture rendering.
- `cmake/vw_provision_model.cmake`, `cmake/vw_check_workers.cmake`, `cmake/vw_packaging.cmake` & `cmake/vw_installer.nsi.in`: release packaging requires the exact tiny and Silero VAD SHA-256 values even when those gitignored model files already exist locally; the portable ZIP uses an explicit model allowlist. The production Windows GPU preset fails if Vulkan cannot be enabled and requires both GPU and CPU workers. An explicit CPU-only installer removes or schedules deletion of a stale canonical GPU worker from an earlier GPU installation. Standalone NSIS acceptance still covers 64-bit VLC discovery, process handling, worker staging, reboot-safe replacement, notice ownership, uninstall cleanup, local media, and live network streams.

## Performance contract

Define the reference machine before claiming “real time”: CPU model/core count, RAM, Windows build, VLC build, model hash, worker flags, and fixture. Record:

- Real-time factor = inference processing time divided by audio duration; target steady-state below 1.0 for tiny.en on reference hardware.
- End-to-caption latency: target p95 below 5 seconds under the selected 8-second/2-second default windowing, measured from segment end PTS to display scheduling.
- No unbounded queue; backlog hard limit 8 seconds (16 × 512 ms chunks); zero intentional playback stalls.

These targets are engineering gates, not a guarantee for every PC or noisy source.

## CI gates

Every merge: format check, C compilation with warnings-as-errors, unit/protocol tests, sanitizer build where target permits, dependency/license scan, and Windows cross-build. Nightly: fuzz corpus, worker integration with pinned model fixture, reproducibility/hash check, and Windows VM VLC smoke test when infrastructure is available.

Release requires all gates green, manual local-file acceptance on clean Windows, documented known failures, protocol/version manifest, model hash verification, and review of diagnostics to ensure no PCM/transcript/path leakage.

**Validation note for this change:** `test_worker_client_seek_epoch` is wired into CTest and release packaging now fails closed on worker/model composition. The branch's final format/build/CTest/Valgrind run and Windows installer/VM smoke remain separate release-validation steps rather than being claimed by this documentation update.
# Test harness safety

Unix-domain integration tests use unique absolute paths under `/tmp`, derived from the process ID. This prevents a
test listener from unlinking its own executable when the build's test working directory is used as the socket path.
Release test binaries are compiled with `NDEBUG` removed because assertions contain executable checks and cleanup
side effects that are part of the test harness contract.
