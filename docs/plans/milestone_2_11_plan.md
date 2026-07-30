# Task: Caption Presenter Spike (Native Subtitles & OSD Fallback)

## Goal

Independently prove that the single VLC-Whisper plugin DLL (`libvlc_whisper_plugin`) can display timed captions on VLC's video output using a primary native subpicture method with a transparent OSD text overlay fallback (`vout_OSDText`).

## Context

- Relevant docs/ADR: `docs/roadmap.md` (Step 11), `docs/decisions.md` (ADR-005: Final-only captions first, ADR-012: Out-of-tree packaging).
- VLC/worker/protocol version affected: Plugin Presentation (`vw_caption_presenter.c`).
- Assumptions and explicit non-goals: Single plugin DLL containing both Native SPU and OSD Fallback strategies.

## Scope

- In scope:
  - Primary Route: Native SPU channel (`spu_t`).
  - Fallback Route: `vout_OSDText` overlay.
  - Mode enum (`VW_PRESENTER_MODE_AUTO`, `VW_PRESENTER_MODE_SPU`, `VW_PRESENTER_MODE_OSD`).
  - Writing `vw_caption_presenter.c` and `vw_caption_presenter.h`.
- Out of scope: Full speech-to-text live streaming (Milestone 3).

## Design

- Inputs and outputs:
  - `vw_caption_presenter_display(filter_t* p_filter, const char* text, int64_t duration_us, vw_presenter_mode_t mode)`
- Ownership/threading model:
  - Non-blocking execution / safe lookup from VLC object tree.

## Acceptance criteria

- [] Single DLL contains both SPU and OSD display logic.
- [] Automatic fallback to OSD if SPU is unavailable.
- [] Tested against sample video file without VLC crashes.

## Definition of done

- [x] C17 code; no project-authored C++ introduced.
- [x] No blocking work in VLC audio callback.
- [x] Warnings-as-errors and formatting pass.
