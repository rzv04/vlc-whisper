#include <math.h>

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

  // Whole-sample rounding tolerance: a producer may round duration up/down by 1 byte (half a
  // sample at 16kHz S16LE). Accept ±1 byte of the expected pcm_bytes; more is a real mismatch.
  audio.duration_us = 511937;  // odd frame count: trunc(duration*32/1000) = 16381, bytes = 16382
  audio.pcm_bytes = 16382;
  EXPECT(vw_protocol_validate_payload(VW_MSG_AUDIO_PCM, &audio));  // +1 byte tolerated
  audio.pcm_bytes = 16381;
  EXPECT(vw_protocol_validate_payload(VW_MSG_AUDIO_PCM, &audio));  // exact
  audio.pcm_bytes = 16380;                                         // -1 byte: within tolerance
  EXPECT(vw_protocol_validate_payload(VW_MSG_AUDIO_PCM, &audio));
  audio.pcm_bytes = 16379;  // -2 bytes: beyond tolerance
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

  // POSITION validation
  vw_msg_position_t pos = {.current_pts_us = 1000000LL, .input_time_us = 1000000LL, .playback_rate = 1.0f, .flags = 0};
  EXPECT(vw_protocol_validate_payload(VW_MSG_POSITION, &pos));

  pos.flags = VW_POSITION_FLAG_SEEK | VW_POSITION_FLAG_PAUSED;
  EXPECT(vw_protocol_validate_payload(VW_MSG_POSITION, &pos));

  pos.flags = 0x04;  // Invalid flag bit
  EXPECT(!vw_protocol_validate_payload(VW_MSG_POSITION, &pos));
  pos.flags = 0;

  pos.current_pts_us = -10000001LL;  // Below -10s floor
  EXPECT(!vw_protocol_validate_payload(VW_MSG_POSITION, &pos));
  pos.current_pts_us = 315360000000001LL;  // Above 10 years
  EXPECT(!vw_protocol_validate_payload(VW_MSG_POSITION, &pos));
  pos.current_pts_us = 0;

  pos.input_time_us = -2LL;  // Below -1 (unset)
  EXPECT(!vw_protocol_validate_payload(VW_MSG_POSITION, &pos));
  pos.input_time_us = 315360000000001LL;  // Above 10 years
  EXPECT(!vw_protocol_validate_payload(VW_MSG_POSITION, &pos));
  pos.input_time_us = -1LL;  // -1 is valid (unset)
  EXPECT(vw_protocol_validate_payload(VW_MSG_POSITION, &pos));
  pos.input_time_us = 0;

  pos.playback_rate = 0.0f;
  EXPECT(!vw_protocol_validate_payload(VW_MSG_POSITION, &pos));

  pos.playback_rate = -1.0f;
  EXPECT(!vw_protocol_validate_payload(VW_MSG_POSITION, &pos));

  pos.playback_rate = 17.0f;
  EXPECT(!vw_protocol_validate_payload(VW_MSG_POSITION, &pos));

  pos.playback_rate = NAN;
  EXPECT(!vw_protocol_validate_payload(VW_MSG_POSITION, &pos));

  // Validate SHUTDOWN (header-only)
  EXPECT(vw_protocol_validate_payload(VW_MSG_SHUTDOWN, NULL));

  // Validate STARTED (1-byte payload)
  EXPECT(!vw_protocol_validate_payload(VW_MSG_STARTED, NULL));
  vw_msg_started_t started = {.source_active = 0};
  EXPECT(vw_protocol_validate_payload(VW_MSG_STARTED, &started));
  started.source_active = 1;
  EXPECT(vw_protocol_validate_payload(VW_MSG_STARTED, &started));
  started.source_active = 2;  // Invalid value
  EXPECT(!vw_protocol_validate_payload(VW_MSG_STARTED, &started));

  printf("test_protocol_validate PASSED\n");
  return 0;
}
