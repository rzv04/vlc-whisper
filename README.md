# VLC-Whisper

<p align="center">
  <img src="./assets/vlc-whisper-logo-animation.gif" width="700" alt="VLC-Whisper">
</p>

<p align="center">
  <a href="https://github.com/rzv04/vlc-whisper/releases">
    <img src="https://img.shields.io/github/v/release/rzv04/vlc-whisper?color=blue&label=version" alt="Release">
  </a>
  <a href="https://github.com/rzv04/vlc-whisper/actions/workflows/ci.yml">
    <img src="https://github.com/rzv04/vlc-whisper/actions/workflows/ci.yml/badge.svg" alt="CI Status">
  </a>
  <img src="https://img.shields.io/badge/platform-Windows%20(Official)%20%7C%20Linux%20(Preview)-informational" alt="Platforms">
  <img src="https://img.shields.io/badge/VLC-3.0%2B%20(64--bit)-orange" alt="VLC 3.0+ (64-bit)">
  <img src="https://img.shields.io/badge/License-MIT-green.svg" alt="MIT License">
  <img src="https://img.shields.io/badge/C-C17-blue" alt="C17">
</p>

> **Private, offline, real-time AI subtitle generation directly inside VLC, with optional live translation.**

Powered by [whisper.cpp](https://github.com/ggerganov/whisper.cpp) and [Silero VAD](https://github.com/snakers4/silero-vad), VLC-Whisper transcribes and translates speech in real time as you watch local media, over-the-network VoD and even IPTV livestreams. Audio processing and speech recognition run 100% locally on your device. Translation sends finalized subtitle text, not audio, to Google Translate endpoints.

---

## Live Demo

<p align="center">
  <video
    src="https://github.com/user-attachments/assets/94ab40aa-f654-4441-b8fb-98575e29d946"
    width="900"
    controls>
  </video>
</p>

---

## Quick Start for Users (Windows)

### Option 1: Standalone Installer (Recommended)

1. **Download**: Grab `vlc-whisper-<version>-win64-setup.exe` from [Latest Releases](https://github.com/rzv04/vlc-whisper/releases).
2. **Install**: Run the setup wizard. It automatically locates your 64-bit VLC installation, installs the audio filter plugin, bundles the multilingual `tiny` speech model, and regenerates VLC's plugin cache. Should your VLC folder not be automatically detected, manually select the destination folder in the wizard (should look something like `C:\Program Files\VideoLAN\VLC` ).
3. **Launch & Watch**: Open VLC using the newly created **"VLC (with AI Whisper Captions)"** desktop shortcut, and play any video — subtitles will appear on screen automatically!
4. Alternatively, open VLC, go to `Tools -> Preferences (Ctrl+P) -> Show settings -> All -> Audio -> Filters` and select the `Offline Whisper AI Captions Filter` checkbox there. Useful when VLC is being launched by third-party apps, such as [IPTVnator](https://github.com/4gray/iptvnator).

### Option 2: Portable Archive (.zip)

1. Download `vlc-whisper-<version>-win64.zip` from [Releases](https://github.com/rzv04/vlc-whisper/releases).
2. Extract the archive directly into your VLC installation directory (e.g. `C:\Program Files\VideoLAN\VLC`).
3. Open a Command Prompt in your VLC directory and refresh the plugin cache:
   ```cmd
   vlc-cache-gen.exe "C:\Program Files\VideoLAN\VLC\plugins"
   ```
4. Open VLC using the newly created **"VLC (with AI Whisper Captions)"** desktop shortcut, and play any video — subtitles will appear on screen automatically!
5. Alternatively, open VLC, go to `Tools -> Preferences (Ctrl+P) -> Show settings -> All -> Audio -> Filters` and select the `Offline Whisper AI Captions Filter` checkbox there. Useful when VLC is being launched by third-party apps, such as [IPTVnator](https://github.com/4gray/iptvnator).

---

## Platform & Compatibility List

| Component / Feature          | Support Status                               | Notes                                                                                                                                     |
| :--------------------------- | :------------------------------------------- | :---------------------------------------------------------------------------------------------------------------------------------------- |
| **Windows 10 / 11 (64-bit)** | **Supported**                                | Complete installer wizard, Vulkan GPU acceleration, and CPU fallback.                                                                     |
| **Linux (x86_64)**           | **Experimental / Dev Preview**               | Compiles and runs from source. Official native packages planned for M5.                                                                   |
| **macOS**                    | **Unsupported**                              | Not currently planned for initial releases.                                                                                               |
| **VLC Media Player**         | **VLC 3.0.x (64-bit)**                       | Standard desktop 64-bit VLC release. 32-bit builds are unsupported.                                                                       |
| **Hardware Backends**        | **Vulkan GPU & Multi-Core CPU**              | Automatic GPU detection with seamless CPU fallback.                                                                                       |
| **Validated Languages**      | **English, Romanian**                        | Fully tested end-to-end. Other languages supported by the bundled Whisper models may work but have not yet been validated by VLC-Whisper. |
| **Subtitle Translation**     | **Opt-in (Keyless Google Translate Engine)** | 3-tier fallback (Web RPC, GTX, Mobile scrape). Off by default.                                                                            |

---

## Key Features

- 🔒 **100% Offline & Private by Default**: All audio decoding, voice activity detection, and speech recognition occur locally in memory. Zero audio or transcript data ever leaves your computer, no telemetry involved (aside from opt-in, local logging for debug purposes; enable/disable in settings).
- ⚡ **Hardware Acceleration**: Automatic Vulkan GPU acceleration for near-instant speech decoding, with transparent multi-threaded CPU fallback.
- 🌐 **Real-Time Subtitle Translation**: Translate subtitles live into your native language with single-line or dual-line display modes. Uses a resilient 3-tier fallback engine with zero API keys or subscriptions required (_opt-in only_).
- 🎙️ **Silero Voice Activity Detection**: Filters out background noise, silence, and instrumental music to eliminate phantom subtitles and hallucinations.
- ⏩ **Seamless Playback Controls**: Full timeline synchronization — seeking, pausing, and resuming seamlessly refresh captions without audio glitching.
- 🎛️ **In-VLC Settings Menu**: Adjust models, languages, CPU thread count, and hardware backend directly inside VLC via **View > VLC-Whisper Settings**.
- 📦 **On-Demand Model Downloader**: Ships with the fast multilingual `tiny` model. Download higher-accuracy models (`base`, `small`, `medium`, `large`) on demand directly within VLC.

---

## Configuring Settings in VLC

Open **View > VLC-Whisper Settings** (or **Tools > Extensions > VLC-Whisper Settings**) from the VLC menu bar:

![settings](./assets/vlc-whisper-settings.png)

- **Engine (Backend)**: Choose the transcription backend. Auto is recommended and will use GPU acceleration when available.
- **Speech Model**: Switch between bundled `tiny` and downloaded models (`tiny.en`, `base`, `small`, `medium`, `large`). The `en` models are restricted to only transcribe English language with slightly better accuracy.
- **Audio Language**: Select the primary spoken language in your media (`English`, `Romanian`, `Spanish`, `French`, `German`, `Turkish`, etc.).
- **CPU Threads**: Number of CPU worker threads. The default of 4 is recommended for most systems.
- **Real-Time Translation**: Check the box to enable live translation of finalized subtitles. Choose **Dual line** (shows original speech and translation stacked) or **Translation only** (default).
- **Downloading Models**: Select any model in the dropdown and click **Download Selected Model**. The worker downloads and validates the model in the background over HTTPS without pausing playback.

---

## Frequently Asked Questions & Troubleshooting

<details>
<summary><b>Subtitles are not appearing when playing media</b></summary>

1. Verify that VLC was launched with the audio filter active. Check that you used the **"VLC (with AI Whisper Captions)"** shortcut, or check **Tools > Preferences > Show settings: All > Audio > Filters** and ensure **vlc_whisper** is checked.
2. Confirm the plugin cache is updated by running `vlc-cache-gen.exe "C:\Program Files\VideoLAN\VLC\plugins"`.
3. Check **View > VLC-Whisper Settings** to ensure a valid model is selected.
4. If all else fails, report a bug using the [issue tracker](https://github.com/rzv04/vlc-whisper/issues), using the specified template. **Ensure you provide logs!**
</details>

<details>
<summary><b>Playback is stuttering or high CPU usage</b></summary>

1. If you do not have a dedicated GPU, use the `tiny` or `base` models for smooth real-time transcription.
2. Adjust the CPU thread count in **View > VLC-Whisper Settings** to match your physical CPU core count (typically 4 or 6).
3. If using Vulkan GPU acceleration, ensure your graphics drivers are up to date.
</details>

<details>
<summary><b>How does privacy work when using translation?</b></summary>

- **Transcription & Audio**: 100% offline. Raw audio and PCM never leave your machine.
- **Model Downloads**: Explicit user action only. Downloads official model weights from Hugging Face / GitHub over HTTPS with SHA-256 integrity verification.
- **Translation (Opt-In)**: When translation is enabled, finalized subtitle text strings are sent to Google Translate endpoints over HTTPS. Audio data is never transmitted.
</details>

<details>
<summary><b>How do I uninstall VLC-Whisper?</b></summary>

Run `uninstall-vlc-whisper.exe` from your VLC installation directory, or use Windows **Settings > Apps > Installed apps > VLC-Whisper AI Subtitle Plugin > Uninstall**. Alternatively, go to **Control Panel > Programs > Uninstall a program** and uninstall **VLC-Whisper AI Subtitle Plugin**. The uninstaller removes all plugin binaries, worker executables, shortcuts, per-user model caches and registry keys.

</details>

---

# Developer & Contributor Guide

The following technical sections are intended for developers, packagers, and contributors building VLC-Whisper from source.

## System Architecture

VLC-Whisper uses an isolated two-process architecture to guarantee VLC media playback stability:

```mermaid
flowchart TB
    subgraph VLC["VLC Media Player Process"]
        direction TB
        AOUT["Audio Output Pipeline"] -->|"PCM Audio Callback"| PLUGIN["vlc_whisper<br/>(Audio Filter Plugin)"]
        GUI["vlc_whisper_settings<br/>(Lua Extension GUI)"] -->|"Config / Download Trigger"| PLUGIN
        PLUGIN -->|"Realtime SPSC Queue"| SENDER["Plugin Sender Thread"]
        SENDER -->|"Render Subpictures"| SPU["VLC Video Output / SPU Subpictures"]
    end

    subgraph IPC["Authenticated Local IPC (Pipe / Unix Socket)"]
        SENDER -->|"Audio Chunks & Control Messages"| WORKER_IN
        WORKER_OUT -->|"Timed Caption Segments & Progress"| SENDER
    end

    subgraph WORKER["vlc-whisper-worker (Isolated AI Process)"]
        direction TB
        WORKER_IN["Worker IPC Receiver"] --> VAD["Silero VAD<br/>(Voice Activity Detection)"]
        VAD -->|"Speech Boundaries"| ENGINE["whisper.cpp<br/>(Vulkan GPU / CPU)"]
        ENGINE --> BUILDER["Segment Builder & Hallucination Filter"]
        BUILDER -->|"Finalized cues"| TRANS["Bounded Async Translation Queue<br/>(optional, 4 pending)"]
        TRANS --> WORKER_OUT["Worker IPC Sender"]
        DOWNLOADER["Background Model Downloader<br/>(WinHTTP / curl)"] -.->|"SHA-256 Validated Model"| ENGINE
        TRANS -.->|"Finalized text only / HTTPS"| GOOGLE["Google Translate Web Endpoints"]
    end
```

Please refer to the documentation at `docs/` to have an extensive view of the current architecture, dependencies, and decisions made.

---

## Building from Source

### Prerequisites

#### Ubuntu / Debian

```bash
# Core build system and compilers
sudo apt-get update && sudo apt-get install -y \
  cmake ninja-build build-essential gcc g++ clang-format valgrind gcovr nsis

# MinGW-w64 cross-compilers (for Windows targets)
sudo apt-get install -y \
  gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64 binutils-mingw-w64-x86-64

# Vulkan SDK and shader compiler (for GPU acceleration)
sudo apt-get install -y libvulkan-dev glslc
```

#### Fedora / RHEL

```bash
sudo dnf install -y cmake ninja-build gcc gcc-c++ clang-tools-extra valgrind \
  mingw64-gcc mingw64-gcc-c++ vulkan-loader-devel glslc nsis
```

---

### Cloning the Repository

Clone recursively to initialize the `whisper.cpp` submodule:

```bash
git clone --recursive https://github.com/rzv04/vlc-whisper.git
cd vlc-whisper
```

---

### CMake Presets Reference

| Preset Name               | Target OS     | Backend                        | Output Binary                | Purpose                      |
| :------------------------ | :------------ | :----------------------------- | :--------------------------- | :--------------------------- |
| `linux-x64-debug`         | Linux (Debug) | Vulkan GPU (auto CPU fallback) | `vlc-whisper-worker`         | Linux development & tests    |
| `linux-x64-debug-cpu`     | Linux (Debug) | CPU-only                       | `vlc-whisper-worker-cpu`     | CPU-only testing             |
| `linux-x64-coverage`      | Linux (Debug) | CPU-only + gcov                | `vlc-whisper-worker-cpu`     | Test code coverage           |
| `windows-x64-release`     | Windows x64   | Vulkan GPU (auto CPU fallback) | `vlc-whisper-worker.exe`     | Production Windows installer |
| `windows-x64-release-cpu` | Windows x64   | CPU-only                       | `vlc-whisper-worker-cpu.exe` | Standalone CPU release       |
| `windows-x64-debug`       | Windows x64   | Vulkan GPU (auto CPU fallback) | `vlc-whisper-worker.exe`     | Windows debug symbols        |

---

### Compilation Commands

#### 1. Linux Native Build & Test

```bash
cmake --preset linux-x64-debug
cmake --build --preset linux-x64-debug -j4
ctest --preset linux-x64-debug --output-on-failure
```

#### 2. Windows Cross-Compilation (CPU-only, MinGW)

```bash
cmake --preset windows-x64-release-cpu
cmake --build --preset windows-x64-release-cpu -j4
```

#### 3. Building the Windows Installer (.exe & .zip)

```bash
# Build CPU fallback binary
cmake --preset windows-x64-release-cpu && cmake --build --preset windows-x64-release-cpu -j4
cp build/windows-x64-release-cpu/worker/vlc-whisper-worker-cpu.exe build/windows-x64-release/worker/ 2>/dev/null || true

# Build release binaries and NSIS installer
cmake --preset windows-x64-release
cmake --build --preset windows-x64-release --target installer

# Package portable ZIP archive
cpack --config build/windows-x64-release/CPackConfig.cmake
```

---

### Testing & Quality Assurance

#### Running Full Test Suite

```bash
ctest --preset linux-x64-debug --output-on-failure
```

#### Valgrind Memory Leak Verification

```bash
ctest --test-dir build/linux-x64-debug -T memcheck --output-on-failure \
  --extra-memcheck-options=--leak-check=full \
  --extra-memcheck-options=--error-exitcode=1
```

#### Code Coverage

```bash
cmake --preset linux-x64-coverage && cmake --build --preset linux-x64-coverage
ctest --preset linux-x64-coverage
gcovr -r . --html-details build/coverage.html --exclude 'worker/third_party/' --exclude 'tests/'
```

---

## Open Source License & Attributions

VLC-Whisper is open-source software licensed under the [MIT License](LICENSE).

Third-party dependencies and assets:

- **whisper.cpp & ggml**: MIT License
- **OpenAI Whisper Models**: MIT License
- **Silero VAD Model**: MIT License
- **VLC Plugin API**: LGPL v2.1+ (Dynamic Linking / Out-of-tree plugin)
- **Vulkan SDK Headers**: Apache License 2.0
- **MinGW-w64 Runtime**: GNU GPL v3 with GCC Runtime Library Exception v3.1

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for complete legal notices and component details.
