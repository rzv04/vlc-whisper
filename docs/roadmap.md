# Roadmap

## Milestone 0: Lock assumptions (Completed)

- [x] 1. Select and record exact Windows x64 VLC release/build, installer layout, compiler, MinGW target triplet, and pinned `whisper.cpp` commit.
- [x] 2. Create repository, root `CMakeLists.txt`, `CMakePresets.json`, C17 warning policy, `.clang-format`, `.gitignore`, `AGENTS.md`, and reproducible toolchain notes.
- [x] 3. Build and verify Windows x64 `vlc-whisper-worker.exe` cross-compilation from Ubuntu host.
- [x] 4. Add model manifest abstraction (`models/manifest.json`) and provision checked `ggml-tiny.en.bin` model.

**Exit Status:** **DONE** — Clean checkout builds worker for Windows and produces version/build metadata.

---

## Milestone 1: Worker proof (Completed)

- [x] 5. Implement C worker host that loads `tiny.en` through whisper.cpp's C API and transcribes a fixed 16 kHz mono WAV fixture (scaffolded in `vw_whisper_engine.c`).
- [x] 6. Implement VAD/window/hop(process frequency) configuration and final-segment normalization/deduplication (`vw_vad.c`, `vw_segment_builder.c`).
- [x] 7. Implement binary frame codec (`vw_protocol_codec.c`), named-pipe/socket server (`vw_ipc_pipe_win32.c`, `vw_ipc_socket_linux.c`), `HELLO`/`token` check, `START`/`AUDIO`/`STOP`, and integration test suites (`test_worker_ipc.c`, `test_worker_lifecycle.c`).
- [x] 8. Add bounds checks, malformed-frame validation, worker crash/error behavior, and redacted structured diagnostics (`vw_log.c`).

**Exit Status:** **DONE** — Worker transcribes fixture PCM over named pipe / Unix socket with 10s accept & 3s I/O timeouts, constant-time auth, and privacy-safe logging.

---

## Milestone 2: VLC feasibility spike (Scaffolded)

- [x] 9. Build the smallest C VLC module against the pinned target and verify load/unload with `-vvv` logs (`plugin/src/vlc_whisper_module.c`).
- [x] 10. Capture decoded PCM plus PTS without blocking the audio callback; prove canonical conversion path and queue behavior (`vw_audio_capture.c`, `vw_queue.c`).
- [ ] 11. Independently prove caption display: native timed subtitle route preferred, OSD overlay fallback documented (`vw_caption_presenter.c`).
- [ ] 12. Decide in-tree/pinned-VLC build versus supported out-of-tree packaging using observed Windows behavior.

**Exit Status:** **PLANNED** — A test module sees timestamped audio and displays a static/deterministic timed caption on the reference VLC.

---

## Milestone 3: Local-file MVP

- [ ] 13. Integrate bounded SPSC audio queue, sender thread, worker supervisor, and authenticated pipe.
- [ ] 14. Receive/validate final segments; schedule/clear them through the proven VLC presenter.
- [ ] 15. Implement lifecycle: start, play, pause, resume, stop/end, worker missing/crash, and unsupported source rejection.
- [ ] 16. Implement discontinuity detection; seeking/rate/title changes must clear captions and fail only the caption session gracefully.
- [ ] 17. Implement benchmark suite and performance output metrics (inference latency, queue high-water mark, audio processing speed).
- [ ] 18. Package a developer install, write a local-video quickstart, and run end-to-end Windows acceptance fixtures.

**Exit Status:** **PLANNED** — Local English files show captions during normal play/pause; VLC remains stable if captions fail.

---

## Milestone 4: Release discipline

- [ ] 19. Add GitHub/GitLab CI build matrix (Ubuntu host -> Windows x64 worker/plugin), static analysis, unit/contract tests, artifact signing/hashes, SBOM/third-party notices, and release manifests.
- [ ] 20. Add Windows VM smoke tests for the pinned VLC installation and manual performance/compatibility matrix.
- [ ] 21. Publish troubleshooting, supported hardware baseline, privacy statement, uninstall/rollback, known limitations, and bug report template.

**Exit Status:** **PLANNED** — Reproducible signed/hashed development release with documented limits.

---

## Post-MVP

- [ ] 22. Add `RESET`/timeline epoch and segment-cache invalidation; implement local-file seeking and regression suite.
- [ ] 23. Add source profiles for VOD and live streams separately, then test IPTV protocols/providers only as legal, accessible fixtures permit.
- [ ] 24. Add standalone settings GUI (`vlc-whisper-settings.exe`) per ADR-011 with validated installed-model list, language choice/auto-detect policy, CPU threads, diagnostics consent, and restart semantics.
- [ ] 25. Add multilingual models and performance profiles; make `base`, `small`, `medium`, and `large` availability capability- and benchmark-based, not unconditional.
- [ ] 26. Port platform layer to Linux: Unix socket, process supervision, installation paths, and Linux VLC test matrix.
