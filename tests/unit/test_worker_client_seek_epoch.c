#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vw_ipc_transport.h"
#include "vw_platform.h"
#include "vw_protocol_codec.h"
#include "vw_protocol_types.h"
#include "vw_worker_client.h"

#define VW_TEST_INITIAL_PTS_US 100000000LL
#define VW_TEST_BACKWARD_PTS_US 30000000LL
#define VW_TEST_FORWARD_PTS_US 40000000LL
#define VW_TEST_SOURCE_URL "file:///tmp/vlc-whisper-seek-epoch.mp4"

static bool vw_test_receive_frame(vw_ipc_handle_t* server, vw_frame_header_t* header, uint8_t* payload,
                                  size_t payload_capacity) {
  uint8_t header_bytes[sizeof(vw_frame_header_t)];
  if (vw_ipc_receive(server, header_bytes, sizeof(header_bytes)) != (int32_t)sizeof(header_bytes)) return false;
  if (!vw_protocol_decode_header(header_bytes, sizeof(header_bytes), header)) return false;
  if (header->payload_length > payload_capacity) return false;
  if (header->payload_length > 0 &&
      vw_ipc_receive(server, payload, header->payload_length) != (int32_t)header->payload_length) {
    return false;
  }
  return true;
}

static bool vw_test_send_started(vw_ipc_handle_t* server, uint64_t* worker_sequence) {
  vw_msg_started_t started = {.source_active = VW_SOURCE_ACTIVE_ACTIVE};
  uint8_t payload[VW_MSG_STARTED_PAYLOAD_BYTES];
  size_t payload_length = 0;
  if (!vw_protocol_encode_payload(VW_MSG_STARTED, &started, payload, sizeof(payload), &payload_length)) return false;

  vw_frame_header_t header = {.magic = VW_PROTOCOL_MAGIC,
                              .major = VW_PROTOCOL_VERSION_MAJOR,
                              .type = VW_MSG_STARTED,
                              .payload_length = (uint32_t)payload_length,
                              .sequence = ++(*worker_sequence)};
  uint8_t header_bytes[sizeof(vw_frame_header_t)];
  if (!vw_protocol_encode_header(&header, header_bytes, sizeof(header_bytes))) return false;
  return vw_ipc_send(server, header_bytes, sizeof(header_bytes)) && vw_ipc_send(server, payload, payload_length);
}

static bool vw_test_send_caption(vw_ipc_handle_t* server, uint64_t* worker_sequence,
                                 const uint8_t session_id[VW_SESSION_ID_BYTES], uint64_t segment_id,
                                 int64_t start_pts_us, const char* translated_text) {
  char source_text[] = "source caption";
  vw_caption_segment_t segment = {.segment_id = segment_id,
                                  .start_pts_us = start_pts_us,
                                  .end_pts_us = start_pts_us + 1000000LL,
                                  .is_final = true,
                                  .text_utf8 = source_text,
                                  .text_bytes = (uint16_t)strlen(source_text),
                                  .translated_text_utf8 = (char*)translated_text,
                                  .translated_text_bytes =
                                      translated_text ? (uint16_t)strlen(translated_text) : 0,
                                  .translation_attempted = translated_text != NULL,
                                  .translation_latency_us = translated_text ? 1000U : 0U,
                                  .translation_tier = translated_text ? 1U : 0U};
  memcpy(segment.session_id.bytes, session_id, VW_SESSION_ID_BYTES);

  uint8_t payload[4096];
  size_t payload_length = 0;
  if (!vw_protocol_encode_payload(VW_MSG_CAPTION_SEGMENT, &segment, payload, sizeof(payload), &payload_length)) {
    return false;
  }
  vw_frame_header_t header = {.magic = VW_PROTOCOL_MAGIC,
                              .major = VW_PROTOCOL_VERSION_MAJOR,
                              .type = VW_MSG_CAPTION_SEGMENT,
                              .payload_length = (uint32_t)payload_length,
                              .sequence = ++(*worker_sequence)};
  uint8_t header_bytes[sizeof(vw_frame_header_t)];
  if (!vw_protocol_encode_header(&header, header_bytes, sizeof(header_bytes))) return false;
  return vw_ipc_send(server, header_bytes, sizeof(header_bytes)) && vw_ipc_send(server, payload, payload_length);
}

