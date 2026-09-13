#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "vw_benchmark.h"
#include "vw_log.h"
#include "vw_test.h"
#include "vw_translate.h"

typedef enum blame_mode {
  BLAME_TRANSPORT,
  BLAME_PROVIDER,
  BLAME_PARSE,
  BLAME_DEADLINE,
} blame_mode_t;

typedef struct hook_state {
  blame_mode_t mode;
  unsigned calls;
} hook_state_t;

typedef struct log_capture {
  vw_log_level_t level;
  char event_id[64];
  char message[512];
  unsigned count;
} log_capture_t;

static void sleep_ms(uint32_t ms) {
#ifdef _WIN32
  Sleep(ms);
#else
  struct timespec ts = {.tv_sec = (time_t)(ms / 1000U), .tv_nsec = (long)(ms % 1000U) * 1000000L};
  while (nanosleep(&ts, &ts) != 0) {
  }
#endif
}

static vw_translate_test_http_outcome_t http_hook(const char* host, const char* path, const char* body,
                                                  const char* type, char* out, size_t cap, uint32_t timeout_ms,
                                                  uint16_t* status, void* data) {
  (void)host;
  (void)path;
  (void)body;
  (void)type;
  hook_state_t* state = (hook_state_t*)data;
  state->calls++;
  if (status) *status = 0;

  if (state->mode == BLAME_PROVIDER) {
    if (status) *status = 429;
    return VW_TRANSLATE_TEST_HTTP_PROVIDER;
  }
  if (state->mode == BLAME_DEADLINE) {
    sleep_ms(timeout_ms);
    return VW_TRANSLATE_TEST_HTTP_DEADLINE;
  }
  if (state->mode == BLAME_PARSE) {
    const char* malformed = "provider-response-without-translation";
    size_t len = strlen(malformed);
    if (len + 1U < cap) memcpy(out, malformed, len + 1U);
    return VW_TRANSLATE_TEST_HTTP_OK;
  }
  return VW_TRANSLATE_TEST_HTTP_TRANSPORT;
}

static void log_sink(vw_log_level_t level, const char* event_id, const char* message, void* data) {
  log_capture_t* capture = (log_capture_t*)data;
  if (!capture) return;
  capture->level = level;
  snprintf(capture->event_id, sizeof(capture->event_id), "%s", event_id ? event_id : "");
  snprintf(capture->message, sizeof(capture->message), "%s", message ? message : "");
  capture->count++;
}

static vw_translate_failure_t run_failure(blame_mode_t mode, unsigned* calls_out) {
  hook_state_t state = {.mode = mode, .calls = 0};
  vw_translate_set_test_http_diagnostic_hook(http_hook, &state);

  char out[256];
  uint8_t tier = 99;
  uint32_t latency_us = 0;
  vw_translate_failure_t failure = {0};
  bool ok =
      vw_translate_text_detailed("failure contract", "en", "ro", out, sizeof(out), &tier, &latency_us, &failure);

  vw_translate_set_test_http_diagnostic_hook(NULL, NULL);
  vw_test_check_false("failure does not report success", ok);
  vw_test_check_true("failure keeps success tier none", tier == VW_TRANSLATE_TIER_NONE);
  if (calls_out) *calls_out = state.calls;
  return failure;
}

