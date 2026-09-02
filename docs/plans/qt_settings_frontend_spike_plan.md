# Task: Standalone Qt Settings Frontend Feasibility Spike

## Goal

Evaluate whether a standalone Qt Widgets process can reproduce the current VLC-Whisper Lua settings dialog closely enough to become the future settings/model-management frontend, while keeping this branch isolated from VLC, plugin, worker, IPC, networking, and production packaging.

## Context

- Base: `main` at the v0.1.0 MVP merge (`55edfa9993b4be6b32a5768656397447a815fc1d`).
- Current source UI: `lua/extensions/vlc_whisper_settings.lua`.
- Current persistence/control path: Lua writes VLC config keys and the plugin polls them.
- Current model provisioning: worker-owned downloader, WinHTTP on Windows and `curl` child process on Linux, with per-model locking, `.part` files, SHA-256 verification, retry, abort, and atomic final rename.
- This spike intentionally evaluates presentation and local JSON persistence only.

## Scope

### In scope

1. Add an isolated `spikes/qt-settings/` CMake project using Qt 6 Widgets/Core only.
2. Reproduce the current Lua dialog's visible controls and button types without redesigning them.
3. Preserve important Lua behavior:
   - Threads remain a text field and clamp to `1..16` on Apply.
   - `tiny.en` and `base.en` force transcription language to English on Apply/Download.
   - Translation test remains guidance-only.
4. Persist the existing setting names to `./settings.json` relative to the process CWD.
5. Use atomic whole-file replacement for JSON writes.
6. Document Ubuntu build/run and Windows native build/deployment testing.
7. Research whether future model-download HTTP transport should move from the worker to the Qt process.
8. Preserve the end-user policy that a future production installer bundles all Qt runtime dependencies; users install no extra packages.

### Out of scope

- Editing or replacing the Lua extension.
- Launching the Qt process from VLC.
- Plugin/worker IPC.
- Reading or writing live VLC configuration.
- Actual model downloading or model hashing in the spike executable.
- Translation HTTP.
- Root build/preset/CI changes.
- NSIS/CPack integration.
- Production settings path/schema migration.

## Design

```text
spikes/qt-settings/vlc-whisper-settings-spike
                    |
                    | Apply
                    v
           <current working dir>/settings.json

No VLC process
No plugin IPC
No worker IPC
No HTTP
```

The JSON keys deliberately retain the existing config names:

- `whisper-backend`
- `model-path`
- `whisper-language`
- `whisper-threads`
- `whisper-logging`
- `whisper-translate-enabled`
- `whisper-translate-from`
- `whisper-translate-to`
- `whisper-translate-mode`

## Acceptance criteria

- [x] Spike lives outside the root build and creates no new mandatory project dependency.
- [x] Qt Widgets provides the same control categories and choices as the current Lua dialog.
- [x] Apply writes and reloads `settings.json` from CWD.
- [x] JSON writes use `QSaveFile` atomic replacement semantics.
- [x] Threads clamp to `1..16`, with invalid text falling back to `4`.
- [x] `.en` models force English on Apply/Download.
- [x] How-to-test and Download buttons are interactive but cannot contact the worker, plugin, or network.
- [x] Ubuntu build instructions are documented.
- [x] Windows clean-machine deployment testing is documented.
- [x] Future end-user deployment requires no separate Qt/runtime installation.
- [x] Downloader ownership recommendation is documented separately.

## Verification

Automated compilation is not added to the existing repository CI because the spike is intentionally isolated and the current CI images do not install Qt development packages. Manual verification commands are documented in `spikes/qt-settings/README.md`.