static bool vw_test_expect_stop(vw_ipc_handle_t* server, const uint8_t expected_session[VW_SESSION_ID_BYTES]) {
  vw_frame_header_t header;
  uint8_t payload[2048];
  if (!vw_test_receive_frame(server, &header, payload, sizeof(payload)) || header.type != VW_MSG_STOP_SESSION) {
    return false;
  }
  vw_msg_control_t stop;
  if (!vw_protocol_decode_payload(VW_MSG_STOP_SESSION, payload, header.payload_length, &stop)) return false;
  return stop.reason == VW_CTRL_REASON_SEEK_DISCONTINUITY &&
         memcmp(stop.session_id.bytes, expected_session, VW_SESSION_ID_BYTES) == 0;
}

static bool vw_test_expect_start(vw_ipc_handle_t* server, int64_t expected_origin_us,
                                 const uint8_t previous_session[VW_SESSION_ID_BYTES],
                                 uint8_t new_session[VW_SESSION_ID_BYTES]) {
  vw_frame_header_t header;
  uint8_t payload[2048];
  if (!vw_test_receive_frame(server, &header, payload, sizeof(payload)) || header.type != VW_MSG_START_SESSION) {
    return false;
  }
  vw_msg_start_t start;
  if (!vw_protocol_decode_payload(VW_MSG_START_SESSION, payload, header.payload_length, &start)) return false;
  if (start.timeline_origin_pts_us != expected_origin_us || strcmp(start.model_id, "tiny") != 0 ||
      strcmp(start.source_url, VW_TEST_SOURCE_URL) != 0 || start.source_kind != VW_SOURCE_LOCAL_FILE ||
      memcmp(start.session_id.bytes, previous_session, VW_SESSION_ID_BYTES) == 0) {
    return false;
  }
  memcpy(new_session, start.session_id.bytes, VW_SESSION_ID_BYTES);
  return true;
}

static bool vw_test_expect_translate(vw_ipc_handle_t* server, const uint8_t expected_session[VW_SESSION_ID_BYTES],
                                     bool enabled) {
  vw_frame_header_t header;
  uint8_t payload[2048];
  if (!vw_test_receive_frame(server, &header, payload, sizeof(payload)) || header.type != VW_MSG_TRANSLATE_CTRL) {
    return false;
  }
  vw_msg_translate_ctrl_t translate;
  if (!vw_protocol_decode_payload(VW_MSG_TRANSLATE_CTRL, payload, header.payload_length, &translate)) return false;
  return memcmp(translate.session_id.bytes, expected_session, VW_SESSION_ID_BYTES) == 0 &&
         translate.enabled == (enabled ? 1U : 0U) && strcmp(translate.source_lang, "en") == 0 &&
         strcmp(translate.target_lang, "ro") == 0 && translate.mode == 1U;
}

static bool vw_test_expect_position(vw_ipc_handle_t* server, const uint8_t expected_session[VW_SESSION_ID_BYTES],
                                    int64_t expected_pts_us) {
  vw_frame_header_t header;
  uint8_t payload[2048];
  if (!vw_test_receive_frame(server, &header, payload, sizeof(payload)) || header.type != VW_MSG_POSITION) return false;
  vw_msg_position_t position;
  if (!vw_protocol_decode_payload(VW_MSG_POSITION, payload, header.payload_length, &position)) return false;
  return memcmp(position.session_id.bytes, expected_session, VW_SESSION_ID_BYTES) == 0 &&
         position.current_pts_us == expected_pts_us && position.input_time_us == expected_pts_us &&
         (position.flags & VW_POSITION_FLAG_SEEK) == 0;
}

