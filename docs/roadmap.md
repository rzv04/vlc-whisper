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

- [x] 9. Build the smallest C VLC module against the pinned target and verify load/unload with `-vvv` logs (`plugin/src/vlc_whisper_module.c`).
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
- [ ] 14b. Plugin client API and transport: extend `vw_worker_client` with session send API (`START`/`AUDIO`/`STOP`, session and sequence tracking), add `vw_ipc_receive_timeout` transport API, and update process supervision (`vw_platform_wait_process` / out-handle).
- [ ] 14c. Plugin real-time streaming & worker thread split: drain SPSC queue on background sender thread to feed `AUDIO` frames across IPC in real time, drain worker frames, degrade to passthrough on pipe death; split `vw_worker.c` into decoupled IPC reader thread + inference worker thread (ADR-013).
- [ ] 15. Receive incoming `SEGMENT` frames on plugin background thread and trigger `vw_caption_presenter_display()`.
- [ ] 16. Implement Play/Pause lifecycle: send `PAUSE`/`RESUME` IPC control frames, suspend PCM queue forwarding on pause, and resume timeline PTS sync on resume.
- [ ] 17. Implement Seeking & Discontinuity support: detect `BLOCK_FLAG_DISCONTINUITY` / non-monotonic PTS, clear active presenter captions, send `STOP` (`SEEK_DISCONTINUITY`), reset SPSC queue/VAD state, and start new session epoch without interrupting playback.
- [ ] 17a. GPU Whisper (Vulkan) Acceleration: Add `ggml-vulkan` backend to worker CLI (`--backend auto|gpu|cpu` & `--gpu-device`), automatic CPU fallback, and parallel build memory limit documentation.
- [ ] 17b. Native SPU Subpicture Subsystem (Look-Ahead Phase 1): Integrate `vout_RegisterSubpictureChannel` and `vout_PutSubpicture` with proper `subpicture_region_New(VLC_CODEC_TEXT)` text regions, `text_segment_New()`, `VW_WEAK` MinGW symbol linkage, and system-to-media date domain conversion (`mdate() - input_time`).
- [ ] 17c. Ahead-of-Time Source File Decoding (Look-Ahead Phase 2): Extend IPC protocol to v1.1 (`VW_CAPABILITY_SOURCE_MODE`, `source_url`, `POSITION` lead pacing), implement FFmpeg (Linux) / Media Foundation (Windows) worker demuxer, Model-Once process lifetime (`ADR-015`), and process-wide `MFStartup`/`MFShutdown`.
- [ ] 17d. Seek Re-Sync Engine (Look-Ahead Phase 3): Implement input clock jump detection (`VW_INPUT_JUMP_DISCONTINUITY_US = 5s`), epoch restarts, SPU channel flushing, explicit `is_seeking` repositioning, and session ID validation.
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
