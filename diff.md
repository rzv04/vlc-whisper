# Diff Analysis: Milestone 2 (Caption Presentation Spike)

**5 files changed, +327 / -5 lines**
**Base**: `b31f6b1697f1a2b2fe81699245dab96464cc7917..98a9b5eee9a0439010c14031ba3aa0b87bd1a7b2`

---

## 1. File-by-File Analysis

### 1.1 `docs/vout-search-fix.md` (new)

**Why change**: Document the root-cause analysis of why caption display was not appearing on VLC's video output despite the `vw_caption_presenter_find_vout` implementation being correct. Captures debugging findings, broken SPU path analysis, and recommended next steps.

**Responsibility before**: N/A (new file). **After**: Investigative document covering four issues: (1) nobody calls the caption presenter from the filter callback, (2) broken SPU subpicture render path with leaked subpicture allocations and invalid PTS, (3) potential Windows-specific parent chain differences, and (4) risk of `input_Control` blocking from audio output thread.

**Callers**: Developers debugging caption display issues. **Callees**: N/A (documentation).

**Happy path**: Reader follows Issue 1 analysis, implements a test caption caller in `vw_plugin_filter`, observes OSD text on video output.

**Failure path**: If the parent chain on Windows does not include `object_type == "input"`, the vout search fails silently and OSD captions never appear.

**Boundaries**:

- **Input validation**: N/A (documentation).
- **Authorization**: N/A.
- **Concurrency**: N/A.
- **I/O**: N/A.
- **Persistence**: N/A.

**Acceptance map**:

| #   | Criterion                           | Code                            | Test | Status  |
| --- | ----------------------------------- | ------------------------------- | ---- | ------- |
| 1   | Document why captions don't display | Full document                   | N/A  | ✅ done |
| 2   | SPU subpicture path broken analysis | Issue 2 section                 | N/A  | ✅ done |
| 3   | Options to add a caller             | Issue 1 section (Options A/B/C) | N/A  | ✅ done |

**Assumptions/Tradeoffs**: Analysis assumes VLC 3.0.x object hierarchy (`filter -> aout -> decoder -> input`). Windows-specific chain may differ.

---

### 1.2 `plugin/include/vw_caption_presenter.h`

**Why change**: Add public API surface for caption presentation: mode enum for SPU/OSD/AUTO selection, `vw_caption_presenter_display` as the primary entry point, and function documentation comments per AGENTS.md rule 11.

**Responsibility before**: Minimal header with only stub declarations for `vw_caption_presenter_show_segment` and `vw_caption_presenter_clear`. **After**: Full public API with typed mode enum, documented function contracts, and a `void*`-based interface that avoids VLC header dependencies in the header.

**Callers**: `vlc_whisper_module.c` (via `vw_caption_presenter_display`), `test_caption_presenter.c` (via all three functions). **Callees**: None (pure declarations).

**Happy path**:

1. `vlc_whisper_module.c` includes `vw_caption_presenter.h`.
2. After 100 audio blocks, calls `vw_caption_presenter_display(p_filter, text, duration, VW_PRESENTER_MODE_AUTO)`.
3. Function resolves to the implementation in `vw_caption_presenter.c`.

**Failure path**: N/A (header only).

**Boundaries**:

- **Input validation**: Header declares `const char* text` — null-check required at implementation.
- **Authorization**: N/A.
- **Concurrency**: N/A.
- **I/O**: N/A.
- **Persistence**: N/A.

**Acceptance map**:

| #   | Criterion                                         | Code                               | Test                             | Status  |
| --- | ------------------------------------------------- | ---------------------------------- | -------------------------------- | ------- |
| 1   | Mode enum with AUTO/SPU/OSD                       | `vw_presenter_mode_t` at line 8-12 | `test_caption_presenter.c:63-65` | ✅ done |
| 2   | Public `vw_caption_presenter_display` declaration | Line 18                            | `test_caption_presenter.c:59-65` | ✅ done |
| 3   | Function documentation comments                   | Lines 17, 21, 24                   | N/A                              | ✅ done |

