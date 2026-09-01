#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "vw_worker_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "vw_audio_capture.h"
#include "vw_ipc_transport.h"
#include "vw_log.h"
#include "vw_platform.h"
#include "vw_protocol_codec.h"
#include "vw_protocol_types.h"

// Receive exactly len bytes, retrying on partial reads and on read timeouts
// (VW_IPC_RECV_TIMEOUT). Returns VW_IPC_RECV_OK (1) on complete read, VW_IPC_RECV_TIMEOUT (-1) when
// the deadline expires before any byte was consumed (clean frame boundary), and VW_IPC_RECV_FATAL
// (-2) on a fatal error (EOF / broken pipe) or on a timeout that consumed part of the frame (desynced).
static int receive_all(vw_ipc_handle_t* ipc, uint8_t* buf, size_t len, int64_t deadline_us) {
  size_t got = 0;
  while (got < len) {
    int64_t now_us = vw_platform_get_monotonic_time_us();
    if (now_us >= deadline_us) return (got == 0) ? VW_IPC_RECV_TIMEOUT : VW_IPC_RECV_FATAL;
    uint32_t remaining_us = (uint32_t)(deadline_us - now_us);
    uint32_t timeout_us = (remaining_us < 3000000U) ? remaining_us : 3000000U;

    int32_t res = vw_ipc_receive_timeout(ipc, buf + got, len - got, timeout_us);
    if (res < 0) {
      if (res == VW_IPC_RECV_TIMEOUT) {  // timeout — keep waiting, bounded by the deadline
        if (vw_platform_get_monotonic_time_us() >= deadline_us)
          return (got == 0) ? VW_IPC_RECV_TIMEOUT : VW_IPC_RECV_FATAL;
        continue;
      }
      return VW_IPC_RECV_FATAL;  // fatal (VW_IPC_RECV_FATAL): EOF / broken pipe / closed
    }
    got += (size_t)res;
  }
  return VW_IPC_RECV_OK;
}

// Format 32 raw bytes as a 64-char lowercase hex string.
static void token_to_hex(const uint8_t tok[VW_AUTH_TOKEN_BYTES], char out[VW_AUTH_TOKEN_BYTES * 2 + 1]) {
  for (size_t i = 0; i < VW_AUTH_TOKEN_BYTES; i++) {
    snprintf(out + i * 2, 3, "%02x", tok[i]);
  }
}

// A frame send that fails part-way leaves the byte stream dead or desynced
// (e.g. header written but payload not: the worker waits for the declared
// payload and would consume the next frame as it). Such a connection can
// never be reframed, so close it and mark the client unusable: every later
// API call fails fast instead of mis-framing the next message.
static void vw_worker_client_drop_transport(vw_worker_client_t* client) {
  if (client && client->pipe_handle) {
    vw_ipc_close((vw_ipc_handle_t*)client->pipe_handle);
    client->pipe_handle = NULL;
    client->session_active = false;
  }
}

static bool send_control_frame(vw_worker_client_t* client, vw_message_type_t type, uint16_t reason);

