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

typedef enum vw_blame_hook_mode {
  VW_BLAME_HOOK_TRANSPORT,
  VW_BLAME_HOOK_PROVIDER,
  VW_BLAME_HOOK_PARSE,
  VW_BLAME_HOOK_DEADLINE,
} vw_blame_hook_mode_t;

typedef struct vw_blame_hook_state {
  vw_blame_hook_mode_t mode;
  unsigned calls;
} vw_blame_hook_state_t;

typedef struct vw_blame_log_capture {
  vw_log_level_t level;
  char event_id[64];
  char message[512];
  unsigned count;
} vw_blame_log_capture_t;

static void vw_blame_sleep_ms(uint32_t ms) {
#ifdef _WIN32
  Sleep(ms);
#else
  struct timespec ts = {.tv_sec = (time_t)(ms / 1000U), .tv_nsec = (long)(ms % 1000U) * 1000000L};
  while (nanosleep(&ts, &ts) != 0) {
  }
#endif
}

static vw_translate_test_http_outcome_t vw_blame_http_hook(const char* host, const char* path, const char* body,
                                                            const char* content_type, char* out_buf, size_t buf_size,
                                                            uint32_t timeout_ms, uint16_t* out_status,
                                                            void* user_data) {
  (void)host;
  (void)path;
  (void)body;
  (void)content_type;
  vw_blame_hook_state_t* state = (vw_blame_hook_state_t*)user_data;
  state->calls++;
  if (out_status) *out_status = 0;

  if (state->mode == VW_BLAME_HOOK_PROVIDER) {
    if (out_status) *out_status = 429;
    return VW_TRANSLATE_TEST_HTTP_PROVIDER;
  }
  if (state->mode == VW_BLAME_HOOK_DEADLINE) {
    vw_blame_sleep_ms(timeout_ms);
    return VW_TRANSLATE_TEST_HTTP_DEADLINE;
  }
  if (state->mode == VW_BLAME_HOOK_PARSE) {
    const char* malformed = "provider-response-without-translation";
    size_t len = strlen(malformed);
    if (len + 1U < buf_size) memcpy(out_buf, malformed, len + 1U);
    return VW_TRANSLATE_TEST_HTTP_OK;
  }
  return VW_TRANSLATE_TEST_HTTP_TRANSPORT;
}

static void vw_blame_log_sink(vw_log_level_t level, const char* event_id, const char* formatted_msg,
                              void* user_data) {
  vw_blame_log_capture_t* capture = (vw_blame_log_capture_t*)user_data;
  if (!capture) return;
  capture->level = level;
  snprintf(capture->event_id, sizeof(capture->event_id), "%s", event_id ? event_id : "");
  snprintf(capture->message, sizeof(capture->message), "%s", formatted_msg ? formatted_msg : "");
  capture->count++;
}

static vw_translate_failure_t vw_blame_run_failure(vw_blame_hook_mode_t mode, unsigned* calls_out) {
  vw_blame_hook_state_t state = {.mode = mode, .calls = 0};
  vw_translate_set_test_http_hook(vw_blame_http_hook, &state);

  char out[256];
  uint8_t tier = 99;
  uint32_t latency_us = 0;
  vw_translate_failure_t failure = {0};
  bool ok = vw_translate_text("failure contract", "en", "ro", out, sizeof(out), &tier, &latency_us, &failure);

  vw_translate_set_test_http_hook(NULL, NULL);
  vw_test_check_false("failure contract does not report translation success", ok);
  vw_test_check_true("failed translation keeps success tier none", tier == VW_TRANSLATE_TIER_NONE);
  if (calls_out) *calls_out = state.calls;
  return failure;
}

