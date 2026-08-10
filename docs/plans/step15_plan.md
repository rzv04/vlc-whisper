# Step 15 Plan: Wire SEGMENT frames to the caption presenter

## Goal
Live VLC playback shows real-time whisper captions: `VW_MSG_CAPTION_SEGMENT` frames
received on the plugin sender thread are rendered via `vw_caption_presenter_display()`.
This completes the roadmap item: "Receive incoming `SEGMENT` frames on plugin background
thread and trigger `vw_caption_presenter_display()`."

## Context
- **Relevant docs/ADRs**: `docs/architecture.md` (caption receiver thread, step 15 row),
  `docs/plans/milestone3_postmortem.md` (archived branch shipped this step once; OSD string
  lifetime + session-ID stamping bugs documented there), `docs/roadmap.md` (line 49).
- **VLC/worker/protocol version affected**: VLC 3.0 audio filter module; no protocol change.
- **Assumptions and explicit non-goals**:
  - The presenter (`vw_caption_presenter.c/h`) is already implemented and unit-tested —
    this step only wires it.
  - Postmortem bug #1 (OSD string lifetime) is already fixed upstream of this step:
    `vw_worker_client_receive_frame` copies segment text into owned `text_buf`
    (`vw_worker_client.c:475-477`), so `recv.segment.text_utf8` is stable for the caller.
  - Postmortem bug #3 (session-ID zero-stamping) is already fixed worker-side
    (`vw_worker.c:372`). The plugin does not validate `session_id` today and this step
    does not add validation (deferred to step 17 epoch logic).
  - No SPU subpicture channels (step 17b), no PAUSE/RESUME (step 16), no seek (step 17).
  - Latency is NOT part of this step: the worker transcribes fixed 8s windows (`VW_WINDOW_SAMPLES 128000` @16kHz) with a 2s hop, so captions display ~8s + inference behind live audio. That is inherent to the batch architecture and the target of steps 17c/17d — do not mistake it for a wiring bug during acceptance.

## Scope
- **In scope**:
  - Add `vw_caption_presenter_t presenter` to `vw_plugin_sys_t`.
  - Initialize `presenter.p_filter_ctx = p_filter` in `vw_plugin_open`.
  - Replace the counting-only `VW_MSG_CAPTION_SEGMENT` case in the sender thread
    (`vw_whisper_module.c:259-261`) with a synchronous
    `vw_caption_presenter_show_segment(&sys->presenter, &recv.segment)` call.
  - `vw_caption_presenter_clear(&sys->presenter)` in `vw_plugin_close` (remove OSD on teardown).
  - Docs: roadmap checkbox, architecture row, test-strategy note.
- **Out of scope**: presenter redesign, session validation, pause/resume, SPU, seek.
- **Files/components expected to change**:
  - `plugin/src/vw_whisper_module.c` (the only code file)
  - `docs/roadmap.md`, `docs/architecture.md`, `docs/test-strategy.md`

## Design
- **Inputs and outputs**: SEGMENT frames decoded by `vw_worker_client_receive_frame` into
  `vw_worker_recv_t` (segment text NUL-terminated in `text_buf`); output is a timed
  `vout_OSDText` overlay at `SUBPICTURE_ALIGN_BOTTOM`.
- **Ownership/threading model**: the call happens on the plugin sender thread (the only
  receiver of worker frames), synchronously, so `text_buf` stays alive for the duration of
  the render. `vw_caption_presenter_find_vout` walks the VLC object tree from this thread;
  `vout_OSDText` is thread-safe (posts to the vout thread). Presenter field is written once
  in open (before the sender thread starts) and read by the sender thread; close joins the
  sender before clearing — no data race.
- **Bounds, time units, and failure behavior**: duration from `segment.end_pts_us -
  start_pts_us`, defaulting to 2 s when non-positive. Any presenter failure (vout not found,
  bad text) returns false and is logged; captions may stop, playback never blocks.
- **Privacy/security implications**: none — text goes to the local OSD only.
- **Protocol change**: none.

## Acceptance criteria
- [ ] Sender thread renders a decoded SEGMENT frame to OSD (live VLC run shows captions).
- [ ] Unknown/vout-missing conditions degrade to no-caption, playback unaffected.
- [ ] Module close clears the OSD overlay.
- [ ] Automated tests cover presenter behavior (existing `test_caption_presenter`) and
      SEGMENT decode (existing `test_worker_client`) — both must stay green.
- [ ] Documentation/version metadata updated (roadmap, architecture, test-strategy).

## Test plan
- Automated gate (AGENTS.md rule 10):
  `clang-format --dry-run --Werror plugin/src/vw_whisper_module.c`
  `cmake --preset linux-x64-debug && cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug`
  `ctest --test-dir build/linux-x64-debug -T memcheck`
  Windows cross-build: `cmake --preset windows-x64-debug && cmake --build --preset windows-x64-debug`
- Manual acceptance (required — stub tests cannot prove OSD rendering):
  run a built VLC with the module on a local video; verify captions appear and track
  speech; verify they clear on close. Mark this explicitly as the step's acceptance gate.

## Definition of done
- [ ] C17 code; no project-authored C++ introduced
- [ ] No blocking work in VLC audio callback (presenter runs on sender thread, not callback)
- [ ] No network access, telemetry, transcript/PCM persistence, or sensitive logs introduced
- [ ] Memory, audio queue, frame, text, and retry limits are bounded
- [ ] Error path is safe: captions may stop, playback does not
- [ ] Postmortem lessons honored: no OSD lifetime bug (text copied to owned buffer),
      no session-ID rejection (worker stamps), single-sweep scope (no SPU/seek bundling)
