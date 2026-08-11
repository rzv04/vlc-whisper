# Step 16 Plan: Play/Pause lifecycle

## Goal
VLC Play/Pause is reflected end-to-end: when the user pauses playback, the plugin stops
forwarding PCM to the worker and sends `PAUSE`; on resume it sends `RESUME`, resumes
forwarding, and caption timing stays synced to the media timeline (no stale or mixed
windows after the gap).

## Context
- **Relevant docs/ADRs**: `docs/api-contracts.md` (PAUSE/RESUME control messages, reason
  codes USER_PAUSE=1 / USER_RESUME=1), `docs/architecture.md` (sender-thread row),
  `docs/roadmap.md` (line 50), `docs/plans/milestone3_postmortem.md` (object-hierarchy walk
  pattern for finding the input thread, from `vw_plugin_find_input_location`).
- **VLC/worker/protocol version affected**: VLC 3.0.23 (vendored headers). No protocol change
  — PAUSE/RESUME frames, codec support, and `vw_msg_control_t` already exist; worker treats
  them as no-ops today.
- **Assumptions and explicit non-goals**:
  - `filter_t` in VLC 3.0.23 has **no `b_paused` field** (verified against the vendored
    header). Pause state must be queried from the input thread: walk the object hierarchy to
    find `input_thread_t` (same walk the presenter already does to find the vout, and the
    pattern documented in the postmortem) and call `input_GetState()` → `PAUSE_S`.
  - No seek/discontinuity handling here (step 17 owns `STOP`/`SEEK_DISCONTINUITY`), no SPU
    (17b), no presenter changes.
  - PAUSE preserves the session (`session_active` stays true, session_id unchanged). STOP
    remains the only session teardown.

## Scope
- **In scope**:
  - Client API: `vw_worker_client_pause_session()` / `vw_worker_client_resume_session()`
    (mirror `vw_worker_client_stop_session`, same control payload, do NOT flip
    `session_active`).
  - Plugin: pause-state detection on the sender thread; on pause transition stop draining the
    SPSC queue and send `PAUSE`; on resume send `RESUME` and resume draining.
  - Worker: `paused` flag; on `PAUSE` clear the in-flight audio window and VAD state (a window
    spanning the pause gap would mix pre/post-pause audio — "preserving the timeline" means
    the PTS epoch and session, not a half-filled window); on `RESUME` un-pause. Drop any
    `AUDIO` frames that arrive while paused (counted, not fatal).
  - Tests + docs (roadmap, api-contracts wording, architecture row, test-strategy).
- **Out of scope**: seek/discontinuity, SPU, presenter changes, GPU.
- **Files/components expected to change**:
  - `plugin/include/vw_worker_client.h`, `plugin/src/vw_worker_client.c`
  - `plugin/src/vw_whisper_module.c` (sender loop + pause detection helper)
  - `worker/src/vw_worker.c`
  - `tests/unit/vw_test_worker_client.c`, `tests/integration/test_worker_lifecycle.c` (or
    `test_worker_ipc.c`), `docs/roadmap.md`, `docs/api-contracts.md`, `docs/architecture.md`,
    `docs/test-strategy.md`

## Design
- **Inputs and outputs**: input thread `PAUSE_S` state → plugin-side `paused` flag → `PAUSE`/
  `RESUME` control frames → worker `paused` flag gating window accumulation.
- **Ownership/threading model**: pause detection runs on the sender thread (already the only
  plugin thread touching the client and queue). The object walk (parents + children, `input`
  type) is read-only; `input_GetState()` is a query — safe off the filter callback thread.
  Worker `paused` is main-loop-local, no atomics needed (same thread as session state).
  Capture itself needs no change: it re-anchors per block from VLC's `input->pts_us`, so PTS
  continuity across pause/resume is VLC's media clock, not plugin state.
- **Bounds, time units, and failure behavior**: pause polled at the sender loop cadence
  (5 ms after sends / 20 ms idle) — sub-20 ms pause latency, irrelevant for 8 s windows.
  Transport failures during PAUSE/RESUME send reuse the existing drop-transport fail-closed
  path (same as stop_session). AUDIO dropped while paused counts into `dropped_audio_us`;
  playback is never affected.
- **Privacy/security implications**: none (local IPC only, no new data).
- **Protocol change**: none — PAUSE/RESUME already in the wire contract and codec.

## Acceptance criteria
- [ ] Pause (spacebar/`pl_pause`): plugin sends `PAUSE` (USER_PAUSE), stops forwarding PCM;
      worker stops accumulating windows; no captions emitted during pause.
- [ ] Resume: plugin sends `RESUME` (USER_RESUME), forwarding resumes; worker starts a fresh
      window (no caption mixing pre-pause and post-pause audio); captions continue and PTS
      stays monotonic.
- [ ] Rapid pause/resume spam does not desync or drop the transport (client is
      fail-closed-safe; worker ignores duplicate PAUSE).
- [ ] Pause with no active session is a safe no-op on both sides.
- [ ] Automated tests cover client send + worker gating; docs updated.

## Test plan
- Automated gate (AGENTS.md rule 10): `clang-format --dry-run --Werror` on touched files;
  `cmake --preset linux-x64-debug && cmake --build --preset linux-x64-debug && ctest
  --preset linux-x64-debug`; `ctest --test-dir build/linux-x64-debug -T memcheck`;
  Windows cross-build (`windows-x64-debug` preset).
- Unit: extend `vw_test_worker_client` with a pause/resume send case (server asserts
  PAUSE then RESUME frames with correct session_id/reason, session stays active after both).
- Integration: extend the lifecycle/IPC test — worker receives `AUDIO`, then `PAUSE` (no
  transcription during), then `RESUME` + `AUDIO` (window restarts, segment still emitted).
- Manual acceptance (live VLC): play video with captions, pause mid-speech — captions freeze,
  no new text during pause; resume — captions continue from the right place, no garbage
  window mixing pre-pause audio. Verify PTS continuity via the `PLUGIN_STATUS` log.

## Definition of done
- [ ] C17 code; no project-authored C++ introduced
- [ ] No blocking work in VLC audio callback (pause detection is on the sender thread)
- [ ] No network access, telemetry, transcript/PCM persistence, or sensitive logs introduced
- [ ] Memory, audio queue, frame, text, and retry limits are bounded
- [ ] Error path is safe: captions may stop, playback does not
- [ ] Postmortem lessons honored: object-walk for input state reuses the documented pattern;
      no protocol drift; single-sweep scope (no seek/SPU bundling)
