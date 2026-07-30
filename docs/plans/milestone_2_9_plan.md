# Implementation Plan - Milestone 2 Step 9: Smallest C VLC Module Load/Unload Verification

## Goal

Build the smallest standard C17 VLC audio filter plugin module (`vlc_whisper_module.c`) against pinned VLC 3.0.23 headers (`worker/third_party/vlc-3.0.23/include`), hook `vw_log.c` diagnostics sink to VLC's `msg_Dbg` / `msg_Warn` logging system using `vw_log_sink_fn`, enforce `vw_` symbol namespacing, verify module registration, load, and unload lifecycle cleanly on Linux native (`.so`) and Windows x64 (`.dll`), and document manual Windows plugin installation and registration testing (installer is out of scope).

## Context

- **Relevant docs/ADR**: `docs/architecture.md`, `docs/decisions.md` (ADR-003: Pin VLC build), `docs/roadmap.md` (Milestone 2, Step 9), `AGENTS.md` (Rule 3: `vw_` symbol namespacing).
- **VLC/worker/protocol version affected**: VLC 3.0.23 C API headers (`worker/third_party/vlc-3.0.23/include/vlc_common.h`, `vlc_plugin.h`, `vlc_filter.h`), `plugin/src/vlc_whisper_module.c`, `plugin/include/vw_plugin.h`, `protocol/include/vw_log.h`, `README.md`.
- **Assumptions and explicit non-goals**:
  - **Installer Out of Scope**: Automated Windows installer creation belongs to Milestone 4. Step 9 documents manual DLL copy and cache refresh commands.
  - **Minimal Scope**: Step 9 proves plugin load/unload and logging only. Audio capture buffers, SPSC queueing, and IPC connections belong to subsequent Milestone 2 steps (Steps 10-12).

## Deep Dive: CMake Definitions, Entry Point Logic, `dlopen` & `dlsym`

### 1. CMake Preprocessor Definitions (`__PLUGIN__` and `MODULE_STRING`)

In `plugin/CMakeLists.txt`:

```cmake
target_compile_definitions(vlc_whisper_plugin PRIVATE
    __PLUGIN__
    MODULE_STRING="vlc_whisper"
)
```

- **`__PLUGIN__`**: Required macro that informs VLC header files (`vlc_plugin.h`) that the source file is being compiled as an out-of-tree dynamic plugin (`.so` / `.dll`).
  - Triggers `DLL_SYMBOL` definition:
    - Windows (MinGW): `__declspec(dllexport)`
    - Linux (GCC/Clang): `__attribute__((visibility("default")))`
  - Ensures the entry point function is publicly exported into the shared library's dynamic symbol table.
- **`MODULE_STRING="vlc_whisper"`**: Defines the module string identifier embedded into plugin metadata.

### 2. Macro Expansion & Entry Point ABI Symbol (`vlc_entry__3_0_0f`)

When `vlc_module_begin()` / `vlc_module_end()` is compiled in `plugin/src/vlc_whisper_module.c`:

```c
vlc_module_begin()
  set_shortname("VLC-Whisper")
  set_description("Offline Whisper AI Captions Filter")
  set_capability("audio filter", 0)
  set_callbacks(vw_plugin_open, vw_plugin_close)
vlc_module_end()
```

`vlc_plugin.h` expands the preprocessor macro into the explicit exported C entry point function:

```c
__attribute__((visibility("default"))) int vlc_entry__3_0_0f(vlc_set_cb vlc_set, void *opaque) {
  // Registers module capabilities, shortname, and function pointers for vw_plugin_open & vw_plugin_close
  ...
}
```

The exported symbol name **`vlc_entry__3_0_0f`** encodes VLC 3.0.x ABI compatibility (`MODULE_SYMBOL = 3_0_0f`).

### 3. Dynamic Loading & Verification Mechanics (`dlopen` & `dlsym`)

VLC plugin discovery (and our automated unit test `test_plugin_load.c`) loads and validates the module using standard POSIX dynamic linking calls:

1. **`dlopen("libvlc_whisper_plugin.so", RTLD_NOW)`** (or `LoadLibraryA("libvlc_whisper_plugin.dll")` on Windows): Loads shared library into process memory space.
2. **`dlsym(handle, "vlc_entry__3_0_0f")`** (or `GetProcAddress(handle, "vlc_entry__3_0_0f")` on Windows): Looks up the entry point symbol in the library's exported symbol table.
   - If `dlsym` returns `NULL`, the shared library is corrupt, not a valid VLC module, or compiled without `__PLUGIN__`.
