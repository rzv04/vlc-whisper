# Task: Make model downloading fully asynchronous

## Goal

Model-download controls remain in the single VLC-Whisper Lua dialog, but Lua never waits, polls, sleeps, performs network I/O, or runs a long callback. The C plugin/worker downloads models asynchronously, renders progress over a dedicated VLC SPU channel while media plays or is paused, activates the downloaded model, and stops safely on abort or process death.

## Context

- Relevant docs/ADR: `docs/architecture.md`, `docs/product.md`, `docs/api-contracts.md`, `docs/whisper-api.md`, ADR-022/ADR-023 in `docs/decisions.md`, and the Step 19c plan.
- Pinned VLC build: vendored VLC 3.0.23 headers under `worker/third_party/vlc-3.0.23`; no upstream VLC dependency for API decisions.
- Existing unstaged changes in `lua/extensions/vlc_whisper_settings.lua`, `plugin/src/vw_whisper_module.c`, and `worker/src/vw_worker.c` are related user work and must be preserved and reconciled, not discarded. `s1.png` is user-provided evidence and will not be committed.
- The dependency graph is stale and is not an implementation authority; callers and contracts are verified from source.
- No second Lua dialog is allowed.

## Scope

### In scope

- Remove the Lua download polling loop and every Lua busy-wait.
- Keep Lua download and abort handlers fire-and-forget through the existing config bridge.
- Dispatch a pending download request when the filter-created worker first becomes available, including when START is rejected because the selected model is missing.
- Allow `MODEL_CTRL` with a zero session ID while no caption session is active.
- Render model download progress/status through the existing C caption presenter using a dedicated wall-clock VLC SPU channel, separate from captions. The sender thread handles updates, so playback pause does not pause downloading or progress reporting.
- Resolve and activate the downloaded catalog model in the per-user model directory after verified completion.
- Stop and clear download status safely on abort, worker disconnect, VLC shutdown, or worker shutdown.
- Add focused presenter/client/lifecycle tests and update architecture, API/UX, source-layout, test-strategy, roadmap, README, Lua documentation, and ADR text where behavior changes.

### Out of scope

- A second Lua/native progress dialog.
- Network access from Lua or the VLC plugin.
- Changes to audio callbacks, inference scheduling, protocol wire formats, or model catalog hashes.
- Background downloads without an explicit user request.
- Dependency-graph regeneration.

### Files/components expected to change

- `lua/extensions/vlc_whisper_settings.lua`
- `plugin/include/vw_caption_presenter.h`, `plugin/src/vw_caption_presenter.c`, `plugin/src/vw_whisper_module.c`, `plugin/src/vw_worker_client.c`, `plugin/include/vw_worker_client.h`
- Focused tests under `tests/unit/` and `tests/integration/` as needed
- `docs/architecture.md`, `docs/api-contracts.md`, `docs/decisions.md`, `docs/plans/step19c_plan.md`, `docs/source-layout.md`, `docs/test-strategy.md`, `docs/roadmap.md`, `lua/README_SETTINGS.md`, and `README.md`

## Design

### Inputs and outputs

- Lua writes `whisper-model-download=<catalog-id>` or `abort` and returns immediately.
- The plugin sender thread consumes the request and sends `MODEL_CTRL`; worker progress frames update config mirrors and invoke the presenter.
- The presenter displays bounded text such as `Model tiny: downloading (42%)` on a dedicated progress channel. Terminal states are shown briefly; worker death clears the active progress overlay and reports stopped status.
- A successful download resolves `<per-user-model-dir>/<catalog-filename>`, updates the selected model path, and performs one config respawn without consuming transport-recovery budget.

### Ownership/threading model

- Lua callbacks perform only bounded config writes and short UI label updates.
- The VLC audio callback remains untouched and only enqueues bounded PCM.
- The plugin sender thread owns config polling, IPC writes/reads, progress presentation, and worker respawn orchestration.
- The worker main loop owns IPC writes; its dedicated model-download thread owns network I/O and hash/file operations. Worker `PAUSE` only suspends caption inference; it must not stop the download thread.
- Worker pipe disconnect or shutdown sets the worker loop false, aborts and joins the download thread, and removes partial state according to existing downloader behavior.

### Bounds, time units, and failure behavior

- Existing bounded IPC, model catalog, progress cadence, path, and retry limits remain authoritative.
- SPU progress updates are event-driven by received `MODEL_PROGRESS` frames; no Lua timer, sleep, polling loop, or busy-wait is introduced.
- Download failures, aborts, missing models, and worker death disable captions or download activity without interrupting media playback.
- A request made before media playback remains pending in the config namespace and is dispatched once the audio filter has spawned the worker.

### Privacy/security implications

The ADR-023 model-download boundary remains: only the worker performs explicit, pinned, sha256-verified model downloads. Future cloud translation is a separate opt-in feature and must disclose transcript egress. Lua and the current plugin path remain network-free; PCM remains non-persistent.

### Protocol change

None. Existing protocol v1.4 `MODEL_CTRL` and `MODEL_PROGRESS` frames are reused; zero session ID is explicitly valid for model control/progress outside a caption session.

## Acceptance criteria

- [x] Lua settings file contains no `os.clock` pacing, wait loop, network call, or long-running download callback.
- [x] Clicking Download before media starts leaves a pending request; starting media launches the worker and begins the download.
- [x] Dedicated SPU progress appears while media plays and continues updating while media is paused.
- [x] Pressing Abort stops the worker download and clears/updates the SPU without stopping playback.
- [x] Worker/VLC shutdown stops the download thread and leaves no active download process.
- [x] A verified downloaded model becomes loadable and captions resume through the existing worker respawn path.
- [x] Existing bundled `tiny` default and explicit user model choices remain correct.
- [x] Automated tests cover zero-session model control, progress presentation helpers, abort/shutdown cleanup, and existing download success/failure paths.
- [x] Documentation describes the single-dialog, no-wait Lua architecture and dedicated-SPU progress behavior.

## Test plan

- `luac -p lua/extensions/vlc_whisper_settings.lua` and a static check that the Lua file contains no `os.clock` or wait loop.
- `clang-format --dry-run --Werror <changed C files>`.
- `cmake --preset linux-x64-debug && cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug`.
- `ctest --test-dir build/linux-x64-debug -T memcheck`.
- Manual VLC 3.0.23 test: request a missing model before media; play media; observe dedicated-SPU progress; pause and verify progress continues; abort; verify playback continues; repeat with worker/VLC termination.
- Manual clean-install test: verify bundled `tiny` remains the default and a completed per-user download is selected and loaded.

## Definition of done

- [x] C17 and project namespace/style rules remain satisfied.
- [x] No blocking work is introduced in the VLC audio callback or Lua extension callback.
- [x] Explicit model-download network policy, future translation boundary, and bounded-resource invariants remain documented.
- [x] Error paths preserve VLC playback.
- [x] Required formatting, build, tests, and Valgrind checks pass.
- [x] Documentation and roadmap state match the implementation.
- [x] Changes are committed on the current branch and not pushed.

## Evidence

- Build/test outputs: `cmake --preset linux-x64-debug`, full CTest, and CTest memcheck pass; the whisper-engine test remains an intentional skip because its fixture is unavailable.
- Lua syntax/static checks and focused presenter, downloader, worker-client, and worker-config tests pass.
- Manual VLC evidence: not run in this Linux workspace; dedicated-SPU behavior is covered by presenter tests and requires the pinned Windows VLC build for end-to-end confirmation.
- Known limitation: progress visibility depends on an active vout; no second dialog is used as fallback. The dedicated SPU channel is intentionally not the caption channel.
