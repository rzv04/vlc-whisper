# Task: Native SPU Subpicture Subsystem (Step 17b)

## Goal
Migrate caption presentation from crude OSD overlay text (`vout_OSDText`) to native VLC SPU subpicture channel rendering (`vout_RegisterSubpictureChannel` + `vout_PutSubpicture` with `VLC_CODEC_TEXT` and `text_segment_New`), preserving 100% real-time streaming, pause/resume sync, and seek discontinuity flushing while establishing the native subtitle pipeline required for future look-ahead decoding.

## Context
- **Relevant Docs/ADRs**: [`docs/architecture.md`](file:///home/razvan/vlc-whisper/.worktrees/gemini/docs/architecture.md), [`docs/decisions.md`](file:///home/razvan/vlc-whisper/.worktrees/gemini/docs/decisions.md) (ADR-016), [`docs/plans/milestone3_postmortem.md`](file:///home/razvan/vlc-whisper/.worktrees/gemini/docs/plans/milestone3_postmortem.md), [`docs/vlc-api-essentials.md`](file:///home/razvan/vlc-whisper/.worktrees/gemini/docs/vlc-api-essentials.md).
- **VLC/Worker/Protocol Version Affected**: VLC 3.0+ on Linux and Windows x64. Wire protocol v1.0 unchanged.
- **Assumptions & Explicit Non-Goals**:
  - *Assumption*: VLC video output thread provides SPU subpicture channels when an active video surface exists. If no video surface exists (audio-only playback) or channel registration fails, presenter gracefully falls back to OSD / no-op.
  - *Non-goals*: Ahead-of-time source file demuxing (deferred to Step 17c), 5s clock jump lookahead seek re-sync (Step 17d), beam-search quality tuning (Step 17e).

## Scope
- **In Scope**:
  1. `plugin/include/vw_platform.h`: Define `VW_WEAK` symbol linkage macro once in this shared header (`__attribute__((weak))` on Linux, empty on Windows) to prevent per-TU macro duplication.
  2. `plugin/include/vw_caption_presenter.h`:
     - Extend `vw_caption_presenter_t` struct with `int spu_channel_id` (registered channel ID, default `-1`) and `bool spu_channel_registered` flag.
     - Update presenter function signatures to accept media timestamp context: `vw_caption_presenter_show_segment(vw_caption_presenter_t* presenter, const vw_caption_segment_t* segment, int64_t input_time_us)` (anchor reserved for 17c media scheduling).
  3. `plugin/src/vw_caption_presenter.c`:
     - Register private SPU channel via `vout_RegisterSubpictureChannel(vout)` on first vout acquisition.
     - Validate channel ID contract: check `presenter->spu_channel_id >= 0`; if `< 0` (registration failed), set `spu_channel_registered = false` and fall back to `vout_OSDText`.
     - Construct structured `subpicture_t` carrying `video_format_Init(&fmt, VLC_CODEC_TEXT)` and `subpicture_region_New(&fmt)` with `text_segment_New(text)`.
     - Align subtitles cleanly at bottom center (`SUBPICTURE_ALIGN_BOTTOM`).
     - Schedule captions in the OSD clock domain (`i_start = mdate()`, `i_stop = i_start + duration`) with `b_subtitle = false`, the configuration this VLC 3.0.23 build demonstrably renders filter-pushed subpictures against; media-domain scheduling (`b_subtitle = true`) is deferred to 17c (see Design §3).
     - Set `subpic->b_subtitle = true`, `subpic->b_ephemer = true`, `subpic->b_absolute = false`.
     - Submit subpictures via `vout_PutSubpicture(vout, subpic)` (transfers ownership to VLC core).
     - Provide cleanup: `subpicture_Delete(subpic)` on region allocation error; `subpicture_region_Delete` / `text_segment_Delete` on partial failure.
     - Flush private SPU channel on seek/blank via `vout_FlushSubpictureChannel(vout, presenter->spu_channel_id)`.
  4. `plugin/src/vw_whisper_module.c`:
     - In sender thread main loop, query current playback position `input_time_us` from the held input thread (`input_Control(input, INPUT_GET_TIME, &position_us)` / `vw_plugin_input_position_us(input)`).
     - Pass `position_us` into `vw_caption_presenter_show_segment(&sys->presenter, &recv.segment, position_us)` (reserved media anchor for 17c).
  5. `plugin/libvlccore.def`: Export required VLC 3.0 SPU and timing symbols for MinGW Windows linking:
     - `vout_RegisterSubpictureChannel`
     - `vout_PutSubpicture`
     - `vout_FlushSubpictureChannel`
     - `subpicture_New`
     - `subpicture_Delete`
     - `subpicture_region_New`
     - `subpicture_region_Delete`
     - `text_segment_New`
     - `text_segment_Delete`
     - `mdate`
     *(Note: `input_GetVout` and `video_format_Init` are inline functions in `<vlc_input.h>` and `<vlc_es.h>` and do not require export).*
  6. `tests/unit/test_caption_presenter.c`:
     - Update mock VLC symbols for SPU and timing pipeline (`subpicture_New`, `subpicture_Delete`, `subpicture_region_New`, `subpicture_region_Delete`, `text_segment_New`, `text_segment_Delete`, `vout_RegisterSubpictureChannel`, `vout_PutSubpicture`, `vout_FlushSubpictureChannel`, `mdate`).
     - Assert SPU channel registration, subpicture region construction, timestamp mapping, channel flushing on seek, and teardown clearing.
     - Assert fallback to OSD when `vout_RegisterSubpictureChannel` returns `-1` (failure).
  7. Documentation:
     - `docs/source-layout.md`: Update `vw_caption_presenter.c` entry to "VLC SPU subpicture channel rendering (`vout_RegisterSubpictureChannel`/`vout_PutSubpicture`/`VLC_CODEC_TEXT`/`text_segment_New`) with OSD fallback" per Rule 14.
     - `docs/architecture.md`, `docs/roadmap.md`, `docs/test-strategy.md` updated with Step 17b details.
- **Out of Scope**:
  - Worker-side FFmpeg/MediaFoundation demuxing (Step 17c).
  - Lookahead queue pacing / source mode protocol extensions (Step 17c).

## Design

### 1. SPU Subpicture Construction & Memory Ownership
When the plugin sender thread receives `VW_MSG_CAPTION_SEGMENT`:
```c
subpicture_t* subpic = subpicture_New(NULL);
if (subpic) {
  video_format_t fmt;
  video_format_Init(&fmt, VLC_CODEC_TEXT);
  subpicture_region_t* region = subpicture_region_New(&fmt);
  if (region) {
    region->p_text = text_segment_New(segment->text_utf8);
    region->i_align = SUBPICTURE_ALIGN_BOTTOM;
    region->i_x = 0;
    region->i_y = 20;
    subpic->p_region = region;
    subpic->i_channel = presenter->spu_channel_id;
    subpic->i_start = start_tick;
    subpic->i_stop = stop_tick;
    subpic->b_subtitle = true;
    subpic->b_ephemer = true;
    subpic->b_absolute = false;
    vout_PutSubpicture(vout, subpic);  // VLC core takes ownership of subpic & region
  } else {
    subpicture_Delete(subpic);  // Frees subpicture on region creation failure
  }
}
```

### 2. SPU Channel Registration, ID Contract & Lifecycle
- **Channel ID Contract**: `vout_RegisterSubpictureChannel(vout)` returns an `int` channel ID (`< 0` on error).
- **Presenter State**:
  ```c
  typedef struct vw_caption_presenter {
    void* p_filter_ctx;
    int spu_channel_id;          // Initialized to -1
    bool spu_channel_registered; // Initialized to false
  } vw_caption_presenter_t;
  ```
- **Acquisition**: On first caption display or `vout` acquisition, if `!presenter->spu_channel_registered`, invoke `presenter->spu_channel_id = vout_RegisterSubpictureChannel(vout)`.
  - If `presenter->spu_channel_id >= 0`, set `presenter->spu_channel_registered = true`.
  - If `presenter->spu_channel_id < 0` (registration failed or unsupported), leave `spu_channel_registered = false` and route to `vout_OSDText` fallback.
- **Seek / Discontinuity**: On seek, call `vw_caption_presenter_blank(presenter)`:
  - If `spu_channel_registered && spu_channel_id >= 0`, call `vout_FlushSubpictureChannel(vout, presenter->spu_channel_id)`.
  - Also flush OSD channel (`vout_FlushSubpictureChannel(vout, 1)`) for OSD fallback grace.
- **Teardown**: On `vw_caption_presenter_clear(presenter)`:
  - Flush the channel, reset `p_filter_ctx = NULL`, `spu_channel_id = -1`, `spu_channel_registered = false`.

### 3. Real-Time Timestamp Mapping (OSD Clock Domain — empirical finding)
VLC 3.0's audio output re-bases audio-filter block PTS into the **system-date domain** (`aout_DecPlay` compares `block->i_pts` against `mdate()`), so the worker's segment PTS are system-date ticks (µs since boot on Windows), NOT media timestamps. The vout renders subpictures against one of two clocks (`ThreadDisplayRenderPicture`): `render_subtitle_date` = displayed picture PTS (media) for `b_subtitle = true`, or `render_osd_date` = `mdate()` for `b_subtitle = false`.

**Live testing finding (2026-08-18, VLC 3.0.23 Windows / d3d11va + direct3d11):** media-domain subpictures (`b_subtitle = true`, `i_start`/`i_stop` converted to the media timeline via `INPUT_GET_TIME`) are silently dropped before region rendering — the vout's `spu` log shows no "original picture size is undefined" warning, which MUST fire for any selected subpicture with unset original dimensions. The subtitle clock is therefore not usable for filter-pushed subpictures in this build. The OSD clock (`mdate()`) is the domain the earlier OSD milestones (11-16) demonstrated displaying, so:
- Presenter schedules `start_tick = mdate()`, `stop_tick = mdate() + duration_us` with `b_subtitle = false` on the registered private SPU channel (native channel, flushable via `vout_FlushSubpictureChannel`, independent of the user's `osd` setting which only gates `vout_OSDText`).
- `input_time_us` (INPUT_GET_TIME) stays in the signature as the reserved anchor for 17c media scheduling; 17c must first observe the subtitle clock's behavior (e.g. push a wide-window probe subpicture) before relying on media-domain timing.
- The S→M conversion (`segment_pts - (system_now - input_time)`, offset stable while paused) is documented in `docs/vlc-api-essentials.md` §3.4 and remains correct in principle for builds where the subtitle clock tracks picture PTS.
- **Expected log noise:** VLC emits `main warning: original picture size is undefined` once per caption. The presenter does not declare `i_original_picture_width/height` (no public API from an audio filter to learn the video source size); VLC falls back to the displayed picture's source size — which keeps text scaling correct — and caches it back into the heap subpicture, so the warning fires once per subpicture, not per frame. Silencing it would require ES plumbing (`INPUT_GET_ES_OBJECTS`) for cosmetic gain; revisit only if log hygiene matters.

### 4. Windows MinGW Symbol Linkage (`VW_WEAK` in Shared Header)
Defined once in `plugin/include/vw_platform.h`:
```c
#ifdef _WIN32
#define VW_WEAK
#else
#define VW_WEAK __attribute__((weak))
#endif
```
And exported in `plugin/libvlccore.def` to ensure clean Windows MinGW dynamic symbol resolution.

## Acceptance Criteria
- [ ] Real-time caption segments construct and display via native SPU `subpicture_t` with `video_format_Init(&fmt, VLC_CODEC_TEXT)`.
- [ ] Subtitles render at bottom center (`SUBPICTURE_ALIGN_BOTTOM`) using VLC's native subtitle renderer.
- [ ] Seeking in playback instantly purges active and queued subtitles via `vout_FlushSubpictureChannel(vout, spu_channel_id)`.
- [ ] Pausing and resuming playback preserves caption display and timeline alignment.
- [ ] Graceful fallback to OSD if SPU channel registration returns `< 0` or if video output is unavailable.
- [ ] `VW_WEAK` defined once in `vw_platform.h`; `libvlccore.def` exports all SPU lifecycle functions.
- [ ] Windows MinGW cross-build links SPU symbols with zero unresolved externals.
- [ ] 100% CTest pass rate (16/16 targets).
- [ ] Valgrind memcheck (`-j1`) verifies zero memory leaks in SPU presenter operations.

## Test Plan
1. **Automated Unit Tests**:
   - Update `tests/unit/test_caption_presenter.c` with mock SPU functions (`subpicture_New`, `subpicture_Delete`, `subpicture_region_New`, `subpicture_region_Delete`, `text_segment_New`, `text_segment_Delete`, `vout_RegisterSubpictureChannel`, `vout_PutSubpicture`, `vout_FlushSubpictureChannel`).
   - Assert SPU subpicture creation, text assignment, channel ID binding, bottom alignment, and channel flush invocations.
   - Assert OSD fallback when `vout_RegisterSubpictureChannel` returns `-1`.
2. **Build Verification**:
   - `cmake --preset linux-x64-debug && cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug`
   - `cmake --preset linux-x64-debug-cpu && cmake --build --preset linux-x64-debug-cpu && ctest --preset linux-x64-debug-cpu`
   - `cmake --preset windows-x64-release`
   - `ctest --test-dir build/linux-x64-debug -T memcheck -j1`
3. **Manual VLC Verification**:
   - Play video file in VLC with plugin enabled: verify subtitles display in native subtitle font at bottom center.
   - Perform rapid seek forward and backward: verify pre-seek subtitles disappear instantly without ghost text.
   - Adjust volume / toggle pause: verify volume OSD text does not overwrite or corrupt subtitles.

## Definition of Done
- [ ] Authored in C17 (`-std=c17`), no C++ code.
- [ ] Audio callback remains 100% lock-free (Rule 4); all SPU calls execute strictly on background sender thread.
- [ ] Zero disk I/O or SRT file generation (Rule 5).
- [ ] Header documentation updated for all presenter functions per Rule 11.
- [ ] Documentation updated in `docs/architecture.md`, `docs/roadmap.md`, `docs/source-layout.md` per Rule 14.
- [ ] Code formatting verified with `clang-format --dry-run --Werror`.
- [ ] Native Linux and Windows builds pass cleanly.
- [ ] Valgrind memory leak verification clean (`-j1`).