static void* vw_test_seek_epoch_server(void* opaque) {
  const char* endpoint = (const char*)opaque;
  vw_ipc_handle_t* server = vw_ipc_listen(endpoint);
  if (!server) return (void*)(intptr_t)1;

  vw_frame_header_t header;
  uint8_t payload[4096];
  if (!vw_test_receive_frame(server, &header, payload, sizeof(payload)) || header.type != VW_MSG_HELLO) {
    vw_ipc_close(server);
    return (void*)(intptr_t)2;
  }

  vw_msg_hello_ack_t ack = {.selected_major = VW_PROTOCOL_VERSION_MAJOR,
                            .selected_minor = VW_PROTOCOL_VERSION_MINOR,
                            .capability_flags = VW_CAPABILITY_PCM_S16LE_16K_MONO | VW_CAPABILITY_SOURCE_MODE |
                                                VW_CAPABILITY_TRANSLATION};
  size_t ack_length = 0;
  if (!vw_protocol_encode_payload(VW_MSG_HELLO_ACK, &ack, payload, sizeof(payload), &ack_length)) {
    vw_ipc_close(server);
    return (void*)(intptr_t)3;
  }
  uint64_t worker_sequence = 1;
  vw_frame_header_t ack_header = {.magic = VW_PROTOCOL_MAGIC,
                                  .major = VW_PROTOCOL_VERSION_MAJOR,
                                  .type = VW_MSG_HELLO_ACK,
                                  .payload_length = (uint32_t)ack_length,
                                  .sequence = worker_sequence};
  uint8_t header_bytes[sizeof(vw_frame_header_t)];
  vw_protocol_encode_header(&ack_header, header_bytes, sizeof(header_bytes));
  if (!vw_ipc_send(server, header_bytes, sizeof(header_bytes)) || !vw_ipc_send(server, payload, ack_length)) {
    vw_ipc_close(server);
    return (void*)(intptr_t)4;
  }

  uint8_t session_a[VW_SESSION_ID_BYTES];
  if (!vw_test_receive_frame(server, &header, payload, sizeof(payload)) || header.type != VW_MSG_START_SESSION) {
    vw_ipc_close(server);
    return (void*)(intptr_t)5;
  }
  vw_msg_start_t initial_start;
  if (!vw_protocol_decode_payload(VW_MSG_START_SESSION, payload, header.payload_length, &initial_start) ||
      initial_start.timeline_origin_pts_us != VW_TEST_INITIAL_PTS_US || strcmp(initial_start.model_id, "tiny") != 0 ||
      strcmp(initial_start.source_url, VW_TEST_SOURCE_URL) != 0) {
    vw_ipc_close(server);
    return (void*)(intptr_t)6;
  }
  memcpy(session_a, initial_start.session_id.bytes, VW_SESSION_ID_BYTES);
  if (!vw_test_send_started(server, &worker_sequence) || !vw_test_expect_translate(server, session_a, false)) {
    vw_ipc_close(server);
    return (void*)(intptr_t)7;
  }

  uint8_t session_b[VW_SESSION_ID_BYTES];
  if (!vw_test_expect_stop(server, session_a) ||
      !vw_test_expect_start(server, VW_TEST_BACKWARD_PTS_US, session_a, session_b) ||
      !vw_test_send_started(server, &worker_sequence) || !vw_test_expect_translate(server, session_b, false) ||
      !vw_test_expect_position(server, session_b, VW_TEST_BACKWARD_PTS_US)) {
    vw_ipc_close(server);
    return (void*)(intptr_t)8;
  }

  if (!vw_test_send_caption(server, &worker_sequence, session_a, 1U, 115000000LL, NULL) ||
      !vw_test_send_caption(server, &worker_sequence, session_b, 2U, 31000000LL, NULL)) {
    vw_ipc_close(server);
    return (void*)(intptr_t)9;
  }

  if (!vw_test_expect_translate(server, session_b, true)) {
    vw_ipc_close(server);
    return (void*)(intptr_t)10;
  }

  uint8_t session_c[VW_SESSION_ID_BYTES];
  if (!vw_test_expect_stop(server, session_b) ||
      !vw_test_expect_start(server, VW_TEST_FORWARD_PTS_US, session_b, session_c) ||
      !vw_test_send_started(server, &worker_sequence) || !vw_test_expect_translate(server, session_c, true) ||
      !vw_test_expect_position(server, session_c, VW_TEST_FORWARD_PTS_US)) {
    vw_ipc_close(server);
    return (void*)(intptr_t)11;
  }

  if (!vw_test_send_caption(server, &worker_sequence, session_b, 3U, 45000000LL, "vechi") ||
      !vw_test_send_caption(server, &worker_sequence, session_c, 4U, 41000000LL, "nou")) {
    vw_ipc_close(server);
    return (void*)(intptr_t)12;
  }

  if (!vw_test_receive_frame(server, &header, payload, sizeof(payload)) || header.type != VW_MSG_SHUTDOWN) {
    vw_ipc_close(server);
    return (void*)(intptr_t)13;
  }

  vw_ipc_close(server);
  return (void*)(intptr_t)0;
}

