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
// (VW_IPC_RECV_TIMEOUT). Returns false on fatal error (EOF / broken pipe,
// VW_IPC_RECV_FATAL) or if the total handshake deadline (deadline_us) expires.
static bool receive_all(vw_ipc_handle_t* ipc, uint8_t* buf, size_t len, int64_t deadline_us) {
  size_t got = 0;
  while (got < len) {
    int64_t now_us = vw_platform_get_monotonic_time_us();
    if (now_us >= deadline_us) return false;
    uint32_t remaining_us = (uint32_t)(deadline_us - now_us);
    uint32_t timeout_us = (remaining_us < 3000000U) ? remaining_us : 3000000U;

    int32_t res = vw_ipc_receive_timeout(ipc, buf + got, len - got, timeout_us);
    if (res < 0) {
      if (res == VW_IPC_RECV_TIMEOUT) {  // timeout — keep waiting, bounded by the deadline
        if (vw_platform_get_monotonic_time_us() >= deadline_us) return false;
        continue;
      }
      return false;  // fatal (VW_IPC_RECV_FATAL): EOF / broken pipe / closed
    }
    got += (size_t)res;
  }
  return true;
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

vw_worker_client_t* vw_worker_client_launch_and_connect(const char* executable_path, const char* endpoint_name,
                                                        const uint8_t auth_token[VW_AUTH_TOKEN_BYTES],
                                                        const char* model_path) {
  if (!endpoint_name || !auth_token) {
    return NULL;
  }

  // Spawn the worker first so it can bind the endpoint before we connect.
  vw_process_t worker_process = (vw_process_t)0;
  if (executable_path) {
    char token_hex[VW_AUTH_TOKEN_BYTES * 2 + 1];  // null terminated
    token_to_hex(auth_token, token_hex);
    // Exactly two argv shapes: with and without --model. NULL model_path omits the flag so the
    // worker's CWD-relative default stays the fallback.
    const char* argv[8];
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
  if (!receive_all(ipc, ack_hdr_buf, sizeof(ack_hdr_buf), handshake_deadline_us)) goto fail;
  vw_frame_header_t ack_hdr;
  if (!vw_protocol_decode_header(ack_hdr_buf, sizeof(ack_hdr_buf), &ack_hdr)) goto fail;  // validates header too
  if (ack_hdr.type != VW_MSG_HELLO_ACK) goto fail;

  if (ack_hdr.payload_length == 0 || ack_hdr.payload_length > 1024) {
    goto fail;
  }
  uint8_t* ack_payload = (uint8_t*)malloc(ack_hdr.payload_length);
  if (!ack_payload) goto fail;
  bool ack_ok = receive_all(ipc, ack_payload, ack_hdr.payload_length, handshake_deadline_us);
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

bool vw_worker_client_start_session(vw_worker_client_t* client, int64_t timeline_origin_pts_us, const char* model_id) {
  if (!client || !client->pipe_handle || client->session_active) return false;

  if (!vw_platform_get_random_bytes(client->session_id, sizeof(client->session_id))) return false;

  vw_msg_start_t start = {.timeline_origin_pts_us = timeline_origin_pts_us,
                          .sample_rate = 16000,
                          .channels = 1,
                          .sample_format = VW_AUDIO_FORMAT_S16,
                          .source_kind = VW_SOURCE_LOCAL_FILE};
  memcpy(start.session_id.bytes, client->session_id, 16);
  if (model_id) strncpy(start.model_id, model_id, sizeof(start.model_id) - 1);
  strncpy(start.language, "en", sizeof(start.language) - 1);

  uint8_t payload_buf[1024];
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
    if (!receive_all(client->pipe_handle, resp_hdr_buf, sizeof(resp_hdr_buf), deadline_us)) {
      // Deadline expired or fatal EOF, possibly mid-frame: the next frame
      // boundary is unknowable and a late STARTED from this attempt could be
      // mistaken for a retry's confirmation. Drop so a retry cannot desync.
      vw_worker_client_drop_transport(client);
      return false;
    }

    vw_frame_header_t resp_hdr;
    if (!vw_protocol_decode_header(resp_hdr_buf, sizeof(resp_hdr_buf), &resp_hdr)) {
      vw_worker_client_drop_transport(client);
      return false;
    }

    if (resp_hdr.type == VW_MSG_STARTED) {
      client->session_active = true;
      return true;
    } else if (resp_hdr.type == VW_MSG_ERROR) {
      if (resp_hdr.payload_length > 0) {
        uint8_t* resp_payload = (uint8_t*)malloc(resp_hdr.payload_length);
        if (!resp_payload) {
          // Declared payload cannot be drained; framing is lost.
          vw_worker_client_drop_transport(client);
          return false;
        }
        bool drained = receive_all(client->pipe_handle, resp_payload, resp_hdr.payload_length, deadline_us);
        vw_msg_error_t err;
        memset(&err, 0, sizeof(err));
        if (drained && vw_protocol_decode_payload(VW_MSG_ERROR, resp_payload, resp_hdr.payload_length, &err)) {
          // Surface the worker's rejection (e.g. E_MODEL_MISSING) so the model-absent path is diagnosable.
          vw_log_event(VW_LOG_LEVEL_WARN, "WORKER_START_ERROR", "code=%u recoverable=%u msg=%.*s", err.error_code,
                       err.recoverable, (int)strnlen(err.message, VW_MAX_ERROR_MSG_BYTES), err.message);
        }
        free(resp_payload);
        if (!drained) {
          vw_worker_client_drop_transport(client);
          return false;
        }
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
      bool drained = receive_all(client->pipe_handle, resp_payload, resp_hdr.payload_length, deadline_us);
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

void vw_worker_client_stop_session(vw_worker_client_t* client, uint16_t reason) {
  if (!client || !client->pipe_handle || !client->session_active) return;
  vw_msg_control_t ctrl = {.reason = reason};
  memcpy(ctrl.session_id.bytes, client->session_id, 16);
  uint8_t payload_buf[64];
  size_t payload_len = 0;
  if (vw_protocol_encode_payload(VW_MSG_STOP_SESSION, &ctrl, payload_buf, sizeof(payload_buf), &payload_len)) {
    vw_frame_header_t hdr = {.magic = VW_PROTOCOL_MAGIC,
                             .major = VW_PROTOCOL_VERSION_MAJOR,
                             .type = VW_MSG_STOP_SESSION,
                             .payload_length = (uint32_t)payload_len,
                             .sequence = ++client->sequence};
    uint8_t hdr_buf[sizeof(vw_frame_header_t)];
    if (vw_protocol_encode_header(&hdr, hdr_buf, sizeof(hdr_buf))) {
      bool ok1 = vw_ipc_send(client->pipe_handle, hdr_buf, sizeof(hdr_buf));
      bool ok2 = vw_ipc_send(client->pipe_handle, payload_buf, payload_len);
      if (ok1 && ok2) {
        client->session_active = false;
      } else {
        // Header-only or no-op write: the stream is desynced (worker waits
        // for the declared STOP payload) or dead. Never leave session_active
        // set against a mis-framed connection — drop it so a replacement
        // session cannot be started on a stream that will eat its frames.
        vw_worker_client_drop_transport(client);
      }
    }
  }
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
    return -1;
  }
  memset(out, 0, sizeof(*out));

  // One per-call deadline for the whole frame (header + payload + any skipped frames). A timeout
  // mid-frame is treated as no-frame (return 0) rather than a transport failure: the stream is
  // still framed because the deadline expired between messages.
  const int64_t deadline_us = vw_platform_get_monotonic_time_us() + (int64_t)timeout_us;

  while (vw_platform_get_monotonic_time_us() < deadline_us) {
    uint8_t hdr_buf[sizeof(vw_frame_header_t)];
    if (!receive_all(client->pipe_handle, hdr_buf, sizeof(hdr_buf), deadline_us)) {
      // Either the deadline expired (no frame arrived) or the transport died. Distinguish by clock.
      if (vw_platform_get_monotonic_time_us() < deadline_us) {
        vw_worker_client_drop_transport(client);
        return -1;  // fatal: transport dead, caller must stop using the client
      }
      return 0;  // timeout: no frame within timeout_us
    }

    vw_frame_header_t hdr;
    if (!vw_protocol_decode_header(hdr_buf, sizeof(hdr_buf), &hdr)) {
      vw_worker_client_drop_transport(client);
      return -1;
    }
    if (!vw_protocol_validate_header(&hdr)) {
      vw_worker_client_drop_transport(client);
      return -1;
    }

    // Read the declared payload, if any.
    uint8_t* payload = NULL;
    if (hdr.payload_length > 0) {
      payload = (uint8_t*)malloc(hdr.payload_length);
      if (!payload) {
        vw_worker_client_drop_transport(client);
        return -1;
      }
      if (!receive_all(client->pipe_handle, payload, hdr.payload_length, deadline_us)) {
        free(payload);
        if (vw_platform_get_monotonic_time_us() < deadline_us) {
          vw_worker_client_drop_transport(client);
          return -1;
        }
        return 0;  // timeout mid-payload: no complete frame
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
          if (n >= VW_MAX_TEXT_BYTES) {
            n = VW_MAX_TEXT_BYTES - 1;
          }
          memcpy(out->text_buf, seg->text_utf8, n);
          out->text_buf[n] = '\0';
          seg->text_utf8 = out->text_buf;
          seg->text_bytes = n;
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
      default:
        // Unknown type (e.g. PAUSE/RESUME/STARTED arriving late): consume and skip.
        break;
    }
    free(payload);
    if (decoded) {
      return 1;
    }
    // Malformed or unsupported frame: keep draining within the same deadline.
  }
  return 0;  // deadline expired
}
