#include <string.h>

#include "vw_protocol_codec.h"
#include "vw_test.h"
#include "vw_test_worker_harness.h"
#include "vw_test_worker_stubs.h"

static bool send_duplicate_start(vw_worker_client_t* client) {
  vw_msg_start_t start;
  memset(&start, 0, sizeof(start));
  memcpy(start.session_id.bytes, client->session_id, VW_SESSION_ID_BYTES);
  start.sample_rate = VW_AUDIO_SAMPLE_RATE;
  start.channels = 1;
  start.sample_format = VW_SAMPLE_FORMAT_S16LE;
  snprintf(start.model_id, sizeof(start.model_id), "%s", "tiny");
  snprintf(start.language, sizeof(start.language), "%s", client->language);
  uint8_t payload[2048];
  size_t payload_len = 0;
  if (!vw_protocol_encode_payload(VW_MSG_START_SESSION, &start, payload, sizeof(payload), &payload_len)) return false;
  vw_frame_header_t header = {.magic = VW_PROTOCOL_MAGIC,
                              .major = VW_PROTOCOL_VERSION_MAJOR,
                              .type = VW_MSG_START_SESSION,
                              .payload_length = (uint32_t)payload_len,
                              .sequence = ++client->sequence};
  uint8_t header_buf[sizeof(header)];
  if (!vw_protocol_encode_header(&header, header_buf, sizeof(header_buf))) return false;
  return vw_ipc_send((vw_ipc_handle_t*)client->pipe_handle, header_buf, sizeof(header_buf)) &&
         vw_ipc_send((vw_ipc_handle_t*)client->pipe_handle, payload, payload_len);
}

static bool send_hello_range(vw_ipc_handle_t* handle, const uint8_t token[VW_AUTH_TOKEN_BYTES], bool compatible) {
  vw_msg_hello_t hello = {.min_major = compatible ? VW_PROTOCOL_VERSION_MAJOR : VW_PROTOCOL_VERSION_MAJOR + 1U,
                          .max_major = compatible ? VW_PROTOCOL_VERSION_MAJOR : VW_PROTOCOL_VERSION_MAJOR + 1U,
                          .auth_token = {0},
                          .client_version_length = 0,
                          .client_version = NULL};
  memcpy(hello.auth_token, token, VW_AUTH_TOKEN_BYTES);
  uint8_t payload[256];
  size_t payload_len = 0;
  if (!vw_protocol_encode_payload(VW_MSG_HELLO, &hello, payload, sizeof(payload), &payload_len)) return false;
  vw_frame_header_t header = {.magic = VW_PROTOCOL_MAGIC,
                              .major = VW_PROTOCOL_VERSION_MAJOR,
                              .type = VW_MSG_HELLO,
                              .payload_length = (uint32_t)payload_len,
                              .sequence = 1};
  uint8_t header_buf[sizeof(header)];
  if (!vw_protocol_encode_header(&header, header_buf, sizeof(header_buf))) return false;
  return vw_ipc_send(handle, header_buf, sizeof(header_buf)) && vw_ipc_send(handle, payload, payload_len);
}