vw_worker_client_t* vw_worker_client_launch_and_connect_ex(const char* executable_path, const char* endpoint_name,
                                                           const uint8_t auth_token[VW_AUTH_TOKEN_BYTES],
                                                           const char* model_path, const char* backend,
                                                           const char* language, int n_threads, int gpu_device,
                                                           const char* model_dir, bool logging_enabled) {
  if (!endpoint_name || !auth_token) {
    return NULL;
  }

  // Spawn the worker first so it can bind the endpoint before we connect.
  vw_process_t worker_process = (vw_process_t)0;
  if (executable_path) {
    char token_hex[VW_AUTH_TOKEN_BYTES * 2 + 1];  // null terminated
    char gpu_buf[16];
    token_to_hex(auth_token, token_hex);
    // 19b: argv grows from 8 to 16 to carry --backend/--gpu-device/--language/--n-threads
    const char* argv[20];
    size_t argc = 0;
    argv[argc++] = executable_path;
    argv[argc++] = "--pipe";
    argv[argc++] = endpoint_name;
    argv[argc++] = "--token";
    argv[argc++] = token_hex;
    if (model_path) {
      argv[argc++] = "--model";
      argv[argc++] = model_path;
    }
    // 19b worker CLI additions — always forward backend/language/threads; gpu-device only if >=0
    const char* eff_backend = (backend && backend[0]) ? backend : "auto";
    argv[argc++] = "--backend";
    argv[argc++] = eff_backend;
    if (gpu_device >= 0) {
      snprintf(gpu_buf, sizeof(gpu_buf), "%d", gpu_device);
      argv[argc++] = "--gpu-device";
      argv[argc++] = gpu_buf;
    }
    const char* eff_language = (language && language[0]) ? language : "en";
    argv[argc++] = "--language";
    argv[argc++] = eff_language;
    char threads_buf[16];
    int eff_threads = n_threads;
    if (eff_threads < 1 || eff_threads > 16) eff_threads = 4;
    snprintf(threads_buf, sizeof(threads_buf), "%d", eff_threads);
    argv[argc++] = "--n-threads";
    argv[argc++] = threads_buf;
    if (model_dir && model_dir[0]) {
      argv[argc++] = "--model-dir";
      argv[argc++] = model_dir;
    }
    if (logging_enabled) argv[argc++] = "--enable-logging";
    argv[argc] = NULL;
    if (!vw_platform_spawn_process(executable_path, argv, &worker_process)) {
      return NULL;
    }
  }

  // Retry connecting for up to 2 seconds (40 * 50ms) to allow the spawned process to bind the socket/pipe.
  vw_ipc_handle_t* ipc = NULL;
  for (int retry = 0; retry < VW_WORKER_CLIENT_RETRY_COUNT; retry++) {
    ipc = vw_ipc_connect(endpoint_name);
    if (ipc) break;
#ifdef _WIN32
    Sleep(50);
#else
    usleep(50000);
#endif
  }
  if (!ipc) {
    if (worker_process) {
      if (!vw_platform_wait_process(worker_process, 1000)) {
        vw_platform_terminate_process(worker_process);
      } else {
        vw_platform_close_process(worker_process);
      }
    }
    return NULL;
  }

  vw_worker_client_t* client = (vw_worker_client_t*)calloc(1, sizeof(vw_worker_client_t));
  if (!client) {
    vw_ipc_close(ipc);
    if (worker_process) {
      if (!vw_platform_wait_process(worker_process, 1000)) {
        vw_platform_terminate_process(worker_process);
      } else {
        vw_platform_close_process(worker_process);
      }
    }
    return NULL;
  }
  client->pipe_handle = ipc;
  client->worker_process = worker_process;
  client->sequence = 1;

  // --- HELLO handshake ---
  vw_msg_hello_t hello = {.min_major = VW_PROTOCOL_VERSION_MAJOR,
                          .max_major = VW_PROTOCOL_VERSION_MAJOR,
                          .client_version = VW_CLIENT_VERSION,
                          .client_version_length = VW_CLIENT_VERSION_LENGTH};
  memcpy(hello.auth_token, auth_token, VW_AUTH_TOKEN_BYTES);

  uint8_t payload_buf[256];
  size_t payload_len = 0;
  if (!vw_protocol_encode_payload(VW_MSG_HELLO, &hello, payload_buf, sizeof(payload_buf), &payload_len)) {
    goto fail;
  }

  vw_frame_header_t hdr = {.magic = VW_PROTOCOL_MAGIC,
                           .major = VW_PROTOCOL_VERSION_MAJOR,
                           .type = VW_MSG_HELLO,
                           .payload_length = (uint32_t)payload_len,
                           .sequence = 1};
  uint8_t hdr_buf[sizeof(vw_frame_header_t)];
  if (!vw_protocol_encode_header(&hdr, hdr_buf, sizeof(hdr_buf))) {
    goto fail;
  }
  if (!vw_ipc_send(ipc, hdr_buf, sizeof(hdr_buf)) || !vw_ipc_send(ipc, payload_buf, payload_len)) {
    goto fail;
  }

  // Total budget for the HELLO/HELLO_ACK reads: a silently-dead worker must not
  // hang module open forever (each vw_ipc_receive can block up to 3s on timeout).
  const int64_t handshake_deadline_us = vw_platform_get_monotonic_time_us() + VW_HANDSHAKE_TIMEOUT_US;

  // Wait for HELLO_ACK (header, then payload)
  uint8_t ack_hdr_buf[sizeof(vw_frame_header_t)];
  if (receive_all(ipc, ack_hdr_buf, sizeof(ack_hdr_buf), handshake_deadline_us) != VW_IPC_RECV_OK) goto fail;
  vw_frame_header_t ack_hdr;
  if (!vw_protocol_decode_header(ack_hdr_buf, sizeof(ack_hdr_buf), &ack_hdr)) goto fail;  // validates header too
  if (ack_hdr.type != VW_MSG_HELLO_ACK) goto fail;

  if (ack_hdr.payload_length == 0 || ack_hdr.payload_length > 1024) {
    goto fail;
  }
  uint8_t* ack_payload = (uint8_t*)malloc(ack_hdr.payload_length);
  if (!ack_payload) goto fail;
  bool ack_ok = (receive_all(ipc, ack_payload, ack_hdr.payload_length, handshake_deadline_us) == VW_IPC_RECV_OK);
  vw_msg_hello_ack_t ack = {0};
  if (ack_ok) {
    ack_ok = vw_protocol_decode_payload(VW_MSG_HELLO_ACK, ack_payload, ack_hdr.payload_length, &ack);
  }
  if (ack_ok) {
    ack_ok = vw_protocol_validate_payload(VW_MSG_HELLO_ACK, &ack);
  }
  if (ack_ok) {
    if (ack.selected_major != VW_PROTOCOL_VERSION_MAJOR) ack_ok = false;
    if ((ack.capability_flags & VW_CAPABILITY_PCM_S16LE_16K_MONO) == 0) ack_ok = false;
  }
  if (ack_ok) {
    client->worker_capabilities = ack.capability_flags;
    client->worker_protocol_minor = ack.selected_minor;
  }
  free(ack_payload);
  if (!ack_ok) goto fail;

  if (ack_hdr.major != VW_PROTOCOL_VERSION_MAJOR) {
    goto fail;
  }

  // Handshake complete: the worker has authenticated us.
  return client;

fail:
  vw_worker_client_disconnect(client);
  return NULL;
}

