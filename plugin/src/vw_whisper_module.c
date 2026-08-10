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

// Probes candidate directories (a file's own directory and up to max_up ancestors) for each of
// `names`, copying the first existing "<dir>/<name>" into out. Returns true on first hit. Shared by
// the worker-binary and model-file resolvers; on Win32 forward slashes are accepted by the APIs.
static bool vw_plugin_probe_ancestors(const char* file_path, int max_up, const char* const* names, size_t n_names,
                                      char* out, size_t out_size) {
  if (!file_path || !file_path[0]) return false;
  const char* slash = NULL;
  for (const char* p = file_path; *p; p++) {
    if (*p == '/' || *p == '\\') slash = p;
  }
  size_t dir_len = slash ? (size_t)(slash - file_path) : 0;
  if (dir_len == 0) return false;

  for (int up = 0; up <= max_up; ++up) {
    size_t try_len = dir_len;
    for (int k = 0; k < up; ++k) {
      if (try_len == 0) break;
      const char* last = NULL;
      for (size_t i = 0; i < try_len; ++i) {
        if (file_path[i] == '/' || file_path[i] == '\\') last = file_path + i;
      }
      if (!last) {
        try_len = 0;
        break;
      }
      try_len = (size_t)(last - file_path);
    }
    if (try_len == 0) continue;
    for (size_t n = 0; n < n_names; n++) {
      size_t need = try_len + 1 + strlen(names[n]) + 1;
      if (need > out_size) continue;
      char candidate[VW_PATH_MAX_BYTES];
      memcpy(candidate, file_path, try_len);
#ifdef _WIN32
      candidate[try_len] = '\\';  // native separator: GetFileAttributesA/CreateProcessW expect backslashes
#else
      candidate[try_len] = '/';
#endif
      strcpy(candidate + try_len + 1, names[n]);
      if (vw_plugin_path_exists(candidate)) {
        strcpy(out, candidate);
        return true;
      }
    }
  }
  return false;
}

// Resolves the vlc-whisper-worker executable path: plugin dir ancestors, then the exe dir.
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
      if (vw_plugin_probe_ancestors(plugin_path, 3, &worker_name, 1, out, out_size)) return true;
    }
  }
  char exe_path[MAX_PATH];
  DWORD elen = GetModuleFileNameA(NULL, exe_path, (DWORD)sizeof(exe_path));
  if (elen > 0 && elen < sizeof(exe_path)) {
    if (vw_plugin_probe_ancestors(exe_path, 0, &worker_name, 1, out, out_size)) return true;
  }
  return false;
#else
  const char* worker_name = "vlc-whisper-worker";
  Dl_info info;
  if (dladdr((void*)&vw_plugin_dl_anchor, &info) && info.dli_fname && info.dli_fname[0]) {
    if (vw_plugin_probe_ancestors(info.dli_fname, 4, &worker_name, 1, out, out_size)) return true;
  }
#ifdef __linux__
  char exe_path[4096];
  ssize_t n = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
  if (n > 0) {
    exe_path[n] = '\0';
    if (vw_plugin_probe_ancestors(exe_path, 0, &worker_name, 1, out, out_size)) return true;
  }
#endif
  return false;
#endif
}

// Resolves the ggml-tiny.en.bin model file: probes "<dir>/ggml-tiny.en.bin" and "<dir>/models/"
// in the same ancestor + exe-dir walk used for the worker binary. Empty out means "no model".
static bool vw_plugin_resolve_model_path(char* out, size_t out_size) {
  const char* model_names[] = {"ggml-tiny.en.bin", "models/ggml-tiny.en.bin"};
#ifdef _WIN32
  char plugin_path[MAX_PATH];
  HMODULE hmod = NULL;
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         (LPCSTR)(void*)vw_plugin_open, &hmod) &&
      hmod) {
    DWORD len = GetModuleFileNameA(hmod, plugin_path, (DWORD)sizeof(plugin_path));
    if (len > 0 && len < sizeof(plugin_path)) {
      if (vw_plugin_probe_ancestors(plugin_path, 3, model_names, 2, out, out_size)) return true;
    }
  }
  char exe_path[MAX_PATH];
  DWORD elen = GetModuleFileNameA(NULL, exe_path, (DWORD)sizeof(exe_path));
  if (elen > 0 && elen < sizeof(exe_path)) {
    if (vw_plugin_probe_ancestors(exe_path, 0, model_names, 2, out, out_size)) return true;
  }
  return false;
