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

## Windows Installation (Plug & Play Setup)

For end users with VLC 3.0 (64-bit) already installed on Windows 10/11:

### Option 1: Standalone Windows Installer (.exe) — Recommended
1. Download `vlc-whisper-0.3.0-win64-setup.exe` from GitHub Releases.
2. Run the installer. It will automatically detect your 64-bit VLC directory (e.g. `C:\Program Files\VideoLAN\VLC`), deploy `libvlc_whisper_plugin.dll` to `plugins\audio_filter\`, place the AI worker and models into `models\`, rebuild the VLC plugin cache (`plugins.dat`), and generate desktop/start menu shortcuts.
3. Launch VLC using the created shortcut **"VLC (with AI Whisper Captions)"** (or pass `--audio-filter=vlc_whisper`).
4. Play any video or audio stream — AI subtitles appear automatically in real time!

### Option 2: Portable Release Archive (.zip)
1. Download `vlc-whisper-0.3.0-win64.zip`.
2. Extract the contents directly into your VLC installation folder (e.g. `C:\Program Files\VideoLAN\VLC`).
3. Open a terminal in the VLC folder and regenerate the plugin cache:
   ```cmd
   vlc-cache-gen.exe "C:\Program Files\VideoLAN\VLC\plugins"
   ```
4. Launch VLC with `--audio-filter=vlc_whisper`.

---

## Compiling the Windows Installer & Packaging Releases

To compile the standalone Windows installer with both Vulkan GPU worker and CPU fallback (requires `nsis` / `makensis`):

```bash
# 1. Install NSIS compiler
sudo apt-get install -y nsis

# 2. (Optional) Build CPU fallback worker to bundle alongside GPU worker
cmake --preset windows-x64-release-cpu
cmake --build --preset windows-x64-release-cpu -j4
cp build/windows-x64-release-cpu/worker/vlc-whisper-worker-cpu.exe build/windows-x64-release/worker/ 2>/dev/null || true

# 3. Build Windows release binaries and NSIS setup installer
cmake --preset windows-x64-release
cmake --build --preset windows-x64-release --target installer

# 4. Generate portable release ZIP archive
cpack --config build/windows-x64-release/CPackConfig.cmake
```

_Outputs generated in `build/windows-x64-release/`:_
- `vlc-whisper-0.3.0-win64-setup.exe` (~74 MB standalone setup wizard with embedded Whisper tiny.en + Silero VAD weights)
- `vlc-whisper-0.3.0-win64.zip` (Portable release archive)

---

## Manual Plugin Installation (Windows Developer Workflow)

To install and verify the VLC plugin manually during development:

1. **Install DLL**: Copy the compiled `libvlc_whisper_plugin.dll` to your VLC installation's plugin directory:
   - Path: `C:\Program Files\VideoLAN\VLC\plugins\audio_filter\libvlc_whisper_plugin.dll`

2. **Install Worker**: Copy the compiled `vlc-whisper-worker.exe` to your VLC root directory:
   - Path: `C:\Program Files\VideoLAN\VLC\vlc-whisper-worker.exe`
   - The worker is self-contained: all MinGW runtime is statically linked — zero external DLL dependencies (ADR-010).

3. **Install the Models**:
   - Speech Model: `C:\Program Files\VideoLAN\VLC\models\ggml-tiny.en.bin`
   - VAD Model: `C:\Program Files\VideoLAN\VLC\models\ggml-silero-vad.bin`

4. **Reset Plugin Cache & Verify Registration**:
   ```cmd
   "C:\Program Files\VideoLAN\VLC\vlc-cache-gen.exe" "C:\Program Files\VideoLAN\VLC\plugins"
   "C:\Program Files\VideoLAN\VLC\vlc.exe" --reset-plugins-cache --list | findstr /i whisper
   ```

5. **Run VLC with AI Subtitles**:
   ```cmd
   "C:\Program Files\VideoLAN\VLC\vlc.exe" --audio-filter=vlc_whisper C:\path\to\media.mp4
   ```

---

## Open Source License & Third-Party Notices

VLC-Whisper is released under the permissive [MIT License](LICENSE).

All bundled models and runtime components adhere to open-source licensing:
- `whisper.cpp` & `ggml`: MIT License
- OpenAI Whisper Weights: MIT License
- Silero VAD Weights: MIT License
- VLC Media Player Plugin API: LGPL v2.1+ (Dynamic Linking / Out-of-tree plugin)
- Windows Media Foundation: Standard Windows OS Component
- Vulkan SDK Headers & Loader: Apache License 2.0
- MinGW-w64 Runtime: GNU GPL v3 with GCC Runtime Library Exception v3.1

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for full legal attributions and license texts.