static void test_translator_causes(void) {
  unsigned calls = 0;
  vw_translate_failure_t failure = run_failure(BLAME_TRANSPORT, &calls);
  vw_test_check_true("transport cause is explicit", failure.cause == VW_TRANSLATE_FAILURE_TRANSPORT);
  vw_test_check_true("transport terminal tier is mobile", failure.terminal_tier == VW_TRANSLATE_TIER_MOBILE_SCRAPE);
  vw_test_check_true("transport records all tiers", failure.attempted_tiers == 0x07U);
  vw_test_check_true("transport attempts all tiers", calls == 3U);

  failure = run_failure(BLAME_PROVIDER, &calls);
  vw_test_check_true("provider cause is explicit", failure.cause == VW_TRANSLATE_FAILURE_PROVIDER);
  vw_test_check_true("provider status is preserved", failure.provider_status == 429U);
  vw_test_check_true("provider terminal tier is mobile", failure.terminal_tier == VW_TRANSLATE_TIER_MOBILE_SCRAPE);

  failure = run_failure(BLAME_PARSE, &calls);
  vw_test_check_true("parse cause is explicit", failure.cause == VW_TRANSLATE_FAILURE_PARSE);
  vw_test_check_true("parse records all tiers", failure.attempted_tiers == 0x07U);
  vw_test_check_true("parse terminal tier is mobile", failure.terminal_tier == VW_TRANSLATE_TIER_MOBILE_SCRAPE);

  failure = run_failure(BLAME_DEADLINE, &calls);
  vw_test_check_true("deadline cause is explicit", failure.cause == VW_TRANSLATE_FAILURE_DEADLINE);
  vw_test_check_true("deadline terminal tier is rpc", failure.terminal_tier == VW_TRANSLATE_TIER_WEB_RPC);
  vw_test_check_true("deadline stops fallbacks", failure.attempted_tiers == 0x01U && calls == 1U);
}

static void test_plugin_logging_and_aggregates(void) {
  vw_benchmark_t benchmark = {.active = true};
  log_capture_t capture = {0};
  vw_log_set_sink(log_sink, &capture);
  vw_log_set_enabled(true);

  vw_benchmark_record_translation(&benchmark, 0, VW_BENCHMARK_TRANSLATION_TIMEOUT_US, false);
  vw_test_check_true("failed caption increments failure total", benchmark.translation_failure_count == 1U);
  vw_test_check_true("latency does not guess timeout", benchmark.translation_timeout_count == 0U);
  vw_test_check_true("caption metric path does not log failure", capture.count == 0U);

  const char* provider = "segment=42 cause=provider tier=mobile attempts=0x07 status=429 latency_ms=734.000";
  vw_benchmark_record_translation_failure(&benchmark, E_TRANSLATION_PROVIDER, provider);
  vw_test_check_true("provider aggregate increments", benchmark.translation_provider_failure_count == 1U);
  vw_test_check_true("one VLC error event is emitted", capture.count == 1U);
  vw_test_check_true("translation event id is stable", strcmp(capture.event_id, "PLUGIN_TRANSLATION_FAILURE") == 0);
  vw_test_check_true("live blame includes cause", strstr(capture.message, "cause=provider") != NULL);
  vw_test_check_true("live blame includes terminal tier", strstr(capture.message, "tier=mobile") != NULL);
  vw_test_check_false("live blame omits subtitle body", strstr(capture.message, "failure contract") != NULL);

  const char* transport = "segment=43 cause=transport tier=gtx attempts=0x03 latency_ms=220.000";
  const char* parse = "segment=44 cause=parse tier=mobile attempts=0x07 latency_ms=310.000";
  const char* deadline = "segment=45 cause=deadline tier=rpc attempts=0x01 latency_ms=800.000";
  vw_benchmark_record_translation_failure(&benchmark, E_TRANSLATION_TRANSPORT, transport);
  vw_benchmark_record_translation_failure(&benchmark, E_TRANSLATION_PARSE, parse);
  vw_benchmark_record_translation_failure(&benchmark, E_TRANSLATION_DEADLINE, deadline);
  vw_test_check_true("transport aggregate increments", benchmark.translation_transport_failure_count == 1U);
  vw_test_check_true("parse aggregate increments", benchmark.translation_parse_failure_count == 1U);
  vw_test_check_true("deadline owns timeout aggregate", benchmark.translation_timeout_count == 1U);

  vw_log_set_enabled(false);
  vw_log_set_sink(NULL, NULL);
}

int main(void) {
  test_translator_causes();
  test_plugin_logging_and_aggregates();
  return vw_test_finish("translation_blame_contracts");
}