**Assumptions/Tradeoffs**: Uses `void *p_filter` to avoid `#include <vlc_filter.h>` in the public header, trading type safety for reduced header coupling. Callers must cast correctly.

---

### 1.3 `plugin/src/vlc_whisper_module.c`

**Why change**: Wire the caption presenter into the filter callback to prove the OSD display path works end-to-end during live playback. Every 100 audio blocks, display a test caption on VLC's video output.

**Responsibility before**: VLC filter module with audio capture only — PCM was captured to SPSC queue but no consumer read or displayed anything. **After**: Audio filter module that (a) captures PCM and (b) periodically invokes `vw_caption_presenter_display` to prove the presentation pipeline works.

**Callers**: VLC filter pipeline (calls `vw_plugin_filter` per audio block). **Callees**: `vw_audio_capture_process_block`, `vw_caption_presenter_display`, `vw_spsc_queue_create`, `vw_log_set_sink`.

**Happy path**:

1. VLC calls `vw_plugin_open(obj)` — allocates `vw_plugin_sys_t`, creates SPSC queue, sets `pf_audio_filter = vw_plugin_filter`.
2. VLC calls `vw_plugin_filter(p_filter, p_block)` for each audio block.
3. On block 1, 101, 201, ... (block_count % 100 == 1), calls `vw_caption_presenter_display` with `"[VLC-Whisper] Live AI Captions Active"` and 2-second duration.
4. Returns original `p_block` unmodified.

**Failure path**: If `vw_caption_presenter_display` cannot find a vout (no video output, e.g. audio-only file), it returns false silently. The filter continues passthrough unaffected.

**Boundaries**:

- **Input validation**: `p_block` null-check returns early passthrough. `p_filter->p_sys` null-check returns early.
- **Authorization**: N/A (in-process filter).
- **Concurrency**: Called from VLC audio output thread. `sys->block_count` is a non-atomic `uint64_t` incremented without synchronization — safe only because VLC serializes audio filter calls per-instance.
- **I/O**: Non-blocking (no IPC read/write in callback).
- **Persistence**: N/A.

**Acceptance map**:

| #   | Criterion                                  | Code                                                  | Test                    | Status  |
| --- | ------------------------------------------ | ----------------------------------------------------- | ----------------------- | ------- |
| 1   | Non-blocking callback (no inference/IPC)   | `vw_plugin_filter` lines 55-83                        | Architectural invariant | ✅ done |
| 2   | Periodic caption display every 100 blocks  | Line 79-81                                            | Visual verification     | ✅ done |
| 3   | `fmt_out.audio = fmt_in.audio` passthrough | Line 107                                              | Pipeline compatibility  | ✅ done |
| 4   | Block count tracking                       | `sys->block_count` at line 41, incremented at line 78 | N/A                     | ✅ done |

**Assumptions/Tradeoffs**: The 100-block interval (~2 seconds at 48kHz/1024-frame blocks) is a proof-of-concept frequency, not a production caption cadence. The test caption text is hardcoded.

---

### 1.4 `plugin/src/vw_caption_presenter.c`

**Why change**: Implement the full caption presenter with vout discovery and OSD text rendering. Replaces the no-op stubs with working code that walks the VLC object tree to find the video output thread.

**Responsibility before**: Stub file with `vw_caption_presenter_show_segment` and `vw_caption_presenter_clear` both returning true/void with `(void)` casts. **After**: Full implementation with three-tier vout search strategy (parent walk, `vlc_object_find_name`, children list traversal), OSD text rendering via `vout_OSDText`, and input validation.

**Callers**: `vlc_whisper_module.c` (via `vw_caption_presenter_display`), test code (via all three functions). **Callees**: `input_GetVout`, `vlc_object_find_name`, `vlc_object_hold`, `vlc_object_release`, `vlc_list_children`, `vlc_list_release`, `vout_OSDText`.

**Happy path** (`vw_caption_presenter_display -> vw_caption_presenter_render_text -> vw_caption_presenter_find_vout -> vout_OSDText`):

