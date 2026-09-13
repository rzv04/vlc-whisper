# Standalone Qt Settings Productionization Plan

Status: planning only. No production code or tests are changed by this plan commit.

## Goal

Replace the sandboxed `vlc.dialog` settings UI with a production standalone Qt 6 settings application while retaining a minimal VLC Lua extension named **VLC-Whisper Settings** as a fire-and-forget launcher. Preserve the current settings surface, defaults, validation, persistence semantics, live-apply behavior, model availability behavior, and worker-owned model download boundary before adding any broader settings features.

The existing `spikes/qt-settings/` implementation is a feasibility reference only. Production code, binaries, package names, UI strings, documentation, and install paths must contain no `spike` naming. The spike directory is removed as part of productionization after its useful behavior is covered by tests and moved into the production target.

## Non-negotiable invariants

- The Lua extension remains discoverable as `VLC-Whisper Settings`, but no longer owns the settings form.
- Lua activation is fire-and-forget: no polling loops, timers, sleeps, process joins/waits, worker IPC loops, HTTP, model hashing, or long-running callbacks on VLC's UI/cooperative extension path.
- Immediate launch validation may only do bounded local work (resolve/check the executable and inspect the spawn return value). If launch cannot be initiated, Lua may show a one-shot user-visible error and return.
- The standalone settings process owns UI and durable user-setting edits, not inference and not network/model download execution.
- HTTP model downloading remains owned by the worker (ADR-023) in this branch. The settings app may request/cancel downloads and display local status, but it must not perform HTTP or SHA-256 download verification itself.
- Future guarantee: the settings-process boundary must not prevent moving model-download orchestration into the settings process later, but this branch does not perform that move.
- No cloud-translation test/request is initiated from the settings application in this branch. Translation configuration remains present; the old `How to test` action is intentionally omitted rather than replaced with HTTP.
- Existing playback/audio real-time invariants remain untouched; settings work must never add blocking work to VLC's audio callback.
- For every implementation slice, tests are written first and demonstrated failing for the intended missing behavior before production code is changed.

## Current behavior that must remain compatible

The production UI mirrors the current Lua settings contract:

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
- User choices made with the legacy Lua settings UI must not be silently lost on upgrade.

## Production architecture

### 1. Production Qt application

Create a first-class root build target named `vlc-whisper-settings` (Windows artifact `vlc-whisper-settings.exe`, Linux ELF artifact `vlc-whisper-settings`). Move the useful Qt Widgets behavior out of `spikes/qt-settings/` into a production source directory and update the C++ policy/ADR so Qt C++17 is an explicit isolated GUI exception while plugin/worker/protocol authored code remains C17.

Keep the spike's small-screen behavior: content-sized window capped to the available work area with scrolling when needed. Remove all frontend-only/spike strings and simulated actions.

Use a single-instance design. A second invocation should cheaply signal the existing settings process to raise/focus its window and then exit. This makes repeated VLC menu activation safe and provides a stable launcher contract for future external callers.

### 2. Durable settings contract

The spike's CWD `settings.json` is not acceptable in production. Introduce one versioned, per-user, atomically replaced settings document shared by the settings app and plugin control path:

- Windows: `%LOCALAPPDATA%\vlc-whisper\settings.json`
- Linux: `$XDG_CONFIG_HOME/vlc-whisper/settings.json`, falling back to `$HOME/.config/vlc-whisper/settings.json`

Keep the existing key names listed above to minimize semantic drift. Validate every field on read; unknown keys are preserved when practical so later schema additions do not get erased by an older UI. A malformed/unreadable file must not crash either process: fall back to validated legacy/default values, surface a non-destructive warning in the UI, and never overwrite the bad file merely by opening the window.

Migration/bootstrap rules:

1. If the shared settings file is valid, it is authoritative for user-editable settings.
2. If it does not exist, preserve legacy VLC configuration values as the bootstrap source rather than resetting users to defaults. The transition implementation must provide a one-time bridge from the values currently visible through VLC config into the shared settings contract (the exact bridge is covered by tests before implementation; the Lua launcher may pass a bounded initial snapshot because it already has safe local config access, but it must not become a synchronization loop).
3. If neither migrated legacy state nor valid shared state exists, use the exact current defaults.
4. Writes use atomic replace (`QSaveFile` or equivalent) and restrictive normal per-user permissions; never write into the executable/VLC install directory.

