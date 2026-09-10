# Task: Standalone Qt Settings Frontend Feasibility Spike

## Goal

Provide an externally testable standalone Qt Widgets process that reproduces the current VLC-Whisper Lua settings dialog closely enough to evaluate it as a future settings/model-management frontend, while remaining isolated from VLC, the plugin, worker IPC, networking, and production packaging.

## Context

- Relevant docs/ADR: `AGENTS.md`, `docs/source-layout.md`, `docs/decisions.md` (ADR-011 and ADR-022), `lua/README_SETTINGS.md`, and `docs/plans/qt_settings_downloader_ownership_research.md`.
- VLC/worker/protocol version affected: none; the spike does not link or communicate with VLC, the plugin, protocol, or worker.
- Base: `main` at the v0.1.0 MVP merge (`55edfa9993b4be6b32a5768656397447a815fc1d`).
- Current source UI: `lua/extensions/vlc_whisper_settings.lua`.
- Current persistence/control path: Lua writes VLC config keys and the plugin polls them.
- Current model provisioning: worker-owned downloader, WinHTTP on Windows and a `curl` child process on Linux, with per-model locking, `.part` files, SHA-256 verification, retry, abort, and atomic final rename.
- Assumptions and explicit non-goals: this is a presentation/local-persistence feasibility spike only. The explicitly approved `spikes/qt-settings/` directory is a narrow C++17 exception because Qt Widgets exposes a C++ API; the exception does not apply to production plugin, protocol, worker, root build, CI, or packaging code.

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
- Any C++ exception outside `spikes/qt-settings/`.

### Files/components expected to change

- `spikes/qt-settings/CMakeLists.txt`
- `spikes/qt-settings/src/vw_qt_settings_main.cpp`
- `spikes/qt-settings/README.md`
- `docs/plans/qt_settings_frontend_spike_plan.md`
- `docs/plans/qt_settings_downloader_ownership_research.md`
- `docs/source-layout.md`
- `AGENTS.md` only to record the narrow spike-only C++17 exception required by Qt Widgets

## Design

### Inputs and outputs

The frontend accepts only local user interaction. `Apply` emits one JSON document at `<current working directory>/settings.json` using these existing setting names:

- `whisper-backend`
- `model-path`
- `whisper-language`
- `whisper-threads`
- `whisper-logging`
- `whisper-translate-enabled`
- `whisper-translate-from`
- `whisper-translate-to`
- `whisper-translate-mode`

### Ownership/threading model

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

The normal Qt event loop owns all UI interaction. There are no worker threads, IPC endpoints, timers, or background network operations in the spike.

### Bounds, time units, and failure behavior

- Threads are parsed as an integer and clamped to `1..16`; invalid text falls back to `4`.
- Model, engine, language, translation source/target, and display-mode values are bounded by fixed UI choice lists matching the Lua extension.
- Invalid or unreadable `settings.json` falls back to defaults and exposes that state in the status row.
- `QSaveFile` provides temporary-file plus commit semantics so Apply does not intentionally replace the destination with a partially written JSON document.
- There are no media timestamps or timing-sensitive paths in this frontend-only spike.

### Privacy/security implications

- The spike performs no HTTP and creates no sockets or named pipes.
- The Download button is a UI-only simulation and transfers no model data.
- The translation test button only changes local guidance text.
- No PCM, transcript, translated text, credentials, telemetry, or remote logs are persisted or transmitted.
- The only persistent output is the explicitly documented non-sensitive settings JSON in the process CWD.

### Protocol change

None. The protocol library and wire version are untouched.

## Acceptance criteria

- [x] Spike lives outside the root build and creates no new mandatory production dependency.
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
- [x] The Qt C++17 exception is restricted to this isolated feasibility spike and does not alter production components.
- [x] Component/file ownership is recorded in `docs/source-layout.md`.

## Test plan

Target: Ubuntu 24.04 or equivalent with Qt 6.2+, plus a Windows 10/11 x64 clean-machine deployment check when a native Qt development kit is available.

Ubuntu build and formatter checks:

```bash
clang-format --dry-run --Werror spikes/qt-settings/src/vw_qt_settings_main.cpp
cmake -S spikes/qt-settings -B build/qt-settings-linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/qt-settings-linux
```

Ubuntu manual verification from a writable scratch CWD:

```bash
mkdir -p /tmp/vlc-whisper-qt-settings-test
cd /tmp/vlc-whisper-qt-settings-test
/path/to/vlc-whisper/build/qt-settings-linux/vlc-whisper-settings-spike
cat settings.json
```

Verify defaults, all dropdown choices, thread values `0`, `17`, and non-numeric text, English-only model forcing, translation settings persistence, guidance-only translation test behavior, and no-network Download behavior. Close and restart from the same CWD to verify persisted selections reload.

Windows developer build/deployment procedure is defined in `spikes/qt-settings/README.md`. Stage the runtime with `windeployqt`, then test that staged directory on Windows without a separate Qt installation. The spike remains excluded from the current installer.

Production regression gates remain the repository's normal checks because no production code is modified:

```bash
cmake --preset linux-x64-debug
cmake --build --preset linux-x64-debug
ctest --preset linux-x64-debug --output-on-failure
ctest --test-dir build/linux-x64-debug -T memcheck
```

## Definition of done

- [x] Production plugin/protocol/worker code remains C17; project-authored C++ is confined to the explicitly approved Qt spike.
- [x] No blocking work is introduced in the VLC audio callback because VLC code is untouched.
- [x] No unapproved network access, telemetry, transcript/PCM persistence, or sensitive logs are introduced.
- [x] UI-configurable numeric input is bounded and invalid settings input degrades to defaults.
- [x] Error paths are frontend-local and cannot affect VLC playback.
- [x] No protocol contract or compatibility version changes are introduced.
- [x] Relevant source-layout and spike documentation are updated.
- [x] A reviewer can reproduce the spike from a clean checkout using the documented Qt development dependencies.
- [ ] Fresh formatter verification passes after review remediation.
- [ ] Repository CI/regression checks pass at the final PR head.
- [ ] Fresh Codex review of the spike reports no remaining blocking findings.

## Evidence

- Build/test outputs or CI links: the project owner reported successful compilation and successful visual/interactive execution on the headless Ubuntu development machine. PR #39 runs the repository's normal GitHub checks; the spike itself remains intentionally outside root CI because Qt development packages are not production dependencies.
- Measured performance: not relevant for a settings frontend feasibility spike.
- Known limitations/follow-ups: no VLC launch integration, runtime IPC, model transfer, model hashing, translation request, installer integration, or production settings ownership is implemented. Windows native appearance and clean-machine runtime bundling remain manual validation steps.
