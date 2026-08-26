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
# NOTE: the Vulkan-enabled linux-x64-debug preset MUST build with -j1 on
# 8 GB-RAM machines: ggml-vulkan's mul_mm.comp.cpp alone can consume most of
# the memory and parallel cc1plus instances get OOM-killed (silent build fail).
cmake --build --preset linux-x64-debug -j1
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
- `vlc-whisper-0.3.0-win64-setup.exe` (~74 MB standalone setup wizard with embedded Whisper tiny + Silero VAD weights)
- `vlc-whisper-0.3.0-win64.zip` (Portable release archive)
---

## Step 19b — Settings GUI (Lua extension)

### What it is

A VLC Lua extension dialog (“VLC-Whisper Settings”) hosted inside VLC (`View` > `VLC-Whisper Settings`).
It exposes four controls that map directly to the plugin’s config namespace:

| Control | Config key | Values / mapping |
|---------|------------|-------------------|
| Engine (backend) | `whisper-backend` | `auto` (default) · `gpu` (Vulkan) · `cpu` |
| Model | `model-path` | dropdown labels map to **relative** paths under `models/`: `tiny.en` → `models/ggml-tiny.en.bin`, `tiny` → `models/ggml-tiny.bin`, `base.en` → `models/ggml-base.en.bin`, `base` → `models/ggml-base.bin`, `small` → `models/ggml-small.bin`, `medium` → `models/ggml-medium.bin`, `large` → `models/ggml-large-v3.bin` (selection allowed even if file absent; missing file disables captions with `E_MODEL_MISSING` until provisioned — see 19c) |
| Language | `whisper-language` | `en` (default) · `ro` · `tr` · `de` · `fr` · `es` — **no `auto` entry** in this dialog; the bundled `tiny` is multilingual, but automatic language selection remains a later UI step |
| Threads | `whisper-threads` | integer `1..16`, default `4` (clamped on Apply) |
| Detected backend (read-only) | `whisper-backend-active` | informational label refreshed on dialog open; shows `gpu` or `cpu` as resolved by the worker (`STATUS` v1.3 `resolved_backend`) after the first session `STARTED` |

The dialog preselects current values on open via `vlc.config.get` (nil-safe defaults `auto` / bundled
`models/ggml-tiny.bin` / `en` / `4`). If the user has not selected another model, plugin discovery prioritizes the
bundled `tiny`; an explicit user-selected `model-path` remains authoritative. `Apply` validates threads (`tonumber`
+ clamp `1..16`), writes all four keys via `vlc.config.set`, and logs `[VLC-Whisper] applied …` lines (filter
`Tools > Messages`).

### How settings apply

- **Backend and model** require a worker respawn (the `whisper_context` is built at init). The plugin’s sender loop polls the four config keys every ~2 s; any diff vs the last-applied snapshot triggers `vw_plugin_respawn_worker()` — mid-play this produces a brief caption gap (~worker restart time) then captions resume on the new epoch (existing `session_id` machinery drops stale segments).
- **Language and threads** *could* apply per-call (SOT token / `n_threads` are `whisper_full_params` state), but **this iteration applies all four via respawn** as well. Documenting honestly: live per-call apply for language/threads without restart is a future optimization — no behavior difference is observable except the brief gap.
- The **Detected backend** label appears as `(pending — start playback)` until the first `STATUS` after `STARTED`; after the worker reports `resolved_backend` the plugin mirrors it into `whisper-backend-active` and the label shows `gpu` or `cpu`.

### Windows manual test (verbatim)

1. Install the plugin + worker either by running the installer **or** by manual copy:
   ```cmd
   REM manual copy (developer workflow)
   copy build\windows-x64-release\plugin\libvlc_whisper_plugin.dll "C:\Program Files\VideoLAN\VLC\plugins\audio_filter\libvlc_whisper_plugin.dll"
   copy build\windows-x64-release\worker\vlc-whisper-worker.exe "C:\Program Files\VideoLAN\VLC\vlc-whisper-worker.exe"
   copy lua\extensions\vlc_whisper_settings.lua "C:\Program Files\VideoLAN\VLC\lua\extensions\vlc_whisper_settings.lua"
   REM with installer the lua file is already bundled
   "C:\Program Files\VideoLAN\VLC\vlc-cache-gen.exe" "C:\Program Files\VideoLAN\VLC\plugins"
   ```
   Verify:
   ```cmd
   dir "C:\Program Files\VideoLAN\VLC\lua\extensions\vlc_whisper_settings.lua"
   ```

2. Open `Tools > Messages`, set **Verbosity** to `2` (Debug) — or launch with `vlc.exe -vvv --audio-filter=vlc_whisper`.

