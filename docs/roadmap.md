# Implementation Roadmap

This document outlines the ordered sequence of deliverables for building `vlc-whisper`. Each milestone must pass its specified exit status before proceeding to the next.

---

## Milestone 0: Core protocol & worker scaffold (Complete)

- [x] 1. Pin external dependencies: `whisper.cpp` (C API), VLC 3.0 headers (`vlc_common.h`, `vlc_filter.h`), and C test runner.
- [x] 2. Establish C17 build system (`CMakeLists.txt`), strict warning flags, clang-format rules, and memory leak checks (Valgrind/ASan).
- [x] 3. Define binary IPC protocol frames: `HELLO`, `HELLO_ACK`, `START`, `STARTED`, `AUDIO`, `SEGMENT`, `STOP`, `PAUSE`, `RESUME`, `SHUTDOWN`, `STATUS`, `ERROR`.
- [x] 4. Implement protocol codec & validation: endianness, magic bytes (`VLCW`), payload size bounds, UTF-8 checks.
- [x] 5. Implement thread-safe SPSC ring buffer for 16 kHz Mono S16LE PCM chunks in C.
- [x] 6. Implement local IPC server (`vlc-whisper-worker` binary) using named pipes (Windows) and Unix domain sockets (Linux) with 32-byte token auth.

**Exit Status:** **DONE** — Protocol test suite passes 100% on Linux and Windows. Clean memory leak check under Valgrind.

---

## Milestone 1: IPC worker & transcription loop (Complete)

- [x] 7. Integrate `whisper.cpp` into worker process: load model (`ggml-tiny.en.bin`), run inference on 16 kHz PCM stream.
- [x] 8. Implement segment builder: split transcription outputs into timed `SEGMENT` frames with monotonically increasing segment IDs and valid media PTS.

**Exit Status:** **DONE** — Worker transcribes fixture PCM over named pipe / Unix socket with 10s accept & 3s I/O timeouts, constant-time auth, and privacy-safe logging.

---

## Milestone 2: VLC feasibility spike (Complete)

- [x] 9. Build the smallest C VLC module against the pinned target and verify load/unload with `-vvv` logs (`plugin/src/vw_whisper_module.c`).
- [x] 10. Capture decoded PCM plus PTS without blocking the audio callback; prove canonical conversion path and queue behavior (`vw_audio_capture.c`, `vw_queue.c`).
- [x] 11. Independently prove caption display: OSD overlay route implemented and verified (`vw_caption_presenter.c`); native SPU deferred.
- [x] 12. Decide in-tree/pinned-VLC build versus supported out-of-tree packaging using observed Windows behavior (ADR-012).

**Exit Status:** **DONE** — A test module sees timestamped audio and displays a static/deterministic timed caption on the reference VLC.

---

## Milestone 3: Local & Live MVP with Seeking & Play/Pause (In Progress)

