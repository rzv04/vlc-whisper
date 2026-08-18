# Diff Analysis: Step 17b Native SPU Presentation & Subpicture Subsystem vs gemini/milestone-3

**15 files changed, +595 / -58 lines**
**Base**: `gemini/milestone-3`

---

## 1. File-by-File Analysis

### 1.1 `docs/api-contracts.md`

**Why change**: Document the critical timing domain distinction between audio-filter block timestamps (system-date scale) and media position timestamps (`INPUT_GET_TIME`) discovered during SPU integration.

**Responsibility before**: Documented IPC protocol framing, message structures, and wire layouts.
**After**: Same, plus explicit timeline contract documentation regarding system-date block PTS vs media position.

**Callers**: Developers and AI agents implementing audio capture and IPC pacing.
**Callees**: None (specification).

**Happy path**: Developer reviews timing contracts before implementing or debugging presenter timestamp conversions.

**Failure path**: Misinterpreting block timestamps as media-relative causes multi-hour timestamp offsets in downstream presentation.

**Boundaries**:
- Documentation invariant: Clock domain rules explicitly defined.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Clock domain distinction documented | `docs/api-contracts.md:L142-148` | N/A (Doc) | Done |

**Assumptions/Tradeoffs**: Documents current VLC 3.0 audio output behavior where `aout_DecPlay` re-bases block timestamps against `mdate()`.

---

### 1.2 `docs/architecture.md`

**Why change**: Update presentation architectural notes to document SPU subpicture scheduling in the OSD clock domain on private channels.

**Responsibility before**: Outlined module interactions, realtime audio callback safety, and worker isolation.
**After**: Same, with updated SPU channel scheduling details.

**Callers**: Architecture documentation consumers.
**Callees**: None.

**Happy path**: Reviewer consults architecture to understand how captions flow from sender thread to SPU compositor without an internal caption queue.

**Failure path**: N/A.

**Boundaries**:
- Realtime callback safety: Audio filter callback remains non-blocking and allocation-free.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | SPU pipeline architecture documented | `docs/architecture.md:L154` | N/A (Doc) | Done |

**Assumptions/Tradeoffs**: Documents ADR-016 (zero internal plugin caption queue).

---

### 1.3 `docs/plans/milestone3_postmortem.md`

**Why change**: Add Addendum documenting the 2026-08-18 Step 17b SPU bugfix trace (Empty SPU ruled out, system-date PTS domain discovered, subtitle-clock selection drop on Windows, OSD clock domain fix).

**Responsibility before**: Postmortem of Milestone 3 branches prior to Step 17b.
**After**: Includes postmortem addendum documenting the root cause of invisible subtitles on Windows.

**Callers**: Engineering team and future AI agent sessions.
**Callees**: None.

**Happy path**: Developer reads addendum to understand why `b_subtitle = false` and `mdate()` are required for reliable caption display on VLC 3.0.23 Windows.

**Failure path**: N/A.

**Boundaries**:
- Historical accuracy: Explicitly noted as post-hoc addendum outside original timeline.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Postmortem addendum added | `docs/plans/milestone3_postmortem.md:L317-378` | N/A (Doc) | Done |

**Assumptions/Tradeoffs**: Preserves original postmortem text unchanged while isolating the new bugfix trace.

---

### 1.4 `docs/plans/step17b_plan.md`

**Why change**: Implementation plan artifact for Step 17b per Rule 9 and `ai/task-template.md`.

**Responsibility before**: Did not exist.
**After**: Full task breakdown, scope, architecture, acceptance criteria, and verification steps for Step 17b.

**Callers**: AI agents and developers implementing and verifying Step 17b.
**Callees**: None.

**Happy path**: Agent loads plan via `@docs/plans/step17b_plan.md` to guide implementation and verification.

**Failure path**: N/A.

**Boundaries**:
- Rule 9 compliance: Follows task template format.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Task plan created and maintained | `docs/plans/step17b_plan.md:L1-157` | N/A (Doc) | Done |

**Assumptions/Tradeoffs**: None.

---

### 1.5 `docs/roadmap.md`

**Why change**: Mark deliverable 17b as complete per Rule 14.

**Responsibility before**: Deliverable 17b was unchecked.
**After**: Deliverable 17b marked `[x]` with summary of shipped features.

**Callers**: Project tracking.
**Callees**: None.

**Happy path**: Roadmap reflects Step 17b completion and sets stage for Step 17c.

**Failure path**: N/A.

