# VLC-Whisper

Offline, real-time speech captions inside VLC for local media.

---

## System Prerequisites

To build and test the project, install the required packages for your development environment:

### Ubuntu / Debian

```bash
# Core build system and native compilers
sudo apt-get update && sudo apt-get install -y \
  cmake ninja-build build-essential gcc g++ clang-format valgrind gcovr

# MinGW-w64 cross-compilers (required for Windows x64 targets)
sudo apt-get install -y \
  gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64 binutils-mingw-w64-x86-64

# Vulkan SDK and shader compiler (required for Linux GPU acceleration)
sudo apt-get install -y libvulkan-dev glslc
```

### Fedora / RHEL

```bash
sudo dnf install -y cmake ninja-build gcc gcc-c++ clang-tools-extra valgrind \
  mingw64-gcc mingw64-gcc-c++ vulkan-loader-devel glslc
```

---

## Cloning the Project

The repository includes `whisper.cpp` as a Git submodule in `worker/third_party/whisper.cpp`. Clone recursively so all submodules are initialized:

```bash
git clone --recursive <repository-url>
cd vlc-whisper
```

If you already cloned without `--recursive`, initialize submodules before configuring CMake:

```bash
git submodule update --init --recursive
```

### Optional: enable [conventional-commits](https://www.conventionalcommits.org/en/v1.0.0/) hook locally ([conventional-commits](https://www.conventionalcommits.org/en/v1.0.0/) will be enforced in CI)

```bash
git config --local core.hooksPath .githooks
```

---

## Building the Project

The project uses CMake (minimum version 3.20) with Ninja and CMake Presets for both native Linux development and cross-compiling standalone Windows x64 binaries via MinGW GCC.

### Available Presets in `CMakePresets.json`

| Preset Name               | Target OS & Type      | Backend                        | Output Worker Binary         | Notes                                      |
| ------------------------- | --------------------- | ------------------------------ | ---------------------------- | ------------------------------------------ |
| `linux-x64-debug`         | Linux Native (Debug)  | Vulkan GPU (auto CPU fallback) | `vlc-whisper-worker`         | Primary Linux dev & test target            |
| `linux-x64-debug-cpu`     | Linux Native (Debug)  | CPU-only                       | `vlc-whisper-worker-cpu`     | No Vulkan/glslc requirement                |
| `linux-x64-coverage`      | Linux Native (Debug)  | CPU-only + gcov                | `vlc-whisper-worker-cpu`     | Code coverage instrumentation              |
| `windows-x64-release`     | Windows x64 (Release) | Vulkan GPU (auto CPU fallback) | `vlc-whisper-worker.exe`     | Requires `VW_VULKAN_SDK` (see below)       |
| `windows-x64-release-cpu` | Windows x64 (Release) | CPU-only                       | `vlc-whisper-worker-cpu.exe` | Fully self-contained, no Vulkan SDK needed |
| `windows-x64-debug`       | Windows x64 (Debug)   | Vulkan GPU (auto CPU fallback) | `vlc-whisper-worker.exe`     | Debug symbols included                     |
| `windows-x64-debug-cpu`   | Windows x64 (Debug)   | CPU-only                       | `vlc-whisper-worker-cpu.exe` | Debug symbols, CPU-only                    |

---

### Building on Linux (Native)

#### 1. Linux GPU Build (Vulkan Acceleration)

Requires `libvulkan-dev` and `glslc`.

```bash
cmake --preset linux-x64-debug
cmake --build --preset linux-x64-debug -j4
ctest --preset linux-x64-debug --output-on-failure
```

_(If `glslc` or `libvulkan-dev` is missing, CMake will warn and automatically degrade to building `vlc-whisper-worker-cpu`)._

#### 2. Linux CPU-Only Build

```bash
cmake --preset linux-x64-debug-cpu
cmake --build --preset linux-x64-debug-cpu -j4
```

---

### Building for Windows (MinGW Cross-Compilation from Linux)

#### 1. Windows CPU-Only Build (No Vulkan SDK required)

Produces statically-linked, standalone Windows binaries with zero external DLL dependencies:

```bash
cmake --preset windows-x64-release-cpu
cmake --build --preset windows-x64-release-cpu -j4
```

_Outputs_: `build/windows-x64-release-cpu/bin/vlc-whisper-worker-cpu.exe` and `build/windows-x64-release-cpu/bin/libvlc_whisper_plugin.dll`.

#### 2. Windows GPU Build (Vulkan Accelerated)

Cross-compiling `ggml-vulkan` for Windows requires target Windows Vulkan import headers and MinGW library (`libvulkan-1.a`).

Set the `VW_VULKAN_SDK` environment variable pointing to the Windows Vulkan MinGW layout:

```text
$VW_VULKAN_SDK/
├── mingw/
│   ├── include/vulkan/ (vulkan.h, etc.)
│   └── lib/libvulkan-1.a
└── spv/
    └── include/spirv/ (unified1/spirv.h, etc.)
```

Build command:

```bash
VW_VULKAN_SDK=/path/to/vulkan-sdk cmake --preset windows-x64-release
cmake --build --preset windows-x64-release -j4
```