static void vw_blame_test_translator_causes(void) {
  unsigned calls = 0;
  vw_translate_failure_t failure = vw_blame_run_failure(VW_BLAME_HOOK_TRANSPORT, &calls);
  vw_test_check_true("transport failure is explicitly classified",
                     failure.cause == VW_TRANSLATE_FAILURE_TRANSPORT);
  vw_test_check_true("transport failure reaches terminal mobile fallback",
                     failure.terminal_tier == VW_TRANSLATE_TIER_MOBILE_SCRAPE);
  vw_test_check_true("transport failure records all attempted fallbacks", failure.attempted_tiers == 0x07U);
  vw_test_check_true("transport failure attempts all three tiers", calls == 3U);

  failure = vw_blame_run_failure(VW_BLAME_HOOK_PROVIDER, &calls);
  vw_test_check_true("provider rejection is explicitly classified",
                     failure.cause == VW_TRANSLATE_FAILURE_PROVIDER);
  vw_test_check_true("provider rejection preserves terminal HTTP status", failure.provider_status == 429U);
  vw_test_check_true("provider rejection reaches terminal mobile fallback",
                     failure.terminal_tier == VW_TRANSLATE_TIER_MOBILE_SCRAPE);

  failure = vw_blame_run_failure(VW_BLAME_HOOK_PARSE, &calls);
  vw_test_check_true("malformed successful response is classified as parse failure",
                     failure.cause == VW_TRANSLATE_FAILURE_PARSE);
  vw_test_check_true("parse failure records all attempted fallbacks", failure.attempted_tiers == 0x07U);
  vw_test_check_true("parse failure reaches terminal mobile fallback",
                     failure.terminal_tier == VW_TRANSLATE_TIER_MOBILE_SCRAPE);

  failure = vw_blame_run_failure(VW_BLAME_HOOK_DEADLINE, &calls);
  vw_test_check_true("deadline exhaustion is explicit", failure.cause == VW_TRANSLATE_FAILURE_DEADLINE);
  vw_test_check_true("deadline reports the tier that consumed the budget",
                     failure.terminal_tier == VW_TRANSLATE_TIER_WEB_RPC);
  vw_test_check_true("deadline stops before later fallbacks", failure.attempted_tiers == 0x01U && calls == 1U);
}

static void vw_blame_test_plugin_logging_and_aggregates(void) {
  vw_benchmark_t benchmark = {0};
  vw_blame_log_capture_t capture = {0};
  vw_log_set_sink(vw_blame_log_sink, &capture);
  vw_log_set_enabled(true);

  vw_benchmark_record_translation(&benchmark, 0, VW_BENCHMARK_TRANSLATION_TIMEOUT_US, false);
  vw_test_check_true("failed caption increments total failure count", benchmark.translation_failure_count == 1U);
  vw_test_check_true("latency no longer guesses timeout cause", benchmark.translation_timeout_count == 0U);
  vw_test_check_true("caption metric path no longer emits duplicate failure log", capture.count == 0U);

  vw_benchmark_record_translation_failure(&benchmark, E_TRANSLATION_PROVIDER,
                                          "segment=42 cause=provider tier=mobile attempts=0x07 status=429 latency_ms=734.000");
  vw_test_check_true("provider failure aggregate increments", benchmark.translation_provider_failure_count == 1U);
  vw_test_check_true("translation blame uses one VLC error event", capture.count == 1U);
  vw_test_check_true("translation blame event id is stable",
                     strcmp(capture.event_id, "PLUGIN_TRANSLATION_FAILURE") == 0);
  vw_test_check_true("translation blame is concise and includes cause", strstr(capture.message, "cause=provider") != NULL);
  vw_test_check_true("translation blame includes terminal fallback", strstr(capture.message, "tier=mobile") != NULL);
  vw_test_check_false("translation blame omits source subtitle body", strstr(capture.message, "failure contract") != NULL);

  vw_benchmark_record_translation_failure(&benchmark, E_TRANSLATION_TRANSPORT,
                                          "segment=43 cause=transport tier=gtx attempts=0x03 latency_ms=220.000");
  vw_benchmark_record_translation_failure(&benchmark, E_TRANSLATION_PARSE,
                                          "segment=44 cause=parse tier=mobile attempts=0x07 latency_ms=310.000");
  vw_benchmark_record_translation_failure(&benchmark, E_TRANSLATION_DEADLINE,
                                          "segment=45 cause=deadline tier=rpc attempts=0x01 latency_ms=800.000");
  vw_test_check_true("transport failure aggregate increments", benchmark.translation_transport_failure_count == 1U);
  vw_test_check_true("parse failure aggregate increments", benchmark.translation_parse_failure_count == 1U);
  vw_test_check_true("explicit deadline owns timeout aggregate", benchmark.translation_timeout_count == 1U);

  vw_log_set_enabled(false);
  vw_log_set_sink(NULL, NULL);
}

int main(void) {
  vw_blame_test_translator_causes();
  vw_blame_test_plugin_logging_and_aggregates();
  return vw_test_finish("translation_blame_contracts");
}
