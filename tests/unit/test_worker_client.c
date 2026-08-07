#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vw_ipc_transport.h"
#include "vw_platform.h"
#include "vw_protocol_codec.h"
#include "vw_protocol_types.h"
#include "vw_test.h"
#include "vw_worker_client.h"

// In-process mock server thread simulating worker IPC responses:
// 1. Listens on local Unix socket / named pipe.
// 2. Completes HELLO / HELLO_ACK handshake.
// 3. Receives START_SESSION and replies with STARTED after 100ms delay.
// 4. Receives AUDIO_PCM message using custom transport receive timeout.
// 5. Receives STOP_SESSION and SHUTDOWN control frames.
static void* fake_server_thread(void* arg) {
  const char* endpoint = (const char*)arg;
  vw_ipc_handle_t* server = vw_ipc_listen(endpoint);
  if (!server) return (void*)1;

  // Step 1: Receive HELLO frame from client launch_and_connect
  uint8_t hdr_buf[20];
  if (vw_ipc_receive(server, hdr_buf, 20) != 20) {
    vw_ipc_close(server);
    return (void*)2;
  }
  vw_frame_header_t hdr;
  vw_protocol_decode_header(hdr_buf, 20, &hdr);

  uint8_t payload[256];
  if (vw_ipc_receive(server, payload, hdr.payload_length) != (int32_t)hdr.payload_length) {
    vw_ipc_close(server);
    return (void*)3;
  }

  // Step 2: Send HELLO_ACK payload to confirm version & capability handshakes
  vw_msg_hello_ack_t ack = {
      .selected_major = VW_PROTOCOL_VERSION_MAJOR,
      .selected_minor = 0,
      .capability_flags = VW_CAPABILITY_PCM_S16LE_16K_MONO,
  };
  size_t ack_len = 0;
  vw_protocol_encode_payload(VW_MSG_HELLO_ACK, &ack, payload, sizeof(payload), &ack_len);
  vw_frame_header_t ack_hdr = {
      .magic = VW_PROTOCOL_MAGIC,
      .major = VW_PROTOCOL_VERSION_MAJOR,
      .type = VW_MSG_HELLO_ACK,
      .payload_length = (uint32_t)ack_len,
      .sequence = 1,
  };
  vw_protocol_encode_header(&ack_hdr, hdr_buf, 20);
  vw_ipc_send(server, hdr_buf, 20);
  vw_ipc_send(server, payload, ack_len);

  // Step 3: Receive START_SESSION payload sent by vw_worker_client_start_session
  if (vw_ipc_receive(server, hdr_buf, 20) != 20) {
    vw_ipc_close(server);
    return (void*)4;
  }
  vw_protocol_decode_header(hdr_buf, 20, &hdr);
  if (hdr.type != VW_MSG_START_SESSION) {
    vw_ipc_close(server);
    return (void*)5;
  }
  if (vw_ipc_receive(server, payload, hdr.payload_length) != (int32_t)hdr.payload_length) {
    vw_ipc_close(server);
    return (void*)6;
  }

  // Step 4: Delay 100ms (testing client's polling/waiting loop), then reply with zero-payload STARTED frame
  vw_platform_sleep_ms(100);
  vw_frame_header_t started_hdr = {
      .magic = VW_PROTOCOL_MAGIC,
      .major = VW_PROTOCOL_VERSION_MAJOR,
      .type = VW_MSG_STARTED,
      .payload_length = 0,
      .sequence = 2,
  };
  vw_protocol_encode_header(&started_hdr, hdr_buf, 20);
  vw_ipc_send(server, hdr_buf, 20);

  // Step 5: Receive AUDIO_PCM payload sent by vw_worker_client_send_audio (verifying vw_ipc_receive_timeout)
  if (vw_ipc_receive(server, hdr_buf, 20) != 20) {
    vw_ipc_close(server);
    return (void*)7;
  }
  vw_protocol_decode_header(hdr_buf, 20, &hdr);
  if (hdr.type != VW_MSG_AUDIO_PCM) {
    vw_ipc_close(server);
    return (void*)8;
  }
  uint8_t* big_payload = (uint8_t*)malloc(hdr.payload_length);
  if (vw_ipc_receive_timeout(server, big_payload, hdr.payload_length, 3000000) != (int32_t)hdr.payload_length) {
    free(big_payload);
    vw_ipc_close(server);
    return (void*)9;
  }
  free(big_payload);

  // Step 6: Receive STOP_SESSION control frame sent by vw_worker_client_stop_session
  if (vw_ipc_receive(server, hdr_buf, 20) != 20) {
    vw_ipc_close(server);
    return (void*)10;
  }
  vw_protocol_decode_header(hdr_buf, 20, &hdr);
  if (hdr.type != VW_MSG_STOP_SESSION) {
    vw_ipc_close(server);
    return (void*)11;
  }
  if (vw_ipc_receive(server, payload, hdr.payload_length) != (int32_t)hdr.payload_length) {
    vw_ipc_close(server);
    return (void*)12;
  }

  // Step 7: Receive SHUTDOWN control frame sent by vw_worker_client_shutdown
  if (vw_ipc_receive(server, hdr_buf, 20) != 20) {
    vw_ipc_close(server);
    return (void*)13;
  }
  vw_protocol_decode_header(hdr_buf, 20, &hdr);
  if (hdr.type != VW_MSG_SHUTDOWN) {
    vw_ipc_close(server);
    return (void*)14;
  }

  vw_ipc_close(server);
  return (void*)0;
}

int main(void) {
  const char* pipe_name = "test_worker_client_socket";
  uint8_t auth_token[VW_AUTH_TOKEN_BYTES] = {0};

  // Start in-process mock IPC server
  pthread_t thread;
  int err = pthread_create(&thread, NULL, fake_server_thread, (void*)pipe_name);
  EXPECT(err == 0);

  vw_platform_sleep_ms(100);

  // Test 1: Connect and perform HELLO/HELLO_ACK handshake
  vw_worker_client_t* client = vw_worker_client_launch_and_connect(NULL, pipe_name, auth_token);
  EXPECT(client != NULL);

  // Test 2: Start session and wait for STARTED confirmation
  EXPECT(vw_worker_client_start_session(client, 1000, "ggml-tiny.en.bin"));

  // Test 3: Frame and send PCM audio chunk
  uint8_t pcm_data[320] = {0};  // 10ms of 16kHz Mono S16LE audio (320 bytes)
  vw_audio_chunk_t chunk = {
      .start_pts_us = 1000,
      .duration_us = 10000,
      .sample_rate = 16000,
      .channels = 1,
      .bytes = 320,
  };
  memcpy(chunk.pcm_data, pcm_data, 320);
  EXPECT(vw_worker_client_send_audio(client, &chunk));

  // Test 4: Send STOP_SESSION and SHUTDOWN control frames
  vw_worker_client_stop_session(client, 0);
  vw_worker_client_shutdown(client);

  // Test 5: Verify server thread cleanly received all expected protocol frames
  void* ret_val;
  pthread_join(thread, &ret_val);
  EXPECT((int)(intptr_t)ret_val == 0);

  // Test 6: Disconnect IPC pipe and release client resources
  vw_worker_client_disconnect(client);

  return 0;
}
