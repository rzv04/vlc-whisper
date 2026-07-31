# Task: Milestone 2 Review Issues 1-5 Fix Plan

## Goal
Fix 5 verified technical and compliance issues identified in code review:
1. **Rule 4 Violation**: Decouple OSD caption presenter calls from VLC's real-time audio callback (`vw_plugin_filter`), keeping the callback 100% lock-free.
2. **Deprecated Symbol**: Remove deprecated `vlc_object_find_name` call from `vw_caption_presenter_find_vout()` in favor of direct parent-walk and child list traversal.
3. **OSD Clear Behavior**: Update `vw_caption_presenter_clear()` to explicitly dismiss active OSD overlays on screen by sending an empty text payload with 0 duration to `vout_OSDText`.
4. **Resampler Timing Drift**: Fix integer truncation in `vw_audio_capture_process_block` by adding a fractional sample remainder accumulator to eliminate sub-frame PTS timing drift over long sessions.
5. **Rule 11 Compliance**: Expand doc comments in `vw_caption_presenter.h` and `vw_queue.h` to meet the mandatory 20–30 word requirement per function.

## Context
- Relevant docs/ADR: `AGENTS.md` (Rule 4, Rule 11), `docs/vlc-api-essentials.md`, `docs/vlc-object-tree-and-vout.md`, `docs/architecture.md`.
- VLC/worker/protocol version affected: Plugin Core (`vlc_whisper_module.c`, `vw_caption_presenter.c`, `vw_audio_capture.c`, `vw_queue.h`, `vw_caption_presenter.h`).
- Assumptions and explicit non-goals: Real-time audio callback must perform zero lock acquisitions or object tree traversals.

## Scope
- In scope:
  - Removing presenter invocation from `vw_plugin_filter()`.
  - Refactoring `vw_caption_presenter_find_vout()` to eliminate deprecated `vlc_object_find_name()`.
  - Updating `vw_caption_presenter_clear()` to dismiss visible OSD text via `vout_OSDText(vout, 1, SUBPICTURE_ALIGN_BOTTOM, 0, "")`.
  - Adding `uint32_t sample_remainder` to `vw_audio_capture_t` for exact fractional sample accumulation.
  - Expanding header doc comments in `vw_caption_presenter.h` and `vw_queue.h`.
  - Updating unit tests (`test_audio_capture.c`, `test_caption_presenter.c`).
- Out of scope: Milestone 3 IPC worker connection.

## Design
- Inputs and outputs:
  - `vw_audio_capture_process_block`: accumulates `sample_remainder` to compute `output_frames`.
  - `vw_caption_presenter_clear`: calls `vout_OSDText(vout, 1, SUBPICTURE_ALIGN_BOTTOM, 0, "")`.
- Ownership/threading model:
  - `vw_plugin_filter()` audio callback remains 100% lock-free.
  - Caption rendering executed on presenter / receiver thread only.

## Acceptance criteria
- [ ] Zero VLC lock acquisitions or object tree traversals inside `vw_plugin_filter()`.
- [ ] Deprecated `vlc_object_find_name` call removed from `vw_caption_presenter_find_vout()`.
- [ ] `vw_caption_presenter_clear()` dispatches empty text OSD clear message to active `vout`.
- [ ] Fractional sample remainder accumulator prevents sub-frame PTS drift in resampler.
- [ ] All header function comments in `vw_caption_presenter.h` and `vw_queue.h` contain 20–30 words per Rule 11.

## Test plan
- Exact commands:
  `clang-format --dry-run --Werror plugin/include/vw_caption_presenter.h plugin/src/vw_caption_presenter.c plugin/src/vlc_whisper_module.c plugin/include/vw_audio_capture.h plugin/src/vw_audio_capture.c plugin/include/vw_queue.h tests/unit/test_audio_capture.c tests/unit/test_caption_presenter.c`
  `cmake --preset linux-x64-debug && cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug --output-on-failure`
  `ctest --test-dir build/linux-x64-debug -T memcheck`
  `cmake --build --preset windows-x64-release`

## Definition of done
- [ ] C17 code; no project-authored C++ introduced
- [ ] No blocking work in VLC audio callback
- [ ] No network access or privacy violations
- [ ] Unit/contract/integration tests pass cleanly
- [ ] Formatting and static checks pass cleanly

## Evidence
- Build/test outputs to be updated upon completion.