**Boundaries**:
- Documentation synchronization.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Roadmap updated | `docs/roadmap.md:L53` | N/A (Doc) | Done |

**Assumptions/Tradeoffs**: None.

---

### 1.6 `docs/source-layout.md`

**Why change**: Update `vw_caption_presenter.c` row description in plugin layout table per Rule 14.

**Responsibility before**: Described presenter as converting final segments into OSD caption cues.
**After**: Describes presenter as VLC SPU subpicture channel rendering with OSD fallback.

**Callers**: Developer onboarding.
**Callees**: None.

**Happy path**: Reviewer verifies source layout matches implementation.

**Failure path**: N/A.

**Boundaries**:
- Documentation synchronization.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Source layout table updated | `docs/source-layout.md:L142` | N/A (Doc) | Done |

**Assumptions/Tradeoffs**: None.

---

### 1.7 `docs/test-strategy.md`

**Why change**: Document unit test contract for Step 17b in `test_caption_presenter.c`.

**Responsibility before**: Documented test coverage through Step 17a.
**After**: Includes Step 17b SPU mocking, fallback, and channel flushing assertions.

**Callers**: Test suite maintainers.
**Callees**: None.

**Happy path**: Test strategy documents exact test cases implemented.

**Failure path**: N/A.

**Boundaries**:
- Test contract documentation.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Test strategy updated | `docs/test-strategy.md:L62` | N/A (Doc) | Done |

**Assumptions/Tradeoffs**: None.

---

### 1.8 `docs/vlc-api-essentials.md`

**Why change**: Document §3.4 audio-filter block PTS re-basing into system-date domain and two SPU clock domains in VLC 3.0.

**Responsibility before**: Documented object hierarchy, thread models, and OSD display.
**After**: Detailed documentation of `aout_DecPlay` timestamp re-basing and SPU clock domain mechanics.

**Callers**: Plugin developers working with VLC 3.0 clock APIs.
**Callees**: None.

**Happy path**: Developer references §3.4 when working on timeline synchronization or look-ahead scheduling.

**Failure path**: N/A.

**Boundaries**:
- API contract documentation.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Clock domain reference added | `docs/vlc-api-essentials.md:L117-124` | N/A (Doc) | Done |

**Assumptions/Tradeoffs**: None.

---

### 1.9 `plugin/include/vw_caption_presenter.h`

**Why change**: Add SPU channel fields (`p_last_vout`, `spu_channel_id`, `spu_channel_registered`) to `vw_caption_presenter_t`, update `vw_caption_presenter_show_segment` signature to accept `input_time_us`, and document all functions per Rule 11.

**Responsibility before**: Simple struct holding only `p_filter_ctx`.
**After**: Manages filter context, cached `vout` pointer for window recreation detection, SPU channel ID, and registration status.

**Callers**: `vw_whisper_module.c`, `test_caption_presenter.c`.
**Callees**: None (header).

**Happy path**: Caller initializes presenter struct with `{0}`, passes to `show_segment`, and presenter caches channel state across calls.

**Failure path**: If SPU registration fails, `spu_channel_id` is set to `-1` and `spu_channel_registered` to `false`.

**Boundaries**:
- Input validation: Channel IDs explicitly guarded (`spu_channel_id >= 0`).
- Concurrency: Presenter operations run exclusively on the plugin background sender thread.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | SPU channel state fields declared | `vw_caption_presenter.h:L11-14` | `test_caption_presenter.c:L157` | Done |
| 2 | Updated show_segment signature with Rule 11 comment | `vw_caption_presenter.h:L21-23` | `test_caption_presenter.c:L147` | Done |

**Assumptions/Tradeoffs**: Presenter is owned by `filter_sys_t` and manipulated only from the sender thread.

---

### 1.10 `plugin/include/vw_platform.h`

**Why change**: Define `VW_WEAK` symbol linkage macro in a shared header to resolve MinGW weak attribute linking differences without duplicating macros across TUs.

**Responsibility before**: Platform OS wrappers (time, threads, processes).
**After**: Same, plus `VW_WEAK` macro definition (`__attribute__((weak))` on Linux, empty on Windows).

**Callers**: `vw_caption_presenter.c`, plugin sources.
**Callees**: None.

**Happy path**: Linux builds emit weak symbol references; Windows MinGW builds use standard `extern` symbol imports backed by `libvlccore.def`.

**Failure path**: Missing `VW_WEAK` on Windows causes weak symbols to evaluate to NULL at runtime.

