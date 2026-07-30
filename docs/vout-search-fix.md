# Vout Search & Caption Display Failure Analysis

## Current Code vs Fix Plan

Changes applied in working tree **match** the `input_GetVout()` approach from the original fix plan:

| Step | fix plan says | actual code | match? |
|------|--------------|-------------|--------|
| `input_Control` in .def | Add to `libvlccore.def` | Already present (line 16) | ✅ |
| Parent walk + "input" match | Walk `obj.parent` until `object_type=="input"` | `vw_caption_presenter_find_vout` does this | ✅ |
| `input_GetVout()` | Call to get held vout ref | Called at line 37 | ✅ |
| Release vout | `vlc_object_release(vout)` after use | Done at lines 73, 78, 86, 91 | ✅ |

The vout fix is **correct per plan**. Why still no captions?

## Issue 1: Nobody Calls the Caption Presenter (Critical)

`vw_caption_presenter_display` and `vw_caption_presenter_show_segment` are **never invoked** from `vlc_whisper_module.c` or any production code. Only the unit test calls them.

The `vw_plugin_filter` callback (line 46 of `vlc_whisper_module.c`) captures PCM via `vw_audio_capture_process_block` but never reads results or calls the presenter. Even if the vout search returns the correct vout, captions never display because the presenter code is dead code.

**Missing caller flow:**

```
vw_plugin_filter (per audio block)
  └─ vw_audio_capture_process_block → writes PCM to SPSC queue
  └─ ??? no consumer reads queue and calls caption presenter

Where the caller should be:
vw_plugin_filter → read SPSC queue for completed captions
  └─ if caption ready → vw_caption_presenter_display(p_filter, text, duration, AUTO)
```

### Options to add a caller

**A. Poll inside filter callback** — On each `vw_plugin_filter` call, check if the SPSC queue contains a caption result. This requires the queue to carry caption events (not just raw PCM). Changes needed: add a "caption result" message type to the queue, or add a separate consumer thread.

**B. Worker thread callback** — (Milestone 3 path) The worker reads PCM from the queue, transcribes it, and sends caption text back. The filter module needs a callback from the worker. This is the proper long-term architecture but requires the worker integration.

**C. Immediate test hook for Milestone 2** — Since Milestone 2 is about proving the pipeline works, add a simple periodic caption in `vw_plugin_filter` (e.g., every 100 blocks, call `vw_caption_presenter_display` with a test string). This proves the vout search + OSD path work end-to-end without the worker.

## Issue 2: SPU Render Path is Broken (Structural Bug)

The SPU render path (`vw_caption_presenter_render_spu`, line 43):

```c
subpicture_t* subpic = subpicture_New(NULL);  // alloc
subpic->i_start = VLC_TICK_0;                 // PTS = 0 (past!)
subpic->i_stop = ...;                         // stop = 0 + duration
// set region, text...
// vout_PutSubpicture(vout, subpic) NEVER CALLED ← subpicture leaked
vout_OSDText(vout, 1, SUBPICTURE_ALIGN_BOTTOM, ...); // same as OSD path
```

Problems:
- `subpicture_New(NULL)` with NULL allocator is fragile (needs `subpicture_Delete` at cleanup)
- `i_start = VLC_TICK_0` (0) is **always in the past** — VLC's subpicture filter drops any subpicture with `i_stop <= current_time`
- The subpicture is allocated but never sent via `vout_PutSubpicture` — it's leaked
- Instead of the SPU path, it falls through to `vout_OSDText` which is identical to the OSD path

**Fix**: Either implement the SPU path properly (set real PTS, call `vout_PutSubpicture`) or remove it entirely and only use OSD. For Milestone 2, OSD-only is simpler and sufficient.

## Issue 3: Parent Chain May Not Include `object_type == "input"` on Windows

The parent chain `filter → aout → decoder → input` assumes:
- `audio_output_t` parent is `decoder_t`
- `decoder_t` parent is `input_thread_t`

On Windows VLC 3.0.23, the audio output might be created by a different code path (e.g., `aout_New` called from the audio output manager rather than the decoder). If the intermediate object in the parent chain has `object_type != "input"` AND its parent is NULL, the walk terminates early.

**Diagnostic**: Add temporary `msg_Dbg` logging of `object_type` at each level of the parent walk. If the chain terminates before reaching "input", we need to understand the Windows-specific hierarchy.

## Issue 4: `input_Control(INPUT_GET_VOUTS)` May Block or Fail from Aout Thread

`input_Control` sends a message to the input thread and waits for a response. This blocks the audio output thread. On Windows, this can:
- Fail if the input thread is between states (seeking, EOF, stopping)
- Deadlock if the input thread is waiting for the aout thread (unlikely but possible)
- Return NULL for `i_vout` if the vout hasn't been created yet (early in playback)

**Mitigation**: Call only after a `p_filter->fmt_in.audio.i_rate` sample count threshold is exceeded, ensuring the input is fully initialized.

## Recommended Next Steps (Milestone 2)

1. **Add a test caption caller** — In `vw_plugin_filter`, after N captures, call `vw_caption_presenter_display(p_filter, "Test caption", 3000000, VW_PRESENTER_MODE_OSD)`. This bypasses Issues 1 and 2.

2. **Strip SPU path to OSD-only** — The SPU subpicture allocation is broken and unnecessary for this milestone. Only use `vout_OSDText`.

3. **Add object_type logging** — Log each parent's `object_type` in `vw_caption_presenter_find_vout` to verify the chain reaches "input".

4. **Verify on actual video media** — Audio-only files have no vout; test with a visual video file.
