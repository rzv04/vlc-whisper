# VLC-Whisper Agent Coding Rules

This repository enforces strict C17 standards, architectural invariants, and privacy constraints for all contributors and AI agents.

## Core Directives

1. **C17 Language Standard**: All authored code is standard C17 (`-std=c17`). No project-authored C++ code. Third-party `whisper.cpp` is linked via its public C API (`whisper.h`).
2. **Code Style**: 2-space indentation, 120-column limit, Google C style rules. Use `clang-format`.
3. **Symbol Namespacing**: All functions, types, macros, and files use the `vw_` prefix (e.g. `vw_protocol_codec.c`, `vw_frame_header_t`).
4. **VLC Realtime Callback Safety**: NEVER perform inference, IPC write/read, blocking locks, or heap allocation inside VLC audio callbacks. Enqueue PCM to bounded SPSC queue only.
5. **Offline & Privacy Invariants**: Authenticated local IPC only (named pipe / Unix domain socket with 32-byte secret token). Zero network requests, cloud APIs, telemetry, or transcript/PCM disk logging.
6. **Timeline Synchronization**: Use signed 64-bit microsecond media timestamps (`int64_t pts_us`). Never use wall-clock time for caption timing.
7. **Discontinuity Handling**: Seeking, rate changes, or media swaps clear captions and end caption session gracefully without affecting VLC media playback.
8. **Mandatory Documentation Inspection**: Before planning or implementing any feature, refactor, or sample, ALWAYS inspect relevant project documentation in `docs/` (`architecture.md`, `product.md`, `source-layout.md`, `api-contracts.md`) to align with project design and contracts.