vw_worker_client_t* vw_worker_client_launch_and_connect(const char* executable_path, const char* endpoint_name,
                                                        const uint8_t auth_token[VW_AUTH_TOKEN_BYTES],
                                                        const char* model_path) {
  // Wrapper preserving legacy 4-arg ABI for tests; forwards defaults (auto/en/4, no gpu-device)
  return vw_worker_client_launch_and_connect_ex(executable_path, endpoint_name, auth_token, model_path, "auto", "en", 4,
                                                -1, NULL, false);
}

void vw_worker_client_disconnect(vw_worker_client_t* client) {
  if (client) {
    if (client->pipe_handle) {
      vw_ipc_close((vw_ipc_handle_t*)client->pipe_handle);
    }
    if (client->worker_process) {
      if (!vw_platform_wait_process(client->worker_process, 5000)) {
        vw_platform_terminate_process(client->worker_process);
      } else {
        vw_platform_close_process(client->worker_process);
      }
    }
    free(client);
  }
}

bool vw_worker_client_start_session(vw_worker_client_t* client, int64_t timeline_origin_pts_us, const char* model_id,
                                    const char* source_url) {
  if (!client || !client->pipe_handle) return false;
  client->session_active = false;
  client->worker_source_active = false;

  if (!vw_platform_get_random_bytes(client->session_id, sizeof(client->session_id))) return false;

  vw_msg_start_t start = {.timeline_origin_pts_us = timeline_origin_pts_us,
                          .sample_rate = 16000,
                          .channels = 1,
                          .sample_format = VW_AUDIO_FORMAT_S16,
                          .source_kind = source_url ? VW_SOURCE_LOCAL_FILE : VW_SOURCE_LIVE_AUDIO};
  memcpy(start.session_id.bytes, client->session_id, 16);
  if (model_id) strncpy(start.model_id, model_id, sizeof(start.model_id) - 1);
  strncpy(start.language, "en", sizeof(start.language) - 1);
  if (source_url) {
    size_t url_len = strlen(source_url);
    if (url_len >= sizeof(start.source_url)) {
      vw_log_event(VW_LOG_LEVEL_WARN, "CLIENT_START_URL", "source_url too long (%zu >= %zu); truncating", url_len,
                   sizeof(start.source_url));
    }
    strncpy(start.source_url, source_url, sizeof(start.source_url) - 1);
    start.source_url[sizeof(start.source_url) - 1] = '\0';
    start.source_url_len = (uint16_t)strlen(start.source_url);
  }

  uint8_t payload_buf[2048];
  size_t payload_len = 0;
  if (!vw_protocol_encode_payload(VW_MSG_START_SESSION, &start, payload_buf, sizeof(payload_buf), &payload_len))
    return false;

  vw_frame_header_t hdr = {.magic = VW_PROTOCOL_MAGIC,
                           .major = VW_PROTOCOL_VERSION_MAJOR,
                           .type = VW_MSG_START_SESSION,
                           .payload_length = (uint32_t)payload_len,
                           .sequence = ++client->sequence};

  uint8_t hdr_buf[sizeof(vw_frame_header_t)];
  if (!vw_protocol_encode_header(&hdr, hdr_buf, sizeof(hdr_buf))) return false;
  if (!vw_ipc_send(client->pipe_handle, hdr_buf, sizeof(hdr_buf)) ||
      !vw_ipc_send(client->pipe_handle, payload_buf, payload_len)) {
    vw_worker_client_drop_transport(client);
    return false;
  }

  const int64_t deadline_us =
      vw_platform_get_monotonic_time_us() + 5000000;  // 5s total budget for the STARTED/ERROR confirmation wait
  while (vw_platform_get_monotonic_time_us() < deadline_us) {
    uint8_t resp_hdr_buf[sizeof(vw_frame_header_t)];
    if (receive_all(client->pipe_handle, resp_hdr_buf, sizeof(resp_hdr_buf), deadline_us) != VW_IPC_RECV_OK) {
      // Fatal EOF, or the 5s handshake deadline expired. This is a one-shot handshake, not a poll:
      // after a timeout the worker's state is unknowable — it may have accepted the START and sent
      // a STARTED that is still in the socket buffer. A retry would regenerate session_id and read
      // that stale STARTED as its own confirmation, then send AUDIO for a session the worker never
      // accepted. Fail closed: drop so any retry must reconnect on a fresh transport.
      vw_worker_client_drop_transport(client);
      return false;
    }

    vw_frame_header_t resp_hdr;
    if (!vw_protocol_decode_header(resp_hdr_buf, sizeof(resp_hdr_buf), &resp_hdr)) {
      vw_worker_client_drop_transport(client);
      return false;
    }

    if (resp_hdr.type == VW_MSG_STARTED) {
      if (resp_hdr.payload_length > 0) {
        uint8_t* resp_payload = (uint8_t*)malloc(resp_hdr.payload_length);
        if (!resp_payload) {
          vw_worker_client_drop_transport(client);
          return false;
        }
        if (receive_all(client->pipe_handle, resp_payload, resp_hdr.payload_length, deadline_us) != VW_IPC_RECV_OK) {
          free(resp_payload);
          vw_worker_client_drop_transport(client);
          return false;
        }
        vw_msg_started_t started_msg = {0};
        if (vw_protocol_decode_payload(VW_MSG_STARTED, resp_payload, resp_hdr.payload_length, &started_msg)) {
          client->worker_source_active = (started_msg.source_active == VW_SOURCE_ACTIVE_ACTIVE);
        } else {
          client->worker_source_active = false;
        }
        free(resp_payload);
      } else {
        client->worker_source_active = false;
      }
      client->session_active = true;
      snprintf(client->active_model_id, sizeof(client->active_model_id), "%s", start.model_id);
      snprintf(client->active_source_url, sizeof(client->active_source_url), "%s", start.source_url);
      return true;
    } else if (resp_hdr.type == VW_MSG_ERROR) {
      if (resp_hdr.payload_length > 0) {
        uint8_t* resp_payload = (uint8_t*)malloc(resp_hdr.payload_length);
        if (!resp_payload) {
          vw_worker_client_drop_transport(client);
          return false;
        }
        bool drained =
            (receive_all(client->pipe_handle, resp_payload, resp_hdr.payload_length, deadline_us) == VW_IPC_RECV_OK);
        vw_msg_error_t err;
        memset(&err, 0, sizeof(err));
        bool decoded = false;
        if (drained && vw_protocol_decode_payload(VW_MSG_ERROR, resp_payload, resp_hdr.payload_length, &err)) {
          decoded = true;
          vw_log_event(VW_LOG_LEVEL_WARN, "WORKER_START_ERROR", "code=%u recoverable=%u msg=%.*s", err.error_code,
                       err.recoverable, (int)strnlen(err.message, VW_MAX_ERROR_MSG_BYTES), err.message);
        }
        free(resp_payload);
        if (!drained) {
          vw_worker_client_drop_transport(client);
          return false;
        }
        // Recoverable E_SOURCE_OPEN is not a fatal START failure: the worker will follow with STARTED(source_active=0)
        // to transparently fall back to live PCM. Treat it as an informational handshake step and continue to STARTED.
        if (decoded && err.error_code == E_SOURCE_OPEN && err.recoverable) {
          // Drain and keep waiting for the mandated STARTED within the same deadline.
          continue;
        }
      } else {
        // Zero-payload ERROR with no info cannot be E_SOURCE_OPEN; treat as fatal.
      }
      return false;
    }
    if (resp_hdr.payload_length > 0) {
      uint8_t* resp_payload = (uint8_t*)malloc(resp_hdr.payload_length);
      if (!resp_payload) {
        // Declared payload cannot be drained; framing is lost.
        vw_worker_client_drop_transport(client);
        return false;
      }
      bool drained =
          (receive_all(client->pipe_handle, resp_payload, resp_hdr.payload_length, deadline_us) == VW_IPC_RECV_OK);
      free(resp_payload);
      if (!drained) {
        vw_worker_client_drop_transport(client);
        return false;
      }
    }
  }
  // Deadline expired between frames: any late response can no longer be
  // trusted to belong to this session_id. Drop rather than risk a stale
  // STARTED confirming a session the worker never accepted.
  vw_worker_client_drop_transport(client);
  return false;
}