> [!NOTE]
> A detailed postmortem evaluation and phased 4-step re-implementation blueprint for real-time PCM streaming, GPU acceleration, SPU subpicture rendering, and look-ahead source decoding is documented in [`docs/plans/milestone3_postmortem.md`](file:///home/razvan/vlc-whisper/.worktrees/gemini/docs/plans/milestone3_postmortem.md).

- [x] 13. Connect VLC plugin IPC client (`vw_worker_client.c`) to worker process during module `Open`.
- [x] 14a. Worker audio pipeline: implement real `vw_whisper_engine` (model load, warmup, transcription) and `vw_audio_buffer` (S16LE→float32 append, bounded drop-oldest), handle `START`/`AUDIO`/`STOP` in `vw_worker.c` (single-threaded session loop), 8 s window / 2 s hop windowing and energy-VAD gating, and emit `CAPTION_SEGMENT` frames.
- [x] 14b. Plugin client API and transport: extend `vw_worker_client` with session send API (`START`/`AUDIO`/`STOP`, session and sequence tracking), add `vw_ipc_receive_timeout` transport API, and update process supervision (`vw_platform_wait_process` / out-handle).
- [x] 14c. Plugin real-time streaming & worker thread split: drain SPSC queue on background sender thread to feed `AUDIO` frames across IPC in real time, drain worker frames, degrade to passthrough on pipe death; split `vw_worker.c` into decoupled IPC reader thread + inference worker thread (ADR-013). Shipped: `vw_worker_queue` (bounded frame queue, drop-oldest-AUDIO), worker reader thread + frame-queue main loop, `vw_worker_client_receive_frame` (SEGMENT/STATUS/ERROR drain), `--model` spawn argv + plugin model discovery (`model-path` option), sender thread with 5/20 ms send/receive cadence; SEGMENT frames counted and discarded until step 15.
- [x] 15. Receive incoming `SEGMENT` frames on plugin background thread and trigger `vw_caption_presenter_display()`. Shipped: `vw_caption_presenter` wired into the sender thread (`show_segment` on `VW_MSG_CAPTION_SEGMENT`, `p_filter_ctx` set in open, OSD cleared on close). Note: batch 8s-window inference means captions display ~8s+ behind live audio; look-ahead (17c/17d) targets this latency.
- [x] 16. Implement Play/Pause lifecycle: send `PAUSE`/`RESUME` IPC control frames, suspend PCM queue forwarding on pause, and resume timeline PTS sync on resume. Shipped: sender thread polls `input_GetState` (PAUSE_S) via the object walk, sends `PAUSE`/`RESUME` (USER_PAUSE/USER_RESUME, shared `send_control_frame`), discards queued PCM while paused; worker gates AUDIO and clears its window on PAUSE; client `vw_worker_client_pause_session`/`resume_session`; unit + lifecycle tests.
- [x] 17. Implement Seeking & Discontinuity support: detect `BLOCK_FLAG_DISCONTINUITY` / non-monotonic PTS, clear active presenter captions, send `STOP` (`SEEK_DISCONTINUITY`), reset SPSC queue/VAD state, and start new session epoch without interrupting playback. Shipped: realtime callback sets atomics (flag or PTS-jump fallback, resume PTS anchor); sender thread clears OSD, STOP(SEEK_DISCONTINUITY), drain-discards SPSC, START with new session_id; worker discards stale segment-builder hypotheses on START; `VW_CTRL_REASON_SEEK_DISCONTINUITY` constant; STOP-reason unit test + STOP→START lifecycle restart test.
- [x] 17a. GPU Whisper (Vulkan) Acceleration: Add `ggml-vulkan` backend to worker CLI (`--backend auto|gpu|cpu` & `--gpu-device`), automatic CPU fallback, and parallel build memory limit documentation. Shipped: `VW_WITH_VULKAN` default ON (auto-degrades to CPU-only when the SDK/glslc is absent; `VW_VULKAN_SDK` env points the MinGW cross build at the SDK root), `--backend`/`--gpu-device` CLI, engine sets `whisper_context_params.use_gpu`/`gpu_device` (whisper's built-in transparent CPU fallback), backend log line, GPU worker (`vlc-whisper-worker`) vs CPU worker (`vlc-whisper-worker-cpu`) dist artifacts via `*-cpu` presets.
- [x] 17b. Native SPU Subpicture Subsystem (Look-Ahead Phase 1): Integrate `vout_RegisterSubpictureChannel` and `vout_PutSubpicture` with proper `subpicture_region_New(VLC_CODEC_TEXT)` text regions, `text_segment_New()`, `VW_WEAK` MinGW symbol linkage, and OSD-clock-domain caption scheduling. Shipped: private SPU subpicture channel registration with OSD fallback on failure; structured `subpicture_t` carrying `video_format_Init(&fmt, VLC_CODEC_TEXT)` and `text_segment_New` text; bottom-center alignment; OSD-clock rendering (`b_subtitle=false`, `i_start = mdate()`) — the domain the 3.0.23 Windows build renders filter-pushed subpictures against (media-domain `b_subtitle=true` scheduling verified dropping before region rendering; S→M conversion documented for 17c); seek/blank channel flushing via `vout_FlushSubpictureChannel(vout, channel_id)`; shared `VW_WEAK` macro in `vw_platform.h`; extended `libvlccore.def` export…
- [ ] 17c. Ahead-of-Time Source File Decoding (Look-Ahead Phase 2): Extend IPC protocol to v1.1 (`VW_CAPABILITY_SOURCE_MODE`, `source_url`, `POSITION` lead pacing), implement FFmpeg (Linux) / Media Foundation (Windows) worker demuxer, Model-Once process lifetime (`ADR-015`), process-wide `MFStartup`/`MFShutdown`, and feed pre-buffered future caption segments directly into the Step 17b SPU subpicture pipeline to achieve zero perceived transcription latency.
- [ ] 17d. Seek Re-Sync Engine (Look-Ahead Phase 3): Implement input clock jump detection (`VW_INPUT_JUMP_DISCONTINUITY_US = 5s`), epoch restarts, SPU channel flushing, explicit `is_seeking` repositioning, and session ID validation. Observation (from step 17): network transport-level discontinuities (re-buffer, jitter) also set VLC `BLOCK_FLAG_DISCONTINUITY`, so a flag-triggered restart without a clock-jump threshold would clear captions on jittery VOD/live streams; the 5s jump gate is the fix.
- [ ] 17e. Transcription Quality Pass: raise caption quality without growing window latency — decode quality and model selection, keeping the 8 s window / 2 s hop geometry. Beam-search decoding (`WHISPER_SAMPLING_BEAM_SEARCH`, `beam_size`, temperature fallback) and optional cross-window prompt context in `vw_whisper_engine.c`; user-selectable model (`--model` / `model-path` already plumbed; add `base.en`/`small.en` support and discovery), documenting the RAM/CPU/inference-time tradeoff per model; keep greedy decode as fallback. Do NOT change window geometry — latency is bounded by steps 17c/17d.
- [ ] 18. Package local developer build and run end-to-end local video and stream acceptance tests.

**Exit Status:** **IN PROGRESS** — Local and stream media show real-time captions with full play/pause timeline sync and seamless seeking support; zero audio stutter or memory leaks.

---

## Milestone 4: Release Discipline & Post-MVP (Planned)

- [ ] 19. Add CI build matrix (Ubuntu host -> Windows x64 worker/plugin), static analysis, unit/contract tests, artifact hashes, and SBOM/licenses.
- [ ] 20. Add Windows VM smoke test matrix for pinned VLC installation.
- [ ] 21. Standalone settings GUI (`vlc-whisper-settings.exe`) per ADR-011 for model selection, CPU thread count, and language policy.
- [ ] 22. Multilingual models (`small`, `medium`, `large`) and automatic language detection.
- [ ] 23. Release documentation: troubleshooting, privacy statement, uninstall guide, and bug report templates.
- [ ] 24. Benchmark suite and performance output metrics (inference latency, queue high-water mark, audio processing speed ratio).

**Exit Status:** **PLANNED** — Reproducible signed/hashed release package with documented compatibility matrix.
