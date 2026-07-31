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
- [x] 12. Decide in-tree/pinned-VLC build versus supported out-of-tree packaging using observed Windows behavior.

**Exit Status:** **DONE** — A test module sees timestamped audio and displays a static/deterministic timed caption on the reference VLC.

---

## Milestone 3: Integrated live captioning pipeline (Planned)

- [ ] 13. Connect VLC plugin IPC client (`vw_worker_client.c`) to worker process during module `Open`.
- [ ] 14. Feed captured PCM chunks from SPSC queue across IPC transport to worker process in real time.
- [ ] 15. Receive incoming `SEGMENT` frames on plugin background thread and trigger `vw_caption_presenter_display()`.
- [ ] 16. Implement seek/discontinuity handling: clear captions, send `STOP` (`SEEK_DISCONTINUITY`), and reset session on non-monotonic PTS.

**Exit Status:** **PLANNED** — End-to-end real-time captioning working during media playback with zero audio stutter or memory leaks.