**Boundaries**:
- Portability: Win32 vs POSIX macro branches.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | `VW_WEAK` macro defined | `vw_platform.h:L13,L16` | Build verification | Done |

**Assumptions/Tradeoffs**: None.

---

### 1.11 `plugin/libvlccore.def`

**Why change**: Export required VLC 3.0 SPU and timing symbols for Windows MinGW dynamic linking.

**Responsibility before**: Exported 14 symbols (OSD, logging, objects, config).
**After**: Exported 21 symbols, adding `vout_RegisterSubpictureChannel`, `vout_PutSubpicture`, `subpicture_New`, `subpicture_Delete`, `subpicture_region_New`, `subpicture_region_Delete`, `text_segment_New`, `text_segment_Delete`, `mdate`.

**Callers**: Windows MinGW linker (`dlltool` generating `libvlccore.a` import library).
**Callees**: `libvlccore.dll` runtime exports.

**Happy path**: Windows cross-compilation resolves all SPU and timing function imports at link time without undefined symbol errors.

**Failure path**: Omitting a symbol causes link failure or runtime DLL import error.

**Boundaries**:
- Linkage: Exact symbol names matching VLC 3.0 `libvlccore.sym`.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | SPU symbols exported | `libvlccore.def:L11-19` | `windows-x64-release-cpu` build | Done |

**Assumptions/Tradeoffs**: Inline header functions (`input_GetVout`, `video_format_Init`) are excluded as they require no `.def` export.

---

### 1.12 `plugin/src/vw_caption_presenter.c`

**Why change**: Implement native VLC SPU subpicture channel rendering, text region construction, vout recreation re-registration, timestamp scheduling in the OSD clock domain, dual-channel flushing, and graceful OSD fallback.

**Responsibility before**: Dispatched caption cues directly to `vout_OSDText` on channel 1.
**After**: Registers private SPU channel via `vout_RegisterSubpictureChannel`, builds structured `subpicture_t` carrying `video_format_Init(&fmt, VLC_CODEC_TEXT)` and `text_segment_New(text)`, sets alignment (`SUBPICTURE_ALIGN_BOTTOM`), tracks `p_last_vout` to handle window recreation, flushes both SPU and OSD channels on blank/clear, and falls back to `vout_OSDText` on registration failure.

**Callers**: `vw_whisper_module.c` (sender thread), unit tests.
**Callees**: VLC Core APIs (`vout_RegisterSubpictureChannel`, `vout_PutSubpicture`, `vout_FlushSubpictureChannel`, `subpicture_New`, `subpicture_Delete`, `subpicture_region_New`, `subpicture_region_Delete`, `text_segment_New`, `text_segment_Delete`, `mdate`, `vout_OSDText`, `vlc_object_hold`, `vlc_object_release`).

**Happy path**: Sender thread invokes `vw_caption_presenter_show_segment`. Presenter finds `vout`, registers private SPU channel 9, constructs `subpicture_t`, populates text region, sets `i_start = mdate()`, `i_stop = mdate() + duration`, calls `vout_PutSubpicture(vout, subpic)`, and logs `PRESENTER_SPU_RENDER`.

**Failure path**: If SPU registration fails (`channel_id < 0`) or subpicture region allocation fails, presenter falls back to `vout_OSDText(vout, 1, ...)` and logs `PRESENTER_OSD_RENDER`.

**Boundaries**:
- Input validation: NULL segment, NULL text, and negative duration checks.
- Memory management: `vout_PutSubpicture` takes subpicture ownership; allocation failures call `subpicture_region_Delete` / `subpicture_Delete` before returning.
- Concurrency: Invoked exclusively from single background sender thread.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Private SPU channel registration | `vw_caption_presenter.c:L172-187` | `test_caption_presenter.c:L168-177` | Done |
| 2 | Subpicture text region construction | `vw_caption_presenter.c:L80-128` | `test_caption_presenter.c:L172-182` | Done |
| 3 | Vout recreation re-registration | `vw_caption_presenter.c:L172` | `test_caption_presenter.c:L217-234` | Done |
| 4 | OSD fallback on channel failure | `vw_caption_presenter.c:L208-213` | `test_caption_presenter.c:L186-196` | Done |
| 5 | Dual-channel flushing | `vw_caption_presenter.c:L226-240` | `test_caption_presenter.c:L198-204` | Done |

