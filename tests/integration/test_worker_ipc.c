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

static void send_hello(vw_ipc_handle_t* handle, const uint8_t* token) {
  vw_msg_hello_t hello_msg;
  memset(&hello_msg, 0, sizeof(hello_msg));
  hello_msg.min_major = 1;
  hello_msg.max_major = 1;
  memcpy(hello_msg.token, token, VW_CAPABILITY_TOKEN_BYTES);
  hello_msg.client_version_length = 5;
  hello_msg.client_version = "1.0.0";

  uint8_t payload_buf[256];
  size_t written = 0;
  EXPECT(vw_protocol_encode_payload(VW_MSG_HELLO, &hello_msg, payload_buf, sizeof(payload_buf), &written));

  vw_frame_header_t hdr;
  hdr.magic = VW_PROTOCOL_MAGIC;
  hdr.major = VW_PROTOCOL_VERSION_MAJOR;
  hdr.type = VW_MSG_HELLO;
  hdr.payload_length = written;
  hdr.sequence = 0;

  uint8_t hdr_buf[20];
  EXPECT(vw_protocol_encode_header(&hdr, hdr_buf, sizeof(hdr_buf)));

  vw_ipc_send(handle, hdr_buf, sizeof(hdr_buf));
  vw_ipc_send(handle, payload_buf, written);
}

int main(void) {
  vw_worker_config_t config;
  memset(&config, 0, sizeof(config));
  strncpy(config.pipe_name, "test_ipc_socket", sizeof(config.pipe_name) - 1);
  for (size_t i = 0; i < VW_CAPABILITY_TOKEN_BYTES; i++) config.token[i] = (uint8_t)i;

  pthread_t thread;
  int err = pthread_create(&thread, NULL, worker_thread, &config);
  (void)err;
  assert(err == 0);

  // Give listener time to bind
  usleep(100000);

  vw_worker_client_t* client = vw_worker_client_launch_and_connect(NULL, config.pipe_name, NULL);
  EXPECT(client != NULL);

  send_hello((vw_ipc_handle_t*)client->pipe_handle, config.token);
  usleep(50000);

  // Send a valid SHUTDOWN message
  vw_frame_header_t hdr;
  hdr.magic = VW_PROTOCOL_MAGIC;
  hdr.major = VW_PROTOCOL_VERSION_MAJOR;
  hdr.type = VW_MSG_SHUTDOWN;
  hdr.payload_length = 0;
  hdr.sequence = 1;

  uint8_t hdr_buf[20];
  EXPECT(vw_protocol_encode_header(&hdr, hdr_buf, 20));

  vw_ipc_send((vw_ipc_handle_t*)client->pipe_handle, hdr_buf, 20);

  // Wait for worker to exit
  void* ret_val;
  pthread_join(thread, &ret_val);
  EXPECT((int)(intptr_t)ret_val == 0);

  vw_worker_client_disconnect(client);

  printf("test_worker_ipc PASSED\n");
  return 0;
}
