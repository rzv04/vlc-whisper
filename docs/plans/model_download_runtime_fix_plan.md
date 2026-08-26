# Task: Make runtime model downloads observable and activatable

## Goal

An explicit download request made before or during playback reaches the live worker exactly once, saves the verified model in the documented per-user directory, and resumes captions without requiring a second VLC restart.

## Context

- Relevant docs/ADR: `docs/architecture.md`, `docs/product.md`, `docs/api-contracts.md`, `docs/whisper-api.md`, ADR-023, and `docs/plans/model_download_no_wait_plan.md`.
- Pinned VLC API: vendored VLC 3.0.23 headers under `worker/third_party/vlc-3.0.23`; the dependency graph is stale and is not used.
- Current evidence: VLC reports missing selected model and no `MODEL_CTRL`/`MODEL_PROGRESS` trace; Windows runtime storage is `%LOCALAPPDATA%\\vlc-whisper\\models`, while worker diagnostics default to `%TEMP%\\vlc-whisper-worker.log`.
- Preserve the single Lua dialog and its no-wait rule. Do not add a second UI, polling loop, or network path to Lua.

## Scope

- In scope:
  - Make the Lua-to-plugin control edge verifiable and robust when config values are set before the filter exists.
  - Log request observation, relay, worker progress, terminal failure, destination, and activation outcomes without logging tokens, PCM, or transcripts.
  - Ensure the plugin does not repeatedly reissue a stale request or lose a request during the initial missing-model START failure.
  - Ensure verified completion maps to the exact per-user path passed to the worker and triggers model activation/respawn.
  - Ensure a worker restarted after download resolves the existing relative selected path against `--model-dir`.
  - Ensure the Windows uninstaller removes only the app-owned per-user downloaded-model directory, including partial files.
  - Add regression coverage for control-edge handling, terminal progress, activation failure, and persisted-path selection where test seams permit.
  - Update user-facing and architectural documentation with the actual Windows paths and troubleshooting evidence.
- Out of scope:
  - Changing the protocol version or adding a second dialog.
  - Automatic downloads, resume/range support, or changes to the model catalog.
  - Changes to realtime audio callback behavior or caption inference algorithms.
- Files/components expected to change:
  - Lua settings extension and its README.
  - Plugin sender/config/download orchestration and worker client tests.
  - Worker Windows downloader diagnostics/tests if required by the confirmed defect.
  - `docs/architecture.md`, `docs/api-contracts.md`, `docs/product.md`, `docs/test-strategy.md`, and this plan.

## Design

- Inputs and outputs:
  - Lua emits one catalog-id or `abort` command and returns immediately.
  - The plugin records the command edge, sends one worker-scoped `MODEL_CTRL`, mirrors bounded status, and consumes `MODEL_PROGRESS` until a verified terminal state.
  - DONE resolves `<per-user-model-dir>/<catalog filename>`, verifies the file exists, updates `model-path`, and respawns the worker once.
- Ownership/threading model:
  - Lua remains command-only.
  - The plugin sender thread owns config observation, IPC control, progress presentation, and activation orchestration.
  - The worker IPC loop owns progress frames; its downloader thread owns WinHTTP/curl and file/hash work.
- Bounds, time units, and failure behavior:
  - Catalog IDs, paths, statuses, progress values, and log messages stay bounded.
  - Failed relay, failed download, failed hash, missing destination, or failed activation reports an explicit terminal status and leaves media playback running.
  - A stale config value cannot trigger an unbounded retry loop; one user command produces at most one active worker download.
- Privacy/security implications:
  - Model downloads use the existing worker network path; future translation requires separate opt-in disclosure. Diagnostics contain no auth tokens, transcript text, or PCM.
- Protocol change: none.

## Acceptance criteria

- [ ] A pre-play Download request is observed and relayed after the filter worker connects.
- [ ] A request during playback produces plugin and worker diagnostic entries and bounded progress frames.
- [ ] The verified file exists in `%LOCALAPPDATA%\\vlc-whisper\\models` on Windows and the exact path is logged.
- [ ] DONE activates the downloaded model and captions can start without VLC restart.
- [x] A worker restarted after download loads the verified file from the per-user directory even when config still contains the relative catalog path.
- [ ] Failed/aborted downloads do not repeatedly restart or block media playback.
- [x] Windows uninstall removes `%LOCALAPPDATA%\\vlc-whisper\\models` and leaves unrelated user data untouched.
- [x] Automated native tests pass for downloader, worker IPC/control, lifecycle, presenter, and plugin loading; the
  remaining initial-IDLE-to-DONE activation assertion requires the Windows VLC integration seam.
- [x] Documentation describes runtime storage and worker-log locations.

## Test plan

- Run Lua syntax and no-wait static checks.
- Run focused model-download, worker-client/control, and worker lifecycle tests.
- Run `clang-format --dry-run --Werror` on changed C/H files.
- Run `cmake --preset linux-x64-debug && cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug`.
- Run `ctest --test-dir build/linux-x64-debug -T memcheck`.
- Check the NSIS uninstall script and, when `makensis` is available, compile the installer script.
- Manual Windows VLC 3.0.23 test: request `tiny.en` before playback, inspect `%TEMP%\\vlc-whisper-worker.log`, verify the `.part`→final rename in `%LOCALAPPDATA%\\vlc-whisper\\models`, observe activation and captions, then repeat during playback and with Abort.

## Definition of done

- [x] C17 and project namespace/style rules remain satisfied.
- [x] No blocking work is introduced in the VLC audio callback or Lua callback.
- [x] Approved network-policy and bounded-resource invariants remain satisfied.
- [x] Error paths preserve VLC playback.
- [x] Required format, build, test, and Valgrind checks pass; Valgrind reports no definite, indirect, or possible
  leaks (only still-reachable allocations from system/runtime libraries).
- [x] Documentation and roadmap state match the implementation.
- [x] Changes are committed on the current branch and not pushed.

## Evidence

- Build/test outputs: `clang-format --dry-run --Werror` passed for all changed C files; `cmake --preset
  linux-x64-debug` configured; `cmake --build --preset linux-x64-debug` completed; `ctest --preset linux-x64-debug`
  passed all 21 tests; `ctest --test-dir build/linux-x64-debug -T memcheck` passed all runnable tests with zero
  definite/indirect/possible leaks. `test_whisper_engine` was skipped only by the Valgrind harness; the regular CTest
  run passed it.
- Lua checks: `luac -p lua/extensions/vlc_whisper_settings.lua` passed; static no-wait scan found no `os.clock`,
  `dlg:update`, `os.execute`, network, wait loop, or repeat construct in the extension.
- Path-resolution regression: `test_worker_config` creates a model file outside the configured relative path and
  confirms the worker-config resolver selects the same filename under `--model-dir`; both worker integration targets
  link the shared resolver and the full native suite passes 21/21.
- Packaging check: the Windows cross-release `installer` target compiled the generated NSIS script successfully and
  includes scoped cleanup for `%LOCALAPPDATA%\\vlc-whisper\\models`; it emitted only the existing nonfatal warning for
  the optional CPU worker artifact.
- Manual Windows evidence: not yet available in this Linux workspace.
- Known limitation: end-to-end WinHTTP, VLC Lua config registration timing, SPU rendering, and installed-binary version
  matching require the pinned Windows VLC environment. The Windows check must inspect `%LOCALAPPDATA%\\vlc-whisper\\models`
  and `%TEMP%\\vlc-whisper-worker.log`, not only the install-time `models\\` directory.