_(Without `VW_VULKAN_SDK`, `windows-x64-release` prints a warning and automatically falls back to producing the CPU-only worker `vlc-whisper-worker-cpu.exe`)._

---

### GPU (Vulkan) Runtime Notes & Build Memory Limits

- **Runtime Fallback**: When `--backend auto` (default) or `--backend gpu` is used, `whisper.cpp` probes the GPU and transparently falls back to CPU if no physical Vulkan driver/GPU is present at runtime.
- **Worker Flags**: Pass `--backend auto|gpu|cpu` or `--gpu-device <id>` when launching the worker manually.
- **Build RAM Usage**: Compiling Vulkan SPIR-V shaders (`glslc`) creates high memory pressure. On systems with ≤8 GB RAM or VMs, limit build concurrency to `-j1` or `-j2` (e.g. `cmake --build --preset <preset> -j1`) to avoid OOM or swap thrashing. The `*-cpu` presets do not invoke `glslc` and are low-memory build targets.

---

## Running Tests

The project includes unit and integration tests.

### Using CMake Presets (Linux native)

To compile and run tests natively on Linux during development:

```bash
# Configure the native Linux debug build
cmake --preset linux-x64-debug

# Build tests (use -j2 on low-RAM hosts: the first Vulkan build compiles glslc shaders,
# which spikes memory — see the GPU acceleration section above)
cmake --build --preset linux-x64-debug -j4

# Run tests and show output for failed ones
ctest --preset linux-x64-debug --output-on-failure
```

### Manual Configuration (Without presets)

```bash
cmake -B build -S .
cmake --build build -j4
cd build && ctest --output-on-failure
```

To run the test suite through Valgrind to check for memory leaks and invalid accesses (requires `valgrind` installed):

```bash
cmake --build --preset linux-x64-debug -j$(nproc)
ctest --test-dir build/linux-x64-debug -T memcheck --output-on-failure
```

For stricter leak detection (fail on any leak):

```bash
ctest --test-dir build/linux-x64-debug -T memcheck --output-on-failure \
  --extra-memcheck-options=--leak-check=full \
  --extra-memcheck-options=--error-exitcode=1
```

### Code Coverage Testing (Linux Native Only)

To generate code coverage reports for project-authored C17 code (excluding third-party libraries and tests), ensure `gcovr` is installed and run:

```bash
# Configure the native Linux coverage build
cmake --preset linux-x64-coverage

# Build and run tests to generate coverage data (.gcda)
cmake --build --preset linux-x64-coverage
ctest --preset linux-x64-coverage

# Generate HTML coverage report (output to build/coverage.html)
gcovr -r . --html-details build/coverage.html --exclude 'worker/third_party/' --exclude 'tests/'

# Or print coverage summary to terminal
gcovr -r . --exclude 'worker/third_party/' --exclude 'tests/'
```

---

### Option 2: Manual CMake Configuration

```bash
# Configure from repository root
cmake -S . -B build/windows-x64 \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchains/windows-x64-mingw.cmake"

# Build all primary targets
cmake --build build/windows-x64
```

---

## Building Sample Snippets

Code snippets located in `samples/snippets/` are registered as standalone CMake targets (`sample_<snippet_name>`).

By default, samples are marked `EXCLUDE_FROM_ALL` and are **not** compiled during standard project builds. To compile sample binaries separately:

```bash
# Build a specific sample snippet (e.g. samples/snippets/whisper_pcm.c)
cmake --build --preset windows-x64-release --target sample_whisper_pcm

# Build all sample snippets at once
cmake --build --preset windows-x64-release --target samples
```

The compiled Windows `.exe` sample binaries are output to `build/windows-x64-release/samples/` (e.g., `sample_whisper_pcm.exe`).

### Running Sample Snippets on Windows

Sample snippet executables accept the path to a GGML model file via command-line arguments:

```cmd
# Run sample_whisper_pcm with a model path
sample_whisper_pcm.exe C:\path\to\ggml-tiny.en.bin
```

---

## Static Runtime Linking (MinGW)

The MinGW cross-compilation setup automatically configures static linking (`-static`, `-static-libgcc`, `-static-libstdc++`, static `libgomp.a`, and static `libwinpthread.a`).

This ensures that output binaries (`vlc-whisper-worker.exe`, `vlc_whisper_plugin.dll`, `sample_whisper_pcm.exe`) are completely self-contained and run natively on Windows without requiring external MinGW runtime DLLs (`libgomp-1.dll`, `libwinpthread-1.dll`, `libstdc++-6.dll`, etc.).

---

## Manual Plugin Installation (Windows)

To install and verify the VLC plugin manually on Windows:

1. **Install DLL**: Copy the compiled `libvlc_whisper_plugin.dll` to your VLC installation's plugin directory:
   - Example path: `C:\Program Files\VideoLAN\VLC\plugins\misc\libvlc_whisper_plugin.dll`

