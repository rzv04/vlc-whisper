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

#include "vw_ipc_transport.h"
#include "vw_platform.h"
#include "vw_protocol_codec.h"
#include "vw_protocol_types.h"

// Receive exactly len bytes, retrying on partial reads and on read timeouts
// (VW_IPC_RECV_TIMEOUT). Returns false on fatal error (EOF / broken pipe,
// VW_IPC_RECV_FATAL) or if the total handshake deadline (deadline_us) expires.
static bool receive_all(vw_ipc_handle_t* ipc, uint8_t* buf, size_t len, int64_t deadline_us) {
  size_t got = 0;
  while (got < len) {
    int32_t res = vw_ipc_receive(ipc, buf + got, len - got);
    if (res < 0) {
      if (res == VW_IPC_RECV_TIMEOUT) {  // timeout — keep waiting, bounded by the deadline
        if (vw_platform_get_time_us() >= deadline_us) return false;
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

vw_worker_client_t* vw_worker_client_launch_and_connect(const char* executable_path, const char* endpoint_name,
                                                        const uint8_t auth_token[VW_AUTH_TOKEN_BYTES]) {
  if (!endpoint_name || !auth_token) {
    return NULL;
  }

  // Spawn the worker first so it can bind the endpoint before we connect.
  if (executable_path) {
    char token_hex[VW_AUTH_TOKEN_BYTES * 2 + 1];  // null terminated
    token_to_hex(auth_token, token_hex);
    const char* argv[] = {executable_path, "--pipe", endpoint_name, "--token", token_hex, NULL};
    if (!vw_platform_spawn_process(executable_path, argv)) {
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

  // Total budget for the HELLO/HELLO_ACK reads: a silently-dead worker must not
  // hang module open forever (each vw_ipc_receive can block up to 3s on timeout).
  const int64_t handshake_deadline_us = vw_platform_get_time_us() + VW_HANDSHAKE_TIMEOUT_US;

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
    free(client);
  }
}