static bool vw_worker_client_send_position_frame(vw_worker_client_t* client, int64_t current_pts_us,
                                                 int64_t input_time_us, float playback_rate, uint32_t flags) {
  vw_msg_position_t pos = {.current_pts_us = current_pts_us,
                           .input_time_us = input_time_us,
                           .playback_rate = playback_rate > 0.0f ? playback_rate : 1.0f,
                           .flags = flags};
  memcpy(pos.session_id.bytes, client->session_id, 16);

  uint8_t payload_buf[64];
  size_t payload_len = 0;
  if (!vw_protocol_encode_payload(VW_MSG_POSITION, &pos, payload_buf, sizeof(payload_buf), &payload_len)) return false;

  vw_frame_header_t hdr = {.magic = VW_PROTOCOL_MAGIC,
                           .major = VW_PROTOCOL_VERSION_MAJOR,
                           .type = VW_MSG_POSITION,
                           .payload_length = (uint32_t)payload_len,
                           .sequence = ++client->sequence};

  uint8_t hdr_buf[sizeof(vw_frame_header_t)];
  if (!vw_protocol_encode_header(&hdr, hdr_buf, sizeof(hdr_buf))) return false;
  if (!vw_ipc_send(client->pipe_handle, hdr_buf, sizeof(hdr_buf)) ||
      !vw_ipc_send(client->pipe_handle, payload_buf, payload_len)) {
    vw_worker_client_drop_transport(client);
    return false;
  }
  return true;
}

