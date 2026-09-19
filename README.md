# VLC-Whisper


<p align="center">
  <img src="./assets/vlc-whisper-logo-animation.gif" width="700" alt="VLC-Whisper">
</p>

<p align="center">
  <a href="https://github.com/rzv04/vlc-whisper/releases"><img src="https://img.shields.io/github/v/release/rzv04/vlc-whisper?color=blue&label=version" alt="Release"></a>
  <a href="https://github.com/rzv04/vlc-whisper/actions/workflows/ci.yml"><img src="https://github.com/rzv04/vlc-whisper/actions/workflows/ci.yml/badge.svg" alt="CI Status"></a>
  <img src="https://img.shields.io/badge/platform-Windows%20(Official)%20%7C%20Linux%20(Preview)-informational" alt="Platforms">
  <img src="https://img.shields.io/badge/VLC-3.0.23%2B%20(64--bit)-orange" alt="VLC 3.0.23+">
  <img src="https://img.shields.io/badge/License-MIT-green.svg" alt="MIT License">
  <img src="https://img.shields.io/badge/core-C17-blue" alt="C17 core">
</p>

> **Private, local real-time AI captions for VLC, with optional live text translation.**

VLC-Whisper transcribes local media, network VoD, and live/non-seekable streams through a local `whisper.cpp` worker. Audio is never sent to a cloud transcription service. When translation is explicitly enabled, finalized subtitle text is sent to Google Translate endpoints over HTTPS.

## Live Demo

<p align="center">
  <video src="https://github.com/user-attachments/assets/94ab40aa-f654-4441-b8fb-98575e29d946" width="900" controls></video>
</p>

## Quick Start — Windows

### Installer (recommended)

