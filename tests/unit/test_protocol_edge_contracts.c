#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vw_protocol_codec.h"
#include "vw_protocol_types.h"
#include "vw_protocol_util.h"
#include "vw_test.h"
#include "vw_worker_config.h"

static void test_protocol_codec_null_pointer_safety(void) {
  // Inner-pointer NULL checks when length > 0 vs NULL with zero length
  vw_msg_hello_t hello = {
      .client_version = NULL,
      .client_version_length = 10,
  };
  uint8_t buf[256];
  size_t encoded_len = 0;
  bool ok = vw_protocol_encode_payload(VW_MSG_HELLO, &hello, buf, sizeof(buf), &encoded_len);
  vw_test_check_false("encode rejects NULL client_version with positive length", ok);

  hello.client_version = NULL;
  hello.client_version_length = 0;
  ok = vw_protocol_encode_payload(VW_MSG_HELLO, &hello, buf, sizeof(buf), &encoded_len);
  vw_test_check_true("encode accepts NULL client_version with zero length", ok);

  vw_msg_hello_ack_t hello_ack = {
      .worker_version = NULL,
      .worker_version_length = 10,
  };
  ok = vw_protocol_encode_payload(VW_MSG_HELLO_ACK, &hello_ack, buf, sizeof(buf), &encoded_len);
  vw_test_check_false("encode rejects NULL worker_version with positive length", ok);

  hello_ack.worker_version = NULL;
  hello_ack.worker_version_length = 0;
  ok = vw_protocol_encode_payload(VW_MSG_HELLO_ACK, &hello_ack, buf, sizeof(buf), &encoded_len);
  vw_test_check_true("encode accepts NULL worker_version with zero length", ok);

  vw_msg_audio_t pcm = {
      .pcm_bytes = 320,
      .pcm_data = NULL,
  };
  ok = vw_protocol_encode_payload(VW_MSG_AUDIO_PCM, &pcm, buf, sizeof(buf), &encoded_len);
  vw_test_check_false("encode rejects NULL pcm_data with positive length", ok);

  pcm.pcm_bytes = 0;
  pcm.pcm_data = NULL;
  ok = vw_protocol_encode_payload(VW_MSG_AUDIO_PCM, &pcm, buf, sizeof(buf), &encoded_len);
  vw_test_check_true("encode accepts NULL pcm_data with zero length", ok);
}

static void test_protocol_codec_trailing_bytes(void) {
  // Payload decoding must reject unconsumed trailing bytes
  vw_msg_hello_t hello = {.client_version = "test", .client_version_length = 4};
  uint8_t buf[256];
  size_t encoded_len = 0;
  bool ok = vw_protocol_encode_payload(VW_MSG_HELLO, &hello, buf, sizeof(buf), &encoded_len);
  vw_test_check_true("encode valid hello message", ok && encoded_len > 0);

  vw_msg_hello_t decoded;
  ok = vw_protocol_decode_payload(VW_MSG_HELLO, buf, encoded_len, &decoded);
  vw_test_check_true("decode exact length hello message", ok);

  // Appending an unconsumed trailing byte should fail decode
  buf[encoded_len] = 0xAA;
  ok = vw_protocol_decode_payload(VW_MSG_HELLO, buf, encoded_len + 1, &decoded);
  vw_test_check_false("decode rejects unconsumed trailing bytes", ok);
}

static void test_protocol_validate_header_bounds(void) {
  // Header type bounds validation
  vw_frame_header_t hdr = {
      .magic = VW_PROTOCOL_MAGIC,
      .major = VW_PROTOCOL_VERSION_MAJOR,
      .type = VW_MSG_HELLO,
      .payload_length = 0,
      .sequence = 1,
  };
  vw_test_check_true("valid header type HELLO accepted", vw_protocol_validate_header(&hdr));

  hdr.type = VW_MSG_TRANSLATE_CTRL;
  vw_test_check_true("valid header type TRANSLATE_CTRL accepted", vw_protocol_validate_header(&hdr));

  hdr.type = 0;
  vw_test_check_false("invalid header type 0 rejected", vw_protocol_validate_header(&hdr));

  hdr.type = VW_MSG_TRANSLATE_CTRL + 1;
  vw_test_check_false("invalid header type beyond upper bound rejected", vw_protocol_validate_header(&hdr));
}

