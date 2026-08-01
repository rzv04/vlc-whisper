#include "vw_worker.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vw_ipc_transport.h"
#include "vw_protocol_codec.h"
#include "vw_protocol_types.h"

// Constant-time comparison of two 32-byte tokens to prevent timing attacks
static bool verify_token_constant_time(const uint8_t token_a[VW_CAPABILITY_TOKEN_BYTES],
                                       const uint8_t token_b[VW_CAPABILITY_TOKEN_BYTES]) {
  volatile uint8_t diff = 0;
  for (size_t i = 0; i < VW_CAPABILITY_TOKEN_BYTES; i++) {
    diff |= (token_a[i] ^ token_b[i]);
  }
  return diff == 0;
}

int vw_worker_run(const vw_worker_config_t* config) {
  if (!config) {
    return 1;
  }

  vw_ipc_handle_t* handle = vw_ipc_listen(config->pipe_name);
  if (!handle) {
    return 1;
  }

  bool running = true;
  bool authenticated = false;
  uint8_t header_buf[sizeof(vw_frame_header_t)];

  while (running) {
    int32_t bytes_read = 0;
    while (bytes_read < (int32_t)sizeof(vw_frame_header_t)) {
      int32_t res = vw_ipc_receive(handle, header_buf + bytes_read, sizeof(vw_frame_header_t) - bytes_read);
      if (res < 0) {
        running = false;
        break;
      }
      if (res == 0) continue;  // timeout, no data yet — keep waiting
      bytes_read += res;
    }
    if (!running) break;

    vw_frame_header_t header;
    if (!vw_protocol_decode_header(header_buf, sizeof(vw_frame_header_t), &header)) {
      break;
    }

    if (!vw_protocol_validate_header(&header)) {
      break;
    }

    uint8_t* payload_buf = NULL;
    if (header.payload_length > 0) {
      payload_buf = (uint8_t*)malloc(header.payload_length);
      if (!payload_buf) break;

      uint32_t payload_read = 0;
      while (payload_read < header.payload_length) {
        int32_t res = vw_ipc_receive(handle, payload_buf + payload_read, header.payload_length - payload_read);
        if (res < 0) {
          running = false;
          break;
        }
        if (res == 0) continue;  // timeout, no data yet — keep waiting
        payload_read += res;
      }
    }

    if (!running) {
      free(payload_buf);
      break;
    }

    union {
      vw_msg_hello_t hello;
      vw_msg_start_t start;
      vw_msg_audio_t audio;
      vw_msg_control_t control;
      vw_msg_status_t status;
    } payload_decoded;

    memset(&payload_decoded, 0, sizeof(payload_decoded));

    bool valid_payload = false;
    if (vw_protocol_decode_payload(header.type, payload_buf, header.payload_length, &payload_decoded)) {
      if (vw_protocol_validate_payload(header.type, &payload_decoded)) {
        valid_payload = true;
      }
    }

    if (!valid_payload && header.payload_length > 0) {
      free(payload_buf);
      break;  // Invalid payload
    }

    if (!authenticated) {
      if (header.type != VW_MSG_HELLO) {
        free(payload_buf);
        break;  // First message must be HELLO
      }
      if (!verify_token_constant_time(config->token, payload_decoded.hello.token)) {
        free(payload_buf);
        break;  // Auth failed
      }
      authenticated = true;

      // We could send HELLO_ACK here
      // For now just continue
      free(payload_buf);
      continue;
    }

    switch (header.type) {
      case VW_MSG_START_SESSION:
        // Handle start session
        break;
      case VW_MSG_AUDIO_PCM:
        // Handle audio chunk
        break;
      case VW_MSG_STOP_SESSION:
        // Handle stop session
        break;
      case VW_MSG_SHUTDOWN:
        running = false;
        break;
      default:
        // Ignored or handled differently
        break;
    }

    free(payload_buf);
  }

  vw_ipc_close(handle);
  return authenticated ? 0 : 1;
}