**Assumptions/Tradeoffs**: Subpictures use `b_subtitle = false` in the OSD clock domain (`mdate()`) to ensure reliable display across all VLC 3.0 video output backends without requiring an active subtitle track.

---

### 1.13 `plugin/src/vw_whisper_module.c`

**Why change**: Pass media position (`current_position_us`) from held `input_thread_t` into `vw_caption_presenter_show_segment`, initialize presenter SPU fields in `vw_plugin_open`, and add `PLUGIN_SEGMENT` diagnostic logging.

**Responsibility before**: Sender thread called `show_segment(&sys->presenter, &recv.segment)` without timing args.
**After**: Tracks `current_position_us` via throttled (100ms) input poll, passes `current_position_us` to `show_segment`, logs segment arrival, and initializes `spu_channel_id = -1` in open.

**Callers**: VLC Core (filter entry points, audio callback, sender thread).
**Callees**: `vw_caption_presenter.h`, `vw_worker_client.h`, VLC Core APIs (`input_Control`, `input_GetState`).

**Happy path**: Sender thread receives `VW_MSG_CAPTION_SEGMENT`, verifies session ID match, logs `PLUGIN_SEGMENT`, and forwards segment and position to `vw_caption_presenter_show_segment`.

**Failure path**: Stale segments from previous seek epochs (`session_id` mismatch) are dropped with `PLUGIN_STALE_SEGMENT` debug log.

**Boundaries**:
- Realtime callback safety: Audio filter callback remains completely decoupled from SPU rendering and logging.
- Input validation: Session ID byte comparison guards against stale pre-seek segments.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Position tracking and presenter dispatch | `vw_whisper_module.c:L268,L389` | `test_caption_presenter.c:L172` | Done |
| 2 | Presenter SPU initialization in open | `vw_whisper_module.c:L531-533` | Unit tests | Done |
| 3 | Pipeline diagnostic logging | `vw_whisper_module.c:L383-387` | Runtime logs | Done |

**Assumptions/Tradeoffs**: 100ms polling throttle protects against excess object-tree allocations while maintaining sub-second seek/pause reactivity.

---

### 1.14 `tests/unit/test_caption_presenter.c`

**Why change**: Provide mock VLC symbols for SPU and timing pipeline, assert SPU channel registration, text region construction, OSD fallback, dual-channel flushing, and vout recreation re-registration.

**Responsibility before**: Unit tested basic OSD display and clear functions against stub symbols.
**After**: Full test suite covering 10 test cases including SPU channel lifecycle, subpicture creation, timestamp assertions, channel flushing, and vout pointer change re-registration.

**Callers**: CTest runner (`ctest --preset linux-x64-debug`).
**Callees**: `vw_caption_presenter.c`, mock VLC APIs.

**Happy path**: Standalone test executable runs all 10 assertions and exits 0 with 0 memory leaks under Valgrind.

**Failure path**: Assertion failure triggers `abort()` with exact line number.

**Boundaries**:
- Standalone isolation: Tests run without live VLC process or display server.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | SPU channel registration tested | `test_caption_presenter.c:L168-185` | Test 6 | Done |
| 2 | OSD fallback tested | `test_caption_presenter.c:L186-196` | Test 7 | Done |
| 3 | Dual-channel flushing tested | `test_caption_presenter.c:L198-204` | Test 8 | Done |
| 4 | Presenter clear tested | `test_caption_presenter.c:L206-215` | Test 9 | Done |
| 5 | Vout recreation re-registration tested | `test_caption_presenter.c:L217-234` | Test 10 | Done |

**Assumptions/Tradeoffs**: Uses lightweight heap-allocated mock structures that emulate VLC SPU lifecycle.

---

### 1.15 `worker/src/vw_worker.c`

**Why change**: Add diagnostic log `WORKER_SEGMENT` when completed caption segments are popped from the segment builder and emitted across IPC.

**Responsibility before**: Transcribed audio and sent `VW_MSG_CAPTION_SEGMENT` frames without an emission log.
**After**: Logs `WORKER_SEGMENT` with segment ID, start/end PTS, finality flag, and text snippet upon IPC dispatch.

**Callers**: Worker main loop.
**Callees**: `vw_protocol_codec.h`, `vw_ipc.h`, `vw_log.h`.

**Happy path**: Inference completes, segment builder pops segment, worker logs `WORKER_SEGMENT` and sends frame across IPC transport.

**Failure path**: IPC write failure breaks worker event loop and triggers clean shutdown.