static void test_protocol_validate_control_reasons(void) {
  // Control reason constants and semantic validation
  vw_msg_pause_t pause_msg = {.reason = VW_CTRL_REASON_USER_PAUSE};
  vw_test_check_true("valid pause reason accepted", vw_protocol_validate_payload(VW_MSG_PAUSE, &pause_msg));

  pause_msg.reason = 2;
  vw_test_check_false("invalid pause reason (2) rejected", vw_protocol_validate_payload(VW_MSG_PAUSE, &pause_msg));

  vw_msg_resume_t resume_msg = {.reason = VW_CTRL_REASON_USER_RESUME};
  vw_test_check_true("valid resume reason accepted", vw_protocol_validate_payload(VW_MSG_RESUME, &resume_msg));

  resume_msg.reason = 2;
  vw_test_check_false("invalid resume reason (2) rejected", vw_protocol_validate_payload(VW_MSG_RESUME, &resume_msg));

  vw_msg_stop_t stop_msg = {.reason = VW_CTRL_REASON_USER_STOP};
  vw_test_check_true("valid stop reason USER_STOP accepted",
                     vw_protocol_validate_payload(VW_MSG_STOP_SESSION, &stop_msg));

  stop_msg.reason = VW_CTRL_REASON_SEEK_DISCONTINUITY;
  vw_test_check_true("valid stop reason SEEK_DISCONTINUITY accepted",
                     vw_protocol_validate_payload(VW_MSG_STOP_SESSION, &stop_msg));

  stop_msg.reason = VW_CTRL_REASON_MEDIA_END;
  vw_test_check_true("valid stop reason MEDIA_END accepted",
                     vw_protocol_validate_payload(VW_MSG_STOP_SESSION, &stop_msg));

  stop_msg.reason = 4;
  vw_test_check_false("arbitrary stop reason rejected", vw_protocol_validate_payload(VW_MSG_STOP_SESSION, &stop_msg));

  vw_msg_error_t err_msg = {.error_code = 0};
  vw_test_check_false("zero error code rejected", vw_protocol_validate_payload(VW_MSG_ERROR, &err_msg));

  err_msg.error_code = VW_ERROR_INTERNAL;
  vw_test_check_true("valid internal error code accepted", vw_protocol_validate_payload(VW_MSG_ERROR, &err_msg));

  err_msg.error_code = VW_ERROR_SOURCE_OPEN;
  vw_test_check_true("valid source open error code accepted", vw_protocol_validate_payload(VW_MSG_ERROR, &err_msg));

  err_msg.error_code = VW_ERROR_MAX + 1;
  vw_test_check_false("out-of-range error code rejected", vw_protocol_validate_payload(VW_MSG_ERROR, &err_msg));
}

static void test_protocol_validate_pcm_alignment(void) {
  // Reject odd pcm_bytes unaligned to 16-bit sample boundary
  int16_t samples[4] = {0};
  vw_msg_audio_t pcm = {
      .start_pts_us = 0,
      .duration_us = 250,
      .pcm_bytes = 4 * sizeof(int16_t),
      .pcm_data = (const uint8_t*)samples,
  };
  vw_test_check_true("even pcm_bytes accepted", vw_protocol_validate_payload(VW_MSG_AUDIO_PCM, &pcm));

  pcm.pcm_bytes = 4 * sizeof(int16_t) + 1;
  vw_test_check_false("odd pcm_bytes rejected", vw_protocol_validate_payload(VW_MSG_AUDIO_PCM, &pcm));
}

static void test_protocol_util_utf8_overlong_and_surrogates(void) {
  // Reject overlong and surrogate codepoints
  const char overlong_c0[] = "\xC0\xAF";
  size_t len = vw_utf8_safe_len(overlong_c0, 2);
  vw_test_check_true("overlong 0xC0 rejected", len == 0);

  const char overlong_c1[] = "\xC1\x81";
  len = vw_utf8_safe_len(overlong_c1, 2);
  vw_test_check_true("overlong 0xC1 rejected", len == 0);

  // High surrogate 0xD800 -> \xED\xA0\x80
  const char surrogate[] = "\xED\xA0\x80";
  len = vw_utf8_safe_len(surrogate, 3);
  vw_test_check_true("surrogate sequence rejected", len == 0);

  const char valid_utf8[] = "Hello \xC3\xA9\xE2\x9C\x93";
  len = vw_utf8_safe_len(valid_utf8, strlen(valid_utf8));
  vw_test_check_true("valid multi-byte UTF-8 preserved", len == strlen(valid_utf8));
}

static void test_worker_config_default_dir(void) {
  // Fallback default model directory probe
  char dir[VW_PATH_MAX_BYTES];
  bool ok = vw_worker_config_default_model_dir(dir, sizeof(dir));
  vw_test_check_true("default model directory probe succeeds", ok);
  vw_test_check_true("default model directory is non-empty", dir[0] != '\0');
  vw_test_check_true("default model directory ends with models", strstr(dir, "models") != NULL);
}

int main(void) {
  test_protocol_codec_null_pointer_safety();
  test_protocol_codec_trailing_bytes();
  test_protocol_validate_header_bounds();
  test_protocol_validate_control_reasons();
  test_protocol_validate_pcm_alignment();
  test_protocol_util_utf8_overlong_and_surrogates();
  test_worker_config_default_dir();

  return vw_test_finish("test_protocol_edge_contracts");
}
