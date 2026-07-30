# VLC Object Tree & Vout Retrieval Architecture

## 1. Overview & Context

In VLC 3.0, all modules, threads, and filters are represented as **VLC objects** (`vlc_object_t`). VLC structures these objects into an in-memory parent-child tree.

The `vlc-whisper` plugin is loaded as an **audio filter** (`capability "audio filter"`). While its primary job is tapping PCM audio from the audio output (`aout`) pipeline, it must also schedule and render timed caption text onto the user's video display (`vout_thread_t`).

Because audio filters and video outputs reside on separate branches of VLC's object hierarchy, navigating the object tree safely and using VLC's official `input_GetVout()` API is critical.

---

## 2. VLC Object Tree Hierarchy

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

### Key Architectural Invariants
1. **Branch Isolation**: The audio output filter (`filter_t`) is a descendant of `audio output`. The video renderer (`vout_thread_t`) is managed by the `input_thread` (or directly under `playlist`).
2. **Sibling Search Limit**: `vlc_object_find_name(p_filter, "vout")` only searches immediate siblings/children of `p_filter`. Calling it on an audio filter returns `NULL` because `vout` is on a parallel branch.
3. **Parent Hierarchy**: Every `vlc_object_t` contains pointers to its `parent` (`obj.parent`) and root instance (`obj.libvlc`).

---

## 3. Vout Retrieval Strategy (`input_GetVout`)

VLC provides an official, thread-safe helper function in `<vlc_input.h>`:

```c
vout_thread_t *input_GetVout(input_thread_t *p_input);
```

`input_GetVout` issues an `INPUT_GET_VOUTS` control query to `input_thread_t`, retrieving a held reference to the active `vout_thread_t`.

### Traversal Algorithm

To locate `input_thread_t` from an audio filter `filter_t *p_filter`:

```c
static vout_thread_t* vw_caption_presenter_find_vout(filter_t* p_filter) {
  if (!p_filter) {
    return NULL;
  }

  // 1. Walk up the parent chain (audio filter -> audio output -> playlist -> libvlc)
  vlc_object_t* cur = VLC_OBJECT(p_filter);
  while (cur) {
    if (cur->obj.object_type) {
      // Direct ancestor match
      if (strcmp(cur->obj.object_type, "input") == 0) {
        vout_thread_t* vout = input_GetVout((input_thread_t*)cur);
        if (vout) return vout;
      } else if (strcmp(cur->obj.object_type, "vout") == 0) {
        vlc_object_hold(cur);
        return (vout_thread_t*)cur;
      }

      // Check for "input" child under ancestor (e.g. under playlist)
      vlc_object_t* p_input_obj = vlc_object_find_name(cur, "input");
      if (p_input_obj) {
        vout_thread_t* vout = input_GetVout((input_thread_t*)p_input_obj);
        vlc_object_release(p_input_obj);
        if (vout) return vout;
      }

      // Fallback: search child list of ancestor for input or vout nodes
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

## 4. Reference Counting & Thread Safety

1. **Object Holding**: `input_GetVout()` and `vlc_object_hold()` increment the target `vout_thread_t` reference counter.
2. **Object Release**: Any code that retrieves a `vout_thread_t*` **must** invoke `vlc_object_release(VLC_OBJECT(vout))` when finished to prevent memory leaks and dangling pointers.
3. **Thread Safety**: `input_GetVout()` locks the `input_thread_t` mutex. Because caption presentation runs periodically (not per-audio sample), calling `input_GetVout()` introduces zero latency jitter to the real-time VLC audio callback.

---

## 5. Caption Presentation (`vout_OSDText`)

Once `vout_thread_t *vout` is acquired, text is presented using VLC's built-in OSD function:

```c
vout_OSDText(vout, 1, SUBPICTURE_ALIGN_BOTTOM, (vlc_tick_t)duration_us, text);
vlc_object_release(VLC_OBJECT(vout));
```

- **Channel**: `1` (default OSD text overlay channel).
- **Position**: `SUBPICTURE_ALIGN_BOTTOM` (aligned bottom-center).
- **Duration**: Timed duration in microseconds (`vlc_tick_t`).
