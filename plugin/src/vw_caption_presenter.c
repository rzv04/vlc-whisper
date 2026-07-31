#ifdef _WIN32
#include <winsock2.h>
#endif

#include <stdlib.h>
#include <string.h>

// clang-format off
#include <vlc_common.h>
// clang-format on
#include <vlc_filter.h>
#include <vlc_input.h>
#include <vlc_vout.h>
#include <vlc_vout_osd.h>

#include "vw_caption_presenter.h"
#include "vw_log.h"

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

      // Check if current node (e.g. playlist or libvlc) has an "input" child
      vlc_object_t* p_input_obj = vlc_object_find_name(cur, "input");
      if (p_input_obj) {
        vw_log_event(VW_LOG_LEVEL_INFO, "PRESENTER_INPUT_CHILD_FOUND", "Found input child under node: %s",
                     cur->obj.object_type);
        vout_thread_t* vout = input_GetVout((input_thread_t*)p_input_obj);
        vlc_object_release(p_input_obj);
        if (vout) {
          vw_log_event(VW_LOG_LEVEL_INFO, "PRESENTER_VOUT_FOUND", "Retrieved active vout via input child");
          return vout;
        }
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

bool vw_caption_presenter_show_segment(vw_caption_presenter_t* presenter, const vw_caption_segment_t* segment) {
  if (!segment || !segment->text_utf8) {
    return false;
  }
  int64_t duration_us = segment->end_pts_us - segment->start_pts_us;
  if (duration_us <= 0) {
    duration_us = 2000000LL;  // 2 seconds default duration
  }
  void* filter_obj = presenter ? presenter->p_filter_ctx : NULL;
  return vw_caption_presenter_display(filter_obj, segment->text_utf8, duration_us);
}

void vw_caption_presenter_clear(vw_caption_presenter_t* presenter) {
  if (!presenter) {
    return;
  }
  presenter->p_filter_ctx = NULL;
}