1. Download `vlc-whisper-<version>-win64-setup.exe` from [Releases](https://github.com/rzv04/vlc-whisper/releases).
2. Run the installer.
3. Launch **VLC (with AI Whisper Captions)** and play media.

For VLC launched by another application, enable the filter under:

`Tools > Preferences > Show settings: All > Audio > Filters > Offline Whisper AI Captions Filter`

### Portable ZIP

1. Extract `vlc-whisper-<version>-win64.zip` into the VLC installation directory.
2. Refresh the plugin cache (or remove `plugins.dat`):

```cmd
vlc-cache-gen.exe "C:\Program Files\VideoLAN\VLC\plugins"
```

3. Enable the VLC-Whisper audio filter in VLC preferences.

## Quick Start — Ubuntu x64

VLC-Whisper currently supports the Ubuntu APT build of VLC **3.0.23 or newer**. Snap and Flatpak VLC are detected by the installer but are not modified because their sandboxed plugin trees are separate.

### Install script (recommended)

```bash
curl -fsSL https://raw.githubusercontent.com/rzv04/vlc-whisper/main/scripts/install.sh | sh
```

The script checks Ubuntu/x86_64, discovers the installed Ubuntu version (`24.04` or `26.04`), checks the available VLC version, downloads the matching checksummed release `.deb`, and installs required runtime dependencies through APT. It discovers VLC's multiarch plugin/Lua paths from the installed Debian packages rather than assuming an `x86_64-linux-gnu` path.

### DEB

Download the matching `vlc-whisper-ubuntu-24.04-amd64.deb` or `vlc-whisper-ubuntu-26.04-amd64.deb` package and its checksum from [Releases](https://github.com/rzv04/vlc-whisper/releases), then install it with:

```bash
sudo apt install ./vlc-whisper-ubuntu-<version>-amd64.deb
```

The package installs the native audio filter, isolated worker, bundled models, the minimal `VLC-Whisper Settings` Lua launcher, and `/usr/bin/vlc-whisper-settings`, then refreshes VLC's plugin cache.

Uninstall either installation with:

```bash
sudo apt remove vlc-whisper
```

## Features

- Local Whisper transcription with Vulkan GPU acceleration or CPU fallback.
- Local files, network VoD, IPTV, and live/non-seekable media.
- Standalone Qt settings for backend, model, language, threads, paused subtitles, and translation.
- Explicit model downloads with SHA-256 integrity verification in the worker process.
- Optional translation of finalized subtitle text.
- Worker-process isolation so caption failures do not block VLC playback.
- Seek, pause/resume, media-swap, and discontinuity handling through caption-session epochs.

## Settings

Open `View > VLC-Whisper Settings`. The Lua extension immediately launches the standalone Qt settings process and deactivates; it does not poll, wait for the child, or perform network work.

![settings](./assets/vlc-whisper-settings.png)

- **Engine:** Auto is recommended; it uses GPU acceleration when available.
- **Speech model:** choose the bundled model or a downloaded catalog model. `.en` models are English-only.
- **Audio language:** choose the primary spoken language.
- **CPU threads:** `4` is a reasonable default for many systems.
- **Paused subtitles:** enabled by default for local files; holds the visible cue and previews the first cue after seeking while paused.
- **Translation:** disabled by default; choose translation-only or dual-line display when enabled.
- **Model download:** choose a model and press **Download Selected Model**. The button becomes **Abort Model Download** while that request is pending.

Settings are written atomically without elevation to `%LOCALAPPDATA%\vlc-whisper\settings.json` on Windows or `$XDG_CONFIG_HOME/vlc-whisper/settings.json` (default `~/.config/vlc-whisper/settings.json`) on Linux. Installing the project again intentionally resets that JSON to the current defaults; downloaded models remain separate user data.

If the launcher cannot resolve or start the settings executable immediately, VLC shows a small reinstall error. It intentionally does not wait approximately one second to verify window creation because VLC 3 Lua has no nonblocking child-liveness primitive; sleeping, joining, or polling there would violate the UI-thread invariant.

## Privacy and Network Behavior

> [!INFO]
> **Transcription audio stays local.** Network access is limited to explicit model downloads and opt-in translation. VLC-Whisper does not use cloud transcription or telemetry.

- **Transcription/audio:** local only.
- **Settings UI:** local filesystem and local single-instance signalling only; it performs no HTTP requests.
- **Model downloads:** explicit user action; the worker downloads and integrity-checks model bytes before activation.
- **Translation:** opt-in; finalized subtitle text is sent over HTTPS. Audio is never sent for translation.
- **Logs:** diagnostics are opt-in and must not contain PCM, subtitle bodies, tokens, or credentials.

## Troubleshooting

<details>
<summary><b>Subtitles are not appearing</b></summary>

1. Confirm the VLC audio filter is enabled.
2. Refresh the VLC plugin cache if using the portable installation.
3. Open VLC-Whisper Settings and confirm a valid model, language, and backend.
4. If the issue persists, open a GitHub issue and include the requested diagnostics.
</details>

<details>
<summary><b>Playback is stuttering or CPU usage is high</b></summary>

1. Prefer a smaller model on CPU-only systems.
2. Keep the CPU thread count near the number of physical cores available to VLC-Whisper.
3. Update graphics drivers when using Vulkan.
4. Disable VLC-Whisper and replay the same media to determine whether playback is healthy without the plugin.
</details>

<details>
<summary><b>How do I uninstall VLC-Whisper?</b></summary>

On Windows, use **Control Panel > Programs > Uninstall a program**, Windows **Installed apps**, or the installed `uninstall-vlc-whisper.exe`. On Ubuntu, run `sudo apt remove vlc-whisper`.
</details>

## Benchmark Results

> [!INFO]
> These are anecdotal project regression measurements, not general `whisper.cpp` benchmarks. They use a small 20-clip FLEURS subset (10 English, 10 Romanian) and the bundled `tiny` model with 4 CPU threads.

| Language | Mode | WER | CER | WER excl. insertions* | CER excl. insertions* | Raw word errors / ref words |
| :--- | :--- | ---: | ---: | ---: | ---: | ---: |
| **English (`en`)** | **Offline** | **10.38%** | **4.97%** | **7.55%** | **3.77%** | 22 / 212 |
| **English (`en`)** | **Local media** | **10.38%** | **4.87%** | **7.55%** | **3.67%** | 22 / 212 |
| **English (`en`)** | **Livestream** | **46.23%** | **39.32%** | **10.38%** | **5.16%** | 98 / 212 |
| **Romanian (`ro`)** | **Offline** | **93.03%** | **29.71%** | **74.59%** | **25.68%** | 227 / 244 |
| **Romanian (`ro`)** | **Local media** | **89.75%** | **33.00%** | **75.82%** | **29.47%** | 219 / 244 |
| **Romanian (`ro`)** | **Livestream** | **159.43%** | **91.52%** | **69.67%** | **24.28%** | 389 / 244 |

_*The insertion-free columns are reconstructed diagnostic rates from the preserved per-sample hypotheses and references using the benchmark's normalizer and the same minimum Levenshtein-distance objective. They remove insertion edit operations from the error numerator while retaining substitutions, deletions, and the original reference denominator. Where multiple minimum-distance alignments exist, the reconstruction uses the minimum insertion count, making the adjustment conservative. These are not standard WER/CER scores; duplicate rolling-window re-emission is a major source of insertions in livestream mode, but the adjustment removes all aligned insertions rather than attempting to label individual insertions as duplicates._

See [`docs/quality-benchmark.md`](docs/quality-benchmark.md) for methodology and benchmark options.

# Developer & Contributor Guide

## Architecture

VLC-Whisper separates realtime VLC integration from settings, inference, and network-capable worker tasks:

```mermaid
flowchart TB
    subgraph VLC["VLC Media Player Process"]
        AOUT["Audio Output Pipeline"] -->|"PCM callback"| PLUGIN["vlc_whisper audio filter"]
        LUA["VLC-Whisper Settings Lua launcher"]
        PLUGIN -->|"bounded SPSC queue"| SENDER["Plugin sender thread"]
        SENDER -->|"SPU subpictures"| SPU["VLC video output"]
    end

    subgraph SETTINGS["Standalone Settings Process"]
        QT["Standalone Qt settings process"]
    end

    LUA -->|"detached launch"| QT
    QT -->|"atomic per-user settings.json / one-shot model command"| SENDER

    subgraph IPC["Authenticated local IPC"]
        SENDER -->|"audio + control"| WORKER_IN
        WORKER_OUT -->|"segments + status"| SENDER
    end

    subgraph WORKER["vlc-whisper-worker"]
        WORKER_IN["IPC reader"] --> QUEUE["Bounded worker queue"]
        QUEUE --> VAD["VAD / windowing"]
        VAD --> ENGINE["whisper.cpp"]
        ENGINE --> BUILDER["Segment builder"]
        BUILDER --> TRANS["Optional bounded translation queue"]
        TRANS --> WORKER_OUT["IPC sender"]
        DOWNLOADER["Model downloader"] -.-> ENGINE
        TRANS -.->|"finalized text only / HTTPS"| GOOGLE["Google Translate endpoints"]
    end
```

> [!INFO]
> The core engineering rule is **captioning may fail; playback must not**. The VLC audio callback performs bounded capture/enqueue work only. Inference, blocking IPC, filesystem access, downloads, translation, and teardown waits belong off that path.

Cross-component contracts are summarized in [`docs/invariants.md`](docs/invariants.md); detailed protocol semantics live in [`docs/api-contracts.md`](docs/api-contracts.md).

## Build Prerequisites

### Ubuntu / Debian

```bash
sudo apt-get update
sudo apt-get install -y cmake ninja-build build-essential gcc g++ clang-format valgrind gcovr nsis curl pkg-config dpkg-dev \
  gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64 binutils-mingw-w64-x86-64 \
  libavformat-dev libavcodec-dev libswresample-dev libavutil-dev libvulkan-dev glslc spirv-headers qt6-base-dev
```

### Fedora / RHEL

```bash
sudo dnf install -y cmake ninja-build gcc gcc-c++ clang-tools-extra valgrind \
  mingw64-gcc mingw64-gcc-c++ vulkan-loader-devel glslc nsis qt6-qtbase-devel
```

## Clone and Build

```bash
git clone --recursive https://github.com/rzv04/vlc-whisper.git
cd vlc-whisper
```

| Preset | Purpose |
| --- | --- |
| `linux-x64-release` | Ubuntu/Debian x64 release + DEB packaging |
| `linux-x64-debug` | Native Linux development/tests |
| `linux-x64-debug-cpu` | CPU-only Linux development |
| `linux-x64-coverage` | Linux coverage build |
| `windows-x64-release` | Production Windows GPU release |
| `windows-x64-release-cpu` | Explicit CPU-only Windows release |
| `windows-x64-debug` | Windows development/debug build |

Typical native build:

```bash
cmake --preset linux-x64-debug
cmake --build --preset linux-x64-debug -j4
ctest --preset linux-x64-debug --output-on-failure
```

> [!WARNING]
> `windows-x64-release` is a GPU production preset and fails closed when Vulkan/`glslc` requirements cannot be resolved. Use `windows-x64-release-cpu` when a CPU-only artifact is intentional; do not silently relabel a CPU build as GPU-capable.

## Windows Packaging

The existing Windows preset cross-compiles the plugin/worker with MinGW, but Ubuntu does not provide the Windows Qt SDK used by the settings GUI. Build and deploy `settings/` once on Windows with a Qt 6 desktop kit, then give that deployment directory to the existing cross-package build:

```bat
cmake -S settings -B build\settings-win -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\Qt\6.11.2\mingw_64
cmake --build build\settings-win --target vw_settings_deploy
```

Then, on the packaging machine, configure the release with the resulting `settings-deploy` directory available as `VW_SETTINGS_DEPLOY_DIR`:

```bash
cmake --preset windows-x64-release -DVW_PROVISION_MODELS=ON -DVW_SETTINGS_DEPLOY_DIR=/path/to/settings-deploy
cmake --build --preset windows-x64-release --target provision_models
cmake --build --preset windows-x64-release --target installer
```

The installer build fails closed if `vlc-whisper-settings.exe` or its deployed Qt runtime is missing. End users do not install a Qt SDK or request elevation to save settings.

For offline packaging, provide the pinned model files manually and omit `VW_PROVISION_MODELS=ON`; the same SHA-256 checks still run.

## Linux Packaging

The two release packages must be built in their matching Ubuntu userspace. Build the Ubuntu 24.04 package inside Ubuntu 24.04 (Noble), and the Ubuntu 26.04 package inside Ubuntu 26.04 (Resolute), using a VM, container, chroot, or native installation. Do not build both release artifacts from one arbitrary host and merely rename them.

Install the release-build dependencies inside each environment:

```bash
apt-get update
apt-get install -y git cmake ninja-build build-essential gcc g++ curl pkg-config dpkg-dev file \
  libavformat-dev libavcodec-dev libswresample-dev libavutil-dev libvulkan-dev glslc spirv-headers qt6-base-dev
```

From a recursive clone of the repository, build the matching package.

### Ubuntu 24.04 (Noble)

```bash
cmake --preset linux-x64-release \
  -DCPACK_PACKAGE_FILE_NAME=vlc-whisper-ubuntu-24.04-amd64 \
  -DVW_PROVISION_MODELS=ON
cmake --build --preset linux-x64-release -j2 --target package

sha256sum --check build/linux-x64-release/vlc-whisper-ubuntu-24.04-amd64.deb.sha256
dpkg-deb --info build/linux-x64-release/vlc-whisper-ubuntu-24.04-amd64.deb
```

Outputs:

- `build/linux-x64-release/vlc-whisper-ubuntu-24.04-amd64.deb`
- `build/linux-x64-release/vlc-whisper-ubuntu-24.04-amd64.deb.sha256`

### Ubuntu 26.04 (Resolute)

```bash
cmake --preset linux-x64-release \
  -DCPACK_PACKAGE_FILE_NAME=vlc-whisper-ubuntu-26.04-amd64 \
  -DVW_PROVISION_MODELS=ON
cmake --build --preset linux-x64-release -j2 --target package

sha256sum --check build/linux-x64-release/vlc-whisper-ubuntu-26.04-amd64.deb.sha256
dpkg-deb --info build/linux-x64-release/vlc-whisper-ubuntu-26.04-amd64.deb
```

Outputs:

- `build/linux-x64-release/vlc-whisper-ubuntu-26.04-amd64.deb`
- `build/linux-x64-release/vlc-whisper-ubuntu-26.04-amd64.deb.sha256`

For offline packaging, provision the pinned Whisper and Silero VAD model files manually and omit `-DVW_PROVISION_MODELS=ON`; the package build still verifies the configured model hashes.

> [!WARNING]
> Compiling Vulkan shader translation units (`ggml-vulkan`) under `-O3` requires significant memory. On systems with less than 8 GB of RAM or without swap space, limit parallel build jobs (for example, `-j2` or `-j1`) to prevent compiler out-of-memory (OOM) termination:
> ```bash
> cmake --build --preset linux-x64-release -j2
> ```

## Verification

```bash
clang-format --dry-run --Werror <modified-c/cpp-files>
cmake --preset linux-x64-debug
cmake --build --preset linux-x64-debug
ctest --preset linux-x64-debug --output-on-failure
ctest --test-dir build/linux-x64-debug -T memcheck
```

> [!INFO]
> The settings smoke test runs with Qt's offscreen platform and isolated XDG directories, so it does not need a desktop or network. Model-gated tests may skip when their documented local model is absent. A Windows cross-build proves artifact creation, not runtime compatibility with VLC; release validation still requires the supported Windows/VLC environment.

## Local EN/RO Quality Benchmark

The developer regression benchmark is headless: it does not launch VLC, play audio, or require a Linux desktop. Corpus media and reports remain local and git-ignored.

```bash
python -m pip install -r tools/quality_benchmark/requirements.txt
python tools/quality_benchmark/vw_download_corpus.py
cmake --preset linux-x64-debug -DVW_QUALITY_BENCHMARK_HOOKS=ON
cmake --build --preset linux-x64-debug --target vw-quality-benchmark vlc-whisper-worker
python tools/quality_benchmark/vw_benchmark.py --build-dir build/linux-x64-debug --model models/ggml-tiny.bin
```

Use [`tools/quality_benchmark/README.md`](tools/quality_benchmark/README.md) for the terse command reference and [`docs/quality-benchmark.md`](docs/quality-benchmark.md) for scoring, completion, and reproducibility semantics.

## Documentation

Start with [`AGENTS.md`](AGENTS.md) and [`docs/invariants.md`](docs/invariants.md), then open only the technical reference relevant to the changed behavior.

- [`docs/architecture.md`](docs/architecture.md) — process and lifecycle architecture.
- [`docs/api-contracts.md`](docs/api-contracts.md) — IPC protocol and API wire semantics.
- [`docs/roadmap.md`](docs/roadmap.md) — current and planned work.

## License

VLC-Whisper is MIT-licensed. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for dependency and asset notices.