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
#ifdef _WIN32
  // Windows named pipes require the \\\\.\\pipe\\ prefix (Unix sockets take a bare path).
  strncpy(config.pipe_name, "\\\\.\\pipe\\test_ipc_socket", sizeof(config.pipe_name) - 1);
#else
  strncpy(config.pipe_name, "test_ipc_socket", sizeof(config.pipe_name) - 1);
#endif
  for (size_t i = 0; i < VW_AUTH_TOKEN_BYTES; i++) config.auth_token[i] = (uint8_t)i;

  pthread_t thread;
  int err = pthread_create(&thread, NULL, worker_thread, &config);
  (void)err;
  assert(err == 0);

  // Give listener time to bind
  usleep(100000);

  vw_worker_client_t* client = vw_worker_client_launch_and_connect(NULL, config.pipe_name, config.auth_token, NULL);
  EXPECT(client != NULL);  // HELLO handshake completed inside

  // START with an unsupported sample rate must be rejected with an E_AUDIO_FORMAT ERROR reply
  vw_msg_start_t start;
  memset(&start, 0, sizeof(start));
  start.session_id.bytes[0] = 1;
  start.sample_rate = 48000;  // worker only accepts 16000
  start.channels = 1;
  start.sample_format = 1;
  strncpy(start.model_id, "ggml-tiny.en.bin", sizeof(start.model_id) - 1);
  strncpy(start.language, "en", sizeof(start.language) - 1);
  start.source_kind = VW_SOURCE_LOCAL_FILE;

  uint8_t start_payload[256];
  size_t start_len = 0;
  EXPECT(vw_protocol_encode_payload(VW_MSG_START_SESSION, &start, start_payload, sizeof(start_payload), &start_len));
  vw_frame_header_t start_hdr = {.magic = VW_PROTOCOL_MAGIC,
                                 .major = VW_PROTOCOL_VERSION_MAJOR,
                                 .type = VW_MSG_START_SESSION,
                                 .payload_length = (uint32_t)start_len,
                                 .sequence = 2};
  uint8_t start_hdr_buf[20];
  EXPECT(vw_protocol_encode_header(&start_hdr, start_hdr_buf, 20));
  vw_ipc_send((vw_ipc_handle_t*)client->pipe_handle, start_hdr_buf, 20);
  vw_ipc_send((vw_ipc_handle_t*)client->pipe_handle, start_payload, start_len);

  vw_frame_header_t reply_hdr;
  uint8_t rbuf[20];
  int32_t got = 0;
  while (got < 20) {
    int32_t r = vw_ipc_receive((vw_ipc_handle_t*)client->pipe_handle, rbuf + got, 20 - got);
    if (r == VW_IPC_RECV_FATAL) break;  // peer closed unexpectedly
    if (r > 0) got += r;                // retry on 3s read timeout
  }
  EXPECT(got == 20);
  EXPECT(vw_protocol_decode_header(rbuf, 20, &reply_hdr));
  EXPECT(reply_hdr.type == VW_MSG_ERROR);

  uint8_t rpayload[512];
  got = 0;
  while (got < (int32_t)reply_hdr.payload_length) {
    int32_t r = vw_ipc_receive((vw_ipc_handle_t*)client->pipe_handle, rpayload + got, reply_hdr.payload_length - got);
    if (r == VW_IPC_RECV_FATAL) break;
    if (r > 0) got += r;
  }
  EXPECT(got == (int32_t)reply_hdr.payload_length);
  union {
    vw_msg_hello_t hello;
    vw_msg_start_t start;
    vw_msg_audio_t audio;
    vw_msg_control_t control;
    vw_msg_status_t status;
    vw_msg_error_t error;
  } dec;
  memset(&dec, 0, sizeof(dec));
  EXPECT(vw_protocol_decode_payload(VW_MSG_ERROR, rpayload, reply_hdr.payload_length, &dec));
  EXPECT(dec.error.error_code == E_AUDIO_FORMAT);

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
