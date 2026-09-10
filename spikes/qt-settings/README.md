# VLC-Whisper Qt Settings Frontend Spike

This directory contains a deliberately isolated feasibility spike for replacing the current `vlc.dialog` settings surface with a standalone Qt Widgets process.

The spike mirrors the controls and button semantics of `lua/extensions/vlc_whisper_settings.lua`, but it has **no plugin IPC, worker IPC, HTTP, model download, translation request, or VLC integration**. Its only persistent side effect is an atomically written `settings.json` in the process current working directory.

## What the spike mirrors

- Engine dropdown: `auto`, Vulkan GPU, CPU only.
- Model dropdown: `tiny.en`, `tiny`, `base.en`, `base`, `small`, `medium`, `large`.
- Language dropdown: English, Romanian, Turkish, German, French, Spanish.
- CPU thread text input with the existing `1..16` clamp on Apply.
- Diagnostic logging checkbox.
- Auto-translation checkbox.
- Translation source and target dropdowns.
- Translation display mode dropdown.
- `How to test`, `Apply`, and `Download Selected Model` push buttons.
- `.en` models force English when Apply or Download is pressed, matching the Lua behavior.

The status rows explicitly state that the spike is not connected. `Download Selected Model` is intentionally a no-network UI simulation.

## `settings.json`

`Apply` writes the file using `QSaveFile`, so the final JSON is replaced atomically rather than being partially overwritten. The path is intentionally the **current working directory**, not the executable directory and not the production per-user data directory.

Example:

```json
{
    "model-path": "models/ggml-tiny.bin",
    "schema": 1,
    "whisper-backend": "auto",
    "whisper-language": "en",
    "whisper-logging": false,
    "whisper-threads": 4,
    "whisper-translate-enabled": false,
    "whisper-translate-from": "auto",
    "whisper-translate-mode": 1,
    "whisper-translate-to": "en"
}
```

The flat names deliberately mirror the existing VLC config keys so a future plugin-side migration adapter can remain straightforward.

## Build and run on Ubuntu

Ubuntu 24.04 or another distribution with Qt 6.2+ is sufficient for the spike.

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build qt6-base-dev

cmake -S spikes/qt-settings \
      -B build/qt-settings-linux \
      -G Ninja \
      -DCMAKE_BUILD_TYPE=Release
cmake --build build/qt-settings-linux
```

Run from a scratch directory so the location of `settings.json` is obvious:

```bash
mkdir -p /tmp/vlc-whisper-qt-settings-test
cd /tmp/vlc-whisper-qt-settings-test
/path/to/vlc-whisper/build/qt-settings-linux/vlc-whisper-settings-spike
```

Change controls, press **Apply**, close the process, then inspect and re-open it:

```bash
cat settings.json
/path/to/vlc-whisper/build/qt-settings-linux/vlc-whisper-settings-spike
```

Expected behavior: the second launch reloads the saved selections, integer threads remain clamped to `1..16`, and selecting `tiny.en` or `base.en` forces English on Apply.

## Build and test on Windows

For this spike, the simplest Windows validation path is a native Qt development kit. It is intentionally **not** wired into the repository's existing Ubuntu-to-MinGW release preset because that preset does not currently provide a cross-compiled Windows Qt SDK.

Install a Qt 6 desktop kit (MinGW 64-bit is convenient), CMake, and Ninja on the development machine. From a terminal where that Qt kit is available:

```bat
cmake -S spikes\qt-settings ^
      -B build\qt-settings-win ^
      -G Ninja ^
      -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_PREFIX_PATH=C:\Qt\6.11.2\mingw_64
cmake --build build\qt-settings-win
```

Adjust the Qt path/version to the installed kit.

To test the **end-user deployment shape**, stage the runtime next to the executable:

```bat
mkdir build\qt-settings-win\deploy
copy build\qt-settings-win\vlc-whisper-settings-spike.exe build\qt-settings-win\deploy\
C:\Qt\6.11.2\mingw_64\bin\windeployqt.exe ^
  --release ^
  --no-translations ^
  build\qt-settings-win\deploy\vlc-whisper-settings-spike.exe
```

Copy only the `deploy` directory to a clean Windows 10/11 x64 VM that does **not** have Qt installed. Launch the executable from `cmd.exe` after `cd`-ing into a writable test directory; press Apply and confirm that `settings.json` appears in that current directory.

Recommended manual checks:

1. All labels, dropdown choices, checkboxes, and buttons match the Lua settings dialog.
2. Default model is `tiny (multilingual) (bundled default)`.
3. Enter `0`, `17`, or non-numeric text in Threads and press Apply; persisted values become `1`, `16`, or `4` respectively.
4. Select `tiny.en` or `base.en`, select a non-English language, press Apply; language becomes English and JSON stores `"en"`.
5. Enable translation, change source/target/display mode, Apply, restart, and confirm persistence.
6. `How to test` updates the guidance label but performs no network call.
7. `Download Selected Model` updates only the frontend status and performs no download.
8. Run the staged copy on a machine without Qt installed; it must start using only bundled runtime files plus Windows system libraries.

## End-user dependency policy

This spike adds development dependencies only. It is not part of the root build, CPack, ZIP, or NSIS installer.

If the Qt frontend is promoted to production, the installer should stage the Qt runtime during packaging (for example through `windeployqt`/Qt CMake deployment helpers) and install those files alongside `vlc-whisper-settings.exe`. The end user must **not** be asked to install Qt, a Qt SDK, CMake, Ninja, or a separate runtime package.

Prefer dynamic Qt deployment for an LGPL-compatible production path. If Qt Network is later adopted for HTTPS model downloads on Windows, package the Schannel TLS backend so HTTPS can use Windows' native TLS implementation without requiring a separately installed OpenSSL runtime.

## Non-goals

- No Lua launcher changes.
- No worker or plugin changes.
- No production settings ownership decision.
- No downloader implementation.
- No installer/CPack changes.
- No Qt dependency in the root project build.
