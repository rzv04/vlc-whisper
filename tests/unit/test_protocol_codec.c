#include "vw_protocol.h"
#include "vw_test.h"

int main(void) {
  uint8_t buffer[2048];
  size_t written = 0;

  // Header
  vw_frame_header_t header = {.magic = VW_PROTOCOL_MAGIC,
                              .major = VW_PROTOCOL_VERSION_MAJOR,
                              .type = VW_MSG_HELLO,
                              .payload_length = 64,
                              .sequence = 1};
  EXPECT(vw_protocol_encode_header(&header, buffer, sizeof(buffer)));
  vw_frame_header_t decoded = {0};
  EXPECT(vw_protocol_decode_header(buffer, sizeof(buffer), &decoded));
  EXPECT(decoded.magic == VW_PROTOCOL_MAGIC);
  EXPECT(decoded.type == VW_MSG_HELLO);
  EXPECT(decoded.sequence == 1);

  // HELLO
  vw_msg_hello_t hello = {.min_major = 1, .max_major = 1, .client_version_length = 6, .client_version = (char*)"1.0.0"};
  memset(hello.auth_token, 0xAB, VW_AUTH_TOKEN_BYTES);
  EXPECT(vw_protocol_encode_payload(VW_MSG_HELLO, &hello, buffer, sizeof(buffer), &written));
  vw_msg_hello_t decoded_hello = {0};
  EXPECT(vw_protocol_decode_payload(VW_MSG_HELLO, buffer, written, &decoded_hello));
  EXPECT(decoded_hello.min_major == 1);
  EXPECT(decoded_hello.auth_token[0] == 0xAB);
  EXPECT_EQ_STR(decoded_hello.client_version, "1.0.0");

  // HELLO_ACK
  vw_msg_hello_ack_t hello_ack = {.selected_major = 1,
                                  .selected_minor = 0,
                                  .capability_flags = 3,
                                  .worker_version_length = 6,
                                  .worker_version = (char*)"1.0.0"};
  EXPECT(vw_protocol_encode_payload(VW_MSG_HELLO_ACK, &hello_ack, buffer, sizeof(buffer), &written));
  vw_msg_hello_ack_t decoded_ack = {0};
  EXPECT(vw_protocol_decode_payload(VW_MSG_HELLO_ACK, buffer, written, &decoded_ack));
  EXPECT(decoded_ack.capability_flags == 3);
  EXPECT_EQ_STR(decoded_ack.worker_version, "1.0.0");

  // START
  vw_msg_start_t start = {
      .timeline_origin_pts_us = 1000, .sample_rate = 16000, .channels = 1, .sample_format = 1, .source_kind = 1};
  memset(start.session_id.bytes, 1, VW_SESSION_ID_BYTES);
  strcpy(start.model_id, "ggml-tiny");
  strcpy(start.language, "en");
  EXPECT(vw_protocol_encode_payload(VW_MSG_START_SESSION, &start, buffer, sizeof(buffer), &written));
  vw_msg_start_t decoded_start = {0};
  EXPECT(vw_protocol_decode_payload(VW_MSG_START_SESSION, buffer, written, &decoded_start));
  EXPECT(decoded_start.sample_rate == 16000);
  EXPECT_EQ_STR(decoded_start.model_id, "ggml-tiny");

  // AUDIO PCM
  vw_msg_audio_t audio = {
      .start_pts_us = 0, .duration_us = 1000000, .pcm_bytes = 4, .pcm_data = (const uint8_t*)"\x01\x02\x03\x04"};
  EXPECT(vw_protocol_encode_payload(VW_MSG_AUDIO_PCM, &audio, buffer, sizeof(buffer), &written));
  vw_msg_audio_t decoded_audio = {0};
  EXPECT(vw_protocol_decode_payload(VW_MSG_AUDIO_PCM, buffer, written, &decoded_audio));
  EXPECT(decoded_audio.duration_us == 1000000);
  EXPECT(decoded_audio.pcm_bytes == 4);
  EXPECT(decoded_audio.pcm_data[0] == 0x01);

  // CONTROL
  vw_msg_control_t control = {.reason = 42};
  memset(control.session_id.bytes, 2, VW_SESSION_ID_BYTES);
  EXPECT(vw_protocol_encode_payload(VW_MSG_PAUSE, &control, buffer, sizeof(buffer), &written));
  vw_msg_control_t decoded_control = {0};
  EXPECT(vw_protocol_decode_payload(VW_MSG_PAUSE, buffer, written, &decoded_control));
  EXPECT(decoded_control.reason == 42);

  // STATUS
  vw_msg_status_t status = {.state = 1, .queued_audio_us = 500, .inference_us = 100, .dropped_audio_us = 0};
  EXPECT(vw_protocol_encode_payload(VW_MSG_STATUS, &status, buffer, sizeof(buffer), &written));
  vw_msg_status_t decoded_status = {0};
  EXPECT(vw_protocol_decode_payload(VW_MSG_STATUS, buffer, written, &decoded_status));
  EXPECT(decoded_status.inference_us == 100);

  // ERROR
  vw_msg_error_t err = {.error_code = 99, .recoverable = 0};
  strcpy(err.message, "Fail");
  EXPECT(vw_protocol_encode_payload(VW_MSG_ERROR, &err, buffer, sizeof(buffer), &written));
  vw_msg_error_t decoded_err = {0};
  EXPECT(vw_protocol_decode_payload(VW_MSG_ERROR, buffer, written, &decoded_err));
  EXPECT(decoded_err.error_code == 99);
  EXPECT_EQ_STR(decoded_err.message, "Fail");

  // CAPTION SEGMENT
  vw_caption_segment_t seg = {.segment_id = 1,
                              .start_pts_us = 10,
                              .end_pts_us = 20,
                              .is_final = true,
                              .text_bytes = 5,
                              .text_utf8 = (char*)"text"};
  EXPECT(vw_protocol_encode_payload(VW_MSG_CAPTION_SEGMENT, &seg, buffer, sizeof(buffer), &written));
  vw_caption_segment_t decoded_seg = {0};
  EXPECT(vw_protocol_decode_payload(VW_MSG_CAPTION_SEGMENT, buffer, written, &decoded_seg));
  EXPECT(decoded_seg.end_pts_us == 20);
  EXPECT(decoded_seg.is_final == true);
  EXPECT_EQ_STR(decoded_seg.text_utf8, "text");

  // STARTED & SHUTDOWN
  EXPECT(vw_protocol_encode_payload(VW_MSG_STARTED, NULL, buffer, sizeof(buffer), &written));
  EXPECT(written == 0);
  EXPECT(vw_protocol_encode_payload(VW_MSG_SHUTDOWN, NULL, buffer, sizeof(buffer), &written));
  EXPECT(written == 0);

  printf("test_protocol_codec PASSED\n");
  return 0;
}