bool vw_worker_client_send_position(vw_worker_client_t* client, int64_t current_pts_us, int64_t input_time_us,
                                    float playback_rate, uint32_t flags) {
  if (!client || !client->pipe_handle || !client->session_active) return false;

  if ((flags & VW_POSITION_FLAG_SEEK) != 0 && client->worker_source_active && client->active_source_url[0] != '\0') {
    char model_id[VW_MAX_MODEL_ID_BYTES];
    char source_url[VW_MAX_SOURCE_URL_BYTES];
    snprintf(model_id, sizeof(model_id), "%s", client->active_model_id);
    snprintf(source_url, sizeof(source_url), "%s", client->active_source_url);

    bool translation_configured = client->translation_configured;
    bool translate_enabled = client->translate_enabled;
    char translate_source_lang[sizeof(client->translate_source_lang)];
    char translate_target_lang[sizeof(client->translate_target_lang)];
    snprintf(translate_source_lang, sizeof(translate_source_lang), "%s", client->translate_source_lang);
    snprintf(translate_target_lang, sizeof(translate_target_lang), "%s", client->translate_target_lang);
    uint8_t translate_mode = client->translate_mode;

    if (!send_control_frame(client, VW_MSG_STOP_SESSION, VW_CTRL_REASON_SEEK_DISCONTINUITY)) return false;
    client->session_active = false;
    client->worker_source_active = false;

    if (!vw_worker_client_start_session(client, current_pts_us, model_id[0] ? model_id : NULL, source_url)) {
      return false;
    }
    if (!client->worker_source_active) {
      vw_log_event(VW_LOG_LEVEL_WARN, "CLIENT_SOURCE_EPOCH",
                   "source seek restart did not re-enter source mode; dropping transport for supervisor recovery");
      vw_worker_client_drop_transport(client);
      return false;
    }

    if (translation_configured && (client->worker_capabilities & VW_CAPABILITY_TRANSLATION) != 0 &&
        !vw_worker_client_send_translate_ctrl(client, translate_enabled, translate_source_lang, translate_target_lang,
                                              translate_mode)) {
      return false;
    }

    vw_log_event(VW_LOG_LEVEL_DEBUG, "CLIENT_SOURCE_EPOCH",
                 "source seek restarted caption epoch at %lldus before pacing update", (long long)current_pts_us);
    return vw_worker_client_send_position_frame(client, current_pts_us, input_time_us, playback_rate,
                                                flags & ~VW_POSITION_FLAG_SEEK);
  }

  return vw_worker_client_send_position_frame(client, current_pts_us, input_time_us, playback_rate, flags);
}

bool vw_worker_client_send_audio(vw_worker_client_t* client, const vw_audio_chunk_t* chunk) {
  if (!client || !client->pipe_handle || !client->session_active || !chunk) return false;

  vw_msg_audio_t audio = {.start_pts_us = chunk->start_pts_us,
                          .duration_us = chunk->duration_us,
                          .pcm_bytes = chunk->bytes,
                          .pcm_data = chunk->pcm_data};
  memcpy(audio.session_id.bytes, client->session_id, 16);

  uint8_t payload_buf[2 * VW_AUDIO_CHUNK_MAX_PCM_BYTES];  // oversized
  size_t payload_len = 0;
  if (!vw_protocol_encode_payload(VW_MSG_AUDIO_PCM, &audio, payload_buf, sizeof(payload_buf), &payload_len))
    return false;

  vw_frame_header_t hdr = {.magic = VW_PROTOCOL_MAGIC,
                           .major = VW_PROTOCOL_VERSION_MAJOR,
                           .type = VW_MSG_AUDIO_PCM,
                           .payload_length = (uint32_t)payload_len,
                           .sequence = ++client->sequence};

  uint8_t hdr_buf[sizeof(vw_frame_header_t)];
  if (!vw_protocol_encode_header(&hdr, hdr_buf, sizeof(hdr_buf))) return false;

  if (!vw_ipc_send(client->pipe_handle, hdr_buf, sizeof(hdr_buf))) {
    vw_worker_client_drop_transport(client);
    return false;
  }
  if (!vw_ipc_send(client->pipe_handle, payload_buf, payload_len)) {
    vw_worker_client_drop_transport(client);
    return false;
  }

  return true;
}