#else
  Dl_info info;
  if (dladdr((void*)&vw_plugin_dl_anchor, &info) && info.dli_fname && info.dli_fname[0]) {
    if (vw_plugin_probe_ancestors(info.dli_fname, 4, model_names, 2, out, out_size)) return true;
  }
#ifdef __linux__
  char exe_path[4096];
  ssize_t n = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
  if (n > 0) {
    exe_path[n] = '\0';
    if (vw_plugin_probe_ancestors(exe_path, 0, model_names, 2, out, out_size)) return true;
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
  vw_caption_presenter_t presenter;  // p_filter_ctx set in open; used by sender thread only

  char pipe_name[256];
  uint8_t auth_token[VW_AUTH_TOKEN_BYTES];
  char worker_path[VW_PATH_MAX_BYTES];

  // Sender thread (14c): drains the SPSC queue to the worker and drains worker frames back.
  vw_thread_t sender_thread;
  bool sender_started;  // thread create succeeded; close joins only when true
  _Atomic bool sender_running;
  _Atomic bool worker_dead;
  uint64_t chunks_sent;
  uint32_t frames_received;
  uint32_t segments_received;
  uint32_t status_received;
  uint32_t errors_received;
  char model_path[VW_PATH_MAX_BYTES];
} vw_plugin_sys_t;
// Sender thread (14c): the only consumer of the SPSC queue and the only user of the worker client.
// Starts one session, then alternates draining queue -> send AUDIO frames with draining worker ->
// plugin frames (SEGMENT/STATUS/ERROR), degrading to passthrough on any fatal transport condition.
static void* vw_plugin_sender_main(void* arg) {
  vw_plugin_sys_t* sys = (vw_plugin_sys_t*)arg;

  // First iteration: start the caption session. A worker rejection (e.g. E_MODEL_MISSING) means
  // captions stay off for this module lifetime; playback is untouched.
  if (sys->client && !vw_worker_client_start_session(sys->client, 0, "tiny.en")) {
    atomic_store(&sys->worker_dead, true);
    vw_log_event(VW_LOG_LEVEL_WARN, "PLUGIN_SESSION_START_FAIL",
                 "worker rejected session; captions disabled, passthrough only");
    return NULL;
  }
  vw_log_event(VW_LOG_LEVEL_INFO, "PLUGIN_SESSION_STARTED", "caption session started (STARTED confirmed)");

  while (atomic_load(&sys->sender_running) && !atomic_load(&sys->worker_dead)) {
    // Drain the SPSC queue (send burst), then one receive: 5ms after sends (audio latency
    // priority), 20ms when idle (the idle wait doubles as cadence, no extra sleep).
    vw_audio_chunk_t chunk;
    bool sent_any = false;
    while (vw_spsc_queue_pop(sys->queue, &chunk)) {
      sys->chunks_sent++;
      if (!vw_worker_client_send_audio(sys->client, &chunk)) {
        atomic_store(&sys->worker_dead, true);
        vw_log_event(VW_LOG_LEVEL_WARN, "PLUGIN_WORKER_DEAD",
                     "send_audio failed after %llu chunks; captions disabled, passthrough only",
                     (unsigned long long)sys->chunks_sent);
        break;
      }
      sent_any = true;
    }
    if (atomic_load(&sys->worker_dead)) break;

    vw_worker_recv_t recv;
    int recv_status = vw_worker_client_receive_frame(sys->client, sent_any ? 5000 : 20000, &recv);
    if (recv_status == VW_IPC_RECV_FATAL) {
      atomic_store(&sys->worker_dead, true);
      vw_log_event(VW_LOG_LEVEL_WARN, "PLUGIN_WORKER_DEAD",
                   "receive_frame fatal (transport dead); captions disabled, passthrough only");
      break;
    }
    if (recv_status == VW_IPC_RECV_OK) {
      sys->frames_received++;
      switch (recv.type) {
        case VW_MSG_CAPTION_SEGMENT:
          sys->segments_received++;
          // Synchronous render: recv.text_buf owns the segment text for this iteration, so the
          // presenter may copy/format it safely. No OSD when the vout walk fails (passthrough).
          vw_caption_presenter_show_segment(&sys->presenter, &recv.segment);
          break;
        case VW_MSG_STATUS:
          sys->status_received++;
          vw_log_event(VW_LOG_LEVEL_DEBUG, "PLUGIN_STATUS", "queued=%lld inference=%lld dropped=%lld",
                       (long long)recv.status.queued_audio_us, (long long)recv.status.inference_us,
                       (long long)recv.status.dropped_audio_us);
          break;
        case VW_MSG_ERROR:
          sys->errors_received++;
          vw_log_event(VW_LOG_LEVEL_ERROR, "PLUGIN_WORKER_ERROR", "code=%u recoverable=%u msg=%.*s",
                       recv.error.error_code, recv.error.recoverable,
                       (int)strnlen(recv.error.message, VW_MAX_ERROR_MSG_BYTES), recv.error.message);
          // A non-recoverable worker error disables captions; playback continues (api-contracts).
          if (!recv.error.recoverable) atomic_store(&sys->worker_dead, true);
          break;
        default:
          break;
      }
    }

    if (sent_any && (sys->chunks_sent % 1024) == 0) {
      vw_log_event(VW_LOG_LEVEL_INFO, "PLUGIN_SENDER", "sent %llu chunks, received %u worker frames",
                   (unsigned long long)sys->chunks_sent, sys->frames_received);
    }
  }
  return NULL;
}

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

  sys->presenter.p_filter_ctx = p_filter;  // sender thread renders SEGMENT frames via this context

  vw_log_set_sink(vw_plugin_log_sink, obj);

