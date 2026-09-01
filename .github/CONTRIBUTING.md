# Contributing to VLC-Whisper

Thank you for contributing to VLC-Whisper! Please review the following guidelines and architectural invariants before submitting pull requests.

---

## Core Directives & Standards

1. **C17 Language Standard**: All authored code must comply with standard C17 (`-std=c17`). No project-authored C++ code is permitted. Third-party `whisper.cpp` is linked exclusively via its public C API (`whisper.h`).
2. **Code Style & Formatting**: 2-space indentation, 120-column limit, Google C style rules. Format code using `clang-format`.
3. **Symbol Namespacing**: All functions, types, macros, and file names must use the `vw_` prefix (e.g., `vw_protocol_codec.c`, `vw_frame_header_t`).
4. **VLC Realtime Callback Safety**: NEVER perform inference, IPC write/read, blocking locks, or heap allocation (`malloc`/`calloc`) inside VLC audio callbacks (`pf_audio_filter`). Enqueue PCM to the bounded SPSC queue only.
5. **Offline & Privacy Invariants**: Authenticated local IPC only (named pipe on Windows, Unix domain socket on Linux with a 32-byte secret token). Zero network requests, cloud APIs, telemetry, or transcript/PCM disk logging.
6. **Timeline Synchronization**: Use signed 64-bit microsecond media timestamps (`int64_t pts_us`). Never use wall-clock time for caption timing.
7. **Discontinuity Handling**: Seeking, rate changes, or media swaps clear captions and end the caption session gracefully without affecting VLC media playback.
8. **Header Documentation**: Every non-third-party function in `.h` header files must have a concise (20–30 words) doc comment explaining its behavior and any realtime constraints.

9. **Versioning**: VLC-Whisper follows Semantic Versioning. During the 0.x phase, patch releases contain fixes and small non-breaking changes; minor releases may introduce new features or behavior changes. Public releases are tagged as vMAJOR.MINOR.PATCH.

---

## Verification Checklist

Before submitting a PR, verify that all three verification checks pass:

```bash
# 1. Code format check
clang-format --dry-run --Werror <modified-files>

# 2. Native debug build & unit test suite
cmake --preset linux-x64-debug && cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug --output-on-failure

# 3. Valgrind memory leak check
ctest --test-dir build/linux-x64-debug -T memcheck
```