3. **Execution**: VLC calls `vlc_entry__3_0_0f(vlc_set_cb, opaque)` to query module metadata, capability (`"audio filter"`), and function pointers (`vw_plugin_open`, `vw_plugin_close`).
4. **`dlclose(handle)`** (or `FreeLibrary(handle)`): Unloads library from memory.

## Scope

- **In scope**:
  - `plugin/src/vlc_whisper_module.c`: Implement `vlc_module_begin()` / `vlc_module_end()` with `set_capability("audio filter", 0)` and `set_callbacks(vw_plugin_open, vw_plugin_close)`.
  - `plugin/src/vlc_whisper_module.c`: Implement static `vw_plugin_log_sink` matching `vw_log_sink_fn` signature from `vw_log.h` bridging `vw_log_event()` to VLC native `msg_Dbg()` / `msg_Err()` / `msg_Warn()` logging calls.
  - `plugin/CMakeLists.txt`: Ensure proper symbol export (`__PLUGIN__`, `MODULE_STRING`), header inclusion, and target output property rules for `.so` (Linux) and `.dll` (Windows MinGW).
  - `tests/integration/test_plugin_load.c`: Native CTest suite validating module entry point symbol `vlc_entry__3_0_0f` via `dlopen()` / `dlsym()`.
  - `README.md`: Document manual Windows plugin installation, plugin cache refresh (`--reset-plugins-cache`), and registration check commands.
  - Documentation: Update `docs/source-layout.md` to reflect new files.
  - Documentation: Update `docs/roadmap.md` Exit Status for Step 9.
- **Out of scope**:
  - Automated Windows installer package (`vlc-whisper-v1.0.0-win64.exe` installer - reserved for Milestone 4).
  - Full PCM audio capture (`vw_audio_capture.c`) and subtitle presentation (`vw_caption_presenter.c`).
- **Files/components expected to change**:
  - `plugin/src/vlc_whisper_module.c`
  - `plugin/include/vw_plugin.h`
  - `plugin/CMakeLists.txt`
  - `tests/CMakeLists.txt`
  - [NEW] `tests/integration/test_plugin_load.c`
  - `README.md`
  - `docs/roadmap.md`

## Design

### 1. Minimal VLC Module Structure (`plugin/src/vlc_whisper_module.c`)

```c
#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_filter.h>
#include "vw_log.h"
#include "vw_plugin.h"

// Implements callback signature matching vw_log_sink_fn from protocol/include/vw_log.h
static void vw_plugin_log_sink(vw_log_level_t level, const char *event_id, const char *formatted_msg, void *user_data) {
  vlc_object_t *obj = (vlc_object_t *)user_data;
  if (!obj) return;
  switch (level) {
    case VW_LOG_LEVEL_ERROR:
      msg_Err(obj, "[vw_log:%s] %s", event_id, formatted_msg);
      break;
    case VW_LOG_LEVEL_WARN:
      msg_Warn(obj, "[vw_log:%s] %s", event_id, formatted_msg);
      break;
    case VW_LOG_LEVEL_INFO:
    case VW_LOG_LEVEL_DEBUG:
    default:
      msg_Dbg(obj, "[vw_log:%s] %s", event_id, formatted_msg);
      break;
  }
}

// Internal plugin callbacks adhering to Rule 3 vw_ prefix
static int vw_plugin_open(vlc_object_t *obj) {
  vw_log_set_sink(vw_plugin_log_sink, obj);
  vw_log_event(VW_LOG_LEVEL_INFO, "PLUGIN_OPEN", "vlc-whisper audio filter module opened");
  return VLC_SUCCESS;
}

static void vw_plugin_close(vlc_object_t *obj) {
  vw_log_event(VW_LOG_LEVEL_INFO, "PLUGIN_CLOSE", "vlc-whisper audio filter module closed");
  vw_log_set_sink(NULL, NULL);
}

// VLC module definition macro expands to vlc_entry__3_0_0f ABI symbol
vlc_module_begin()
  set_shortname("VLC-Whisper")
  set_description("Offline Whisper AI Captions Filter")
  set_capability("audio filter", 0)
  set_callbacks(vw_plugin_open, vw_plugin_close)
vlc_module_end()
```

