#ifdef _WIN32
#include <winsock2.h>
#endif

#include <stdlib.h>
#include <string.h>

// clang-format off
#include <vlc_common.h>
// clang-format on
#include <vlc_es.h>
#include <vlc_filter.h>
#include <vlc_input.h>
#include <vlc_subpicture.h>
#include <vlc_text_style.h>
#include <vlc_threads.h>
#include <vlc_vout.h>
#include <vlc_vout_osd.h>

#include "vw_caption_presenter.h"
#include "vw_log.h"
#include "vw_platform.h"

// Walk parent chain and children list to find input_thread and retrieve held vout reference
static vout_thread_t* vw_caption_presenter_find_vout(filter_t* p_filter) {
  if (!p_filter) {
    return NULL;
  }

  vlc_object_t* cur = VLC_OBJECT(p_filter);
  while (cur) {
    if (cur->obj.object_type) {
      vw_log_event(VW_LOG_LEVEL_DEBUG, "PRESENTER_PARENT_WALK", "Inspecting object_type: %s", cur->obj.object_type);

      // Check if current node is directly input or vout
      if (strcmp(cur->obj.object_type, "input") == 0) {
        vout_thread_t* vout = input_GetVout((input_thread_t*)cur);
        if (vout) {
          vw_log_event(VW_LOG_LEVEL_INFO, "PRESENTER_VOUT_FOUND", "Retrieved vout directly from input_thread ancestor");
          return vout;
        }
      } else if (strcmp(cur->obj.object_type, "vout") == 0) {
        vlc_object_hold(cur);
        return (vout_thread_t*)cur;
      }

      // Search children list of current node for input or vout
      vlc_list_t* children = vlc_list_children(cur);
      if (children) {
        for (int i = 0; i < children->i_count; i++) {
          vlc_object_t* child = (vlc_object_t*)children->p_values[i].p_address;
          if (child && child->obj.object_type) {
            if (strcmp(child->obj.object_type, "input") == 0) {
              vout_thread_t* vout = input_GetVout((input_thread_t*)child);
              if (vout) {
                vlc_list_release(children);
                vw_log_event(VW_LOG_LEVEL_INFO, "PRESENTER_VOUT_FOUND",
                             "Retrieved active vout via children list input");
                return vout;
              }
            } else if (strcmp(child->obj.object_type, "vout") == 0) {
              vlc_object_hold(child);
              vlc_list_release(children);
              vw_log_event(VW_LOG_LEVEL_INFO, "PRESENTER_VOUT_FOUND",
                           "Retrieved active vout directly via children list");
              return (vout_thread_t*)child;
            }
          }
        }
        vlc_list_release(children);
      }
    }
    cur = cur->obj.parent;
  }

  vw_log_event(VW_LOG_LEVEL_WARN, "PRESENTER_VOUT_NOT_FOUND", "Could not locate active vout from filter hierarchy");
  return NULL;
}

static bool vw_caption_presenter_render_spu(vw_caption_presenter_t* presenter, vout_thread_t* vout, const char* text,
                                            int64_t start_tick, int64_t stop_tick) {
  if (!presenter || !vout || !text || presenter->spu_channel_id < 0) {
    return false;
  }

  subpicture_t* subpic = subpicture_New(NULL);
  if (!subpic) {
    return false;
  }

  video_format_t fmt;
  video_format_Init(&fmt, VLC_CODEC_TEXT);
  fmt.i_sar_num = 1;
  fmt.i_sar_den = 1;
  subpicture_region_t* region = subpicture_region_New(&fmt);
  if (!region) {
    subpicture_Delete(subpic);
    return false;
  }

  region->p_text = text_segment_New(text);
  if (!region->p_text) {
    subpicture_region_Delete(region);
    subpicture_Delete(subpic);
    return false;
  }

  region->i_align = SUBPICTURE_ALIGN_BOTTOM;
  region->i_text_align = SUBPICTURE_ALIGN_BOTTOM;
  region->i_x = 0;
  region->i_y = 20;

  subpic->p_region = region;
  subpic->i_channel = presenter->spu_channel_id;
  subpic->i_start = (vlc_tick_t)start_tick;
  subpic->i_stop = (vlc_tick_t)stop_tick;
  subpic->b_subtitle = false;  // Uses system clock (mdate) on private SPU channel without requiring subtitle ES track
  subpic->b_ephemer = true;
  subpic->b_absolute = false;
  subpic->b_fade = true;

  vout_PutSubpicture(vout, subpic);
  return true;
}

static bool vw_caption_presenter_render_text(filter_t* p_filter, const char* text, int64_t duration_us) {
  vout_thread_t* vout = vw_caption_presenter_find_vout(p_filter);
  if (!vout) {
    return false;
  }

  vout_OSDText(vout, 1, SUBPICTURE_ALIGN_BOTTOM, (vlc_tick_t)duration_us, text);
  vlc_object_release(VLC_OBJECT(vout));
  return true;
}

