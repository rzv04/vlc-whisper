# Coding Style Guidelines

This document outlines the code formatting standards and architectural guidelines for `vlc-whisper`.

---

## 1. Overview

Code formatting is strictly automated using [`clang-format`](https://clang.llvm.org/docs/ClangFormat.html). The configuration file `.clang-format` located at the repository root governs formatting across all C and C++ files.

---

## 2. Base Style & Core Rules

Our code formatting builds upon the **Google Style Guide** with explicit project overrides:

| Parameter | Value | Description |
| :--- | :--- | :--- |
| **Base Style** | `Google` | Derived from Google C/C++ Style Guide |
| **Language Standard** | `C17` | Standard C17 (`-std=c17`) for all authored code |
| **Indent Width** | `2` | 2 spaces per indentation level (NO hard tabs `\t`) |
| **Column Limit** | `120` | Maximum line width of 120 characters |
| **Language** | `Cpp` | Applies to `.c`, `.h`, `.cpp`, and `.hpp` files |

---

## 3. Naming Conventions

To ensure consistent symbol namespacing and prevent linkage conflicts:

- **Files**: Lowercase snake_case prefixed with `vw_` (e.g. `vw_protocol_codec.c`, `vw_queue.h`).
- **Functions**: Lowercase snake_case prefixed with `vw_` (e.g. `vw_protocol_encode_frame()`, `vw_queue_push()`).
- **Structs / Typedefs**: Lowercase snake_case ending with `_t` and prefixed with `vw_` (e.g. `vw_frame_header_t`, `vw_caption_segment_t`).
- **Enums**: Uppercase `VW_ENUM_NAME` for enum types, and `VW_PREFIX_KEY` for enum values (e.g. `VW_MSG_HELLO`, `VW_MSG_AUDIO_PCM`).
- **Macros / Constants**: Uppercase `VW_` (e.g. `VW_MAX_PAYLOAD_BYTES`, `VW_PROTOCOL_VERSION_MAJOR`).
- **Header Guards**: `VW_<MODULE>_<FILENAME>_H_` (e.g. `#ifndef VW_PROTOCOL_TYPES_H_`, `#define VW_PROTOCOL_TYPES_H_`).

---

## 4. Header & Include Guidelines

Include headers in the following strict order, separated by blank lines:

1. Main corresponding header for the `.c` file (e.g. `"vw_protocol_codec.h"` in `vw_protocol_codec.c`).
2. Standard C system library headers in angle brackets (e.g. `<stdio.h>`, `<stdint.h>`, `<stdbool.h>`).
3. Third-party library headers in angle brackets or quotes (e.g. `<vlc_common.h>`, `"whisper.h"`).
4. Internal project headers in quotes using relative or include-path qualified includes (e.g. `"vw_protocol.h"`).

Every header file MUST be self-contained and include `#pragma once` or header guards.

---

## 5. Architectural & Real-Time Safety Rules

- **Zero Callback Block**: Never perform IPC I/O, Whisper inference, blocking lock waits, or memory allocation inside VLC audio callbacks.
- **Bounded Overload Handling**: When audio queues fill up, drop old PCM chunks and increment `audio_dropped_us`. Never stall playback.
- **Strict Privacy**: Logs must NEVER contain raw PCM samples, transcript text, or local file system paths.
- **Media Timestamp Alignment**: Always compute and pass media PTS in 64-bit microseconds (`int64_t pts_us`), never wall-clock time.

---

## 6. Developer Tools & Formatting Checks

```bash
# Format a single file in-place
clang-format -i path/to/file.c

# Format all C/C++ files across the project
find plugin worker protocol tests -type f \( -name "*.c" -o -name "*.h" -o -name "*.cpp" -o -name "*.hpp" \) -exec clang-format -i {} +
```
