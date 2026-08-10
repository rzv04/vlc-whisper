# VLC 3.0 C API & Core Engine Reference

> **Vendor dependency:** [`VLC 3.0.23`](file:///home/razvan/vlc-whisper/.worktrees/gemini/worker/third_party/vlc-3.0.23/) — linked via public VLC headers in `plugin/include` & `worker/third_party/vlc-3.0.23/include/`.  
> **Header inclusion:** Always include `<vlc_common.h>` before any other VLC headers (`vlc_block.h`, `vlc_filter.h`, `vlc_input.h`, etc.). Protect header inclusion order using `// clang-format off` / `// clang-format on` to prevent alphabetical header reordering by `clang-format`.  
> **Linked by:** `vlc_whisper_plugin` only (the worker executable *must not* link VLC headers or libraries).  
> **Verified against:** the vendored VLC 3.0.23 headers (`worker/third_party/vlc-3.0.23/include/`) and VideoLAN docs via Context7 (`/videolan/vlc`). Where this document differs from common VLC references, the vendored headers win.

This document provides a comprehensive, deep-dive specification of VLC 3.0 C API contracts, internal engine mechanics, object tree traversal algorithms, clock and timestamp synchronization, stream capability detection (seeking, IPTV, VOD), and rendering subsystem integration.

---

## Table of Contents

1. [Core Structures & Types](#1-core-structures--types)
2. [Audio Filter Pipeline & Realtime Contract](#2-audio-filter-pipeline--realtime-contract)
3. [VLC Clock & Timeline Synchronization Architecture](#3-vlc-clock--timeline-synchronization-architecture)
4. [Seeking & Timeline Discontinuity Signals](#4-seeking--timeline-discontinuity-signals)
5. [Livestream, IPTV, and Network VOD Detection](#5-livestream-iptv-and-network-vod-detection)
6. [VLC Object Tree & Vout Retrieval Architecture](#6-vlc-object-tree--vout-retrieval-architecture)
7. [Subtitle & OSD Rendering Subsystem](#7-subtitle--osd-rendering-subsystem)
8. [Module Registration & ABI Invariants](#8-module-registration--abi-invariants)

---

## 1. Core Structures & Types

### `struct filter_t` (`<vlc_filter.h>`)

Structure representing an audio, video, or subtitle filter plugin instance inside VLC.

| Field | Type | Description |
|---|---|---|
| `p_sys` | `filter_sys_t*` | Private plugin state handle (`vw_plugin_sys_t*`). |
| `fmt_in` | `es_format_t` | Input elementary stream format (sample rate, channels, codec format). |
| `fmt_out` | `es_format_t` | Output elementary stream format. **Must match `fmt_in.audio` for passthrough**. |
| `pf_audio_filter` | `block_t* (*)(filter_t*, block_t*)` | Callback function executed per audio block on the audio output thread. |
| `obj` | `vlc_object_t` | Base object header (contains `parent`, `libvlc`, `object_type`). |

> **Critical Invariant:** Audio passthrough filters MUST explicitly set `p_filter->fmt_out.audio = p_filter->fmt_in.audio;` during module initialization (`vw_plugin_open`). Uninitialized `fmt_out` causes VLC to insert an unwanted automatic resampler, leading to severe PTS drift and audio distortion.

### `struct block_t` (`<vlc_block.h>`)

Structure representing a discrete unit of raw PCM or encoded media data moving through VLC's pipeline.

| Field | Type | Description |
|---|---|---|
| `p_buffer` | `uint8_t*` | Pointer to raw sample payload buffer. |
| `i_buffer` | `size_t` | Size of payload buffer in bytes. |
| `i_nb_samples` | `unsigned` | Number of audio frames/samples contained in the block. |
| `i_pts` | `vlc_tick_t` | Presentation TimeStamp in signed 64-bit microseconds (`pts_us`). |
| `i_dts` | `vlc_tick_t` | Decoding TimeStamp in signed 64-bit microseconds. |
| `i_length` | `vlc_tick_t` | Duration of the audio block in microseconds. |
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

### Internal Audio Pipeline Execution Flow (`src/audio_output/filters.c`)

```text
[Decoder] ──> aout_FiltersPipelinePlay()
                    │
                    ├── Filter 1: vlc_whisper (Passthrough)
                    │     ├── Reads PCM & enqueues to SPSC queue
                    │     └── Returns p_block untouched
                    │
                    ├── Filter 2: Volume / Equalizer
                    │
                    └── [Audio Output Device Drivers (ALSA / WASAPI / PulseAudio)]
```

---

## 3. VLC Clock & Timeline Synchronization Architecture

### `vlc_tick_t` & Microsecond Resolution

In VLC 3.0, all time measurements use **`vlc_tick_t`**, which is defined as a signed 64-bit integer (`int64_t`) representing time in **microseconds** ($1\text{ s} = 1,000,000\text{ ticks}$).

```c
#define VLC_TICK_INVALID INT64_MIN
#define VLC_TICK_0 INT64_C(0)
#define VLC_TICK_FROM_MS(ms) ((vlc_tick_t)(ms) * 1000)
#define VLC_TICK_FROM_SEC(s) ((vlc_tick_t)(s) * 1000000)
```

### Media PTS vs Wall-Clock Time

- **Media PTS (`i_pts`)**: The exact position on the media timeline. Must always be used for caption timing.
- **Wall-Clock Time (`vlc_tick_now()`)**: System clock time. **Must NEVER be used for caption timing**.
- **Reason**: Using wall-clock time causes severe caption desynchronization during video pause, playback rate changes (0.5x, 2.0x), and user seeking.

---

## 4. Seeking & Timeline Discontinuity Signals

VLC signals user seeking, playback rate changes, or media item transitions through explicit block flags and PTS jumps.

### Discontinuity Flags & Time Signals

| Flag / Signal | Value | Meaning |
|---|---|---|
| `BLOCK_FLAG_DISCONTINUITY` | `0x0001` | Set on `p_block->i_flags` when a seek or stream gap occurs. |
| `VLC_TICK_INVALID` | `INT64_MIN` | Invalid or unassigned timestamp. |
| Non-monotonic PTS | `pts < last_pts` | Backward seek or media rewind detected. |

### Discontinuity Handling Workflow

```text
vw_whisper_module (pf_audio_filter)
  │
  ├── Detect (i_flags & BLOCK_FLAG_DISCONTINUITY) OR (i_pts < last_pts_us)
  │
  ├── 1. Clear active presentation: vw_caption_presenter_clear()
  ├── 2. Send IPC control message: STOP (reason = SEEK_DISCONTINUITY)
  ├── 3. Reset SPSC queue & VAD state: vw_spsc_queue_reset()
  └── 4. Re-initialize session: Send START message with updated timeline_origin_pts_us
```

---

## 5. Livestream, IPTV, and Network VOD Detection

VLC categorizes media inputs into **Seekable VOD / Local Files** vs **Live IPTV / Network Streams**. Stream capabilities are exposed through **two** mechanisms — and `INPUT_CAN_SEEK` / `INPUT_CAN_PAUSE` / `INPUT_CAN_CONTROL_PACE` do **not** exist anywhere in `enum input_query_e` (`vlc_input.h`); code using them will not compile:

1. **Input object variables** (plugin-safe): read-only bools on the `input_thread_t` — `can-seek`, `can-pause`, `can-rate`, `can-rewind`, `can-record` (documented in `vlc_input.h`). **There is no public pace variable**, so pace control can only be tested through the demux query below.
2. **Demux queries** (internal to VLC core): `DEMUX_CAN_SEEK`, `DEMUX_CAN_PAUSE`, `DEMUX_CAN_CONTROL_PACE` in `enum query_e` (`vlc_demux.h`), issued via `demux_Control()`.

### Querying Stream Capabilities (plugin-safe: input variables)

`p_input` is the `input_thread_t` found by walking the parent chain (Section 6). `var_GetBool` is declared in `vlc_variables.h`:

```c
#include <vlc_input.h>
#include <vlc_variables.h>

// 1. Query seeking capability
bool b_seekable = var_GetBool(p_input, "can-seek");

// 2. Query pause capability
bool b_can_pause = var_GetBool(p_input, "can-pause");

// 3. Query rate control (0.5x / 2.0x playback)
bool b_can_rate = var_GetBool(p_input, "can-rate");
```

### Querying Stream Capabilities (internal: demux controls)

`input_GetDemux()` is an **internal** API (`src/input/input_internal.h`) — it is *not* exported by any public VLC 3.0 header, so a plugin cannot link against it. Use `demux_Control` only inside VLC core:

```c
#include <vlc_demux.h>

input_thread_t *p_input = /* find input ancestor */;
demux_t *p_demux = input_GetDemux(p_input);   // internal API — NOT available to plugins
bool b_seekable = false;
demux_Control(p_demux, DEMUX_CAN_SEEK, &b_seekable);

bool b_can_pace = false;
demux_Control(p_demux, DEMUX_CAN_CONTROL_PACE, &b_can_pace);

bool b_can_pause = false;
demux_Control(p_demux, DEMUX_CAN_PAUSE, &b_can_pause);
```

> **Plugin takeaway:** from a filter plugin, query the input variables for seek/pause/rate. For pace control (the strongest live-vs-VOD signal) there is no public variable — infer liveness from `can-seek == false` combined with `can-pause == false`, or add a demux probe inside the core.

### Capability Matrix

| Stream Type | `can-seek` (`DEMUX_CAN_SEEK`) | `DEMUX_CAN_CONTROL_PACE` | `can-pause` (`DEMUX_CAN_PAUSE`) | Plugin Behavior |
|---|---|---|---|---|
| **Local File** | `true` | `true` | `true` | Full seeking support, pause/resume timeline sync. |
| **Network VOD (HTTP/MP4)** | `true` | `true` | `true` | Seeking supported; handle buffer stalls via PTS gaps. |
| **HLS / DASH Live Stream** | `false` / bounded | `true` | `true` / bounded | Sliding window timeline; drop stale backlog audio. |
| **IPTV / RTSP Broadcast** | `false` | `false` | `false` | Realtime network pace; no seeking; continuous VAD. |

> **Livestream Rule:** For live IPTV streams (`DEMUX_CAN_CONTROL_PACE == false`; from a plugin, approximated by `can-seek == false` && `can-pause == false`), the plugin must never buffer audio beyond the maximum backlog threshold (8 s; 16 × 512 ms chunks). Stale PCM chunks are dropped, and captions are presented strictly aligned with incoming media PTS.

---

## 6. VLC Object Tree & Vout Retrieval Architecture

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

Because `audio filter` and `vout_thread_t` reside on separate branches under `playlist`, `vlc_object_find_name(p_filter, "vout")` returns `NULL`. The plugin uses tree traversal and the official `input_GetVout()` API (`static inline` in `vlc_input.h`):

> **Deprecation warning:** `vlc_object_find_name()` is declared `VLC_DEPRECATED` in `vlc_objects.h` (3.0.23). Using it emits `-Wdeprecated-declarations` warnings. The real code keeps it as a fast-path (step 3 below) — suppress with `-Wno-deprecated-declarations` or drop step 3; step 4 (type-based child scan) is the reliable fallback.
>
> **Name vs type:** `vlc_object_t` / `struct vlc_common_members` has **no `psz_object_name` field** in 3.0. Names are internal, read via `vlc_object_get_name()`, and `vlc_object_find_name(cur, "input")` matches by *name*, not by type — it only finds objects explicitly named `"input"` (VLC does name input threads this way, which is why step 3 works at all). The type check in step 4 (`child->obj.object_type`) is what actually does the heavy lifting.

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

> **Type-safety caveat:** `VLC_OBJECT(x)` in 3.0 is a `_Generic`-based type-safe cast (`vlc_common.h`) — it verifies the argument embeds `struct vlc_common_members`. The raw `(input_thread_t*)cur` / `(vout_thread_t*)cur` casts after a `strcmp` bypass that safety: they compile and work, but the *only* guard is the string comparison on `object_type`, which is fragile if internal type names ever change.

### Object Reference Counting (`vlc_object_hold` / `vlc_object_release`)

VLC objects (`filter_t`, `vout_thread_t`, `input_thread_t`, …) are lifetime-managed by an **atomic reference count** — the C equivalent of a shared pointer. The counter lives in the private `vlc_object_internals_t`, not in the public struct (`vlc_object_t` is only `VLC_COMMON_MEMBERS`). Declared in `vlc_objects.h`, implemented in `src/misc/objects.c`.

- **`vlc_object_hold(obj)`** — atomically increments the refcount, returns the same object. While held, the object is guaranteed not to be destroyed.
- **`vlc_object_release(obj)`** — atomically decrements the refcount. At zero the object is marked killed, and actual destruction is **deferred to the object's own thread** (VLC objects *are* threads; `vlc_object_delete` runs when that thread exits). This makes release safe from any other thread — e.g. the audio filter callback — because it never frees memory under the caller.

Both are macros wrapping `VLC_OBJECT()` (the type-safe cast), so any object type can be passed directly. The refcount operations are atomic — callable from any thread without external locking.

> **What hold/release do NOT do:** they are not a mutex. They provide **no mutual exclusion** for concurrent access to an object's internals — thread safety of vout calls comes from the vout's own internal locks (e.g. `vout->p->osd.lock`). Hold/release answer one question only: *can this object still be destroyed right now?*

**The ownership convention that matters:** APIs that *hand you* an object return it **already held** — the caller must release it when done. Raw pointers from tree walks are **not** held:

| Source of object | Held for you? | Caller must… |
|---|---|---|
| `input_GetVout(p_input)` (`vlc_input.h`: “needs to be released with `vlc_object_release()`”) | yes | `vlc_object_release(VLC_OBJECT(vout))` when done |
| `vlc_object_find_name(...)` | yes | `vlc_object_release(p_input_obj)` after use |
| `child` pointers from `vlc_list_children(...)` (`children->p_values[i].p_address`) | **no** | `vlc_object_hold(child)` *before* returning it |
| Ancestor walk (`cur = cur->obj.parent`) | **no** | hold before use, release after |

That is exactly why the algorithm above calls `vlc_object_hold(cur)` / `vlc_object_hold(child)` before returning a vout found via the parent chain or children list, and `vlc_object_release(p_input_obj)` after step 3's `vlc_object_find_name`.

> **Why the plugin needs this:** the vout can be stopped and destroyed at any moment — user closes the window, switches media, or VLC exits. Without the hold around `vout_OSDText()`, the audio filter thread would dereference a vout thread object that may have been killed mid-call (use-after-free).

---

## 7. Subtitle & OSD Rendering Subsystem

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

> **Memory Rule:** `input_GetVout()` returns an *already-held* reference, so always call `vlc_object_release(VLC_OBJECT(vout))` immediately after calling `vout_OSDText()` to drop it (see “Object Reference Counting” in Section 6). This is safe from the audio filter thread — destruction of the vout is deferred to the vout's own thread, so the memory is never freed under the caller.

**Silent no-ops** — `vout_OSDText()` returns `void` and fails silently in two cases, so a missing overlay is not a bug in the caller:

- The user's `osd` setting is disabled (checked via `var_InheritBool(vout, "osd")` in `video_text.c`).
- `duration <= 0` (no display lifetime).

**No buffer lifetime requirement:** the implementation copies the text (`strdup`) into the subpicture text region, so the caller's `const char*` may be freed or reused immediately after the call returns.

For persistent, styled, or refresh-driven overlays (e.g. a live caption track), the more capable pattern is a subpicture **filter** source: `filter_NewSubpicture()` + `subpicture_region_NewText()` + `text_segment_New()`, with `b_ephemer` and `i_start`/`i_stop` timing — see the canonical `marq.c` (`modules/spu/marq.c`) implementation, which context7 surfaced as the reference pattern.

---

## 8. Module Registration & ABI Invariants

### VLC Module Entry Macros

Every VLC plugin module exports a entry point macro (`vlc_module_begin` ... `vlc_module_end`) that expands to the `vlc_entry__3_0_0f` ABI symbol required by VLC's plugin loader:

```c
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
vlc_module_begin()
    set_shortname("VLC-Whisper")
    set_description("Offline Whisper AI Captions Filter")
    set_capability("audio filter", 0)
    add_shortcut("vlc_whisper", "whisper")
    set_callbacks(vw_plugin_open, vw_plugin_close)
vlc_module_end()
#pragma GCC diagnostic pop
```

| Macro | Description |
|---|---|
| `set_shortname` | Short human-readable identifier for VLC settings UI. |
| `set_description` | Full plugin description displayed in VLC module lists. |
| `set_capability` | Category string (`"audio filter"`) and priority score (`0`). |
| `add_shortcut` | Command line flags (`--audio-filter=vlc_whisper`). |
| `add_loadfile` | Registers the `worker-path` string option consumed via `config_GetPsz()` for the worker executable location override. |
| `set_callbacks` | Initialization (`open`) and cleanup (`close`) function pointers. |

### Worker executable discovery

The plugin locates `vlc-whisper-worker[.exe]` in this order:

1. `config_GetPsz(obj, "worker-path")` — explicit override (CLI `--vlc-whisper-worker-path`, or the module prefs).
2. Next to the plugin module (`dladdr` on POSIX / `GetModuleHandleEx` + `GetModuleFileNameA` on Windows), walking up to 4 ancestor directories.
3. Next to the VLC executable (Linux `/proc/self/exe`, Windows `GetModuleFileNameA(NULL)`).

If none of those produce an existing file, the plugin falls back to a bare `vlc-whisper-worker[.exe]` name. On POSIX that is resolved through `PATH` via `posix_spawnp` — never relative to VLC's current working directory — so a worker installed on `PATH` starts even when VLC launches from an arbitrary directory.