#ifdef _WIN32
  snprintf(sys->pipe_name, sizeof(sys->pipe_name), "\\\\.\\pipe\\vlc-whisper-%lu", (unsigned long)_getpid());
#else
  snprintf(sys->pipe_name, sizeof(sys->pipe_name), "/tmp/vlc-whisper-%ld.sock", (long)getpid());
#endif

  // Explicit per-install override for layouts outside the bounded discovery paths.
  char* configured = config_GetPsz(obj, "worker-path");
  if (configured && configured[0]) {
    if (strlen(configured) >= sizeof(sys->worker_path)) {
      // Fail closed: the user explicitly chose this worker; falling back to discovery would launch
      // a DIFFERENT binary than configured. Refuse to open rather than substitute a wrong artifact.
      vw_log_event(VW_LOG_LEVEL_ERROR, "PLUGIN_CONFIG_PATH",
                   "worker-path exceeds %zu bytes (OS path limit); refusing to launch a different worker",
                   sizeof(sys->worker_path) - 1);
      free(configured);
      vw_log_set_sink(NULL, NULL);  // close() never runs after a failed open; drop the dangling sink
      vw_spsc_queue_destroy(sys->queue);
      p_filter->p_sys = NULL;
      free(sys);
      return VLC_EGENERIC;
    }
    snprintf(sys->worker_path, sizeof(sys->worker_path), "%s", configured);
  }
  if (sys->worker_path[0] == '\0' && vw_plugin_resolve_worker_path(sys->worker_path, sizeof(sys->worker_path))) {
    // Resolved to a concrete path next to the plugin or VLC executable.
  }
  if (sys->worker_path[0] == '\0') {
    // Fall back to a bare name; the spawn layer resolves it via PATH
    // (posix_spawnp), never relative to VLC's CWD.
#ifdef _WIN32
    snprintf(sys->worker_path, sizeof(sys->worker_path), "%s", "vlc-whisper-worker.exe");
#else
    snprintf(sys->worker_path, sizeof(sys->worker_path), "%s", "vlc-whisper-worker");
#endif
  }
  free(configured);

  // Model path: explicit option wins, then discovery next to the plugin/VLC exe. A bad configured
  // path surfaces as E_MODEL_MISSING at session start — do not pre-check existence here. An
  // oversized configured path fails closed (refuse to open) rather than load a different model or
  // silently truncate.
  char* model_cfg = config_GetPsz(obj, "model-path");
  if (model_cfg && model_cfg[0]) {
    if (strlen(model_cfg) >= sizeof(sys->model_path)) {
      vw_log_event(VW_LOG_LEVEL_ERROR, "PLUGIN_CONFIG_PATH",
                   "model-path exceeds %zu bytes (OS path limit); refusing to load a different model",
                   sizeof(sys->model_path) - 1);
      free(model_cfg);
      vw_log_set_sink(NULL, NULL);  // close() never runs after a failed open; drop the dangling sink
      vw_spsc_queue_destroy(sys->queue);
      p_filter->p_sys = NULL;
      free(sys);
      return VLC_EGENERIC;
    }
    snprintf(sys->model_path, sizeof(sys->model_path), "%s", model_cfg);
  } else if (vw_plugin_resolve_model_path(sys->model_path, sizeof(sys->model_path))) {
    // Resolved to a concrete file next to the plugin or VLC executable.
  }
  free(model_cfg);

  if (!vw_platform_get_random_bytes(sys->auth_token, VW_AUTH_TOKEN_BYTES)) {
    vw_log_event(VW_LOG_LEVEL_WARN, "PLUGIN_RNG_FAIL", "failed to generate random auth_token");
  } else {
    vw_log_event(VW_LOG_LEVEL_INFO, "PLUGIN_WORKER_LAUNCH", "spawning worker: %s", sys->worker_path);
    sys->client = vw_worker_client_launch_and_connect(sys->worker_path, sys->pipe_name, sys->auth_token,
                                                      sys->model_path[0] ? sys->model_path : NULL);
    vw_log_event(sys->client ? VW_LOG_LEVEL_INFO : VW_LOG_LEVEL_WARN, "PLUGIN_WORKER_CONNECT",
                 sys->client ? "worker connected (HELLO handshake ok)" : "worker connect failed");
  }

  if (!sys->client) {
    vw_log_event(VW_LOG_LEVEL_WARN, "PLUGIN_WORKER_UNAVAILABLE",
                 "caption worker unavailable; running passthrough only");
  } else {
    // Start the sender thread; a spawn failure keeps passthrough (close path stays safe).
    atomic_init(&sys->sender_running, true);
    atomic_init(&sys->worker_dead, false);
    if (vw_platform_thread_create(&sys->sender_thread, vw_plugin_sender_main, sys)) {
      sys->sender_started = true;
      vw_log_event(VW_LOG_LEVEL_INFO, "PLUGIN_SENDER_START", "sender thread started (5/20 ms cadence)");
    } else {
      atomic_store(&sys->sender_running, false);
      vw_log_event(VW_LOG_LEVEL_WARN, "PLUGIN_SENDER_START_FAIL",
                   "sender thread creation failed; captions disabled, passthrough only");
    }
  }

  vw_log_event(VW_LOG_LEVEL_INFO, "PLUGIN_OPEN", "vlc-whisper audio filter module opened");
  return VLC_SUCCESS;
}

