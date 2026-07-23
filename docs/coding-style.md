# Coding Style Guidelines

This document outlines the code formatting standards and guidelines for `vlc-whisper`.

---

## 1. Overview

To maintain consistency and readability across the codebase, code formatting is automated using [`clang-format`](https://clang.llvm.org/docs/ClangFormat.html).

The configuration is specified in the root file.

---

## 2. Base Style & Core Rules

Our code formatting is based on the **Google Style Guide** with the following key overrides:

| Parameter | Value | Description |
| :--- | :--- | :--- |
| **Base Style** | `Google` | Derived from Google C/C++ Style Guide |
| **Indent Width** | `2` | 2 spaces per indentation level |
| **Column Limit** | `120` | Maximum line width of 120 characters |
| **Language** | `Cpp` | Applies to C and C++ source/header files |

---

## 3. Configuration Summary

The root file contents:

```yaml
---
Language: Cpp
BasedOnStyle: Google
IndentWidth: 2
ColumnLimit: 120
...
```

---

## 4. Key Formatting Rules

### Indentation & Spacing

- Use **2 spaces** per indentation level. Do not use hard tabs (`\t`).
- Space around binary operators (`+`, `-`, `=`, `==`, etc.).
- Space after control flow keywords (`if`, `for`, `while`, `switch`).

### Line Length & Wrapping

- Limit lines to **120 characters**.
- When splitting function calls or declarations across lines, align parameters cleanly.

### Language Standards & Dialects

- Authored code in this repository targets **C17** (or **C++17** where applicable).
- Keep header includes organized and grouped logically:
  1. Main module header
  2. System headers (`<stdio.h>`, `<stdlib.h>`, etc.)
  3. Third-party library headers (e.g. VLC SDK, whisper.h)
  4. Project headers (`"..."`)

---

## 5. Developer Tools & CI

### Running Formatting Locally

To check or format code locally:

```bash
# Format a single file in-place
clang-format -i path/to/file.c

# Format all C/C++ files in the project
find vlc-plugin worker -type f \( -name "*.c" -o -name "*.h" -o -name "*.cpp" -o -name "*.hpp" \) -exec clang-format -i {} +
```

### Editor Integration

Most modern IDEs and editors support `.clang-format` automatically:

- **VS Code**: Enable `"editor.formatOnSave": true` and select the C/C++ extension formatter.
- **CLion / Qt Creator**: Automatically respects `.clang-format` in the project root.
