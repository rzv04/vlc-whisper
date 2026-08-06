#include "vw_worker.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vw_ipc_transport.h"
#include "vw_protocol_codec.h"
#include "vw_protocol_types.h"

// Constant-time comparison of two 32-byte tokens to prevent timing attacks
static bool verify_token_constant_time(const uint8_t token_a[VW_AUTH_TOKEN_BYTES],
                                       const uint8_t token_b[VW_AUTH_TOKEN_BYTES]) {
  volatile uint8_t diff = 0;
  for (size_t i = 0; i < VW_AUTH_TOKEN_BYTES; i++) {
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
        if (res == VW_IPC_RECV_TIMEOUT) continue;  // timeout — keep waiting (video pause)
        running = false;                           // fatal (VW_IPC_RECV_FATAL): peer closed / broken pipe
        break;
      }
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

      // receive the payload in a loop to handle partial reads
      uint32_t payload_read = 0;
      while (payload_read < header.payload_length) {
        int32_t res = vw_ipc_receive(handle, payload_buf + payload_read, header.payload_length - payload_read);
        if (res < 0) {
          if (res == VW_IPC_RECV_TIMEOUT) continue;  // timeout — keep waiting (video pause)
          running = false;                           // fatal (VW_IPC_RECV_FATAL): peer closed / broken pipe
          break;
        }
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

    // also enforce after receiving
    if (!valid_payload && header.payload_length > 0) {
      free(payload_buf);
      break;  // Invalid payload
    }

    if (!authenticated) {
      if (header.type != VW_MSG_HELLO) {
        free(payload_buf);
        break;  // First message must be HELLO
      }
      if (!verify_token_constant_time(config->auth_token, payload_decoded.hello.auth_token)) {
        free(payload_buf);
        break;  // Auth failed
      }
      authenticated = true;

      // Reply HELLO_ACK with the negotiated version and supported capabilities
      vw_msg_hello_ack_t ack = {.selected_major = VW_PROTOCOL_VERSION_MAJOR,
                                .selected_minor = VW_PROTOCOL_VERSION_MINOR,
                                .capability_flags = VW_CAPABILITY_PCM_S16LE_16K_MONO,
                                .worker_version = VW_WORKER_VERSION,
                                .worker_version_length = VW_WORKER_VERSION_LENGTH};
      uint8_t ack_payload[256];
      size_t ack_len = 0;
      if (!vw_protocol_encode_payload(VW_MSG_HELLO_ACK, &ack, ack_payload, sizeof(ack_payload), &ack_len)) {
        free(payload_buf);
        break;
      }
      vw_frame_header_t ack_hdr = {.magic = VW_PROTOCOL_MAGIC,
                                   .major = VW_PROTOCOL_VERSION_MAJOR,
                                   .type = VW_MSG_HELLO_ACK,
                                   .payload_length = (uint32_t)ack_len,
                                   .sequence = 1};
      uint8_t ack_hdr_buf[sizeof(vw_frame_header_t)];
      if (!vw_protocol_encode_header(&ack_hdr, ack_hdr_buf, sizeof(ack_hdr_buf))) {
        free(payload_buf);
        break;
      }
      vw_ipc_send(handle, ack_hdr_buf, sizeof(ack_hdr_buf));
      vw_ipc_send(handle, ack_payload, ack_len);

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