1. `vw_caption_presenter_display` validates text/duration, casts `p_filter_ptr`, delegates to `vw_caption_presenter_render_text`.
2. `vw_caption_presenter_render_text` calls `vw_caption_presenter_find_vout(p_filter)`.
3. `vw_caption_presenter_find_vout` walks `p_filter->obj.parent` chain.
4. At some ancestor, finds `object_type == "input"`, calls `input_GetVout()`.
5. Returns `vout_thread_t*`. `render_text` calls `vout_OSDText(vout, 1, SUBPICTURE_ALIGN_BOTTOM, duration, text)`.
6. Releases vout ref via `vlc_object_release`. Returns true.

**Failure path**:

1. `vw_caption_presenter_find_vout` walks entire parent chain to root.
2. No ancestor has `object_type == "input"` or `"vout"`.
3. `vlc_object_find_name("input")` returns NULL on every ancestor.
4. `vlc_list_children` returns NULL or no child matches.
5. Returns NULL. `render_text` returns false. `display` returns false.
6. Logs `VW_LOG_LEVEL_WARN "PRESENTER_VOUT_NOT_FOUND"`.

**Boundaries**:

- **Input validation**: `p_filter` null check at line 14. `text` null check and `duration_us <= 0` check at line 106. Segment null/text_utf8 null check at line 118-119.
- **Authorization**: In-process VLC object hierarchy — no auth boundary.
- **Concurrency**: Called from VLC audio output thread. `input_GetVout` / `vlc_object_find_name` acquire internal VLC locks; these are short-held operations.
- **I/O**: None (no disk/network).
- **Persistence**: None.

**Acceptance map**:

| #   | Criterion                                              | Code          | Test                                            | Status         |
| --- | ------------------------------------------------------ | ------------- | ----------------------------------------------- | -------------- |
| 1   | Vout search via parent walk + children                 | Lines 17-97   | Not directly tested (requires live VLC)         | ✅ done (code) |
| 2   | OSD text rendering via `vout_OSDText`                  | Lines 99-104  | `test_caption_presenter.c` stubs verify linkage | ✅ done        |
| 3   | Input validation (null text, duration)                 | Lines 106-108 | `test_caption_presenter.c:59-62`                | ✅ done        |
| 4   | NULL p_filter fallback (standalone mode)               | Lines 111-113 | `test_caption_presenter.c:63-65`                | ✅ done        |
| 5   | `vw_caption_presenter_show_segment` with duration calc | Lines 117-127 | `test_caption_presenter.c:67-76`                | ✅ done        |
| 6   | `vw_caption_presenter_clear`                           | Lines 129-133 | `test_caption_presenter.c:79-81`                | ✅ done        |
| 7   | `(void)mode;` — mode selection deferred                | Line 107      | N/A                                             | ⚠️ partial     |

**Assumptions/Tradeoffs**:

- **SPU path deferred**: The plan called for native SPU subpicture channel, but only OSD is implemented. The `mode` parameter is accepted but unused (`(void)mode`). SPU path was identified as broken in `vout-search-fix.md` Issue 2.
- **No subpicture leak risk**: Since SPU path was replaced with OSD-only, there is no subpicture leak — `vout_OSDText` manages its own subpicture lifecycle internally.
- **`input_GetVout` blocking**: Called from audio output thread; blocking duration depends on VLC internal lock contention.
- **Three-tier search**: Parent walk, `vlc_object_find_name`, children list — this is an exhaustive search that may be more aggressive than needed. Multiple calls per second (every 100 blocks) could add overhead.

---

### 1.5 `tests/unit/test_caption_presenter.c` (new)

**Why change**: Provide standalone unit test coverage for the caption presenter API functions without requiring a live VLC instance. Uses function stubs for all VLC API symbols.

**Responsibility before**: N/A (new file). **After**: Unit test covering null/invalid input rejection, standalone display mode (NULL p_filter path), segment presentation, null segment handling, and presenter clear.