bool vw_worker_client_send_model_ctrl(vw_worker_client_t* client, uint8_t action, const char* model_id) {
  // Model provisioning is worker-scoped, not caption-session-scoped: a missing selected model can reject START,
  // while the same authenticated worker must still accept DOWNLOAD/ABORT with a zero session id.
  if (!client || !client->pipe_handle) return false;
  vw_msg_model_ctrl_t msg;
  memset(&msg, 0, sizeof(msg));
  msg.action = action;
  if (model_id) {
    snprintf(msg.model_id, sizeof(msg.model_id), "%s", model_id);
  }
  uint8_t payload_buf[128];
  size_t payload_len = 0;
  if (!vw_protocol_encode_payload(VW_MSG_MODEL_CTRL, &msg, payload_buf, sizeof(payload_buf), &payload_len))
    return false;
  vw_frame_header_t hdr = {.magic = VW_PROTOCOL_MAGIC,
                           .major = VW_PROTOCOL_VERSION_MAJOR,
                           .type = VW_MSG_MODEL_CTRL,
                           .payload_length = (uint32_t)payload_len,
                           .sequence = ++client->sequence};
  uint8_t hdr_buf[sizeof(vw_frame_header_t)];
  if (!vw_protocol_encode_header(&hdr, hdr_buf, sizeof(hdr_buf))) return false;
  if (!vw_ipc_send(client->pipe_handle, hdr_buf, sizeof(hdr_buf)) ||
      !vw_ipc_send(client->pipe_handle, payload_buf, payload_len)) {
    vw_worker_client_drop_transport(client);
    return false;
  }
  return true;
}

bool vw_worker_client_send_translate_ctrl(vw_worker_client_t* client, bool enabled, const char* source_lang,
                                          const char* target_lang, uint8_t mode) {
  if (!client || !client->pipe_handle) return false;
  if ((client->worker_capabilities & VW_CAPABILITY_TRANSLATION) == 0) {
    // Same-major older workers remain usable when translation is disabled. Enabling an unsupported optional feature
    // fails locally without sending an unknown message that would desynchronize/terminate the older worker.
    if (!enabled) {
      client->translation_configured = true;
      client->translate_enabled = false;
      snprintf(client->translate_source_lang, sizeof(client->translate_source_lang), "%s",
               source_lang ? source_lang : "auto");
      snprintf(client->translate_target_lang, sizeof(client->translate_target_lang), "%s",
               target_lang ? target_lang : "en");
      client->translate_mode = mode;
    }
    return !enabled;
  }
  vw_msg_translate_ctrl_t ctrl;
  memset(&ctrl, 0, sizeof(ctrl));
  memcpy(ctrl.session_id.bytes, client->session_id, VW_SESSION_ID_BYTES);
  ctrl.enabled = enabled ? 1 : 0;
  snprintf(ctrl.source_lang, sizeof(ctrl.source_lang), "%s", source_lang ? source_lang : "auto");
  snprintf(ctrl.target_lang, sizeof(ctrl.target_lang), "%s", target_lang ? target_lang : "en");
  ctrl.mode = mode;

  uint8_t payload_buf[VW_MSG_TRANSLATE_CTRL_PAYLOAD_BYTES];
  size_t payload_len = 0;
  if (!vw_protocol_encode_payload(VW_MSG_TRANSLATE_CTRL, &ctrl, payload_buf, sizeof(payload_buf), &payload_len))
    return false;
  vw_frame_header_t hdr = {.magic = VW_PROTOCOL_MAGIC,
                           .major = VW_PROTOCOL_VERSION_MAJOR,
                           .type = VW_MSG_TRANSLATE_CTRL,
                           .payload_length = (uint32_t)payload_len,
                           .sequence = ++client->sequence};
  uint8_t hdr_buf[sizeof(vw_frame_header_t)];
  if (!vw_protocol_encode_header(&hdr, hdr_buf, sizeof(hdr_buf))) return false;
  if (!vw_ipc_send(client->pipe_handle, hdr_buf, sizeof(hdr_buf)) ||
      !vw_ipc_send(client->pipe_handle, payload_buf, payload_len)) {
    vw_worker_client_drop_transport(client);
    return false;
  }
  client->translation_configured = true;
  client->translate_enabled = enabled;
  snprintf(client->translate_source_lang, sizeof(client->translate_source_lang), "%s", ctrl.source_lang);
  snprintf(client->translate_target_lang, sizeof(client->translate_target_lang), "%s", ctrl.target_lang);
  client->translate_mode = mode;
  return true;
}