3. Play any media. Open `View > VLC-Whisper Settings` (on some skins `Tools > Extensions > VLC-Whisper Settings`). The dialog preselects current values; the **Detected backend** label initially shows `(pending — start playback)` and switches to `gpu` or `cpu` after the first session starts.

4. Change **Language** `en` → `ro`, click **Apply**. Expected within ~2 s (next sender-loop poll):
   - Log line `[VLC-Whisper] applied whisper-language=ro` (plus the other three `applied …` lines).
   - Plugin log `PLUGIN_RESPAWN` and worker restart (`worker` process respawn, `STARTED` with new `session_id`).
   - Captions continue after the brief gap (new epoch).

5. The bundled `tiny` model is multilingual, so selecting a supported concrete language such as `ro` is valid. The
   dialog still has no `auto` entry; automatic language selection remains a future UI step.

6. Change **Engine** to `gpu` on a machine without Vulkan support, click **Apply**. After restart the **Detected backend** label reads `cpu` (worker resolved `VW_HAVE_VULKAN` → `cpu`; `STATUS` `resolved_backend` is mirrored into `whisper-backend-active`). Log shows `PLUGIN_RESPAWN` and backend resolution.

7. Close and re-open the dialog — it re-reads `whisper-backend`, `model-path`, `whisper-language`, `whisper-threads` and shows the last applied values.

### What NOT to expect (19b scope)

- **No auto-detect language option** — deliberately omitted from the Language dropdown (only `en`/`ro`/`tr`/`de`/`fr`/`es`).
- **No translation** — language selects the Whisper SOT token only; translation to another language is 21b (opt-in, network).
- **Settings do NOT persist across VLC restart unless VLC exits cleanly** — `vlcrc` is saved on clean exit (`config_SaveConfigFile`); if VLC is killed, the last `Apply` is lost. Re-apply after a crash or set keys in `vlcrc` manually.


### Downloading additional models

The installer bundles the universal multilingual `ggml-tiny.bin` as the default model — offline
captions work immediately with no download. Additional models (`tiny.en`, `base.en`, `base`,
`small`, `medium`, `large`) are lazy-downloaded on demand, sha256-pinned against the committed
catalog (`worker/include/vw_model_catalog.h` / `models/manifest.json`).

To download: select the desired model in the **Model** dropdown, then open the extension menu
(`Tools` > `Extensions` > `VLC-Whisper Settings` > menu) and choose **Download selected model**. Lua submits the
request and returns immediately; it performs no timer, polling loop, sleep, or network I/O. Once media is playing,
the worker executes the transfer on a dedicated thread (WinHTTP on Windows, `curl` on Linux), while the plugin
sender renders stage/percent progress on a dedicated SPU overlay. The video keeps playing and pausing the video does
not pause the download. **Abort** is available via the same menu (`Abort model download`) — the `.part` file is
removed and the status returns to `idle`.

Models are stored per-user (`%LOCALAPPDATA%\vlc-whisper\models` on Windows,
`$XDG_DATA_HOME/vlc-whisper/models` on Linux; `--model-dir` override), so MS Store installs and
restricted Program Files locations remain supported. Resolve order: explicit `model-path` → install
`models/` → per-user dir. All downloads are explicit and worker-only (see ADR-023); offline use
stays fully functional with the bundled `ggml-tiny.bin`.

#### Download troubleshooting

The worker logs its model path, download destination, HTTP failures, SHA-256 result, and final atomic rename to
`%TEMP%\vlc-whisper-worker.log` on Windows. In VLC Messages, look for `PLUGIN_MODEL_CTRL` (request relay),
`PLUGIN_MODEL_PROGRESS` (stage/bytes), `PLUGIN_MODEL_PATH` (selected model and destination), and
`PLUGIN_MODEL_ACTIVATE` (successful completion/respawn). The first `idle:<model>` progress status is an initial
worker snapshot; it is not cancellation. A completed file should be under `%LOCALAPPDATA%\vlc-whisper\models\`,
not the installed VLC `models\` directory. If the Lua dialog reports that the control value was not retained,
restart VLC and retry after confirming the extension and plugin came from the same build.

## Manual Plugin Installation (Windows Developer Workflow)

To install and verify the VLC plugin manually during development:

1. **Install DLL**: Copy the compiled `libvlc_whisper_plugin.dll` to your VLC installation's plugin directory:
   - Path: `C:\Program Files\VideoLAN\VLC\plugins\audio_filter\libvlc_whisper_plugin.dll`

2. **Install Worker**: Copy the compiled `vlc-whisper-worker.exe` to your VLC root directory:
   - Path: `C:\Program Files\VideoLAN\VLC\vlc-whisper-worker.exe`
   - The worker is self-contained: all MinGW runtime is statically linked — zero external DLL dependencies (ADR-010).

3. **Install the Models**:
   - Speech Model: `C:\Program Files\VideoLAN\VLC\models\ggml-tiny.bin`
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