### 2. CMake Definitions (`plugin/CMakeLists.txt`)

Must define `__PLUGIN__` and `MODULE_STRING="vlc_whisper"` so `vlc_plugin.h` expands entry symbol `vlc_entry__3_0_0f`:

```cmake
target_compile_definitions(vlc_whisper_plugin PRIVATE
    __PLUGIN__
    MODULE_STRING="vlc_whisper"
)
```

## Acceptance Criteria

- [ ] `vlc_whisper_module.c` defines valid VLC 3.0.x plugin entry point (`vlc_entry__3_0_0f`) using standard `vlc_module_begin()` macros with `vw_` namespaced internal callbacks (`vw_plugin_open`, `vw_plugin_close`).
- [ ] `vw_plugin_log_sink` matches `vw_log_sink_fn` typedef from `vw_log.h` and forwards diagnostic log events directly into VLC's `msg_Dbg` / `msg_Err` / `msg_Warn` engine.
- [ ] `cmake --preset linux-x64-debug && cmake --build --preset linux-x64-debug` builds `libvlc_whisper_plugin.so`.
- [ ] `cmake --preset windows-x64-release && cmake --build --preset windows-x64-release` cross-compiles `libvlc_whisper_plugin.dll`.
- [ ] Automated CTest (`test_plugin_load`) verifies `dlsym(handle, "vlc_entry__3_0_0f") != NULL`.
- [ ] `README.md` documented with step-by-step manual Windows plugin installation, cache reset (`--reset-plugins-cache`), and registration check commands.
- [ ] Code format (`clang-format`), warnings-as-errors, and Valgrind memory leak checks pass cleanly.

## Test Plan

### Automated Tests

1. `clang-format --dry-run --Werror plugin/src/vlc_whisper_module.c plugin/include/vw_plugin.h tests/integration/test_plugin_load.c`
2. `cmake --preset linux-x64-debug && cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug`
3. `ctest --test-dir build/linux-x64-debug -T memcheck`

### Manual Verification & Installation Guide (Windows Host / VM)

1. **Build DLL**: Cross-compile plugin on Linux host:
   ```bash
   cmake --preset windows-x64-release
   cmake --build --preset windows-x64-release
   ```
2. **Install DLL**: Copy output binary `build/windows-x64-release/plugin/libvlc_whisper_plugin.dll` to VLC's plugin directory on Windows:
   - Path: `C:\Program Files\VideoLAN\VLC\plugins\misc\libvlc_whisper_plugin.dll`
3. **Reset Plugin Cache & Verify Registration**:
   Open Command Prompt / PowerShell on Windows:
   ```cmd
   "C:\Program Files\VideoLAN\VLC\vlc.exe" --reset-plugins-cache --list | findstr /i whisper
   ```
   _Expected Output_: Displays registered `VLC-Whisper` audio filter module.
4. **Inspect Debug Log Output**:
   Since `vlc_whisper` is an audio filter, its `vw_plugin_open` callback is *only* executed when an audio stream is actually playing and the filter is explicitly inserted into the audio chain. Just launching VLC will not open the filter.
   ```cmd
   "C:\Program Files\VideoLAN\VLC\vlc.exe" --reset-plugins-cache --audio-filter=vlc_whisper --extraintf=logger --file-logging --logfile=vlc-debug.log -vvv C:\Windows\Media\tada.wav vlc://quit
   ```
   _Expected Output_: Open `vlc-debug.log` and verify it contains the `[vw_log:PLUGIN_OPEN] vlc-whisper audio filter module opened` debug log entry.

## Definition of Done

- [ ] Standard C17 code (`-std=c17`); no C++ introduced in plugin.
- [ ] All functions, types, and static callbacks use `vw_` prefix (Rule 3).
- [ ] No blocking operations inside VLC callbacks.
- [ ] Manual Windows installation and plugin cache reset documented in `README.md`.
- [ ] Formatting, warnings-as-errors (`-Werror`), and Valgrind checks pass cleanly.
- [ ] Plan saved to `docs/plans/milestone_2_9_plan.md`.

## Evidence

- Build & test logs: To be added upon execution.