// Sends one control frame (PAUSE/RESUME/STOP) stamped with the client's session id and reason.
// Drops the transport fail-closed on any write failure (the stream is mis-framed or dead — a
// partial control frame can never be re-synced). Returns true only when the whole frame was sent.
static bool send_control_frame(vw_worker_client_t* client, vw_message_type_t type, uint16_t reason) {
  if (!client || !client->pipe_handle) return false;
  vw_msg_control_t ctrl = {.reason = reason};
  memcpy(ctrl.session_id.bytes, client->session_id, 16);
  uint8_t payload_buf[64];
  size_t payload_len = 0;
  if (!vw_protocol_encode_payload(type, &ctrl, payload_buf, sizeof(payload_buf), &payload_len)) return false;
  vw_frame_header_t hdr = {.magic = VW_PROTOCOL_MAGIC,
                           .major = VW_PROTOCOL_VERSION_MAJOR,
                           .type = type,
                           .payload_length = (uint32_t)payload_len,
                           .sequence = ++client->sequence};
  uint8_t hdr_buf[sizeof(vw_frame_header_t)];
  if (!vw_protocol_encode_header(&hdr, hdr_buf, sizeof(hdr_buf))) return false;
  bool ok1 = vw_ipc_send(client->pipe_handle, hdr_buf, sizeof(hdr_buf));
  bool ok2 = vw_ipc_send(client->pipe_handle, payload_buf, payload_len);
  if (ok1 && ok2) {
    return true;
  }
  vw_worker_client_drop_transport(client);
  return false;
}

void vw_worker_client_stop_session(vw_worker_client_t* client, uint16_t reason) {
  if (!client || !client->pipe_handle || !client->session_active) return;
  if (send_control_frame(client, VW_MSG_STOP_SESSION, reason)) {
    client->session_active = false;
  }
}

void vw_worker_client_pause_session(vw_worker_client_t* client) {
  if (!client || !client->pipe_handle || !client->session_active) return;
  send_control_frame(client, VW_MSG_PAUSE, VW_CTRL_REASON_USER_PAUSE);
}

void vw_worker_client_resume_session(vw_worker_client_t* client) {
  if (!client || !client->pipe_handle || !client->session_active) return;
  send_control_frame(client, VW_MSG_RESUME, VW_CTRL_REASON_USER_RESUME);
}

void vw_worker_client_shutdown(vw_worker_client_t* client) {
  if (!client || !client->pipe_handle) return;
  vw_frame_header_t hdr = {.magic = VW_PROTOCOL_MAGIC,
                           .major = VW_PROTOCOL_VERSION_MAJOR,
                           .type = VW_MSG_SHUTDOWN,
                           .payload_length = 0,
                           .sequence = ++client->sequence};
  uint8_t hdr_buf[sizeof(vw_frame_header_t)];
  if (vw_protocol_encode_header(&hdr, hdr_buf, sizeof(hdr_buf))) {
    vw_ipc_send(client->pipe_handle, hdr_buf, sizeof(hdr_buf));
  }
}

