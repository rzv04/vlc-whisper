# VLC-Whisper

<p align="center">
  <img src="./assets/vlc-whisper-logo-animation.gif" width="700" alt="VLC-Whisper">
</p>

<p align="center">
  <a href="https://github.com/rzv04/vlc-whisper/releases"><img src="https://img.shields.io/github/v/release/rzv04/vlc-whisper?color=blue&label=version" alt="Release"></a>
  <a href="https://github.com/rzv04/vlc-whisper/actions/workflows/ci.yml"><img src="https://github.com/rzv04/vlc-whisper/actions/workflows/ci.yml/badge.svg" alt="CI Status"></a>
  <img src="https://img.shields.io/badge/platform-Windows%20(Official)%20%7C%20Linux%20(Preview)-informational" alt="Platforms">
  <img src="https://img.shields.io/badge/VLC-3.0%2B%20(64--bit)-orange" alt="VLC 3.0+">
  <img src="https://img.shields.io/badge/License-MIT-green.svg" alt="MIT License">
</p>

**Local realtime AI captions for VLC, with optional live text translation.** Speech recognition runs locally through `whisper.cpp`; audio is not sent to a cloud transcription service. When translation is explicitly enabled, finalized subtitle text is sent to Google Translate endpoints.

## Quick start — Windows

### Installer (recommended)

1. Download `vlc-whisper-<version>-win64-setup.exe` from [Releases](https://github.com/rzv04/vlc-whisper/releases).
2. Run the installer.
3. Launch **VLC (with AI Whisper Captions)** and play media.

For VLC launched by another application, enable the filter under:

`Tools > Preferences > Show settings: All > Audio > Filters > Offline Whisper AI Captions Filter`

### Portable ZIP

1. Extract `vlc-whisper-<version>-win64.zip` into the VLC installation directory.
2. Refresh the VLC plugin cache (or remove `plugins.dat`):

```cmd
vlc-cache-gen.exe "C:\Program Files\VideoLAN\VLC\plugins"
```

3. Enable the audio filter from VLC preferences.

## Features

- Local Whisper transcription with Vulkan GPU acceleration or CPU fallback.
- Local media, network VoD, and live/non-seekable media support.
- In-VLC settings for backend, model, language, threads, and translation.
- Explicit model downloading with integrity verification.
- Optional translation of finalized text.
- Worker process isolation so caption failures do not block VLC playback.

## Settings

Open `View > VLC-Whisper Settings`.

![settings](./assets/vlc-whisper-settings.png)

- **Engine:** Auto is recommended; it uses GPU acceleration when available.
- **Speech model:** choose the bundled model or a downloaded catalog model. `.en` models are English-only.
- **Audio language:** choose the primary spoken language.
- **CPU threads:** `4` is a reasonable default for many systems.
- **Translation:** off by default; choose dual-line or translation-only display when enabled.
- **Model download:** choose a model and press **Download Selected Model**.

The settings UI is a VLC Lua extension. If model status appears stale, press **Apply** to refresh the effective configuration.

## Privacy and network behavior

- **Transcription/audio:** local only.
- **Model downloads:** explicit user action; downloaded model bytes are integrity-checked.
- **Translation:** opt-in; finalized subtitle text is sent over HTTPS. Audio is never sent for translation.
- **Telemetry/cloud transcription:** not used.

## Troubleshooting

### No generated subtitles

1. Confirm the VLC audio filter is enabled.
2. Refresh the VLC plugin cache if using the portable installation.
3. Open VLC-Whisper Settings and confirm a valid model/language/backend configuration.
4. If the issue persists, open a GitHub issue and include the requested diagnostics.

### High CPU or stuttering

- Prefer a smaller model on CPU-only systems.
- Keep the CPU thread count near the number of physical cores available to VLC-Whisper.
- Update graphics drivers when using Vulkan.
- Disable the plugin to confirm whether VLC playback is healthy without it before filing a crash/performance issue.

### Uninstall

Use **Control Panel > Programs > Uninstall a program**, Windows **Installed apps**, or the installed `uninstall-vlc-whisper.exe`.

## Developer quick start

Clone with submodules:

```bash
git clone --recursive https://github.com/rzv04/vlc-whisper.git
cd vlc-whisper
```

Typical Linux development build:

```bash
cmake --preset linux-x64-debug
cmake --build --preset linux-x64-debug
ctest --preset linux-x64-debug --output-on-failure
```

Common release presets:

| Preset | Purpose |
| --- | --- |
| `linux-x64-debug` | Native Linux development/tests |
| `linux-x64-debug-cpu` | CPU-only Linux development |
| `linux-x64-coverage` | Linux coverage build |
| `windows-x64-release` | Production Windows GPU release; Vulkan required |
| `windows-x64-release-cpu` | Explicit CPU-only Windows release |

The production `windows-x64-release` preset fails closed if Vulkan requirements cannot be resolved; use the CPU preset intentionally rather than silently producing a mislabeled GPU artifact.

### Windows packaging

Release packages require the pinned Whisper and Silero VAD model files. To explicitly provision and package them:

```bash
cmake --preset windows-x64-release -DVW_PROVISION_MODELS=ON
cmake --build --preset windows-x64-release --target provision_models
cmake --build --preset windows-x64-release --target installer
cpack --config build/windows-x64-release/CPackConfig.cmake
```

Offline packaging can use manually supplied pinned model files; the same hash checks apply.

## Verification

```bash
clang-format --dry-run --Werror <modified-c-files>
cmake --preset linux-x64-debug
cmake --build --preset linux-x64-debug
ctest --preset linux-x64-debug --output-on-failure
ctest --test-dir build/linux-x64-debug -T memcheck
```

## Local EN/RO quality benchmark

The regression corpus is developer-only, headless, and git-ignored; it does not launch VLC or play audio.

```bash
python -m pip install -r tools/quality_benchmark/requirements.txt
python tools/quality_benchmark/vw_download_corpus.py
cmake --preset linux-x64-debug -DVW_QUALITY_BENCHMARK_HOOKS=ON
cmake --build --preset linux-x64-debug --target vw-quality-benchmark vlc-whisper-worker
python tools/quality_benchmark/vw_benchmark.py --build-dir build/linux-x64-debug --model models/ggml-tiny.bin
```

Use `tools/quality_benchmark/README.md` for the terse command reference and `docs/quality-benchmark.md` for scoring/reproducibility details. Historical benchmark findings live in `docs/quality-benchmark-report.md` rather than this user README.

## Documentation

Start at [`docs/README.md`](docs/README.md). It routes contributors and agents to the smallest relevant reference instead of requiring the full documentation set to be loaded.

Key references:

- [`docs/invariants.md`](docs/invariants.md) — cross-component engineering contracts.
- [`docs/architecture.md`](docs/architecture.md) — detailed architecture.
- [`docs/api-contracts.md`](docs/api-contracts.md) — IPC/API semantics.
- [`docs/test-strategy.md`](docs/test-strategy.md) — test/failure-path rules.
- [`docs/roadmap.md`](docs/roadmap.md) — planned work.

## License

VLC-Whisper is MIT-licensed. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for dependency and asset notices.
