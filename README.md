# VLC-Whisper

Offline, real-time speech captions inside VLC for local media.

---

## Cloning the Project

```bash
git clone <repository-url>
cd vlc-whisper
```

### Optional: enable [conventional-commits](https://www.conventionalcommits.org/en/v1.0.0/) hook locally ([conventional-commits](https://www.conventionalcommits.org/en/v1.0.0/) will be enforced in CI)

```bash
git config --local core.hooksPath .githooks
```

---

## Building the Project

The project uses CMake (minimum version 3.20) with Ninja and CMake Presets for cross-compiling Windows x64 binaries from Linux using MinGW GCC.

### Option 1: Using CMake Presets (Recommended)

```bash
# Configure the Windows x64 Release build
cmake --preset windows-x64-release

# Build primary targets (protocol library, worker executable, native plugin, unit tests)
cmake --build --preset windows-x64-release

# Run unit and integration tests
ctest --preset windows-x64-release
```

Available presets in `CMakePresets.json`:

- `windows-x64-release` (Windows x64 Release via MinGW cross-compiler)
- `windows-x64-debug` (Windows x64 Debug via MinGW cross-compiler)
- `linux-x64-debug` (Host native Linux debug build for local testing)

---

## Running Tests

The project includes unit and integration tests.

### Using CMake Presets (Linux native)

To compile and run tests natively on Linux during development:

```bash
# Configure the native Linux debug build
cmake --preset linux-x64-debug

# Build tests with 4 parallel jobs
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

2. **Reset Plugin Cache & Verify Registration**:
   Open Command Prompt or PowerShell and run:

   ```cmd
   "C:\Program Files\VideoLAN\VLC\vlc.exe" --reset-plugins-cache --list | findstr /i whisper
   ```

   _Expected Output_: You should see the `VLC-Whisper` audio filter module listed.

3. **Inspect Debug Logs**:
   Audio filters only instantiate when audio media is playing and the filter is explicitly selected in the audio chain. To trigger `vw_plugin_open` and write debug output to a file:

   ```cmd
   "C:\Program Files\VideoLAN\VLC\vlc.exe" --reset-plugins-cache --audio-filter=vlc_whisper --file-logging --logfile=vlc-debug.log -vvv C:\path\to\audio.mp3
   ```

   _(Note: For `.mp4` video files in Virtual Machines or dual-GPU laptops, add `--avcodec-hw=none` to avoid D3D11 hardware acceleration crashes)._

   _Expected Output_: Inspect `vlc-debug.log` to confirm `vlc_whisper debug: [vw_log:PLUGIN_OPEN] vlc-whisper audio filter module opened`.