int vw_worker_client_receive_frame(vw_worker_client_t* client, uint32_t timeout_us, vw_worker_recv_t* out) {
  if (!client || !client->pipe_handle || !out) {
    // A NULL pipe handle means the transport was dropped (dead) — report fatal, not the timeout
    // value, so a caller can never mistake a dead client for a benign poll timeout.
    return VW_IPC_RECV_FATAL;
  }
  memset(out, 0, sizeof(*out));

  // One per-call deadline for waiting for a frame to START (header). The transport is
  // message-oriented, so a header arrives whole or not at all: a clean header timeout (0 bytes
  // consumed) is a safe frame boundary and returns 0. Once the header is consumed the stream is
  // mid-frame — finish it with the transport's own receive bound (3 s) so a transient delay
  // between the header and payload messages cannot desync the stream. Any payload failure (timeout
  // or fatal) still drops: the header is gone, so a later call would read the payload as a header.
  const int64_t deadline_us = vw_platform_get_monotonic_time_us() + (int64_t)timeout_us;

  while (vw_platform_get_monotonic_time_us() < deadline_us) {
    uint8_t hdr_buf[sizeof(vw_frame_header_t)];
    int hdr_rc = receive_all(client->pipe_handle, hdr_buf, sizeof(hdr_buf), deadline_us);
    if (hdr_rc != VW_IPC_RECV_OK) {
      if (hdr_rc == VW_IPC_RECV_TIMEOUT) {
        return VW_IPC_RECV_TIMEOUT;  // clean timeout: no header bytes consumed, still at a frame boundary
      }
      vw_worker_client_drop_transport(client);
      return VW_IPC_RECV_FATAL;  // fatal: transport dead, caller must stop using the client
    }

    vw_frame_header_t hdr;
    if (!vw_protocol_decode_header(hdr_buf, sizeof(hdr_buf), &hdr)) {
      // Header bytes were consumed but don't parse: framing is lost, so this is a fatal transport
      // state — report VW_IPC_RECV_FATAL, never the timeout value, or the sender would keep polling
      // a dropped client instead of marking the worker dead.
      vw_worker_client_drop_transport(client);
      return VW_IPC_RECV_FATAL;
    }
    if (!vw_protocol_validate_header(&hdr)) {
      vw_worker_client_drop_transport(client);
      return VW_IPC_RECV_FATAL;
    }
    // VW-016: Monotonic per-direction sequence validation. Reject duplicate/reordered/replayed frames.
    if (client->worker_sequence_valid && hdr.sequence <= client->last_worker_sequence) {
      vw_log_event(VW_LOG_LEVEL_WARN, "CLIENT_SEQUENCE", "stale worker sequence %llu <= %llu type=%u; discarding",
                   (unsigned long long)hdr.sequence, (unsigned long long)client->last_worker_sequence, hdr.type);
      // Drain declared payload to keep framing, then discard.
      if (hdr.payload_length > 0) {
        uint8_t* tmp = (uint8_t*)malloc(hdr.payload_length);
        if (tmp) {
          const int64_t drain_deadline = vw_platform_get_monotonic_time_us() + 3000000;
          if (receive_all(client->pipe_handle, tmp, hdr.payload_length, drain_deadline) != VW_IPC_RECV_OK) {
            free(tmp);
            vw_worker_client_drop_transport(client);
            return VW_IPC_RECV_FATAL;
          }
          free(tmp);
        } else {
          vw_worker_client_drop_transport(client);
          return VW_IPC_RECV_FATAL;
        }
      }
      continue;  // discard stale frame within same deadline, do not desync
    }
    client->last_worker_sequence = hdr.sequence;
    client->worker_sequence_valid = true;

    const int64_t payload_deadline_us = vw_platform_get_monotonic_time_us() + 3000000;  // transport bound
    uint8_t* payload = NULL;
    if (hdr.payload_length > 0) {
      payload = (uint8_t*)malloc(hdr.payload_length);
      if (!payload) {
        vw_worker_client_drop_transport(client);
        return VW_IPC_RECV_FATAL;
      }
      if (receive_all(client->pipe_handle, payload, hdr.payload_length, payload_deadline_us) != VW_IPC_RECV_OK) {
        free(payload);
        vw_worker_client_drop_transport(client);
        return VW_IPC_RECV_FATAL;  // desync: header consumed but payload incomplete; framing is lost
      }
    }

    // Decode the three worker->plugin types; anything else is drained and skipped.
    bool decoded = false;
    switch (hdr.type) {
      case VW_MSG_CAPTION_SEGMENT: {
        vw_caption_segment_t* seg = &out->segment;
        if (vw_protocol_decode_payload(VW_MSG_CAPTION_SEGMENT, payload, hdr.payload_length, seg) &&
            vw_protocol_validate_payload(VW_MSG_CAPTION_SEGMENT, seg)) {
          // Copy segment text into owned storage so the caller's zero-heap path never aliases
          // the freed wire payload.
          uint16_t n = seg->text_bytes;
          memcpy(out->text_buf, seg->text_utf8, n);
          out->text_buf[n] = '\0';
          seg->text_utf8 = out->text_buf;
          seg->text_bytes = n;

          if (seg->translated_text_utf8 && seg->translated_text_bytes > 0) {
            uint16_t tn = seg->translated_text_bytes;
            memcpy(out->trans_text_buf, seg->translated_text_utf8, tn);
            out->trans_text_buf[tn] = '\0';
            seg->translated_text_utf8 = out->trans_text_buf;
            seg->translated_text_bytes = tn;
          } else {
            out->trans_text_buf[0] = '\0';
            seg->translated_text_utf8 = NULL;
            seg->translated_text_bytes = 0;
          }

          out->type = VW_MSG_CAPTION_SEGMENT;
          decoded = true;
        }
        break;
      }
      case VW_MSG_STATUS:
        if (vw_protocol_decode_payload(VW_MSG_STATUS, payload, hdr.payload_length, &out->status) &&
            vw_protocol_validate_payload(VW_MSG_STATUS, &out->status)) {
          out->type = VW_MSG_STATUS;
          decoded = true;
        }
        break;
      case VW_MSG_ERROR:
        if (vw_protocol_decode_payload(VW_MSG_ERROR, payload, hdr.payload_length, &out->error) &&
            vw_protocol_validate_payload(VW_MSG_ERROR, &out->error)) {
          out->type = VW_MSG_ERROR;
          decoded = true;
        }
        break;
      case VW_MSG_MODEL_PROGRESS:
        if (vw_protocol_decode_payload(VW_MSG_MODEL_PROGRESS, payload, hdr.payload_length, &out->progress) &&
            vw_protocol_validate_payload(VW_MSG_MODEL_PROGRESS, &out->progress)) {
          out->type = VW_MSG_MODEL_PROGRESS;
          decoded = true;
        }
        break;
      default:
        // Unknown type (e.g. PAUSE/RESUME/STARTED arriving late): consume and skip.
        break;
    }
    free(payload);
    if (decoded) {
      return VW_IPC_RECV_OK;
    }
    // Malformed or unsupported frame: keep draining within the same deadline.
  }
  return VW_IPC_RECV_TIMEOUT;  // deadline expired at a frame boundary
}

bool vw_worker_client_is_source_active(const vw_worker_client_t* client) {
  return client && client->session_active && client->worker_source_active;
}
