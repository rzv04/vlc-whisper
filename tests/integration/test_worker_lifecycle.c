#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define _XOPEN_SOURCE 500
#include <unistd.h>

#include "vw_ipc_transport.h"
#include "vw_protocol_codec.h"
#include "vw_protocol_types.h"
#include "vw_test.h"
#include "vw_worker.h"
#include "vw_worker_client.h"

static void* worker_thread(void* arg) {
  vw_worker_config_t* config = (vw_worker_config_t*)arg;
  int res = vw_worker_run(config);
  return (void*)(intptr_t)res;
}

int main(void) {
  vw_worker_config_t config;
  memset(&config, 0, sizeof(config));
  strncpy(config.pipe_name, "test_lifecycle_socket", sizeof(config.pipe_name) - 1);
  for (int i = 0; i < VW_AUTH_TOKEN_BYTES; i++) config.auth_token[i] = (uint8_t)i;

  pthread_t thread;
  int err = pthread_create(&thread, NULL, worker_thread, &config);
  (void)err;
  assert(err == 0);
  usleep(100000);

  // 1. Try with wrong token: handshake must fail and the worker must exit
  uint8_t bad_token[VW_AUTH_TOKEN_BYTES] = {0};
  vw_worker_client_t* bad_client = vw_worker_client_launch_and_connect(NULL, config.pipe_name, bad_token);
  EXPECT(bad_client == NULL);  // HELLO rejected by worker

  void* ret_val;
  pthread_join(thread, &ret_val);
  EXPECT((int)(intptr_t)ret_val == 1);  // Worker exited due to bad auth

  // Restart listener
  err = pthread_create(&thread, NULL, worker_thread, &config);
  (void)err;
  assert(err == 0);
  usleep(100000);

  // 2. Connect with good token (handshake inside), send START_SESSION, then SHUTDOWN
  vw_worker_client_t* client = vw_worker_client_launch_and_connect(NULL, config.pipe_name, config.auth_token);
  EXPECT(client != NULL);  // HELLO handshake completed

  vw_msg_start_t start_msg = {.timeline_origin_pts_us = 0,
                              .sample_rate = 16000,
                              .channels = 1,
                              .sample_format = 1,
                              .model_id = "ggml-tiny.en.bin",
                              .language = "en",
                              .source_kind = VW_SOURCE_LOCAL_FILE};

  uint8_t payload_buf[1024];
  size_t written = 0;
  EXPECT(vw_protocol_encode_payload(VW_MSG_START_SESSION, &start_msg, payload_buf, sizeof(payload_buf), &written));

  vw_frame_header_t hdr;
  hdr.magic = VW_PROTOCOL_MAGIC;
  hdr.major = VW_PROTOCOL_VERSION_MAJOR;
  hdr.type = VW_MSG_START_SESSION;
  hdr.payload_length = written;
  hdr.sequence = 1;

  uint8_t hdr_buf[20];
  EXPECT(vw_protocol_encode_header(&hdr, hdr_buf, 20));

  vw_ipc_send((vw_ipc_handle_t*)client->pipe_handle, hdr_buf, 20);
  vw_ipc_send((vw_ipc_handle_t*)client->pipe_handle, payload_buf, written);

  usleep(50000);

  // Now send SHUTDOWN
  hdr.type = VW_MSG_SHUTDOWN;
  hdr.payload_length = 0;
  hdr.sequence = 2;
  EXPECT(vw_protocol_encode_header(&hdr, hdr_buf, 20));
  vw_ipc_send((vw_ipc_handle_t*)client->pipe_handle, hdr_buf, 20);

  pthread_join(thread, &ret_val);
  EXPECT((int)(intptr_t)ret_val == 0);  // Worker exited successfully

  vw_worker_client_disconnect(client);

  // 3. Client-side failure paths: bad args and no listener
  EXPECT(vw_worker_client_launch_and_connect(NULL, NULL, config.auth_token) == NULL);  // NULL endpoint
  EXPECT(vw_worker_client_launch_and_connect(NULL, "no_such_lifecycle_endpoint", config.auth_token) ==
         NULL);                                                                       // no listener
  EXPECT(vw_worker_client_launch_and_connect(NULL, config.pipe_name, NULL) == NULL);  // NULL token

  // 4. First frame is not HELLO: worker must reject the connection and exit non-zero
  err = pthread_create(&thread, NULL, worker_thread, &config);
  (void)err;
  assert(err == 0);
  usleep(100000);

  vw_ipc_handle_t* raw = vw_ipc_connect(config.pipe_name);
  EXPECT(raw != NULL);

  vw_frame_header_t non_hello = {.magic = VW_PROTOCOL_MAGIC,
                                 .major = VW_PROTOCOL_VERSION_MAJOR,
                                 .type = VW_MSG_SHUTDOWN,
                                 .payload_length = 0,
                                 .sequence = 1};
  uint8_t non_hello_buf[sizeof(vw_frame_header_t)];
  EXPECT(vw_protocol_encode_header(&non_hello, non_hello_buf, sizeof(non_hello_buf)));
  vw_ipc_send(raw, non_hello_buf, sizeof(non_hello_buf));

  pthread_join(thread, &ret_val);
  EXPECT((int)(intptr_t)ret_val == 1);  // First message must be HELLO
  vw_ipc_close(raw);

  printf("test_worker_lifecycle PASSED\n");
  return 0;
}