The plugin's existing non-realtime config reconciliation path is extended to consume this durable settings source while retaining legacy VLC config fallback during migration. The audio callback never reads or parses the file.

### 3. Transient model-download control without moving HTTP

Do not make durable settings fields double as an endlessly replayed command. Keep model download execution in the worker and introduce a small one-shot local control handoff from settings UI -> plugin control path -> worker. The handoff must support request and abort, be safe when no plugin/worker is currently active, and avoid duplicate replay after restart. A queued request may remain pending until playback starts, matching current behavior.

The settings process may inspect model files locally to render availability, but must not hash large models on the UI thread. Worker-side manifest allowlisting, HTTP, `.part` handling, SHA-256 verification, atomic rename, and failure policy remain unchanged.

Document as a future architectural option—not branch work—that download orchestration could later move behind the settings process while retaining worker verification/execution boundaries or deliberately revising ADR-023 in a separate change.

### 4. Lua launcher

Reduce `lua/extensions/vlc_whisper_settings.lua` to descriptor/menu metadata, executable discovery, bounded preflight, detached spawn, logging, and immediate failure UI only.

Activation contract:

1. Resolve the production settings executable from trusted install locations; never concatenate user-controlled shell fragments.
2. Verify that the target exists/is plausibly executable using bounded local checks.
3. Launch detached and return immediately. No wait-for-window, child handle join, periodic process check, sleep, polling, or IPC loop.
4. On an immediate resolution/spawn failure only, log and show a small one-shot VLC error explaining that the settings application could not be started.
5. Deactivate the extension immediately after launch so VLC never treats it as a long-lived settings controller.

The Lua menu should no longer expose separate direct `Download selected model` / `Abort model download` callbacks. Those controls belong in the Qt UI; the Lua entry remains the simple `VLC-Whisper Settings` launcher.

### 5. Platform launch and packaging

Windows:

- Build `vlc-whisper-settings.exe` as a GUI executable.
- Install it in a stable trusted location owned by the VLC-Whisper installer (initial target: alongside the existing VLC-Whisper worker/VLC installation assets so the Lua extension can resolve it deterministically).
- Package all required Qt runtime/plugins using an explicit deployment step; prevent any artifact named `*spike*` from entering the installer staging tree.
- Uninstaller removes the settings executable and its packaged Qt runtime assets but does not delete the user's settings file by default.

Ubuntu/Debian:

- Build the same Qt 6 Widgets source as a native ELF executable named `vlc-whisper-settings`.
- Install the user-facing launcher binary at `/usr/bin/vlc-whisper-settings`; keep worker binaries in the existing VLC private lib location.
- Add the required Qt 6 runtime dependency/deployment policy to the DEB instead of treating the GUI as a Windows-only executable.
- The Lua extension launches the stable installed binary without depending on the current working directory.
- Package uninstall removes binaries but preserves per-user settings unless the user explicitly purges them.

The executable must also be directly launchable outside VLC. Its CLI/single-instance entry point should stay stable enough for a future Chrome extension/native-messaging launcher to invoke it without coupling Chrome to VLC internals. No Chrome integration is implemented in this branch.

## Test-first implementation sequence

### Phase A — freeze parity in tests

Before moving production code, add tests around a shared settings/schema layer for all current defaults, accepted values, thread clamping, `.en` language forcing, model-path mapping, translation values/modes, malformed input, unknown keys, and atomic-save failure paths. Add fixtures representing legacy Lua-era settings.

### Phase B — persistence/migration tests, then implementation

Add failing tests proving: per-user path resolution on Windows/Linux, valid-file precedence, legacy bootstrap, no reset on malformed file, atomic replacement, concurrent/read-during-write safety, settings applied while VLC is absent, and plugin pickup on next/live run. Only then introduce the production persistence bridge and plugin-side non-realtime reload logic.

### Phase C — launcher tests, then implementation

Add static/unit/integration coverage proving the Lua extension contains no polling/sleep/wait/join/network loop; repeated activation is non-blocking; missing executable produces a bounded visible failure; paths with spaces/non-ASCII characters are quoted safely; and a running settings instance is focused rather than duplicated. Then replace the Lua dialog with the launcher.

### Phase D — model-control tests, then implementation

Add tests for request/abort handoff, worker absent/present, queued-until-playback behavior, duplicate command suppression, crash/restart during a pending command, invalid catalog ID rejection, partial download recovery, and proof that only the worker HTTP path is invoked. Then wire the Qt buttons to the worker-bound path.

### Phase E — packaging/e2e tests, then implementation

