# Standalone Qt Settings Productionization Plan

Status: implementation complete pending PR verification.

## Goal

Replace the sandboxed `vlc.dialog` settings UI with a production standalone Qt 6 settings application while retaining a minimal VLC Lua extension named **VLC-Whisper Settings** as the launcher. Preserve the current settings surface, defaults, validation, persistence semantics, live-apply behavior, model availability behavior, and worker-owned model download boundary before adding broader settings features.

The former `spikes/qt-settings/` implementation was a feasibility reference only. Production code, binaries, package names, UI strings, documentation, and install paths contain no `spike` naming; its useful behavior has been promoted into the production target and the spike directory removed.

## Non-negotiable invariants

- The Lua extension remains discoverable as `VLC-Whisper Settings`, but no longer owns the settings form.
- Lua performs no shell process launch, polling loops, timers, sleeps, window-ready probes, child-lifetime monitoring, worker IPC loops, HTTP, model hashing, or long-running callbacks.
- Launch acknowledgement is bounded to process creation: Lua opens `vlc-whisper-settings://launch`; the existing VLC-Whisper plugin handles that local access URI without a command shell and invokes the GUI's short `--launch-detached` bootstrap. The bootstrap calls `QProcess::startDetached()` and returns success only when the OS creates the real settings process.
- No `os.execute`, `io.popen`, `start`, `cmd.exe`, PowerShell, or console-helper command is part of the launcher path. The Windows settings target is a GUI executable.
- A later crash of the detached settings process is outside the Lua extension's responsibility.
- The standalone settings process owns UI and durable user-setting edits, not inference and not network/model download execution.
- HTTP model downloading remains owned by the worker (ADR-023). The settings app may request/cancel downloads and display local status, but it must not perform HTTP or SHA-256 download verification itself.
- No cloud-translation test/request is initiated from the settings application. The retained **How to test** control only shows local guidance and performs no network call.
- Existing playback/audio realtime invariants remain untouched; settings work never adds blocking work to VLC's audio callback.
- Tests remain network-free and headless-safe on Linux.

## Current behavior that must remain compatible

| UI setting | Persisted/runtime key | Required behavior |
| --- | --- | --- |
| Engine | `whisper-backend` | `auto`, `gpu`, `cpu`; default `auto` |
| Model | `model-path` | Existing seven Whisper model paths and labels; default `models/ggml-tiny.bin` |
| Language | `whisper-language` | `en`, `ro`, `tr`, `de`, `fr`, `es`; default `en` |
| CPU threads | `whisper-threads` | Parse integer, invalid -> `4`, clamp `1..16` |
| Diagnostic logging | `whisper-logging` | Boolean |
| Paused subtitles | `whisper-show-paused` | Boolean; default enabled |
| Auto translation | `whisper-translate-enabled` | Boolean |
| Translation source | `whisper-translate-from` | `auto` plus current 13 concrete language codes |
| Translation target | `whisper-translate-to` | Current 13 concrete language codes; default `en` |
| Translation display mode | `whisper-translate-mode` | `1` dual line, `0` translation only |

Compatibility details:

- `tiny.en` and `base.en` force transcription language to `en` on load and Apply.
- Existing model filenames, catalog IDs, and bundled/downloaded lookup locations remain unchanged.
- Model availability still distinguishes bundled, per-user downloaded, both, and missing.
- Backend status retains the current pending state when no active backend is known.
- Applying settings during playback continues through the plugin sender thread's existing non-realtime reconciliation path.
- Applying settings while VLC/plugin is not running remains durable and is picked up on the next plugin start.

## Production architecture

### 1. Production Qt application

The first-class target is `vlc-whisper-settings` (Windows `vlc-whisper-settings.exe`, Linux ELF `vlc-whisper-settings`). It retains the spike's small-screen behavior: content-sized native Widgets layout capped to the available work area with scrolling when needed. The layout mirrors the former Lua dialog closely and uses Qt/native DPI scaling.

The same executable has a short `--launch-detached` mode. It creates no settings window itself; it uses `QProcess::startDetached()` to create the normal instance and exits with a process-creation result. Normal instances retain the user-scoped single-instance socket and raise/focus an existing settings window.

### 2. Durable settings contract

Use one versioned, per-user, atomically replaced settings document:

- Windows: `%LOCALAPPDATA%\vlc-whisper\settings.json`
- Linux: `$XDG_CONFIG_HOME/vlc-whisper/settings.json`, falling back to `$HOME/.config/vlc-whisper/settings.json`

Writes use normal per-user permissions and never require administrator/root rights. A reinstall intentionally removes the current JSON and leaves a reset marker so the plugin sees defaults even before the settings UI next opens.

### 3. Worker-owned model downloads

HTTP, manifest allowlisting, `.part` handling, SHA-256 verification, and final installation remain worker-owned. The settings app writes only the one-shot local request/abort handoff consumed by the plugin sender path.

### 4. Lua launcher

`lua/extensions/vlc_whisper_settings.lua` contains only descriptor/menu metadata, a local `vlc.stream()` call to the plugin's launcher access submodule, logging, and a one-shot error dialog. The native bridge uses `CreateProcessW(..., CREATE_NO_WINDOW, ...)` on Windows or `posix_spawn()` on Linux to invoke the Qt bootstrap. A successful bootstrap means `QProcess::startDetached()` created the real process; it does not promise that the window painted or that the child stayed alive.

### 5. Packaging

Windows installs the GUI executable and deployed Qt runtime in the VLC-Whisper install area. The shell-free launcher bridge is embedded in the existing VLC-Whisper plugin, so no extra launcher executable or DLL is shipped. Ubuntu/Debian installs `/usr/bin/vlc-whisper-settings` and declares the Qt runtime dependencies. No shipped artifact contains `spike` naming.

## Test-first implementation sequence

1. Freeze settings defaults/validation/path semantics in network-free tests.
2. Add persistence and plugin-pickup checks, then implement shared per-user settings.
3. Add launcher contract checks, then reduce Lua, add the native shell-free access bridge, and add Qt `startDetached()` acknowledgement.
4. Add model-command checks, then wire request/abort while retaining worker HTTP ownership.
5. Add packaging assertions and headless Qt smoke coverage, then remove the spike directory and stale docs.

## Acceptance criteria

The VLC menu entry opens the standalone Qt settings app through a shell-free VLC access bridge and acknowledged `QProcess::startDetached()` process creation without polling or blank shell-window helpers; current settings persist and apply live; settings storage requires no elevation; reinstall resets the JSON settings file; model download remains worker-owned; settings performs no translation HTTP test; Windows and Ubuntu packages include the production GUI; the spike directory is gone; network-free/headless checks pass; and the completed branch is submitted as a PR.
