# Building

## Worker Executable

To configure and build the main `vlc-whisper-worker` binary:

```bash
cmake -S worker -B build \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchains/windows-x64-mingw.cmake"

# Builds the worker executable only (samples are excluded from default build)
cmake --build build
```

### Static Runtime Linking (MinGW)

The MinGW cross-compilation setup automatically configures static linking for all target binaries (`-static`, `-static-libgcc`, `-static-libstdc++`, static `libgomp.a`, and static `libwinpthread.a`).

This ensures that output binaries (`vlc-whisper-worker.exe`, `sample_whisper_pcm.exe`) are completely self-contained and run natively on Windows without requiring external MinGW runtime DLLs (`libgomp-1.dll`, `libwinpthread-1.dll`, `libstdc++-6.dll`, etc.).

---

## Building Sample Snippets

Code snippets located in `samples/snippets/` are registered dynamically as standalone CMake targets (`sample_<snippet_name>`).

By default, samples are marked with `EXCLUDE_FROM_ALL` and are **not** compiled during a standard `cmake --build build`. To compile a specific snippet:

```bash
# Build a specific snippet (e.g. samples/snippets/whisper_pcm.c)
cmake --build build --target sample_whisper_pcm

# Build all sample snippets at once
cmake --build build --target samples
```

The compiled Windows `.exe` sample binaries are output to `build/samples/` (e.g. `build/samples/sample_whisper_pcm.exe`).

### Running Sample Snippets on Windows

Sample snippet executables accept the path to a GGML model file via command-line arguments:

```cmd
# Run sample_whisper_pcm with a model path
sample_whisper_pcm.exe C:\path\to\ggml-tiny.en.bin
```

Standard output and error streams (`stdout`/`stderr`) are unbuffered in sample binaries, guaranteeing immediate output printing in Windows CMD or PowerShell.
