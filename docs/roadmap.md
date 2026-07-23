# Roadmap

## Milestone 0: Lock assumptions

1. Select and record the exact Windows x64 VLC release/build, installer layout, compiler, MinGW target triplet, and whisper.cpp commit.
2. Create repository, CMake presets, C17 warning policy, formatter/linter configuration, `.gitignore`, license decision, third-party notices, and reproducible toolchain container/VM notes.
3. Build and run a Windows x64 `vlc-whisper-worker.exe` from Ubuntu; verify on Windows with `--version` and a deterministic self-test.
4. Add a model manifest and manually provision a checked tiny.en model; do not embed unverified downloads.

**Exit:** clean checkout builds worker for Windows and produces version/build metadata.

## Milestone 1: Worker proof

5. Implement a C worker host that loads tiny.en through whisper.cpp's C API and transcribes a fixed 16 kHz mono WAV fixture.
6. Implement VAD/window/hop configuration, final-segment normalization/deduplication, and benchmark output.
7. Implement binary frame codec, named-pipe server, HELLO/token check, START/AUDIO/STOP, and golden-frame contract tests.
8. Add bounds checks, malformed-frame fuzzing, worker crash/error behavior, and redacted structured diagnostics.

**Exit:** Windows worker transcribes fixture PCM over named pipe without network access.

## Milestone 2: VLC feasibility spike

9. Build the smallest C VLC module against the pinned target and verify load/unload with `-vvv` logs.
10. Capture decoded PCM plus PTS without blocking the audio callback; prove canonical conversion path and queue behavior.
11. Independently prove caption display: native timed subtitle route preferred, OSD overlay fallback documented.
12. Decide in-tree/pinned-VLC build versus supported out-of-tree packaging using observed Windows behavior.

**Exit:** a test module sees timestamped audio and displays a static/deterministic timed caption on the reference VLC.

## Milestone 3: Local-file MVP

13. Integrate bounded SPSC audio queue, sender thread, worker supervisor, and authenticated pipe.
14. Receive/validate final segments; schedule/clear them through the proven VLC presenter.
15. Implement lifecycle: start, play, pause, resume, stop/end, worker missing/crash, and unsupported source rejection.
16. Implement discontinuity detection; seeking/rate/title changes must clear captions and fail only the caption session gracefully.
17. Package a developer install, write a local-video quickstart, and run end-to-end Windows acceptance fixtures.

**Exit:** local English files show captions during normal play/pause; VLC remains stable if captions fail.

## Milestone 4: Release discipline

18. Add GitHub/GitLab CI build matrix (Ubuntu host -> Windows x64 worker/plugin), static analysis, unit/contract tests, artifact signing/hashes, SBOM/third-party notices, and release manifests.
19. Add Windows VM smoke tests for the pinned VLC installation and manual performance/compatibility matrix.
20. Publish troubleshooting, supported hardware baseline, privacy statement, uninstall/rollback, known limitations, and bug report template.

**Exit:** reproducible signed/hashed development release with documented limits.

## Post-MVP

21. Add `RESET`/timeline epoch and segment-cache invalidation; implement local-file seeking and regression suite.
22. Add source profiles for VOD and live streams separately, then test IPTV protocols/providers only as legal, accessible fixtures permit.
23. Add settings UI with validated installed-model list, language choice/auto-detect policy, CPU threads, diagnostics consent, and restart semantics.
24. Add multilingual models and performance profiles; make `base`, `small`, `medium`, and `large` availability capability- and benchmark-based, not unconditional.
25. Port platform layer to Linux: Unix socket, process supervision, installation paths, and Linux VLC test matrix.
