# Standalone Qt Settings Productionization Plan

Status: implementation branch.

## Goal

Replace the sandboxed `vlc.dialog` settings UI with a production standalone Qt 6 settings application while retaining a minimal VLC Lua extension named **VLC-Whisper Settings** as a fire-and-forget launcher. Preserve the current settings surface, defaults, validation, persistence semantics, live-apply behavior, model availability behavior, and worker-owned model download boundary before adding any broader settings features.

The existing `spikes/qt-settings/` implementation is a feasibility reference only. Production code, binaries, package names, UI strings, documentation, and install paths must contain no `spike` naming. The spike directory is removed as part of productionization after its useful behavior is covered by tests and moved into the production target.

## Non-negotiable invariants

- The Lua extension remains discoverable as `VLC-Whisper Settings`, but no longer owns the settings form.
- Lua activation is fire-and-forget: no polling loops, timers, process joins/waits, worker IPC loops, HTTP, model hashing, or long-running callbacks on VLC's UI/cooperative extension path.
- Immediate launch validation may only do bounded local work (resolve/check the executable and inspect the spawn return value).
- Narrow launcher exception: a single bounded delayed verification of approximately one second is permitted solely to detect an immediately failed child and show a reinstall error. It must not poll, loop, join the child, wait for window creation, or become a liveness monitor. If VLC Lua cannot express this safely without blocking its cooperative UI path, the implementation must skip delayed verification and retain immediate spawn-failure reporting only.
- The standalone settings process owns UI and durable user-setting edits, not inference and not network/model download execution.
- HTTP model downloading remains owned by the worker (ADR-023) in this branch. The settings app may request/cancel downloads and display local status, but it must not perform HTTP or SHA-256 download verification itself.
- Future guarantee: the settings-process boundary must not prevent moving model-download orchestration into the settings process later, but this branch does not perform that move.
- No cloud-translation test/request is initiated from the settings application in this branch. Translation configuration remains present; the old cloud test action is not implemented.
- Existing playback/audio real-time invariants remain untouched; settings work must never add blocking work to VLC's audio callback.
- Tests for each non-trivial implementation slice are added before production code where practical and remain network-free/headless-safe.

## Current behavior that must remain compatible

| UI setting | Persisted/runtime key | Required behavior |
| --- | --- | --- |
| Engine | `whisper-backend` | `auto`, `gpu`, `cpu`; default `auto` |
| Model | `model-path` | Existing seven Whisper model paths and labels; default `models/ggml-tiny.bin` |
| Language | `whisper-language` | `en`, `ro`, `tr`, `de`, `fr`, `es`; default `en` |
| CPU threads | `whisper-threads` | Parse integer, invalid -> `4`, clamp `1..16` |
| Diagnostic logging | `whisper-logging` | Boolean |
| Auto translation | `whisper-translate-enabled` | Boolean |
| Translation source | `whisper-translate-from` | `auto` plus current 13 concrete language codes |
| Translation target | `whisper-translate-to` | Current 13 concrete language codes; default `en` |
| Translation display mode | `whisper-translate-mode` | `1` dual line, `0` translation only |

Compatibility details:

- `tiny.en` and `base.en` force transcription language to `en` on load and Apply.
- Existing model filenames, catalog IDs, and bundled/downloaded lookup locations remain unchanged.
- Model availability still distinguishes bundled, per-user downloaded, both, and missing.
- Backend status retains the current pending state when no active backend is known.
- Applying settings during playback continues to trigger the existing plugin-side live configuration reconciliation/worker restart behavior rather than requiring VLC restart.
- Applying settings while VLC/plugin is not running remains durable and is picked up on the next plugin start.

## Production architecture

### 1. Production Qt application

Create a first-class root build target named `vlc-whisper-settings` (Windows artifact `vlc-whisper-settings.exe`, Linux ELF artifact `vlc-whisper-settings`). Promote the useful Qt Widgets behavior out of `spikes/qt-settings/` into production. Keep the spike's small-screen behavior: content-sized window capped to the available work area with scrolling when needed. Remove all frontend-only/spike strings and simulated actions.

### 2. Durable settings contract

Use one versioned, per-user, atomically replaced settings document:

- Windows: `%LOCALAPPDATA%\vlc-whisper\settings.json`
- Linux: `$XDG_CONFIG_HOME/vlc-whisper/settings.json`, falling back to `$HOME/.config/vlc-whisper/settings.json`

Writes must use normal per-user permissions and never require administrator/root rights. The installer/reinstaller intentionally removes this settings JSON so a reinstall resets applied settings to defaults.

### 3. Worker-owned model downloads

Keep HTTP, manifest allowlisting, `.part` handling, SHA-256 verification, and final installation worker-owned. The settings app only writes the existing request/abort controls through the shared control handoff. Moving downloader orchestration into settings remains a future architectural option only.

### 4. Lua launcher

Reduce `lua/extensions/vlc_whisper_settings.lua` to descriptor/menu metadata, executable discovery, detached spawn, logging, immediate failure UI, and at most the single bounded delayed failure check above. No polling, child join, or network work.

### 5. Packaging

Windows installs `vlc-whisper-settings.exe` and its Qt runtime in the VLC-Whisper install area. Ubuntu/Debian installs the same Qt app as `/usr/bin/vlc-whisper-settings` and declares the Qt runtime dependency. No shipped artifact may contain `spike` naming.

## Test-first implementation sequence

1. Freeze settings defaults/validation/path semantics in network-free tests.
2. Add persistence and plugin-pickup tests, then implement shared per-user settings.
3. Add launcher static/behavior checks, then reduce Lua to detached launch.
4. Add model-command tests, then wire request/abort while retaining worker HTTP ownership.
5. Add packaging assertions and headless Qt smoke coverage, then remove the spike directory and stale docs.

## Acceptance criteria

The VLC menu entry opens the standalone Qt settings app without polling or UI-thread loops; current settings persist and apply live; settings storage requires no elevation; reinstall resets the JSON settings file; model download remains worker-owned; no cloud translation test originates from settings; Windows and Ubuntu packages include the production GUI; the spike directory is gone; and a PR is opened after verification.
