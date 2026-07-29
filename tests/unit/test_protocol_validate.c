#include "vw_protocol.h"
#include "vw_test.h"

int main(void) {
  vw_frame_header_t valid = {.magic = VW_PROTOCOL_MAGIC,
                             .major = VW_PROTOCOL_VERSION_MAJOR,
                             .type = VW_MSG_START_SESSION,
                             .payload_length = 100,
                             .sequence = 42};
  EXPECT(vw_protocol_validate_header(&valid));

  vw_frame_header_t invalid_magic = valid;
  invalid_magic.magic = 0xDEADBEEF;
  EXPECT(!vw_protocol_validate_header(&invalid_magic));

  vw_frame_header_t invalid_payload = valid;
  invalid_payload.payload_length = VW_MAX_PAYLOAD_BYTES + 1;
  EXPECT(!vw_protocol_validate_header(&invalid_payload));

  // Validate HELLO
  vw_msg_hello_t hello = {.client_version_length = 4, .client_version = (char*)"test"};
  EXPECT(vw_protocol_validate_payload(VW_MSG_HELLO, &hello));
  hello.client_version = NULL;
  EXPECT(!vw_protocol_validate_payload(VW_MSG_HELLO, &hello));

  // Validate HELLO_ACK
  vw_msg_hello_ack_t hello_ack = {.worker_version_length = 4, .worker_version = (char*)"test"};
  EXPECT(vw_protocol_validate_payload(VW_MSG_HELLO_ACK, &hello_ack));

  // Validate START (always valid)
  vw_msg_start_t start = {0};
  EXPECT(vw_protocol_validate_payload(VW_MSG_START_SESSION, &start));

  // Validate AUDIO
  vw_msg_audio_t audio = {.duration_us = 1000000, .pcm_bytes = 32000, .pcm_data = (const uint8_t*)"12"};
  EXPECT(vw_protocol_validate_payload(VW_MSG_AUDIO_PCM, &audio));
  audio.duration_us = 0;  // Invalid
  EXPECT(!vw_protocol_validate_payload(VW_MSG_AUDIO_PCM, &audio));
  audio.duration_us = 1000000;
  audio.pcm_bytes = 1000;  // Mismatch with duration
  EXPECT(!vw_protocol_validate_payload(VW_MSG_AUDIO_PCM, &audio));

  // Validate CONTROL
  vw_msg_control_t control = {0};
  EXPECT(vw_protocol_validate_payload(VW_MSG_PAUSE, &control));
  EXPECT(vw_protocol_validate_payload(VW_MSG_RESUME, &control));
  EXPECT(vw_protocol_validate_payload(VW_MSG_STOP_SESSION, &control));

  // Validate STATUS / ERROR
  vw_msg_status_t status = {0};
  EXPECT(vw_protocol_validate_payload(VW_MSG_STATUS, &status));
  vw_msg_error_t err = {0};
  EXPECT(vw_protocol_validate_payload(VW_MSG_ERROR, &err));

  // Validate SEGMENT
  vw_caption_segment_t seg = {.start_pts_us = 10, .end_pts_us = 20, .text_bytes = 4, .text_utf8 = (char*)"test"};
  EXPECT(vw_protocol_validate_payload(VW_MSG_CAPTION_SEGMENT, &seg));

  seg.end_pts_us = 5;  // end < start
  EXPECT(!vw_protocol_validate_payload(VW_MSG_CAPTION_SEGMENT, &seg));

  seg.end_pts_us = 20;
  seg.text_utf8 = (char*)"    ";  // whitespace only
  EXPECT(!vw_protocol_validate_payload(VW_MSG_CAPTION_SEGMENT, &seg));

  seg.text_utf8 = (char*)"valid";
  seg.text_bytes = 5;
  EXPECT(vw_protocol_validate_payload(VW_MSG_CAPTION_SEGMENT, &seg));

  // Invalid UTF-8: basic invalid bytes
  seg.text_utf8 = (char*)"\xFF\xFE";
  seg.text_bytes = 2;
  EXPECT(!vw_protocol_validate_payload(VW_MSG_CAPTION_SEGMENT, &seg));

  // Invalid UTF-8: Overlong 2-byte sequences (RFC 3629 rejection)
  seg.text_utf8 = (char*)"\xC0\x80";
  seg.text_bytes = 2;
  EXPECT(!vw_protocol_validate_payload(VW_MSG_CAPTION_SEGMENT, &seg));

  seg.text_utf8 = (char*)"\xC1\xBF";
  seg.text_bytes = 2;
  EXPECT(!vw_protocol_validate_payload(VW_MSG_CAPTION_SEGMENT, &seg));

  // Invalid UTF-8: Overlong 3-byte sequences
  seg.text_utf8 = (char*)"\xE0\x80\x80";
  seg.text_bytes = 3;
  EXPECT(!vw_protocol_validate_payload(VW_MSG_CAPTION_SEGMENT, &seg));

  seg.text_utf8 = (char*)"\xE0\x9F\xBF";
  seg.text_bytes = 3;
  EXPECT(!vw_protocol_validate_payload(VW_MSG_CAPTION_SEGMENT, &seg));

  // Invalid UTF-8: Overlong 4-byte sequence
  seg.text_utf8 = (char*)"\xF0\x80\x80\x80";
  seg.text_bytes = 4;
  EXPECT(!vw_protocol_validate_payload(VW_MSG_CAPTION_SEGMENT, &seg));

  // Invalid UTF-8: UTF-16 surrogates (U+D800 to U+DFFF)
  seg.text_utf8 = (char*)"\xED\xA0\x80";
  seg.text_bytes = 3;
  EXPECT(!vw_protocol_validate_payload(VW_MSG_CAPTION_SEGMENT, &seg));

  seg.text_utf8 = (char*)"\xED\xBF\xBF";
  seg.text_bytes = 3;
  EXPECT(!vw_protocol_validate_payload(VW_MSG_CAPTION_SEGMENT, &seg));

  // Invalid UTF-8: Code point above U+10FFFF
  seg.text_utf8 = (char*)"\xF4\x90\x80\x80";
  seg.text_bytes = 4;
  EXPECT(!vw_protocol_validate_payload(VW_MSG_CAPTION_SEGMENT, &seg));

  // Invalid UTF-8: Truncated sequence
  seg.text_utf8 = (char*)"\xC2";
  seg.text_bytes = 1;
  EXPECT(!vw_protocol_validate_payload(VW_MSG_CAPTION_SEGMENT, &seg));

  // Valid multi-byte UTF-8 sequences (2-byte, 3-byte, 4-byte)
  seg.text_utf8 = (char*)"\xC2\xA2";  // U+00A2
  seg.text_bytes = 2;
  EXPECT(vw_protocol_validate_payload(VW_MSG_CAPTION_SEGMENT, &seg));

  seg.text_utf8 = (char*)"\xE2\x82\xAC";  // U+20AC
  seg.text_bytes = 3;
  EXPECT(vw_protocol_validate_payload(VW_MSG_CAPTION_SEGMENT, &seg));

  seg.text_utf8 = (char*)"\xF0\x9F\x98\x80";  // U+1F600
  seg.text_bytes = 4;
  EXPECT(vw_protocol_validate_payload(VW_MSG_CAPTION_SEGMENT, &seg));

  // Validate SHUTDOWN / STARTED
  EXPECT(vw_protocol_validate_payload(VW_MSG_SHUTDOWN, NULL));
  EXPECT(vw_protocol_validate_payload(VW_MSG_STARTED, NULL));

  printf("test_protocol_validate PASSED\n");
  return 0;
}
