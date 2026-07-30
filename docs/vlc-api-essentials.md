# VLC 3.0 C API & Core Engine Reference

> **Vendor dependency:** [`VLC 3.0.23`](file:///home/razvan/vlc-whisper/.worktrees/gemini/worker/third_party/vlc-3.0.23/) — linked via public VLC headers in `plugin/include` & `worker/third_party/vlc-3.0.23/include/`.  
> **Header inclusion:** Always include `<vlc_common.h>` before any other VLC headers (`vlc_block.h`, `vlc_filter.h`, `vlc_input.h`, etc.). Protect header inclusion order using `// clang-format off` / `// clang-format on` to prevent alphabetical header reordering.  
> **Linked by:** `vlc_whisper_plugin` only (the worker executable *must not* link VLC headers or libraries).

This document specifies the essential VLC 3.0 C API contracts, internal engine mechanics, object tree traversal algorithms, seeking/discontinuity signals, and livestream/VOD query interfaces required for the VLC-Whisper integration.

---

## Table of Contents

1. [Core Types & Structures](#1-core-types--structures)
2. [Audio Filter Pipeline & Realtime Contract](#2-audio-filter-pipeline--realtime-contract)
3. [Seeking & Timeline Discontinuity Signals](#3-seeking--timeline-discontinuity-signals)
4. [Livestream, IPTV, and Network VOD Detection](#4-livestream-iptv-and-network-vod-detection)
5. [VLC Object Tree & Vout Retrieval Architecture](#5-vlc-object-tree--vout-retrieval-architecture)
6. [Subtitle & OSD Rendering APIs](#6-subtitle--osd-rendering-apis)

---

## 1. Core Types & Structures

### `struct filter_t` (`<vlc_filter.h>`)

Structure representing an audio, video, or subtitle filter plugin instance inside VLC.

| Field | Type | Description |
|---|---|---|
| `p_sys` | `filter_sys_t*` | Private plugin state handle (`vw_plugin_sys_t*`). |
| `fmt_in` | `es_format_t` | Input elementary stream format (sample rate, channels, codec format). |
| `fmt_out` | `es_format_t` | Output elementary stream format. **Must match `fmt_in.audio` for passthrough**. |
| `pf_audio_filter` | `block_t* (*)(filter_t*, block_t*)` | Callback function executed per audio block on the audio output thread. |

> **Critical Invariant:** Audio passthrough filters MUST explicitly set `p_filter->fmt_out.audio = p_filter->fmt_in.audio;` during module initialization (`vw_plugin_open`). Uninitialized `fmt_out` causes VLC to insert an unwanted automatic resampler, leading to severe PTS drift and audio distortion.

### `struct block_t` (`<vlc_block.h>`)

Structure representing a discrete unit of raw PCM or encoded media data moving through VLC's pipeline.

| Field | Type | Description |
|---|---|---|
| `p_buffer` | `uint8_t*` | Pointer to raw sample payload buffer. |
| `i_buffer` | `size_t` | Size of payload buffer in bytes. |
| `i_nb_samples` | `unsigned` | Number of audio frames/samples contained in the block. |
| `i_pts` | `vlc_tick_t` | Presentation TimeStamp in signed 64-bit microseconds (`pts_us`). |
| `i_flags` | `unsigned` | Status bitfield (`BLOCK_FLAG_DISCONTINUITY`, `BLOCK_FLAG_HEADER`, etc.). |

### `struct input_thread_t` (`<vlc_input.h>`)

Opaque engine thread representing an active playback item/stream. Controls stream properties, seeking capabilities, title/chapter transitions, and video outputs.

### `struct vout_thread_t` (`<vlc_vout.h>`)

Video output engine thread responsible for rendering video frames, SPU subpictures, and OSD text overlays.

---

## 2. Audio Filter Pipeline & Realtime Contract

### Realtime Audio Callback Constraints (`pf_audio_filter`)

The audio filter callback `vw_plugin_filter` is executed synchronously on VLC's high-priority audio output thread.

- **Rule 4 Safety Constraint**: MUST NEVER perform heap allocation (`malloc`/`calloc`), file/pipe IPC operations, blocking mutex locks, or Whisper model inference inside `pf_audio_filter`.
- **Bounded Enqueue**: Input PCM bytes are converted to canonical 16 kHz S16LE or FL32 format and enqueued to a bounded SPSC queue (`vw_spsc_queue_t`) only.

### Audio Format Negotiation

| Codec Constant | Format Enum | Description |
|---|---|---|
| `VLC_CODEC_FL32` | `VW_AUDIO_FORMAT_FL32` | 32-bit IEEE Single Precision Floating Point |
| `VLC_CODEC_S16N` | `VW_AUDIO_FORMAT_S16` | 16-bit Signed Integer Native Endian |
| `VLC_CODEC_S32N` | `VW_AUDIO_FORMAT_S32` | 32-bit Signed Integer Native Endian |

---

## 3. Seeking & Timeline Discontinuity Signals

VLC signals user seeking, playback rate changes, or media item transitions through explicit block flags and PTS jumps.

### Discontinuity Flags & Time Signals

| Flag / Signal | Value | Meaning |
|---|---|---|
| `BLOCK_FLAG_DISCONTINUITY` | `0x0001` | Set on `p_block->i_flags` when a seek or stream gap occurs. |
| `VLC_TICK_INVALID` | `INT64_MIN` | Invalid or unassigned timestamp. |
| Non-monotonic PTS | `pts < last_pts` | Backward seek or media rewind detected. |

### Discontinuity Handling Workflow

```text
vlc_whisper_module (pf_audio_filter)
  │
  ├── Detect (i_flags & BLOCK_FLAG_DISCONTINUITY) OR (i_pts < last_pts_us)
  │
  ├── 1. Clear active presentation: vw_caption_presenter_clear()
  ├── 2. Send IPC control message: STOP (reason = SEEK_DISCONTINUITY)
  ├── 3. Reset SPSC queue & VAD state: vw_spsc_queue_reset()
  └── 4. Re-initialize session: Send START message with updated timeline_origin_pts_us
```

---

## 4. Livestream, IPTV, and Network VOD Detection

VLC categorizes media inputs into **Seekable VOD / Local Files** vs **Live IPTV / Network Streams** via `input_Control()` query flags.

### Querying Stream Capabilities (`input_Control`)

```c
#include <vlc_input.h>

// 1. Query seeking capability
bool b_seekable = false;
input_Control((input_thread_t*)p_input, INPUT_CAN_SEEK, &b_seekable);

// 2. Query pace control (live vs file/buffered stream)
bool b_can_pace = false;
input_Control((input_thread_t*)p_input, INPUT_CAN_CONTROL_PACE, &b_can_pace);

// 3. Query pause capability
bool b_can_pause = false;
input_Control((input_thread_t*)p_input, INPUT_CAN_PAUSE, &b_can_pause);
```

### Capability Matrix

| Stream Type | `INPUT_CAN_SEEK` | `INPUT_CAN_CONTROL_PACE` | `INPUT_CAN_PAUSE` | Plugin Behavior |
|---|---|---|---|---|
| **Local File** | `true` | `true` | `true` | Full seeking support, pause/resume timeline sync. |
| **Network VOD (HTTP/MP4)** | `true` | `true` | `true` | Seeking supported; handle buffer stalls via PTS gaps. |
| **HLS / DASH Live Stream** | `false` / bounded | `true` | `true` / bounded | Sliding window timeline; drop stale backlog audio. |
| **IPTV / RTSP Broadcast** | `false` | `false` | `false` | Realtime network pace; no seeking; continuous VAD. |

> **Livestream Rule:** For live IPTV streams (`INPUT_CAN_CONTROL_PACE == false`), the plugin must never buffer audio beyond the maximum backlog threshold (~15s). Stale PCM chunks are dropped, and captions are presented strictly aligned with incoming media PTS.

---

## 5. VLC Object Tree & Vout Retrieval Architecture

### Object Tree Structure

```text
libvlc (object_type="libvlc", root instance)
  └── playlist (object_type="playlist")
        ├── audio output (object_type="audio output")
        │     └── filter_t (vlc-whisper plugin) (object_type="audio filter")
        └── input_thread (object_type="input")
              ├── decoder (audio)
              ├── decoder (video)
              └── vout_thread_t (object_type="vout")
```

### `vout` Retrieval Algorithm

Because `audio filter` and `vout_thread_t` reside on separate branches under `playlist`, `vlc_object_find_name(p_filter, "vout")` returns `NULL`. The plugin uses tree traversal and the official `input_GetVout()` API:

```c
static vout_thread_t* vw_caption_presenter_find_vout(filter_t* p_filter) {
  if (!p_filter) return NULL;

  vlc_object_t* cur = VLC_OBJECT(p_filter);
  while (cur) {
    if (cur->obj.object_type) {
      // 1. Direct input thread ancestor match
      if (strcmp(cur->obj.object_type, "input") == 0) {
        vout_thread_t* vout = input_GetVout((input_thread_t*)cur);
        if (vout) return vout;
      }
      // 2. Direct vout ancestor match
      else if (strcmp(cur->obj.object_type, "vout") == 0) {
        vlc_object_hold(cur);
        return (vout_thread_t*)cur;
      }

      // 3. Search named "input" child under ancestor node (e.g. playlist)
      vlc_object_t* p_input_obj = vlc_object_find_name(cur, "input");
      if (p_input_obj) {
        vout_thread_t* vout = input_GetVout((input_thread_t*)p_input_obj);
        vlc_object_release(p_input_obj);
        if (vout) return vout;
      }

      // 4. Search children list for input or vout nodes
      vlc_list_t* children = vlc_list_children(cur);
      if (children) {
        for (int i = 0; i < children->i_count; i++) {
          vlc_object_t* child = (vlc_object_t*)children->p_values[i].p_address;
          if (child && child->obj.object_type) {
            if (strcmp(child->obj.object_type, "input") == 0) {
              vout_thread_t* vout = input_GetVout((input_thread_t*)child);
              if (vout) {
                vlc_list_release(children);
                return vout;
              }
            } else if (strcmp(child->obj.object_type, "vout") == 0) {
              vlc_object_hold(child);
              vlc_list_release(children);
              return (vout_thread_t*)child;
            }
          }
        }
        vlc_list_release(children);
      }
    }
    cur = cur->obj.parent;
  }
  return NULL;
}
```

---

## 6. Subtitle & OSD Rendering APIs

### `vout_OSDText` (`<vlc_vout_osd.h>`)

Displays text overlays on the active `vout_thread_t`.

```c
void vout_OSDText(vout_thread_t *vout, int channel, int position, vlc_tick_t duration, const char *text);
```

| Parameter | Type | Value | Description |
|---|---|---|---|
| `vout` | `vout_thread_t*` | Non-NULL | Active video output object acquired via `vw_caption_presenter_find_vout()`. |
| `channel` | `int` | `1` | OSD subpicture channel index. |
| `position` | `int` | `SUBPICTURE_ALIGN_BOTTOM` | Screen positioning (bottom-center aligned). |
| `duration` | `vlc_tick_t` | `duration_us` | Display lifetime in microsecond ticks ($1\text{ s} = 1,000,000\text{ ticks}$). |
| `text` | `const char*` | UTF-8 string | Subtitle caption text to present on screen. |

> **Memory Rule:** Always call `vlc_object_release(VLC_OBJECT(vout))` immediately after calling `vout_OSDText()` to decrement reference count and avoid memory leaks.
