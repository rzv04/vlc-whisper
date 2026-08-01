# Task: Caption Presenter Spike (OSD Overlay)

## Goal

Independently prove that the single VLC-Whisper plugin DLL (`libvlc_whisper_plugin`) can display timed captions on VLC's video output using a transparent OSD text overlay (`vout_OSDText`). Native SPU subpicture channel deferred to Milestone 3.

## Context

- Relevant docs/ADR: `docs/roadmap.md` (Step 11), `docs/decisions.md` (ADR-005: Final-only captions first, ADR-012: Out-of-tree packaging).
- VLC/worker/protocol version affected: Plugin Presentation (`vw_caption_presenter.c`).
- Assumptions and explicit non-goals: Single plugin DLL using OSD overlay path for Milestone 2.

## Scope

- In scope:
  - OSD Overlay Route: `vout_OSDText` overlay on active `vout_thread_t`.
  - Object tree search algorithm (`vw_caption_presenter_find_vout`).
  - Writing `vw_caption_presenter.c` and `vw_caption_presenter.h`.
- Out of scope: SPU subpicture channel (deferred to Milestone 3), live IPC pipeline (Milestone 3).

## Design

- Inputs and outputs:
  - `vw_caption_presenter_display(void* p_filter, const char* text, int64_t duration_us)`
- Ownership/threading model:
  - Non-blocking execution / safe lookup from VLC object tree.

## Acceptance criteria

- [x] Single DLL contains OSD display logic (`vout_OSDText`).
- [x] SPU subpicture channel stripped for Milestone 2 (OSD overlay sole path).
- [x] Tested against sample video file without VLC crashes.

## Definition of done

- [x] C17 code; no project-authored C++ introduced.
- [x] No blocking work in VLC audio callback.
- [x] Warnings-as-errors and formatting pass.
