#include <stdio.h>
#include <string.h>

#include "vw_protocol_codec.h"

static int g_failures = 0;

static void check_true(const char* name, bool condition) {
  if (!condition) {
    fprintf(stderr, "FAIL: %s\n", name);
    g_failures++;
  }
}

static void check_false(const char* name, bool condition) {
  if (condition) {
    fprintf(stderr, "FAIL: %s\n", name);
    g_failures++;
  }
}

static vw_msg_start_t make_valid_live_start(void) {
  vw_msg_start_t start;
  memset(&start, 0, sizeof(start));
  memset(start.session_id.bytes, 0x5a, sizeof(start.session_id.bytes));
  start.timeline_origin_pts_us = 0;
  start.sample_rate = 16000;
  start.channels = 1;
  start.sample_format = 1;
  snprintf(start.model_id, sizeof(start.model_id), "%s", "ggml-tiny.en.bin");
  snprintf(start.language, sizeof(start.language), "%s", "en");
  start.source_kind = VW_SOURCE_LIVE_AUDIO;
  return start;
}

static vw_msg_start_t make_valid_local_start(void) {
  vw_msg_start_t start = make_valid_live_start();
  static const char k_source_url[] = "file:///tmp/vlc-whisper-start-contract.wav";
  start.source_kind = VW_SOURCE_LOCAL_FILE;
  start.source_url_len = (uint16_t)strlen(k_source_url);
  memcpy(start.source_url, k_source_url, start.source_url_len + 1U);
  return start;
}

int main(void) {
  vw_msg_start_t start = make_valid_live_start();
  check_true("baseline live START must remain valid", vw_protocol_validate_payload(VW_MSG_START_SESSION, &start));

  start = make_valid_live_start();
  start.sample_rate = 48000;
  check_false("START rejects non-16kHz sample rate", vw_protocol_validate_payload(VW_MSG_START_SESSION, &start));

  start = make_valid_live_start();
  start.channels = 2;
  check_false("START rejects non-mono channel count", vw_protocol_validate_payload(VW_MSG_START_SESSION, &start));

  start = make_valid_live_start();
  start.sample_format = 0;
  check_false("START rejects non-S16LE sample format", vw_protocol_validate_payload(VW_MSG_START_SESSION, &start));

  start = make_valid_live_start();
  start.source_kind = 99;
  check_false("START rejects unknown source kind", vw_protocol_validate_payload(VW_MSG_START_SESSION, &start));

  start = make_valid_live_start();
  snprintf(start.source_url, sizeof(start.source_url), "%s", "file:///tmp/contradictory.wav");
  start.source_url_len = (uint16_t)strlen(start.source_url);
  check_false("live START rejects a source URL", vw_protocol_validate_payload(VW_MSG_START_SESSION, &start));

  start = make_valid_local_start();
  start.source_url[0] = '\0';
  start.source_url_len = 0;
  check_false("local-file START requires a source URL", vw_protocol_validate_payload(VW_MSG_START_SESSION, &start));

  start = make_valid_local_start();
  start.source_url_len = 1;
  check_false("START rejects source_url_len/string mismatch",
              vw_protocol_validate_payload(VW_MSG_START_SESSION, &start));

  start = make_valid_local_start();
  start.source_url_len = VW_MAX_SOURCE_URL_BYTES + 1U;
  check_false("START rejects source URL length beyond protocol maximum",
              vw_protocol_validate_payload(VW_MSG_START_SESSION, &start));

  start = make_valid_live_start();
  start.language[0] = '\0';
  check_false("START rejects an empty language", vw_protocol_validate_payload(VW_MSG_START_SESSION, &start));

  start = make_valid_live_start();
  memset(start.language, 'x', sizeof(start.language));
  check_false("START rejects a non-terminated language field",
              vw_protocol_validate_payload(VW_MSG_START_SESSION, &start));

  if (g_failures != 0) {
    fprintf(stderr, "test_protocol_start_failure_paths: %d contract failure(s)\n", g_failures);
    return 1;
  }

  printf("test_protocol_start_failure_paths PASSED\n");
  return 0;
}
