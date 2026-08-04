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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <process.h>
#include <windows.h>
#else
#include <sys/types.h>
#include <unistd.h>
#endif

#include "vw_audio_capture.h"
#include "vw_caption_presenter.h"
#include "vw_platform.h"
#include "vw_queue.h"
#include "vw_worker_client.h"

// Plugin instance state
typedef struct {
  vw_spsc_queue_t* queue;
  vw_audio_capture_t capture;
  vw_worker_client_t* client;

  char pipe_name[256];
  uint8_t auth_token[VW_AUTH_TOKEN_BYTES];
  char worker_path[256];
} vw_plugin_sys_t;

// Passthrough filter callback required by VLC filter pipeline (100% lock-free, Rule 4 compliant)
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

#ifdef _WIN32
  snprintf(sys->pipe_name, sizeof(sys->pipe_name), "\\\\.\\pipe\\vlc-whisper-%lu", (unsigned long)_getpid());
  snprintf(sys->worker_path, sizeof(sys->worker_path), "%s", "vlc-whisper-worker.exe");
#else
  snprintf(sys->pipe_name, sizeof(sys->pipe_name), "/tmp/vlc-whisper-%ld.sock", (long)getpid());
  snprintf(sys->worker_path, sizeof(sys->worker_path), "%s", "vlc-whisper-worker");
#endif

  if (!vw_platform_get_random_bytes(sys->auth_token, VW_AUTH_TOKEN_BYTES)) {
    vw_log_event(VW_LOG_LEVEL_WARN, "PLUGIN_RNG_FAIL", "failed to generate random auth_token");
  } else {
    sys->client = vw_worker_client_launch_and_connect(sys->worker_path, sys->pipe_name, sys->auth_token);
  }

  if (!sys->client) {
    vw_log_event(VW_LOG_LEVEL_WARN, "PLUGIN_WORKER_UNAVAILABLE",
                 "caption worker unavailable; running passthrough only");
  }

  vw_log_event(VW_LOG_LEVEL_INFO, "PLUGIN_OPEN", "vlc-whisper audio filter module opened");
  return VLC_SUCCESS;
}

static void vw_plugin_close(vlc_object_t* obj) {
  filter_t* p_filter = (filter_t*)obj;
  vw_plugin_sys_t* sys = (vw_plugin_sys_t*)p_filter->p_sys;

  if (sys) {
    if (sys->client) {
      vw_worker_client_disconnect(sys->client);
      sys->client = NULL;
    }
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