2. **Install Worker**: Copy the compiled `vlc-whisper-worker.exe` to your VLC installation's root directory:
   - Example path: `C:\Program Files\VideoLAN\VLC\vlc-whisper-worker.exe`
   - The worker is self-contained: all MinGW runtime (incl. OpenMP) is statically linked — no extra DLLs to copy (ADR-010).
   - The plugin looks for the worker next to the plugin, up to three ancestor directories, and next to the VLC executable. If your layout places it elsewhere, set the module option `--vlc-whisper-worker-path` (a.k.a. `worker-path`) to its full path.

3. **Install the Models**:
   - **Speech Model (`ggml-tiny.en.bin`)**: Copy `ggml-tiny.en.bin` next to the worker (VLC root), into a `models\` subdirectory of any ancestor of the plugin, or next to the VLC executable — the plugin probes `<dir>\ggml-tiny.en.bin` and `<dir>\models\ggml-tiny.en.bin` during module open. If the model lives elsewhere, set the module option `--vlc-whisper-model-path` (a.k.a. `model-path`) to its full path. Without a model, captions are disabled cleanly (`E_MODEL_MISSING`) and playback is unaffected.
   - **Voice Activity Detection Model (`ggml-silero-vad.bin`) (Optional but Recommended)**:
     - Download helper scripts are provided in `models/`:
       - **Linux / POSIX**: `./models/download-vad-model.sh`
       - **Windows**: `.\models\download-vad-model.cmd`
     - Place `ggml-silero-vad.bin` in the same directory as your speech model or pass `--vad-model <path>` to the worker. The worker automatically detects `ggml-silero-vad.bin` in the model directory or alongside the worker executable.
     - *Zero-Config Fallback*: If `ggml-silero-vad.bin` is not provided, the worker automatically falls back to built-in RMS Energy VAD. Silero VAD provides superior discrimination between human voice and background music/soundtracks, suppressing phantom captions during non-speech audio.

4. **Reset Plugin Cache & Verify Registration**:
   Open Command Prompt or PowerShell and run:

   ```cmd
   "C:\Program Files\VideoLAN\VLC\vlc.exe" --reset-plugins-cache --list | findstr /i whisper
   ```

   _Expected Output_: You should see the `VLC-Whisper` audio filter module listed. (Always re-run `--reset-plugins-cache` after copying a new plugin DLL — VLC caches module metadata and may otherwise keep using the old one.)

   **Listed ≠ active.** The module being listed (or shown as "enabled" in Tools → Preferences) does not mean it runs: an audio filter is only instantiated when it is actually in the audio chain. Either (a) pass `--audio-filter=vlc_whisper` on the command line, or (b) enable it in Preferences → All → Audio → Filters (it appears there because the module declares the audio-filter subcategory). If you toggle it in the GUI, restart VLC before playing.

5. **Inspect Debug Logs**:
   Audio filters only instantiate when audio media is playing and the filter is explicitly selected in the audio chain. To trigger `vw_plugin_open` and write debug output to a file:

   ```cmd
   "C:\Program Files\VideoLAN\VLC\vlc.exe" --reset-plugins-cache --audio-filter=vlc_whisper --file-logging --logfile=vlc-debug.log -vvv C:\path\to\audio.mp3
   ```

   _(Note: For `.mp4` video files in Virtual Machines or dual-GPU laptops, add `--avcodec-hw=none` to avoid D3D11 hardware acceleration crashes)._

   _Expected Output_: Inspect `vlc-debug.log` to confirm `vlc_whisper debug: [vw_log:PLUGIN_OPEN] vlc-whisper audio filter module opened`.

   **Success signal**: while the audio plays, `vlc-whisper-worker.exe` must appear in Task Manager (it is a long-lived process, not a flash; with `CREATE_NO_WINDOW` it shows under _Background processes_, not Apps). If it appears, the plugin spawned it; if `PLUGIN_OPEN` is logged but no worker appears, check `PLUGIN_WORKER_UNAVAILABLE`/`PLUGIN_SESSION_START_FAIL` in the log (worker path, model path, or spawn failure). If `PLUGIN_OPEN` itself is missing, the filter is not in the chain — re-check `--audio-filter=vlc_whisper` and the module cache.

   **Worker diagnostics**: the worker is spawned with no console, so its stdout/stderr (whisper output, worker lifecycle logs) are captured to `%TEMP%\vlc-whisper-worker.log` on Windows (`/tmp/vlc-whisper-worker.log` or `$XDG_RUNTIME_DIR` on Linux) — truncated every run, so it always holds the last worker session. When diagnosing "worker died" issues, check this file — it shows `WORKER_LIFECYCLE`/`WORKER_ENGINE`/`WORKER_SESSION`/`WORKER_INFERENCE`/`WORKER_READER` events up to the crash. To place the log elsewhere, pass `--vlc-whisper-worker-path ... --log-file <path>` via the worker invocation (the plugin passes through `worker-path`; for a custom worker log location, set the `model-path`-style worker option or launch the worker manually with `--log-file`). The VLC log additionally carries `PLUGIN_WORKER_LAUNCH` (resolved worker path), `PLUGIN_WORKER_CONNECT`, and `PLUGIN_WORKER_DEAD` (failing call + chunk count).