bool vw_caption_presenter_display(void* p_filter_ptr, const char* text, int64_t duration_us) {
  if (!text || duration_us <= 0) {
    return false;
  }
  filter_t* p_filter = (filter_t*)p_filter_ptr;
  if (!p_filter) {
    // Standalone unit test mode without live VLC object hierarchy
    return true;
  }

  return vw_caption_presenter_render_text(p_filter, text, duration_us);
}

bool vw_caption_presenter_show_segment(vw_caption_presenter_t* presenter, const vw_caption_segment_t* segment,
                                       int64_t input_time_us) {
  if (!segment || !segment->text_utf8) {
    return false;
  }
  int64_t duration_us = segment->end_pts_us - segment->start_pts_us;
  if (duration_us <= 0) {
    duration_us = 2000000LL;  // 2 seconds default duration
  }

  if (!presenter || !presenter->p_filter_ctx) {
    // Standalone unit test mode without live VLC object hierarchy
    return vw_caption_presenter_display(NULL, segment->text_utf8, duration_us);
  }

  filter_t* p_filter = (filter_t*)presenter->p_filter_ctx;
  vout_thread_t* vout = vw_caption_presenter_find_vout(p_filter);
  if (!vout) {
    return false;
  }

  // Register SPU channel on first vout acquisition
  if (!presenter->spu_channel_registered) {
    int channel_id = vout_RegisterSubpictureChannel(vout);
    if (channel_id >= 0) {
      presenter->spu_channel_id = channel_id;
      presenter->spu_channel_registered = true;
      vw_log_event(VW_LOG_LEVEL_INFO, "PRESENTER_SPU_REGISTERED", "Registered SPU subpicture channel %d", channel_id);
    } else {
      presenter->spu_channel_id = -1;
      presenter->spu_channel_registered = false;
      vw_log_event(VW_LOG_LEVEL_WARN, "PRESENTER_SPU_FAILED",
                   "Failed to register SPU channel (%d); falling back to OSD", channel_id);
    }
  }

  bool rendered = false;
  if (presenter->spu_channel_registered && presenter->spu_channel_id >= 0) {
    // System-to-media timeline conversion:
    // start_tick = mdate() + (segment->start_pts_us - input_time_us)
    // stop_tick  = mdate() + (segment->end_pts_us - input_time_us)
    int64_t now_tick = (int64_t)mdate();
    int64_t start_tick;
    int64_t stop_tick;
    if (input_time_us >= 0) {
      start_tick = now_tick + (segment->start_pts_us - input_time_us);
      stop_tick = now_tick + (segment->end_pts_us - input_time_us);
    } else {
      start_tick = now_tick;
      stop_tick = now_tick + duration_us;
    }
    // Guard: if stop_tick is in the past (e.g. batch inference latency), clamp to current time
    if (stop_tick <= now_tick) {
      start_tick = now_tick;
      stop_tick = now_tick + (duration_us > 0 ? duration_us : 2000000LL);
    }
    rendered = vw_caption_presenter_render_spu(presenter, vout, segment->text_utf8, start_tick, stop_tick);
  }

  // Graceful fallback to OSD if SPU rendering failed or was unregistered
  if (!rendered) {
    vout_OSDText(vout, 1, SUBPICTURE_ALIGN_BOTTOM, (vlc_tick_t)duration_us, segment->text_utf8);
    rendered = true;
  }

  vlc_object_release(VLC_OBJECT(vout));
  return rendered;
}

// Blanks the current caption overlays (flushes SPU and OSD channels) but KEEPS the filter context,
// so later segments still render. Safe mid-session — e.g. erase captions on a seek before the
// restarted session emits new ones. No-op when the presenter has no filter context.
#define VW_OSD_BLANK_DURATION_US 1000
void vw_caption_presenter_blank(vw_caption_presenter_t* presenter) {
  if (!presenter || !presenter->p_filter_ctx) {
    return;
  }
  filter_t* p_filter = (filter_t*)presenter->p_filter_ctx;
  vout_thread_t* vout = vw_caption_presenter_find_vout(p_filter);
  if (vout) {
    if (presenter->spu_channel_registered && presenter->spu_channel_id >= 0) {
      vout_FlushSubpictureChannel(vout, presenter->spu_channel_id);
    }
    vout_FlushSubpictureChannel(vout, 1);
    vout_OSDText(vout, 1, SUBPICTURE_ALIGN_BOTTOM, VW_OSD_BLANK_DURATION_US, "");
    vlc_object_release(VLC_OBJECT(vout));
  }
}

// Clears active caption overlays AND resets the presenter context (p_filter_ctx = NULL).
// Teardown-only: after this, show_segment/blank become no-ops, so call only when the module
// (and the filter context it holds) is going away — never mid-session. Called only from
// vw_plugin_close (module teardown); mid-session clears use vw_caption_presenter_blank.
void vw_caption_presenter_clear(vw_caption_presenter_t* presenter) {
  vw_caption_presenter_blank(presenter);
  if (presenter) {
    presenter->p_filter_ctx = NULL;
    presenter->spu_channel_id = -1;
    presenter->spu_channel_registered = false;
  }
}