static void vw_plugin_close(vlc_object_t* obj) {
  filter_t* p_filter = (filter_t*)obj;
  vw_plugin_sys_t* sys = (vw_plugin_sys_t*)p_filter->p_sys;

  if (sys) {
    // Stop and join the sender thread before touching the client/queue it uses.
    atomic_store(&sys->sender_running, false);
    if (sys->sender_started) {
      vw_platform_thread_join(sys->sender_thread);
    }
    if (sys->client) {
      if (!atomic_load(&sys->worker_dead)) {
        vw_worker_client_stop_session(sys->client, 0);
      }
      vw_worker_client_shutdown(sys->client);
      vw_worker_client_disconnect(sys->client);
      sys->client = NULL;
    }
    if (sys->queue) {
      vw_log_event(VW_LOG_LEVEL_INFO, "PLUGIN_CLOSE",
                   "Dropped %llu us of audio during session; sent %llu chunks, %u segments, %u status, %u errors",
                   (unsigned long long)vw_spsc_queue_get_dropped_microseconds(sys->queue),
                   (unsigned long long)sys->chunks_sent, sys->segments_received, sys->status_received,
                   sys->errors_received);
      vw_spsc_queue_destroy(sys->queue);
    }
    vw_caption_presenter_clear(&sys->presenter);  // remove OSD overlay before releasing p_filter
    free(sys);
  }

  vw_log_event(VW_LOG_LEVEL_INFO, "PLUGIN_CLOSE", "vlc-whisper audio filter module closed");
  vw_log_set_sink(NULL, NULL);
}

// VLC module definition macro expands to vlc_entry__3_0_0f ABI symbol
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
vlc_module_begin() set_shortname("VLC-Whisper") set_description("Offline Whisper AI Captions Filter")
    set_capability("audio filter", 0) set_category(CAT_AUDIO) set_subcategory(SUBCAT_AUDIO_AFILTER)
        add_shortcut("vlc_whisper", "whisper")
            add_loadfile("worker-path", NULL, "Path to vlc-whisper-worker executable (optional)",
                         "Explicit location of vlc-whisper-worker[.exe] for installs where it is "
                         "not co-located with the plugin; defaults to discovery",
                         false)
                add_loadfile("model-path", NULL, "Path to ggml-tiny.en.bin model file (optional)",
                             "Explicit location of the whisper model; defaults to discovery next to the plugin", false)
                    set_callbacks(vw_plugin_open, vw_plugin_close) vlc_module_end()
#pragma GCC diagnostic pop
