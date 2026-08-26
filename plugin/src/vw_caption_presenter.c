#ifdef _WIN32
#include <winsock2.h>
#endif

#include <stdio.h>
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
#include "vw_protocol_util.h"

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

static bool vw_caption_presenter_render_spu(vout_thread_t* vout, int channel_id, const char* text, int alignment, int y,
                                            int64_t start_tick, int64_t stop_tick, bool replace_existing) {
  if (!vout || !text || channel_id < 0) {
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

  region->i_align = alignment;
  region->i_text_align = alignment;
  region->i_x = 0;
  region->i_y = y;

  subpic->p_region = region;
  subpic->i_channel = channel_id;
  subpic->i_start = (vlc_tick_t)start_tick;
  subpic->i_stop = (vlc_tick_t)stop_tick;
  // Render in the OSD clock domain: b_subtitle=false selects render_osd_date = mdate(), the
  // clock this VLC 3.0.23 build demonstrably renders filter-pushed subpictures against (the
  // subtitle clock render_subtitle_date is the displayed picture PTS and, per live testing,
  // does not select these subpictures — they are dropped before region rendering). i_start/
  // i_stop must therefore be mdate-based. Media-domain scheduling (b_subtitle=true) is the
  // 17c look-ahead target, blocked on observing the subtitle clock's behavior.
  subpic->b_subtitle = false;
  subpic->b_ephemer = true;
  subpic->b_absolute = false;
  subpic->b_fade = true;

  if (replace_existing) {
    // Both operations enter VLC's ordered vout control queue, so the prior live cue is rejected before replacement.
    vout_FlushSubpictureChannel(vout, channel_id);
  }
  vout_PutSubpicture(vout, subpic);
  return true;
}

static bool vw_caption_presenter_register_model_progress_channel(vw_caption_presenter_t* presenter,
                                                                 vout_thread_t* vout) {
  if (!presenter || !vout) {
    return false;
  }
  if (presenter->model_progress_channel_registered && presenter->p_model_progress_held_vout == (void*)vout) {
    return true;
  }

  if (presenter->p_model_progress_held_vout) {
    if (presenter->model_progress_channel_registered && presenter->model_progress_channel_id >= 0) {
      vout_FlushSubpictureChannel((vout_thread_t*)presenter->p_model_progress_held_vout,
                                  presenter->model_progress_channel_id);
    }
    vlc_object_release(VLC_OBJECT((vout_thread_t*)presenter->p_model_progress_held_vout));
    presenter->p_model_progress_held_vout = NULL;
  }

  int channel_id = vout_RegisterSubpictureChannel(vout);
  if (channel_id < 0) {
    presenter->model_progress_channel_id = -1;
    presenter->model_progress_channel_registered = false;
    return false;
  }

  vlc_object_hold(VLC_OBJECT(vout));
  presenter->p_model_progress_held_vout = (void*)vout;
  presenter->model_progress_channel_id = channel_id;
  presenter->model_progress_channel_registered = true;
  return true;
}

bool vw_caption_presenter_show_model_progress(vw_caption_presenter_t* presenter,
                                              const vw_msg_model_progress_t* progress) {
  if (!presenter || !presenter->p_filter_ctx || !progress) {
    return false;
  }

  vout_thread_t* vout = vw_caption_presenter_find_vout((filter_t*)presenter->p_filter_ctx);
  if (!vout || !vw_caption_presenter_register_model_progress_channel(presenter, vout)) {
    if (vout) {
      vlc_object_release(VLC_OBJECT(vout));
    }
    return false;
  }

  const char* stage = "idle";
  switch (progress->stage) {
    case VW_MODEL_STAGE_DOWNLOADING:
      stage = "downloading";
      break;
    case VW_MODEL_STAGE_VERIFYING:
      stage = "verifying";
      break;
    case VW_MODEL_STAGE_DONE:
      stage = "done";
      break;
    case VW_MODEL_STAGE_FAILED:
      stage = "failed";
      break;
    case VW_MODEL_STAGE_ABORTING:
      stage = "aborting";
      break;
    default:
      break;
  }
  int progress_percent = progress->pct > 100 ? 100 : progress->pct;
  char progress_text[128];
  snprintf(progress_text, sizeof(progress_text), "Model %s: %s (%d%%)", progress->model_id, stage, progress_percent);

  int64_t start_tick = (int64_t)mdate();
  bool rendered = vw_caption_presenter_render_spu(
      vout, presenter->model_progress_channel_id, progress_text, SUBPICTURE_ALIGN_TOP, 20, start_tick,
      vw_saturating_add_i64(start_tick, VW_MODEL_PROGRESS_DISPLAY_DURATION_US), false);
  vlc_object_release(VLC_OBJECT(vout));
  return rendered;
}

void vw_caption_presenter_clear_model_progress(vw_caption_presenter_t* presenter) {
  if (!presenter) {
    return;
  }
  if (presenter->p_model_progress_held_vout) {
    if (presenter->model_progress_channel_registered && presenter->model_progress_channel_id >= 0) {
      vout_FlushSubpictureChannel((vout_thread_t*)presenter->p_model_progress_held_vout,
                                  presenter->model_progress_channel_id);
    }
    vlc_object_release(VLC_OBJECT((vout_thread_t*)presenter->p_model_progress_held_vout));
  }
  presenter->p_model_progress_held_vout = NULL;
  presenter->model_progress_channel_id = -1;
  presenter->model_progress_channel_registered = false;
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
  if (duration_us < VW_CAPTION_MIN_DISPLAY_DURATION_US) {
    duration_us = VW_CAPTION_MIN_DISPLAY_DURATION_US;
  }
  filter_t* p_filter = (filter_t*)p_filter_ptr;
  if (!p_filter) {
    // Standalone unit test mode without live VLC object hierarchy
    return true;
  }

  return vw_caption_presenter_render_text(p_filter, text, duration_us);
}

static float vw_caption_presenter_get_rate(vw_caption_presenter_t* presenter) {
  if (!presenter || !presenter->p_filter_ctx) {
    return 1.0f;
  }
  vlc_object_t* obj = (vlc_object_t*)presenter->p_filter_ctx;
  vlc_value_t rval;
  while (obj) {
    if (var_Get(obj, "rate", &rval) == VLC_SUCCESS && rval.f_float > 0.05f) {
      return rval.f_float;
    }
    obj = obj->obj.parent;
  }
  return 1.0f;
}

static bool vw_caption_presenter_render_internal(vw_caption_presenter_t* presenter, const vw_caption_segment_t* segment,
                                                 int64_t duration_us, int64_t input_time_us, bool media_timeline) {
  if (!presenter || !segment || !segment->text_utf8) {
    return false;
  }

  float rate = vw_caption_presenter_get_rate(presenter);

  if (!presenter->p_filter_ctx) {
    // Standalone unit test mode without live VLC object hierarchy
    return vw_caption_presenter_display(NULL, segment->text_utf8, duration_us);
  }

  filter_t* p_filter = (filter_t*)presenter->p_filter_ctx;
  vout_thread_t* vout = vw_caption_presenter_find_vout(p_filter);
  if (!vout) {
    return false;
  }

  // Register or re-register SPU channel whenever vout instance changes (e.g. video resize/recreate) or is unregistered
  if (!presenter->spu_channel_registered || presenter->p_held_vout != (void*)vout || presenter->spu_channel_id < 0) {
    if (presenter->p_held_vout) {
      vlc_object_release(VLC_OBJECT((vout_thread_t*)presenter->p_held_vout));
      presenter->p_held_vout = NULL;
    }
    int channel_id = vout_RegisterSubpictureChannel(vout);
    if (channel_id >= 0) {
      vlc_object_hold(VLC_OBJECT(vout));
      presenter->p_held_vout = (void*)vout;
      presenter->spu_channel_id = channel_id;
      presenter->spu_channel_registered = true;
      vw_log_event(VW_LOG_LEVEL_INFO, "PRESENTER_SPU_REGISTERED", "Registered SPU subpicture channel %d on vout %p",
                   channel_id, (void*)vout);
    } else {
      presenter->spu_channel_id = -1;
      presenter->spu_channel_registered = false;
      presenter->p_held_vout = NULL;
      vw_log_event(VW_LOG_LEVEL_WARN, "PRESENTER_SPU_FAILED",
                   "Failed to register SPU channel on vout %p (%d); falling back to OSD", (void*)vout, channel_id);
    }
  }

  bool rendered = false;
  int64_t start_tick = 0;
  int64_t stop_tick = 0;
  int64_t dur_wallclock_us = (int64_t)((double)duration_us / (double)rate);

  if (presenter->spu_channel_registered && presenter->spu_channel_id >= 0) {
    int64_t now_tick = (int64_t)mdate();
    int64_t lead_us = 0;
    if (media_timeline && input_time_us >= 0 && segment->start_pts_us > input_time_us) {
      int64_t diff = vw_saturating_sub_i64(segment->start_pts_us, input_time_us);
      lead_us = (int64_t)((double)diff / (double)rate);
      if (lead_us > 60000000LL) {
        lead_us = 60000000LL;  // Cap at 60s max wall-clock lead horizon
      }
    }
    start_tick = vw_saturating_add_i64(now_tick, lead_us);
    stop_tick = vw_saturating_add_i64(start_tick, dur_wallclock_us);
    rendered = vw_caption_presenter_render_spu(vout, presenter->spu_channel_id, segment->text_utf8,
                                               SUBPICTURE_ALIGN_BOTTOM, 20, start_tick, stop_tick, !media_timeline);
  }

  // Graceful fallback to OSD if SPU rendering failed or was unregistered
  if (!rendered) {
    vout_OSDText(vout, 1, SUBPICTURE_ALIGN_BOTTOM, (vlc_tick_t)dur_wallclock_us, segment->text_utf8);
    rendered = true;
    vw_log_event(VW_LOG_LEVEL_INFO, "PRESENTER_OSD_RENDER",
                 "Rendered caption via OSD fallback (text_len=%zu duration=%lldus)",
                 segment->text_utf8 ? strlen(segment->text_utf8) : 0, (long long)dur_wallclock_us);
  } else {
    vw_log_event(VW_LOG_LEVEL_INFO, "PRESENTER_SPU_RENDER",
                 "Rendered caption on SPU ch=%d vout=%p (text_len=%zu start=%lldus stop=%lldus)",
                 presenter->spu_channel_id, (void*)vout, segment->text_utf8 ? strlen(segment->text_utf8) : 0,
                 (long long)start_tick, (long long)stop_tick);
  }

  vlc_object_release(VLC_OBJECT(vout));
  return rendered;
}

bool vw_caption_presenter_show_segment(vw_caption_presenter_t* presenter, const vw_caption_segment_t* segment,
                                       int64_t input_time_us, bool media_timeline) {
  if (!presenter || !segment || !segment->text_utf8) {
    return false;
  }

  float rate = vw_caption_presenter_get_rate(presenter);
  int64_t min_media_floor_us = (int64_t)((double)VW_CAPTION_MIN_DISPLAY_DURATION_US * (double)rate);

  // If a preceding cue was buffered, dispatch it now with duration clipped to the incoming segment's start PTS
  if (presenter->has_pending) {
    int64_t raw_duration_us = presenter->pending_segment.end_pts_us - presenter->pending_segment.start_pts_us;
    int64_t target_dur_us = (raw_duration_us <= 0)                   ? 2000000LL
                            : (raw_duration_us < min_media_floor_us) ? min_media_floor_us
                                                                     : raw_duration_us;
    int64_t target_end_us = presenter->pending_segment.start_pts_us + target_dur_us;

    // Clip target_end_us to incoming segment start if incoming cue starts after pending cue's start
    int64_t clipped_end_us =
        (segment->start_pts_us > presenter->pending_segment.start_pts_us && target_end_us > segment->start_pts_us)
            ? segment->start_pts_us
            : target_end_us;
    int64_t duration_us = clipped_end_us - presenter->pending_segment.start_pts_us;
    if (duration_us <= 0) {
      duration_us = (raw_duration_us > 0) ? raw_duration_us : 2000000LL;
    }

    vw_caption_presenter_render_internal(presenter, &presenter->pending_segment, duration_us, input_time_us,
                                         media_timeline);
    presenter->has_pending = false;
  }

  // Buffer incoming segment as pending so its display duration can be clipped by any successor cue
  presenter->has_pending = true;
  presenter->pending_segment = *segment;
  strncpy(presenter->pending_text, segment->text_utf8, sizeof(presenter->pending_text) - 1);
  presenter->pending_text[sizeof(presenter->pending_text) - 1] = '\0';
  presenter->pending_segment.text_utf8 = presenter->pending_text;
  presenter->pending_segment.text_bytes = (uint16_t)strlen(presenter->pending_text);

  return true;
}

bool vw_caption_presenter_flush(vw_caption_presenter_t* presenter, int64_t input_time_us, bool media_timeline) {
  if (!presenter || !presenter->has_pending) {
    return false;
  }

  float rate = vw_caption_presenter_get_rate(presenter);
  int64_t min_media_floor_us = (int64_t)((double)VW_CAPTION_MIN_DISPLAY_DURATION_US * (double)rate);
  int64_t raw_duration_us = presenter->pending_segment.end_pts_us - presenter->pending_segment.start_pts_us;
  int64_t duration_us = (raw_duration_us <= 0)                   ? 2000000LL
                        : (raw_duration_us < min_media_floor_us) ? min_media_floor_us
                                                                 : raw_duration_us;

  bool rendered = vw_caption_presenter_render_internal(presenter, &presenter->pending_segment, duration_us,
                                                       input_time_us, media_timeline);
  presenter->has_pending = false;
  return rendered;
}

// Blanks the current caption overlays (flushes SPU and OSD channels) but KEEPS the filter context,
// so later segments still render. Safe mid-session — e.g. erase captions on a seek before the
// restarted session emits new ones. No-op when the presenter has no filter context.
#define VW_OSD_BLANK_DURATION_US 1000
void vw_caption_presenter_blank(vw_caption_presenter_t* presenter) {
  if (presenter) {
    presenter->has_pending = false;
  }
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
  vw_caption_presenter_clear_model_progress(presenter);
  vw_caption_presenter_blank(presenter);
  if (presenter) {
    presenter->has_pending = false;
    if (presenter->p_held_vout) {
      vlc_object_release(VLC_OBJECT((vout_thread_t*)presenter->p_held_vout));
      presenter->p_held_vout = NULL;
    }
    presenter->p_filter_ctx = NULL;
    presenter->spu_channel_id = -1;
    presenter->spu_channel_registered = false;
  }
}
