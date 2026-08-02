#include "vw_worker_client.h"

#include <stdlib.h>
#include <string.h>

#include "vw_ipc_transport.h"
#include "vw_platform.h"
#include "vw_protocol_codec.h"
#include "vw_protocol_types.h"

// Receive exactly len bytes, retrying on partial reads. Returns true on success, false on timeout/EOF/error.
static bool receive_all(vw_ipc_handle_t* ipc, uint8_t* buf, size_t len) {
  size_t got = 0;
  while (got < len) {
    int32_t res = vw_ipc_receive(ipc, buf + got, len - got);
    if (res <= 0) return false;  // 0 = timeout, -1 = error/EOF
    got += (size_t)res;
  }
  return true;
}

vw_worker_client_t* vw_worker_client_launch_and_connect(const char* executable_path, const char* endpoint_name,
                                                        const uint8_t auth_token[VW_AUTH_TOKEN_BYTES]) {
  if (!endpoint_name || !auth_token) {
    return NULL;
  }

  // Spawn the worker first so it can bind the endpoint before we connect.
  if (executable_path) {
    const char* argv[] = {executable_path, NULL};
    if (!vw_platform_spawn_process(executable_path, argv)) {
      return NULL;
    }
  }

  vw_ipc_handle_t* ipc = vw_ipc_connect(endpoint_name);
  if (!ipc) {
    return NULL;
  }

  vw_worker_client_t* client = (vw_worker_client_t*)calloc(1, sizeof(vw_worker_client_t));
  if (!client) {
    vw_ipc_close(ipc);
    return NULL;
  }
  client->pipe_handle = ipc;

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

  // Wait for HELLO_ACK (header, then payload)
  uint8_t ack_hdr_buf[sizeof(vw_frame_header_t)];
  if (!receive_all(ipc, ack_hdr_buf, sizeof(ack_hdr_buf))) goto fail;
  vw_frame_header_t ack_hdr;
  if (!vw_protocol_decode_header(ack_hdr_buf, sizeof(ack_hdr_buf), &ack_hdr)) goto fail;  // validates header too
  if (ack_hdr.type != VW_MSG_HELLO_ACK) goto fail;

  if (ack_hdr.payload_length > 0 && ack_hdr.payload_length <= VW_MAX_PAYLOAD_BYTES) {
    uint8_t* ack_payload = (uint8_t*)malloc(ack_hdr.payload_length);
    if (!ack_payload) goto fail;
    bool decoded = receive_all(ipc, ack_payload, ack_hdr.payload_length);
    if (decoded) {
      vw_msg_hello_ack_t ack;
      decoded = vw_protocol_decode_payload(VW_MSG_HELLO_ACK, ack_payload, ack_hdr.payload_length, &ack);
    }
    free(ack_payload);
    if (!decoded) goto fail;
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
    free(client);
  }
}