int main(void) {
  vw_test_worker_stubs_reset();
  vw_test_worker_fixture_t hello_fixture;
  memset(&hello_fixture, 0, sizeof(hello_fixture));
  bool hello_setup = vw_worker_config_init_defaults(&hello_fixture.config);
#ifdef _WIN32
  snprintf(hello_fixture.config.pipe_name, sizeof(hello_fixture.config.pipe_name), "\\\\.\\pipe\\vw-test-hello-%ld",
           vw_test_process_id());
#else
  snprintf(hello_fixture.config.pipe_name, sizeof(hello_fixture.config.pipe_name), "/tmp/vw-test-hello-%ld.sock",
           vw_test_process_id());
#endif
  for (size_t i = 0; i < VW_AUTH_TOKEN_BYTES; i++) hello_fixture.config.auth_token[i] = (uint8_t)(0x60U + i);
  snprintf(hello_fixture.config.model_path, sizeof(hello_fixture.config.model_path), "%s", "stub-model.bin");
  hello_fixture.config.backend = VW_WORKER_BACKEND_CPU;
  hello_setup = hello_setup &&
                pthread_create(&hello_fixture.thread, NULL, vw_test_worker_thread_main, &hello_fixture.config) == 0;
  hello_fixture.thread_started = hello_setup;
  if (hello_setup) {
    vw_platform_sleep_ms(100);
    vw_ipc_handle_t* raw = vw_ipc_connect(hello_fixture.config.pipe_name);
    vw_test_check_true("incompatible HELLO connects", raw != NULL);
    if (raw) {
      vw_test_check_true("incompatible HELLO is transmitted",
                         send_hello_range(raw, hello_fixture.config.auth_token, false));
      uint8_t header_buf[sizeof(vw_frame_header_t)];
      int32_t header_read = vw_ipc_receive_timeout(raw, header_buf, sizeof(header_buf), 500000);
      vw_test_check_true("incompatible HELLO receives protocol error", header_read > 0);
      vw_ipc_close(raw);
    }
    void* hello_result = NULL;
    pthread_join(hello_fixture.thread, &hello_result);
    hello_fixture.thread_started = false;
    vw_test_check_true("incompatible HELLO exits worker nonzero", (int)(intptr_t)hello_result != 0);
  }
  vw_test_worker_fixture_t malformed_fixture;
  memset(&malformed_fixture, 0, sizeof(malformed_fixture));
  bool malformed_setup = vw_worker_config_init_defaults(&malformed_fixture.config);
#ifdef _WIN32
  snprintf(malformed_fixture.config.pipe_name, sizeof(malformed_fixture.config.pipe_name),
           "\\\\.\\pipe\\vw-test-malformed-%ld", vw_test_process_id());
#else
  snprintf(malformed_fixture.config.pipe_name, sizeof(malformed_fixture.config.pipe_name),
           "/tmp/vw-test-malformed-%ld.sock", vw_test_process_id());
#endif
  for (size_t i = 0; i < VW_AUTH_TOKEN_BYTES; i++) malformed_fixture.config.auth_token[i] = (uint8_t)(0x70U + i);
  snprintf(malformed_fixture.config.model_path, sizeof(malformed_fixture.config.model_path), "%s", "stub-model.bin");
  malformed_fixture.config.backend = VW_WORKER_BACKEND_CPU;
  malformed_setup = malformed_setup && pthread_create(&malformed_fixture.thread, NULL, vw_test_worker_thread_main,
                                                      &malformed_fixture.config) == 0;
  malformed_fixture.thread_started = malformed_setup;
  if (malformed_setup) {
    vw_platform_sleep_ms(100);
    vw_ipc_handle_t* raw = vw_ipc_connect(malformed_fixture.config.pipe_name);
    vw_test_check_true("malformed-frame worker connects", raw != NULL);
    if (raw) {
      vw_test_check_true("valid HELLO authenticates before malformed frame",
                         send_hello_range(raw, malformed_fixture.config.auth_token, true));
      uint8_t ack_header[sizeof(vw_frame_header_t)];
      int32_t ack_read = vw_ipc_receive_timeout(raw, ack_header, sizeof(ack_header), 500000);
      vw_test_check_true("authenticated HELLO receives ACK", ack_read > 0);
      uint8_t bad_header[sizeof(vw_frame_header_t)] = {0};
      vw_frame_header_t malformed = {.magic = VW_PROTOCOL_MAGIC,
                                     .major = VW_PROTOCOL_VERSION_MAJOR,
                                     .type = 0xffffU,
                                     .payload_length = 0,
                                     .sequence = 2};
      vw_protocol_encode_header(&malformed, bad_header, sizeof(bad_header));
      vw_test_check_true("authenticated malformed frame is transmitted",
                         vw_ipc_send(raw, bad_header, sizeof(bad_header)));
      vw_ipc_close(raw);
    }
    void* malformed_result = NULL;
    pthread_join(malformed_fixture.thread, &malformed_result);
    malformed_fixture.thread_started = false;
    vw_test_check_true("authenticated malformed frame exits worker nonzero", (int)(intptr_t)malformed_result != 0);
  }
  vw_test_worker_fixture_t tail_fixture;
  bool tail_started = vw_test_worker_fixture_start(&tail_fixture, "p2-tail-failure");
  vw_test_check_true("tail-failure worker starts", tail_started);
  if (tail_started) {
    vw_test_worker_stubs_reset();
    vw_test_whisper_set_failure(true);
    vw_test_check_true("tail-failure session starts",
                       vw_worker_client_start_session(tail_fixture.client, 0, "tiny", NULL));
    vw_test_check_true("short tail audio is delivered", vw_test_send_audio_chunks(tail_fixture.client, 2, 0));
    vw_worker_client_stop_session(tail_fixture.client, VW_CTRL_REASON_MEDIA_END);
    bool tail_error = false;
    for (int i = 0; i < 10 && !tail_error; i++) {
      vw_worker_recv_t recv;
      if (vw_worker_client_receive_frame(tail_fixture.client, 100000, &recv) == VW_IPC_RECV_OK &&
          recv.type == VW_MSG_ERROR) {
        tail_error = true;
      }
    }
    vw_test_check_true("failed MEDIA_END tail emits an error", tail_error);
    vw_test_check_true("failed MEDIA_END tail exits nonzero", vw_test_worker_fixture_shutdown(&tail_fixture) != 0);
  }

  vw_test_worker_fixture_t fixture;
  bool started = vw_test_worker_fixture_start(&fixture, "p2-contracts");
  vw_test_check_true("stub worker starts", started);
  if (started) {
    /* The authenticated client path below exercises duplicate START and inference failures. */
    vw_test_check_true("initial START succeeds", vw_worker_client_start_session(fixture.client, 0, "tiny", NULL));
    vw_test_check_true("duplicate START is transmitted for deterministic response",
                       send_duplicate_start(fixture.client));
    bool duplicate_rejected = false;
    for (int i = 0; i < 10 && !duplicate_rejected; i++) {
      vw_worker_recv_t recv;
      if (vw_worker_client_receive_frame(fixture.client, 100000, &recv) == VW_IPC_RECV_OK &&
          recv.type == VW_MSG_ERROR && recv.error.recoverable != 0) {
        duplicate_rejected = true;
      }
    }
    vw_test_check_true("duplicate START receives a recoverable response", duplicate_rejected);

    vw_test_worker_stubs_reset();
    vw_test_whisper_set_failure(true);
    vw_test_check_true("failure session starts", vw_worker_client_start_session(fixture.client, 0, "tiny", NULL));
    vw_test_check_true("failure session receives enough audio", vw_test_send_audio_chunks(fixture.client, 4, 0));
    bool inference_error = false;
    for (int i = 0; i < 12 && !inference_error; i++) {
      vw_worker_recv_t recv;
      if (vw_worker_client_receive_frame(fixture.client, 100000, &recv) == VW_IPC_RECV_OK &&
          recv.type == VW_MSG_ERROR && recv.error.recoverable == 0) {
        inference_error = true;
      }
    }
    vw_test_check_true("failed inference is surfaced as fatal error", inference_error);
    vw_test_check_true("fatal inference exits worker nonzero", vw_test_worker_fixture_shutdown(&fixture) != 0);
    fixture.thread_started = false;
  }
  if (fixture.thread_started) {
    vw_test_check_true("worker thread joins after fatal inference", vw_test_worker_fixture_shutdown(&fixture) != 2);
  }
  return vw_test_finish("vw_test_worker_p2_contracts");
}
