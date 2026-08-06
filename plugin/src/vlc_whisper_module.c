#ifdef _WIN32
#include <winsock2.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// clang-format off
#include <vlc_common.h>
#include <vlc_block.h>
// clang-format on
#include <vlc_filter.h>
#include <vlc_plugin.h>

#include "vw_log.h"
#include "vw_plugin.h"

// Implements callback signature matching vw_log_sink_fn from protocol/include/vw_log.h
// Posts messages to VLC logging system (msg_Err, msg_Warn, msg_Dbg) with event_id prefix
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

#ifdef _WIN32
#include <process.h>
#include <windows.h>
#else
#include <dlfcn.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "vw_audio_capture.h"
#include "vw_caption_presenter.h"
#include "vw_platform.h"
#include "vw_queue.h"
#include "vw_worker_client.h"

static int vw_plugin_open(vlc_object_t* obj);
static void vw_plugin_close(vlc_object_t* obj);

static char vw_plugin_dl_anchor;

static bool vw_plugin_path_exists(const char* path) {
#ifdef _WIN32
  DWORD attr = GetFileAttributesA(path);
  return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
#else
  return access(path, F_OK) == 0;
#endif
}

static bool vw_plugin_resolve_worker_path(char* out, size_t out_size) {
#ifdef _WIN32
  const char* worker_name = "vlc-whisper-worker.exe";
  char plugin_path[MAX_PATH];
  HMODULE hmod = NULL;
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         (LPCSTR)(void*)vw_plugin_open, &hmod) &&
      hmod) {
    DWORD len = GetModuleFileNameA(hmod, plugin_path, (DWORD)sizeof(plugin_path));
    if (len > 0 && len < sizeof(plugin_path)) {
      char* slash = strrchr(plugin_path, '\\');
      char* slash2 = strrchr(plugin_path, '/');
      if (slash2 && (!slash || slash2 > slash)) slash = slash2;
      size_t dir_len = slash ? (size_t)(slash - plugin_path) : 0;
      if (dir_len > 0) {
        for (int up = 0; up <= 3; ++up) {
          size_t try_len = dir_len;
          for (int k = 0; k < up; ++k) {
            if (try_len == 0) break;
            char* last = NULL;
            for (size_t i = 0; i < try_len; ++i) {
              if (plugin_path[i] == '\\' || plugin_path[i] == '/') last = plugin_path + i;
            }
            if (!last) {
              try_len = 0;
              break;
            }
            try_len = (size_t)(last - plugin_path);
          }
          if (up > 0 && try_len == 0) continue;
          size_t need = try_len + 1 + strlen(worker_name) + 1;
          if (need > out_size) continue;
          char candidate[MAX_PATH];
          memcpy(candidate, plugin_path, try_len);
          candidate[try_len] = '\\';
          strcpy(candidate + try_len + 1, worker_name);
          if (vw_plugin_path_exists(candidate)) {
            strcpy(out, candidate);
            return true;
          }
        }
      }
    }
  }
  char exe_path[MAX_PATH];
  DWORD elen = GetModuleFileNameA(NULL, exe_path, (DWORD)sizeof(exe_path));
  if (elen > 0 && elen < sizeof(exe_path)) {
    char* slash = strrchr(exe_path, '\\');
    char* slash2 = strrchr(exe_path, '/');
    if (slash2 && (!slash || slash2 > slash)) slash = slash2;
    if (slash) {
      size_t dir_len = (size_t)(slash - exe_path);
      size_t need = dir_len + 1 + strlen(worker_name) + 1;
      if (need <= out_size) {
        char candidate[MAX_PATH];
        memcpy(candidate, exe_path, dir_len);
        candidate[dir_len] = '\\';
        strcpy(candidate + dir_len + 1, worker_name);
        if (vw_plugin_path_exists(candidate)) {
          strcpy(out, candidate);
          return true;
        }
      }
    }
  }
  return false;
#else
  const char* worker_name = "vlc-whisper-worker";
  Dl_info info;
  if (dladdr((void*)&vw_plugin_dl_anchor, &info) && info.dli_fname && info.dli_fname[0]) {
    const char* fname = info.dli_fname;
    const char* slash = strrchr(fname, '/');
    size_t dir_len = slash ? (size_t)(slash - fname) : 0;
    for (int up = 0; up <= 4; ++up) {
      size_t try_len = dir_len;
      for (int k = 0; k < up; ++k) {
        if (try_len == 0) break;
        const char* last = NULL;
        for (size_t i = 0; i < try_len; ++i) {
          if (fname[i] == '/') last = fname + i;
        }
        if (!last) {
          try_len = 0;
          break;
        }
        try_len = (size_t)(last - fname);
      }
      if (try_len == 0) continue;
      char candidate[1024];
      size_t need = try_len + 1 + strlen(worker_name) + 1;
      if (need > sizeof(candidate)) continue;
      memcpy(candidate, fname, try_len);
      candidate[try_len] = '/';
      strcpy(candidate + try_len + 1, worker_name);
      if (vw_plugin_path_exists(candidate)) {
        if (strlen(candidate) + 1 > out_size) return false;
        strcpy(out, candidate);
        return true;
      }
    }
  }
#ifdef __linux__
  char exe_path[4096];
  ssize_t n = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
  if (n > 0) {
    exe_path[n] = '\0';
    char* slash = strrchr(exe_path, '/');
    if (slash) {
      size_t dir_len = (size_t)(slash - exe_path);
      char candidate[4096];
      size_t need = dir_len + 1 + strlen(worker_name) + 1;
      if (need <= sizeof(candidate)) {
        memcpy(candidate, exe_path, dir_len);
        candidate[dir_len] = '/';
        strcpy(candidate + dir_len + 1, worker_name);
        if (vw_plugin_path_exists(candidate)) {
          if (strlen(candidate) + 1 <= out_size) {
            strcpy(out, candidate);
            return true;
          }
        }
      }
    }
  }
#endif
  return false;
#endif
}

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
#else
  snprintf(sys->pipe_name, sizeof(sys->pipe_name), "/tmp/vlc-whisper-%ld.sock", (long)getpid());
#endif

  // Explicit per-install override for layouts outside the bounded discovery paths.
  char* configured = config_GetPsz(obj, "worker-path");
  if (configured && configured[0]) {
    snprintf(sys->worker_path, sizeof(sys->worker_path), "%s", configured);
  } else if (vw_plugin_resolve_worker_path(sys->worker_path, sizeof(sys->worker_path))) {
    // Resolved to a concrete path next to the plugin or VLC executable.
  } else {
    // Fall back to a bare name; the spawn layer resolves it via PATH
    // (posix_spawnp), never relative to VLC's CWD.
#ifdef _WIN32
    snprintf(sys->worker_path, sizeof(sys->worker_path), "%s", "vlc-whisper-worker.exe");
#else
    snprintf(sys->worker_path, sizeof(sys->worker_path), "%s", "vlc-whisper-worker");
#endif
  }
  free(configured);

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
        add_loadfile("worker-path", NULL, "Path to vlc-whisper-worker executable (optional)",
                     "Explicit location of vlc-whisper-worker[.exe] for installs where it is "
                     "not co-located with the plugin; defaults to discovery",
                     false) set_callbacks(vw_plugin_open, vw_plugin_close) vlc_module_end()
#pragma GCC diagnostic pop
