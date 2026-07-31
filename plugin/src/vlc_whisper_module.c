#ifdef _WIN32
#include <winsock2.h>
#endif

// clang-format off
#include <vlc_common.h>
#include <vlc_block.h>
// clang-format on
#include <vlc_filter.h>
#include <vlc_plugin.h>

#include "vw_log.h"
#include "vw_plugin.h"

// Implements callback signature matching vw_log_sink_fn from protocol/include/vw_log.h
static void vw_plugin_log_sink(vw_log_level_t level, const char* event_id, const char* formatted_msg, void* user_data) {
  vlc_object_t* obj = (vlc_object_t*)user_data;
  if (!obj) {
    return;
  }
  switch (level) {
    case VW_LOG_LEVEL_ERROR:
      msg_Err(obj, "[vw_log:%s] %s", event_id, formatted_msg);
      break;
    case VW_LOG_LEVEL_WARN:
      msg_Warn(obj, "[vw_log:%s] %s", event_id, formatted_msg);
      break;
    case VW_LOG_LEVEL_INFO:
    case VW_LOG_LEVEL_DEBUG:
    default:
      msg_Dbg(obj, "[vw_log:%s] %s", event_id, formatted_msg);
      break;
  }
}

#include "vw_audio_capture.h"
#include "vw_caption_presenter.h"
#include "vw_queue.h"

// Plugin instance state
typedef struct {
  vw_spsc_queue_t* queue;
  vw_audio_capture_t capture;
  uint64_t block_count;
} vw_plugin_sys_t;

// Passthrough filter callback required by VLC filter pipeline
static block_t* vw_plugin_filter(filter_t* p_filter, block_t* p_block) {
  vw_plugin_sys_t* sys = (vw_plugin_sys_t*)p_filter->p_sys;
  if (!sys || !p_block) {
    return p_block;  // Passthrough unchanged
  }

  // Determine format from VLC pipeline
  vw_audio_format_t fmt;
  if (p_filter->fmt_in.audio.i_format == VLC_CODEC_FL32) {
    fmt = VW_AUDIO_FORMAT_FL32;
  } else if (p_filter->fmt_in.audio.i_format == VLC_CODEC_S16N) {
    fmt = VW_AUDIO_FORMAT_S16;
  } else if (p_filter->fmt_in.audio.i_format == VLC_CODEC_S32N) {
    fmt = VW_AUDIO_FORMAT_S32;
  } else {
    // Unsupported codec; skip tapping but allow passthrough
    return p_block;
  }

  vw_audio_input_t input = {.pcm_data = p_block->p_buffer,
                            .frame_count = p_block->i_nb_samples,
                            .pts_us = p_block->i_pts,  // VLC PTS is in microseconds
                            .format = fmt,
                            .sample_rate = p_filter->fmt_in.audio.i_rate,
                            .channels = p_filter->fmt_in.audio.i_channels};

  vw_audio_capture_process_block(&sys->capture, &input);

  sys->block_count++;
  if (sys->block_count % 100 == 1) {
    vw_caption_presenter_display(p_filter, "[VLC-Whisper] Live AI Captions Active", 2000000LL);
  }

  // Return the original block untouched to preserve user playback quality
  return p_block;
}

// Internal plugin callbacks adhering to Rule 3 vw_ prefix
static int vw_plugin_open(vlc_object_t* obj) {
  filter_t* p_filter = (filter_t*)obj;

  vw_plugin_sys_t* sys = calloc(1, sizeof(vw_plugin_sys_t));
  if (!sys) {
    return VLC_ENOMEM;
  }

  // Create an 8-second buffer capacity assuming 16kHz Mono chunks (16384 bytes = 512ms per chunk)
  // 8 seconds / 0.512 seconds = ~16 chunks
  sys->queue = vw_spsc_queue_create(16);
  if (!sys->queue) {
    free(sys);
    return VLC_ENOMEM;
  }

  sys->capture.target_sample_rate = VW_AUDIO_TARGET_RATE;
  sys->capture.target_channels = 1;
  sys->capture.queue = sys->queue;

  p_filter->p_sys = (filter_sys_t*)sys;
  p_filter->pf_audio_filter = vw_plugin_filter;
  p_filter->fmt_out.audio = p_filter->fmt_in.audio;

  vw_log_set_sink(vw_plugin_log_sink, obj);
  vw_log_event(VW_LOG_LEVEL_INFO, "PLUGIN_OPEN", "vlc-whisper audio filter module opened");
  return VLC_SUCCESS;
}

static void vw_plugin_close(vlc_object_t* obj) {
  filter_t* p_filter = (filter_t*)obj;
  vw_plugin_sys_t* sys = (vw_plugin_sys_t*)p_filter->p_sys;

  if (sys) {
    if (sys->queue) {
      vw_log_event(VW_LOG_LEVEL_INFO, "PLUGIN_CLOSE", "Dropped %llu us of audio during session",
                   (unsigned long long)vw_spsc_queue_get_dropped_microseconds(sys->queue));
      vw_spsc_queue_destroy(sys->queue);
    }
    free(sys);
  }

  vw_log_event(VW_LOG_LEVEL_INFO, "PLUGIN_CLOSE", "vlc-whisper audio filter module closed");
  vw_log_set_sink(NULL, NULL);
}

// VLC module definition macro expands to vlc_entry__3_0_0f ABI symbol
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
vlc_module_begin() set_shortname("VLC-Whisper") set_description("Offline Whisper AI Captions Filter")
    set_capability("audio filter", 0) add_shortcut("vlc_whisper", "whisper")
        set_callbacks(vw_plugin_open, vw_plugin_close) vlc_module_end()
#pragma GCC diagnostic pop