Add/extend packaging assertions and manual/e2e checks for Windows and Ubuntu: installed paths, Qt runtime presence, direct launch, launch from VLC, repeated launches, small-screen scrolling, Apply persistence across process/VLC restart, live apply during playback, model availability/download request/abort, uninstall/reinstall, non-admin per-user state, and absence of any `spike` artifact/string in shipped outputs.

After tests cover the promoted behavior, remove `spikes/qt-settings/` completely and remove/update stale spike-specific documentation and the temporary C++ spike exception. Production documentation becomes the only source of truth.

## Edge cases to cover explicitly

### Process launch/lifetime

- Settings executable missing, renamed, quarantined, blocked by AV, or lacking execute permission.
- Install path contains spaces, apostrophes, Unicode, shell metacharacters, or a symlink.
- VLC is installed somewhere non-default or portable; architecture/package layout differs from expected.
- Repeated clicks, double activation, two VLC instances, and direct external launch race at the same time.
- Existing settings process is hung, starting, closing, or belongs to another user/session.
- Settings process crashes immediately after detached spawn; Lua still must not wait/poll for it.
- VLC exits while settings remains open; settings must stay functional for durable edits and close cleanly.

### Persistence/concurrency

- First run, upgrade from Lua-only settings, downgrade, reinstall, and missing user directories.
- Empty, truncated, malformed, wrong-schema, wrong-type, out-of-range, read-only, and permission-denied settings files.
- Power loss/process kill during save; old complete file survives.
- Settings app and plugin read while a save occurs; no partial JSON observation.
- Multiple writers are prevented/serialized; stale UI does not silently overwrite newer settings.
- Unknown future keys survive ordinary Apply where feasible.
- Legacy VLC values differ from shared-file values; precedence is deterministic and documented.

### Runtime application

- Apply before playback, during playback, paused, seeking, worker starting/stopping, and worker crash/restart.
- Rapid consecutive Apply operations coalesce safely and do not create respawn storms.
- Invalid model path or unavailable selected model retains current graceful `E_MODEL_MISSING` behavior.
- Switching to/from `.en` models normalizes language exactly as today.
- CPU thread text with whitespace, negatives, zero, decimals, overflow, and non-numeric input normalizes to current semantics.
- Backend active/status is unavailable or stale; UI shows pending/unknown rather than inventing state.

### Model download boundary

- Worker absent, playback stopped, request queued, worker starts later.
- Duplicate request, redownload existing model, abort before start, abort in-flight, abort after completion.
- Settings process closes immediately after request; worker operation continues independently.
- Invalid/corrupt manifest entry, network failure, disk full, permissions, existing `.part`, hash mismatch, and destination rename failure remain worker-handled.
- UI never performs download HTTP or hashes large model payloads on its event thread.

### Translation/privacy

- Translation enable/from/to/mode persist exactly as today.
- No test request/button can send text to a cloud endpoint from settings.
- Privacy copy remains explicit that enabling runtime translation may send finalized subtitle text according to the worker translation path; PCM remains local.

## Documentation/ADR changes required with implementation

- Supersede ADR-022's in-Lua dialog decision with the standalone Qt UI + minimal Lua launcher decision; reconcile ADR-011 history instead of silently contradicting it.
- Amend ADR-002 to allow only the isolated Qt settings application to use C++17/Qt while VLC plugin, worker, protocol, and realtime code remain C17.
- Keep ADR-023 accepted and explicit: model download HTTP/verification remains worker-owned in this branch.
- Update `architecture.md`, `invariants.md`, settings documentation, Windows manual checklist, Linux install/package docs, installer/uninstaller expectations, and user-facing screenshots only after behavior lands.

## Cleanup rules

- Remove completed/stale plan files once this plan is committed.
- Remove `spikes/qt-settings/` only during the implementation sequence after equivalent production behavior is test-covered; do not ship or document a spike target.
- Delete or rename all build targets, application names, status strings, file paths, and package staging entries containing `spike` before release.

## Acceptance criteria

This branch is complete only when the VLC menu entry opens the standalone production Qt settings app without blocking VLC; all current settings survive Apply/restart and affect the plugin as they do today; model download request/abort still reaches the worker-owned downloader; no cloud translation test originates from settings; Windows and Ubuntu packages install and launch the production GUI; repeated/external launch is safe; tests preceded each implementation slice; the spike directory and stale spike artifacts are gone; and shipped outputs contain no `spike` naming.