static void vw_test_assert_stale_segment(const vw_worker_recv_t* received,
                                         const uint8_t previous_session[VW_SESSION_ID_BYTES],
                                         const uint8_t current_session[VW_SESSION_ID_BYTES], uint64_t segment_id) {
  assert(received->type == VW_MSG_CAPTION_SEGMENT);
  assert(received->segment.segment_id == segment_id);
  assert(memcmp(received->segment.session_id.bytes, previous_session, VW_SESSION_ID_BYTES) == 0);
  assert(memcmp(received->segment.session_id.bytes, current_session, VW_SESSION_ID_BYTES) != 0);
}

int main(void) {
#ifdef _WIN32
  const char* endpoint = "\\\\.\\pipe\\test_worker_client_seek_epoch";
#else
  const char* endpoint = "test_worker_client_seek_epoch";
#endif
  uint8_t auth_token[VW_AUTH_TOKEN_BYTES] = {0};

  pthread_t server_thread;
  assert(pthread_create(&server_thread, NULL, vw_test_seek_epoch_server, (void*)endpoint) == 0);
  vw_platform_sleep_ms(100);

  vw_worker_client_t* client = vw_worker_client_launch_and_connect(NULL, endpoint, auth_token, NULL);
  assert(client != NULL);
  assert(vw_worker_client_start_session(client, VW_TEST_INITIAL_PTS_US, "tiny", VW_TEST_SOURCE_URL));
  assert(vw_worker_client_is_source_active(client));
  assert(vw_worker_client_send_translate_ctrl(client, false, "en", "ro", 1U));

  uint8_t session_a[VW_SESSION_ID_BYTES];
  memcpy(session_a, client->session_id, sizeof(session_a));
  assert(vw_worker_client_send_position(client, VW_TEST_BACKWARD_PTS_US, VW_TEST_BACKWARD_PTS_US, 1.0f,
                                        VW_POSITION_FLAG_SEEK));
  assert(memcmp(session_a, client->session_id, VW_SESSION_ID_BYTES) != 0);

  vw_worker_recv_t received;
  memset(&received, 0, sizeof(received));
  assert(vw_worker_client_receive_frame(client, 1000000U, &received) == VW_IPC_RECV_OK);
  vw_test_assert_stale_segment(&received, session_a, client->session_id, 1U);

  memset(&received, 0, sizeof(received));
  assert(vw_worker_client_receive_frame(client, 1000000U, &received) == VW_IPC_RECV_OK);
  assert(received.type == VW_MSG_CAPTION_SEGMENT);
  assert(received.segment.segment_id == 2U);
  assert(memcmp(received.segment.session_id.bytes, client->session_id, VW_SESSION_ID_BYTES) == 0);
  assert(received.segment.start_pts_us == 31000000LL);
  assert(received.segment.translated_text_utf8 == NULL);

  uint8_t session_b[VW_SESSION_ID_BYTES];
  memcpy(session_b, client->session_id, sizeof(session_b));
  assert(vw_worker_client_send_translate_ctrl(client, true, "en", "ro", 1U));
  assert(vw_worker_client_send_position(client, VW_TEST_FORWARD_PTS_US, VW_TEST_FORWARD_PTS_US, 1.0f,
                                        VW_POSITION_FLAG_SEEK));
  assert(memcmp(session_b, client->session_id, VW_SESSION_ID_BYTES) != 0);

  memset(&received, 0, sizeof(received));
  assert(vw_worker_client_receive_frame(client, 1000000U, &received) == VW_IPC_RECV_OK);
  vw_test_assert_stale_segment(&received, session_b, client->session_id, 3U);
  assert(received.segment.translated_text_utf8 != NULL);
  assert(strcmp(received.segment.translated_text_utf8, "vechi") == 0);

  memset(&received, 0, sizeof(received));
  assert(vw_worker_client_receive_frame(client, 1000000U, &received) == VW_IPC_RECV_OK);
  assert(received.type == VW_MSG_CAPTION_SEGMENT);
  assert(received.segment.segment_id == 4U);
  assert(memcmp(received.segment.session_id.bytes, client->session_id, VW_SESSION_ID_BYTES) == 0);
  assert(received.segment.start_pts_us == 41000000LL);
  assert(received.segment.translated_text_utf8 != NULL);
  assert(strcmp(received.segment.translated_text_utf8, "nou") == 0);

  vw_worker_client_shutdown(client);
  vw_worker_client_disconnect(client);

  void* server_result = NULL;
  pthread_join(server_thread, &server_result);
  assert((intptr_t)server_result == 0);
  return 0;
}