**Boundaries**:
- Privacy invariant: Logging does not persist raw PCM or transcripts to disk by default.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Worker segment emission log added | `vw_worker.c:L436-439` | `test_worker_lifecycle` | Done |

**Assumptions/Tradeoffs**: None.

---

## 2. Happy-Path Request Trace

End-to-end trace of a transcribed caption segment from worker emission to VLC SPU rendering:

1. **Worker Emission** ([`worker/src/vw_worker.c:L420-440`](file:///home/razvan/vlc-whisper/.worktrees/gemini/worker/src/vw_worker.c#L420-L440)):
   - `vw_segment_builder_pop` extracts completed `vw_caption_segment_t`.
   - `vw_protocol_encode_payload` encodes `VW_MSG_CAPTION_SEGMENT`.
   - `vw_log_event(VW_LOG_LEVEL_INFO, "WORKER_SEGMENT", ...)` logs segment dispatch.
   - `vw_ipc_send` transmits header and payload over authenticated local socket/pipe.

2. **Plugin Reception** ([`plugin/src/vw_whisper_module.c:L371-390`](file:///home/razvan/vlc-whisper/.worktrees/gemini/plugin/src/vw_whisper_module.c#L371-L390)):
   - Background sender thread receives `VW_MSG_CAPTION_SEGMENT` via `vw_worker_client_receive_frame`.
   - Verifies `recv.segment.session_id` matches active session epoch.
   - Logs `[PLUGIN_SEGMENT]` with segment metadata.
   - Dispatches to `vw_caption_presenter_show_segment(&sys->presenter, &recv.segment, current_position_us)`.

3. **Presenter SPU Processing** ([`plugin/src/vw_caption_presenter.c:L150-220`](file:///home/razvan/vlc-whisper/.worktrees/gemini/plugin/src/vw_caption_presenter.c#L150-L220)):
   - `vw_caption_presenter_find_vout` walks object hierarchy and retrieves held `vout_thread_t*`.
   - Checks `!presenter->spu_channel_registered || presenter->p_last_vout != vout`.
   - Calls `vout_RegisterSubpictureChannel(vout)` returning channel ID (e.g. `9`).
   - Computes `start_tick = mdate()` and `stop_tick = start_tick + duration_us`.
   - Calls `vw_caption_presenter_render_spu`:
     - Allocates `subpic = subpicture_New(NULL)`.
     - Initializes `video_format_Init(&fmt, VLC_CODEC_TEXT)` with `i_sar_num = 1; i_sar_den = 1;`.
     - Allocates `region = subpicture_region_New(&fmt)`.
     - Populates `region->p_text = text_segment_New(text)`.
     - Sets `region->i_align = SUBPICTURE_ALIGN_BOTTOM`, `region->i_text_align = SUBPICTURE_ALIGN_BOTTOM`.
     - Sets `subpic->b_subtitle = false`, `subpic->b_ephemer = true`, `subpic->b_fade = true`.
     - Calls `vout_PutSubpicture(vout, subpic)` (transferring full ownership to VLC compositor).
   - Logs `[PRESENTER_SPU_RENDER]` and releases held `vout` reference via `vlc_object_release`.

---

## 3. Most Important Failure Path

Failure trace: **Video Output Recreation During Playback (Window Resize)**

1. VLC video display module (`direct3d11` / X11) resizes window, destroying old `vout_thread_t` and creating a new instance at a new memory address.
2. Next caption segment arrives at sender thread and enters `vw_caption_presenter_show_segment`.
3. `vw_caption_presenter_find_vout` discovers new `vout` pointer (`vout != presenter->p_last_vout`).
4. Presenter detects pointer mismatch at line 172.
5. Invokes `vout_RegisterSubpictureChannel(new_vout)` to allocate a valid channel ID on the new `vout`.
6. Updates `presenter->spu_channel_id = new_channel_id`, `presenter->p_last_vout = new_vout`.
7. `vout_PutSubpicture` successfully queues the subpicture to the active compositor heap without silent drops.

---

## 4. Boundary Summary

| Boundary Type | Implementation & Defense Mechanism | Verification / Test |
|---|---|---|
| **Input Validation** | NULL checks on presenter, segment, and UTF-8 text pointers. Zero/negative duration defaults to 2,000,000 µs (2s). Past-timestamp clamp guards `stop_tick <= now_tick`. | `test_caption_presenter.c:L130-154` |
| **Authorization** | Session ID validation on sender thread (`memcmp` against `client->session_id`) discards stale pre-seek segments. | `test_worker_lifecycle` |
| **Concurrency** | Presenter state is owned exclusively by the background sender thread. No shared locks with realtime audio callback. `vout` references are held and released cleanly. | Valgrind memcheck, TSAN |
| **I/O & SPU** | Graceful fallback to `vout_OSDText` on channel registration failure (`< 0`) or subpicture allocation error. Dual-channel flush on seek. | `test_caption_presenter.c:L186-204` |
| **Persistence / Memory** | SPU ownership transferred to VLC core on `vout_PutSubpicture`. On any allocation failure before put, `subpicture_region_Delete` and `subpicture_Delete` clean up allocations. | Valgrind 0 leaks |

---

## 5. Acceptance Criterion → Code Mapping

| # | Acceptance Criterion | Source Location | Test Assertion | Status |
|---|---|---|---|---|
| 1 | Register private SPU channel with vout | `vw_caption_presenter.c:L173` | `test_caption_presenter.c:L172` | Done |
| 2 | Construct subpicture with `VLC_CODEC_TEXT` and `text_segment_New` | `vw_caption_presenter.c:L91-105` | `test_caption_presenter.c:L174` | Done |
| 3 | Set bottom-center alignment (`SUBPICTURE_ALIGN_BOTTOM`) | `vw_caption_presenter.c:L108-109` | `test_caption_presenter.c:L175` | Done |
| 4 | Schedule in OSD clock domain (`mdate()`) with duration bounds | `vw_caption_presenter.c:L188-204` | `test_caption_presenter.c:L176-178` | Done |
| 5 | Re-register SPU channel on vout pointer change (`p_last_vout`) | `vw_caption_presenter.c:L172` | `test_caption_presenter.c:L217-234` | Done |
| 6 | Graceful fallback to `vout_OSDText` on channel failure | `vw_caption_presenter.c:L208-213` | `test_caption_presenter.c:L186-196` | Done |
| 7 | Dual-channel flushing on blank and teardown | `vw_caption_presenter.c:L226-240` | `test_caption_presenter.c:L198-204` | Done |
| 8 | Define shared `VW_WEAK` macro in `vw_platform.h` | `vw_platform.h:L13,L16` | Cross-build linkage | Done |
| 9 | Export SPU & timing symbols in `libvlccore.def` | `libvlccore.def:L11-19` | Windows MinGW build | Done |
| 10 | Update all documentation (Roadmap, Layout, Strategy, Contracts, Essentials) | `docs/*.md` | Doc inspection | Done |

---

## 7. Code Review Findings (Bugs, Risks, Nitpicks)

### Bugs (Sorted by Priority)

| Priority | Component / Location | Description | Impact | Proposed Fix |
|---|---|---|---|---|
| **Low** | `plugin/src/vw_caption_presenter.c:L172` | Heap address reuse edge case: if a destroyed `vout_thread_t` is immediately reallocated at the exact same memory address, `presenter->p_last_vout != (void*)vout` would evaluate to false. | In rare rapid allocation recycling, SPU channel re-registration might be delayed until the next distinct pointer. | Presenter could verify channel validity or track a generation counter if VLC core exposes one. Currently low impact as allocator address reuse across window recreations is rare. |

### Architectural & Operational Risks

| Category | Risk Description | Affected Files | Mitigation Strategy |
|---|---|---|---|
| **Roadmap Boundary** | Step 17b schedules subpictures at `mdate()` (OSD clock domain) for reactive live streaming. Step 17c (Look-Ahead Source Decoding) will produce future segments that cannot be timestamped at `mdate()`. | `vw_caption_presenter.c`, `docs/plans/step17b_plan.md` | Step 17c will implement either plugin-side pacing or subtitle-clock channel probing as documented in `milestone3_postmortem.md` addendum. |
| **VLC SPU Warning** | VLC logs `main warning: original picture size is undefined` once per caption because `subpic->i_original_picture_width/height` are 0. | `vw_caption_presenter.c` | VLC core automatically substitutes source video dimensions (`fmt_src->i_visible_width/height`); text rendering scales properly. |

### Code Style & Quality Nitpicks

| Issue Type | File & Line | Description | Recommendation |
|---|---|---|---|
| **Compiler Warning** | `tests/unit/test_caption_presenter.c:L140-155` | Unused variables when assertions are compiled out in Release mode (`NDEBUG`). | Added `(void)` casts after assertions to maintain zero-warning builds across both Debug and Release configurations. |