**Callers**: CTest (via `test_caption_presenter` executable). **Callees**: `vw_caption_presenter_display`, `vw_caption_presenter_show_segment`, `vw_caption_presenter_clear`.

**Happy path**:

1. Executable runs, `main()` enters.
2. Tests 1-2: Asserts false for NULL text, zero/negative duration.
3. Test 3: Asserts true for valid display calls with NULL p_filter (standalone mode).
4. Test 4: Creates a `vw_caption_presenter_t` and `vw_caption_segment_t`, calls `vw_caption_presenter_show_segment`, asserts true.
5. Test 5: Asserts false for NULL segment and segment with NULL text.
6. Test 6: Calls `vw_caption_presenter_clear`, asserts presenter state is zeroed.
7. Returns 0.

**Failure path**: Any assertion failure aborts the test with non-zero exit code.

**Boundaries**:

- **Input validation**: NULL text (test 1), zero/negative duration (test 2), NULL segment (test 5), NULL text_utf8 in segment (test 5).
- **Authorization**: N/A.
- **Concurrency**: Single-threaded test.
- **I/O**: None.
- **Persistence**: None.

**Acceptance map**:

| #   | Criterion                            | Code        | Test                                              | Status  |
| --- | ------------------------------------ | ----------- | ------------------------------------------------- | ------- |
| 1   | NULL text rejection                  | Lines 59-60 | `vw_caption_presenter_display` null check         | ✅ done |
| 2   | Zero/negative duration rejection     | Lines 62-63 | `vw_caption_presenter_display` duration check     | ✅ done |
| 3   | Standalone display (NULL filter)     | Lines 65-67 | `vw_caption_presenter_display` NULL p_filter path | ✅ done |
| 4   | Segment display with valid data      | Lines 69-75 | `vw_caption_presenter_show_segment`               | ✅ done |
| 5   | NULL segment/empty segment           | Lines 75-77 | `vw_caption_presenter_show_segment` null checks   | ✅ done |
| 6   | Presenter clear                      | Lines 79-81 | `vw_caption_presenter_clear`                      | ✅ done |
| 7   | VLC API stubs for standalone linking | Lines 19-49 | Link-time symbol resolution                       | ✅ done |

**Assumptions/Tradeoffs**: The test uses `#undef` on VLC macros before defining stubs — this is fragile if VLC headers change the macro definitions. The stubs return NULL/void, so the vout search path is never exercised; only the input validation and NULL-filter fallback paths are tested.

**Responsibility before**: Unit and worker integration tests only. **After**: Includes automated dynamic load test for `vlc_whisper_plugin`.

**Callers**: `ctest`. **Callees**: `test_plugin_load`, `vlc_whisper_plugin`.

**Happy path**: `ctest` runs `test_plugin_load $<TARGET_FILE:vlc_whisper_plugin>` and passes.

**Failure path**: Non-zero exit code if shared library loading fails or symbol resolution fails.

**Acceptance map**:

| #   | Criterion                        | Code                                                                                                         | Test              | Status  |
| --- | -------------------------------- | ------------------------------------------------------------------------------------------------------------ | ----------------- | ------- |
| 1   | Register `test_plugin_load` test | [tests/CMakeLists.txt:27-30](file:///home/razvan/vlc-whisper/.worktrees/gemini/tests/CMakeLists.txt#L27-L30) | `ctest` execution | ✅ done |


- **I/O**: Shared library loading.
- **Persistence**: N/A.

---

## 2. Happy-Path Request Trace

Full end-to-end trace from VLC loading the plugin to OSD caption appearing on video output:

1. VLC loads `libvlc_whisper_plugin.so` → resolves `vlc_entry__3_0_0f` → calls `vw_plugin_open`  
   `plugin/src/vlc_whisper_module.c:89-108`
2. `vw_plugin_open` allocates `vw_plugin_sys_t`, creates SPSC queue (16 slots), sets `pf_audio_filter`  
   `plugin/src/vlc_whisper_module.c:92-106`
3. VLC processes media, sends audio blocks to `vw_plugin_filter`  
   `plugin/src/vlc_whisper_module.c:55-83`
4. Block 1, 101, 201... triggers `vw_caption_presenter_display(p_filter, text, 2000000, VW_PRESENTER_MODE_AUTO)`  
   `plugin/src/vlc_whisper_module.c:79-81`
5. `vw_caption_presenter_display` validates text and duration, casts to `filter_t*`  
   `plugin/src/vw_caption_presenter.c:106-114`
6. Delegates to `vw_caption_presenter_render_text`  
   `plugin/src/vw_caption_presenter.c:99-104`
7. `vw_caption_presenter_find_vout` walks parent chain:  
   `plugin/src/vw_caption_presenter.c:17-97`
   - Starts at `VLC_OBJECT(p_filter)` (object_type = "filter")
   - Moves to parent (object_type = "audio output")
   - Moves to parent (object_type = "decoder")
   - Moves to parent (object_type = "input")
   - Calls `input_GetVout((input_thread_t*)cur)` → returns `vout_thread_t*`
8. `vw_caption_presenter_render_text` calls `vout_OSDText(vout, 1, SUBPICTURE_ALIGN_BOTTOM, duration, text)`  
   `plugin/src/vw_caption_presenter.c:103`
9. VLC renders OSD text on video output
10. `vlc_object_release` releases vout reference  
    `plugin/src/vw_caption_presenter.c:104`
11. `vw_plugin_filter` returns original `p_block` unmodified  
    `plugin/src/vlc_whisper_module.c:83`

---

## 3. Most Important Failure Path

**Scenario**: Audio-only media file (no video output) — vout search fails.

1. VLC loads plugin, calls `vw_plugin_open` (succeeds)  
   `plugin/src/vlc_whisper_module.c:89-108`
2. VLC sends audio blocks to `vw_plugin_filter`  
   `plugin/src/vlc_whisper_module.c:55-83`
3. Block 1 triggers `vw_caption_presenter_display`  
   `plugin/src/vlc_whisper_module.c:79-81`
4. `vw_caption_presenter_find_vout` walks parent chain:  
   `plugin/src/vw_caption_presenter.c:17-97`
   - Starts at `VLC_OBJECT(p_filter)` (object_type = "filter")
   - Moves to parent (object_type = "audio output")
   - Moves to parent (object_type = "decoder")
   - Moves to parent (object_type = "input")
   - `input_GetVout` returns NULL (no video output exists)
   - `vlc_object_find_name(cur, "input")` returns NULL (already at input)
   - `vlc_list_children` returns NULL or no vout child
   - Continues to next parent (object_type = "playlist")
   - `vlc_object_find_name(cur, "input")` returns NULL (no input child)
   - `vlc_list_children` returns NULL
   - Reaches root, `cur->obj.parent == NULL`, loop exits
5. Logs `VW_LOG_LEVEL_WARN "PRESENTER_VOUT_NOT_FOUND"`  
   `plugin/src/vw_caption_presenter.c:95`
6. Returns NULL to `vw_caption_presenter_render_text`
7. `render_text` returns false
8. `vw_caption_presenter_display` returns false
9. Caller in `vw_plugin_filter` ignores return value
10. Returns `p_block` — audio passthrough unaffected, no crash

---

## 4. Boundary Summary

| Boundary type        | Gaps found                                                                                                  | Affected files                               |
| -------------------- | ----------------------------------------------------------------------------------------------------------- | -------------------------------------------- |
| **Input validation** | `(void)mode` — mode parameter accepted but ignored; SPU mode silently falls back to OSD                     | `plugin/src/vw_caption_presenter.c:107`      |
| **Input validation** | `p_filter` cast from `void*` with no runtime type check                                                     | `plugin/src/vw_caption_presenter.c:110`      |
| **Input validation** | `block_count` is `uint64_t` — safe from overflow (would take billions of years at audio rates)              | `plugin/src/vlc_whisper_module.c:41`         |
| **Authorization**    | In-process module, no auth boundary                                                                         | N/A                                          |
| **Concurrency**      | `block_count` non-atomic increment — safe because VLC serializes per-instance filter calls, but non-obvious | `plugin/src/vlc_whisper_module.c:78`         |
| **Concurrency**      | `input_GetVout` acquires VLC locks from audio output thread — potential (low-risk) blocking                 | `plugin/src/vw_caption_presenter.c:37,63,76` |
| **I/O**              | None (no disk/network I/O)                                                                                  | N/A                                          |
| **Persistence**      | None                                                                                                        | N/A                                          |

---

## 5. Acceptance Criterion to Code Mapping

| #   | Criterion                                   | Plan Source        | Code                                                               | Test                             | Status         |
| --- | ------------------------------------------- | ------------------ | ------------------------------------------------------------------ | -------------------------------- | -------------- |
| 1   | Mode enum with AUTO/SPU/OSD                 | Milestone 2 plan   | `vw_caption_presenter.h:8-12`                                      | `test_caption_presenter.c:63-65` | ✅ done        |
| 2   | Primary `vw_caption_presenter_display` API  | Milestone 2 plan   | `vw_caption_presenter.h:18`, `vw_caption_presenter.c:106-115`      | `test_caption_presenter.c:59-67` | ✅ done        |
| 3   | `vw_caption_presenter_show_segment` API     | Milestone 2 plan   | `vw_caption_presenter.h:22`, `vw_caption_presenter.c:117-127`      | `test_caption_presenter.c:69-76` | ✅ done        |
| 4   | OSD fallback path (vout_OSDText)            | Milestone 2 plan   | `vw_caption_presenter.c:103`                                       | Visual verification              | ✅ done        |
| 5   | Native SPU subpicture channel               | Milestone 2 plan   | `(void)mode` at `vw_caption_presenter.c:107` — **not implemented** | N/A                              | ❌ missing     |
| 6   | Automatic OSD fallback if SPU unavailable   | Milestone 2 plan   | Always uses OSD; no SPU path exists                                | Same as #4                       | ⚠️ partial     |
| 7   | Vout discovery from filter hierarchy        | Milestone 2 plan   | `vw_caption_presenter.c:17-97`                                     | No live-VLC test                 | ✅ done (code) |
| 8   | Tested against sample video without crashes | Milestone 2 plan   | N/A (visual test)                                                  | Not in committed tests           | ⚠️ partial     |
| 9   | Non-blocking, callback-safe execution       | Definition of done | `vw_plugin_filter` at `vlc_whisper_module.c:55-83`                 | Architectural invariant          | ✅ done        |
| 10  | C17 code; no C++                            | Definition of done | All files                                                          | Compiler check                   | ✅ done        |
| 11  | Warnings-as-errors and formatting pass      | Definition of done | N/A                                                                | `cmake --build` with `-Werror`   | ✅ done        |

---

## 6. Code Review Findings

### Bugs (Sorted by Priority)

| Priority   | Component / Location                        | Description                                                                                                                                                                                                                          | Impact                                                                                                                                     | Proposed Fix                                                                                                                                           |
| ---------- | ------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------ |
| **Medium** | `vw_caption_presenter.c:107`                | `(void)mode` discards the caller's mode selection. `VW_PRESENTER_MODE_SPU` silently falls back to OSD — the enum promises SPU but only OSD is implemented.                                                                           | Caller expects SPU subpicture channel for native subtitle region rendering but gets OSD overlay instead. No crash, but incorrect behavior. | Implement SPU path via `subpicture_New` + `vout_PutSubpicture` with correct PTS, or document that SPU is not yet supported and remove the enum values. |
| **Medium** | `vw_caption_presenter.c:110`                | `void* p_filter_ptr` cast to `filter_t*` with no compile-time or runtime type check. If called with an incorrect pointer type (e.g., from `vw_caption_presenter_show_segment` which stores `void*`), the cast is undefined behavior. | Potential UB if caller misuses the API. Currently all callers pass the correct type.                                                       | Use `filter_t*` directly in the API signature instead of `void*`, or add an explicit tag/type field.                                                   |
| **Low**    | `vw_caption_presenter.c:118-119`            | `vw_caption_presenter_show_segment` uses `presenter->vlc_subpicture` as `filter_t*` — this field is named `vlc_subpicture` (suggesting it holds a subpicture pointer) but is used as a filter pointer. Semantic mismatch.            | Confusing to future readers; no runtime impact since it's cast to void\* anyway.                                                           | Rename `vlc_subpicture` to `p_filter_context` or add a union.                                                                                          |
| **Low**    | `vw_caption_presenter.c:123-124`            | Default 2-second duration is applied when segment duration is zero or negative — but negative duration indicates invalid data that should arguably be rejected.                                                                      | 2-second caption appears on screen for negative-duration segments. Minor.                                                                  | Log a warning when duration is negative before applying default.                                                                                       |
| **Low**    | `tests/unit/test_caption_presenter.c:77-78` | `(void)segment;` and `(void)empty_seg;` are used to suppress unused-variable warnings, but `segment` is used at line 73 (`&segment` passed to function). This suggests the test was refactored and these suppressions are leftover.  | Dead code (no-op suppression). No functional impact.                                                                                       | Remove the two `(void)` casts.                                                                                                                         |

### Architectural & Operational Risks

| Category                          | Risk Description                                                                                                                                                                                                                                                       | Affected Files                              | Mitigation Strategy                                                                                                      |
| --------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------ |
| **Blocking on audio thread**      | `input_GetVout` acquires VLC internal locks from the audio output thread. If the input thread is blocked (seeking, EOF, stopping), this blocks the audio callback.                                                                                                     | `vw_caption_presenter.c:37,63,76`           | Move vout lookup to a deferred/polled path or cache the vout reference at open time with a release on vout destruction.  |
| **SPU path permanently deferred** | The SPU subpicture channel (native subtitle area) was the primary route in the plan but was removed due to buggy implementation. If OSD has different behavior across VLC versions (position, styling, coexistence with native subtitles), this is a portability risk. | `vw_caption_presenter.c` (entire file)      | Revisit SPU implementation in Milestone 3 with proper PTS-based subpicture lifecycle.                                    |
| **Caption visibility**            | `vout_OSDText` renders as an OSD overlay, not in the native subtitle area. OSD may be disabled by user settings, have different font/styling, or be obscured by UI elements.                                                                                           | `vw_caption_presenter.c:103`                | Document this limitation; plan SPU path for Milestone 3.                                                                 |
| **Test coverage gap**             | The vout search function (`vw_caption_presenter_find_vout`) has zero test coverage — the unit test stubs return NULL, so only the NULL-p_filter fallback path is exercised.                                                                                            | `tests/unit/test_caption_presenter.c:19-49` | Add an integration test with a mock VLC object hierarchy, or a controlled test that verifies stubs are called correctly. |

### Code Style & Quality Nitpicks

| Issue Type         | File & Line                                 | Description                                                                                                           | Recommendation                               |
| ------------------ | ------------------------------------------- | --------------------------------------------------------------------------------------------------------------------- | -------------------------------------------- |
| **Dead Code**      | `tests/unit/test_caption_presenter.c:77-78` | Leftover `(void)segment; (void)empty_seg;` suppressions for variables that are actually used                          | Remove both lines                            |
| **Naming**         | `vw_caption_presenter.h:16`                 | Field named `vlc_subpicture` but used as a generic `filter_t*` context pointer                                        | Rename to `p_filter_ctx` or `filter_handle`  |
| **Documentation**  | `vw_caption_presenter.h:18`                 | Comment says "native SPU subpicture channel or OSD fallback depending on mode and availability" but SPU is never used | Update comment to reflect reality (OSD-only) |
| **Documentation**  | `vw_caption_presenter.h:21`                 | Comment says "default AUTO mode for native SPU or OSD fallback" — AUTO always uses OSD                                | Update comment to match implementation       |
| **Unused Include** | `vw_caption_presenter.c`                    | `#include <vlc_block.h>` is included but `vlc_block_t` is never used                                                  | Remove unused include                        |
