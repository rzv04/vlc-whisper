#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "vw_translate.h"

static int64_t test_monotonic_us(void) {
#ifdef _WIN32
  LARGE_INTEGER freq;
  LARGE_INTEGER counter;
  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&counter);
  int64_t seconds = counter.QuadPart / freq.QuadPart;
  int64_t remainder = counter.QuadPart % freq.QuadPart;
  return seconds * 1000000LL + (remainder * 1000000LL) / freq.QuadPart;
#else
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000000LL + (int64_t)ts.tv_nsec / 1000LL;
#endif
}

static void test_sleep_ms(uint32_t ms) {
#ifdef _WIN32
  Sleep(ms);
#else
  struct timespec ts = {.tv_sec = (time_t)(ms / 1000U), .tv_nsec = (long)(ms % 1000U) * 1000000L};
  while (nanosleep(&ts, &ts) != 0) {
  }
#endif
}

static void test_url_encode(void) {
  char buf[256];
  bool ok = vw_url_encode("", buf, sizeof(buf));
  assert(ok);
  assert(strcmp(buf, "") == 0);

  ok = vw_url_encode("HelloWorld123", buf, sizeof(buf));
  assert(ok);
  assert(strcmp(buf, "HelloWorld123") == 0);

  ok = vw_url_encode("hello world", buf, sizeof(buf));
  assert(ok);
  assert(strcmp(buf, "hello%20world") == 0);

  ok = vw_url_encode("foo&bar=baz?q=1+2", buf, sizeof(buf));
  assert(ok);
  assert(strcmp(buf, "foo%26bar%3Dbaz%3Fq%3D1%2B2") == 0);

  char small_buf[5];
  ok = vw_url_encode("hello world", small_buf, sizeof(small_buf));
  assert(!ok);
}

static void test_html_unescape(void) {
  char buf[256];
  bool ok = vw_html_unescape("Hello World", buf, sizeof(buf));
  assert(ok);
  assert(strcmp(buf, "Hello World") == 0);

  ok = vw_html_unescape("&quot;Hello &amp; &lt;World&gt;&#39;", buf, sizeof(buf));
  assert(ok);
  assert(strcmp(buf, "\"Hello & <World>'") == 0);

  ok = vw_html_unescape("<div class=\"result\">Salut <b>lume</b>!</div>", buf, sizeof(buf));
  assert(ok);
  assert(strcmp(buf, "Salut lume!") == 0);

  ok = vw_html_unescape("&#72;&#101;&#108;&#108;&#111; &#x1F600;", buf, sizeof(buf));
  assert(ok);
  assert(strcmp(buf, "Hello \xF0\x9F\x98\x80") == 0);
}

static void test_parse_rpc_response(void) {
  char buf[256];
  const char* rpc_resp =
      ")]}'\n\n[[\"wrb.fr\",\"MkEWBc\",\"[[[\\\"Salut lume\\\",null,null,null,1]]\\n]\",null,null,null,\"generic\"]]\n";
  bool ok = vw_translate_parse_rpc_response(rpc_resp, buf, sizeof(buf));
  assert(ok);
  assert(strcmp(buf, "Salut lume") == 0);

  ok = vw_translate_parse_rpc_response("null", buf, sizeof(buf));
  assert(!ok);
  ok = vw_translate_parse_rpc_response("[[\"other_rpc\",\"payload\"]]", buf, sizeof(buf));
  assert(!ok);
}

static void test_parse_gtx_response(void) {
  char buf[256];
  const char* gtx_single = "[[[\"Salut lume\",\"Hello world\",null,null,1]],null,\"en\"]";
  bool ok = vw_translate_parse_gtx_response(gtx_single, buf, sizeof(buf));
  assert(ok);
  assert(strcmp(buf, "Salut lume") == 0);

  const char* gtx_multi =
      "[[[\"Salut lume \",\"Hello world \",null,null,1],[\"cum merge?\",\"how is it "
      "going?\",null,null,1]],null,\"en\"]";
  ok = vw_translate_parse_gtx_response(gtx_multi, buf, sizeof(buf));
  assert(ok);
  assert(strcmp(buf, "Salut lume cum merge?") == 0);

  const char* gtx_emoji = "[[[\"Smile \\uD83D\\uDE00\",\"Smile\",null,null,1]],null,\"en\"]";
  ok = vw_translate_parse_gtx_response(gtx_emoji, buf, sizeof(buf));
  assert(ok);
  assert(strcmp(buf, "Smile \xF0\x9F\x98\x80") == 0);

  const char* gtx_escaped = "[[[\"path \\\\ and quote \\\"ok\\\"\",\"x\",null,null,1]],null,\"en\"]";
  ok = vw_translate_parse_gtx_response(gtx_escaped, buf, sizeof(buf));
  assert(ok);
  assert(strcmp(buf, "path \\ and quote \"ok\"") == 0);

  const char* lone_surrogate = "[[[\"bad \\uD83D\",\"x\",null,null,1]],null,\"en\"]";
  assert(!vw_translate_parse_gtx_response(lone_surrogate, buf, sizeof(buf)));

  char tiny[8];
  assert(!vw_translate_parse_gtx_response(gtx_multi, tiny, sizeof(tiny)));
  assert(!vw_translate_parse_gtx_response("[]", buf, sizeof(buf)));
  assert(!vw_translate_parse_gtx_response("error", buf, sizeof(buf)));
}

static void test_parse_mobile_response(void) {
  char buf[256];
  const char* html_resp =
      "<!DOCTYPE html><html><body><div class=\"main\"><div class=\"result-container\">Salut lume &amp; "
      "prieteni</div></div></body></html>";
  bool ok = vw_translate_parse_mobile_response(html_resp, buf, sizeof(buf));
  assert(ok);
  assert(strcmp(buf, "Salut lume & prieteni") == 0);

  const char* bad_html = "<html><body><div>No result here</div></body></html>";
  ok = vw_translate_parse_mobile_response(bad_html, buf, sizeof(buf));
  assert(!ok);
}

static void test_rpc_request_escaping(void) {
  char body[65536];
  bool ok = vw_translate_build_rpc_body_for_test("He said \"hi\" at C:\\tmp\nnext", "en", "ro", body, sizeof(body));
  assert(ok);
  assert(strncmp(body, "f.req=", 6) == 0);
  // The nested JSON string must contain the quote as an escaped quote in the inner JSON and then be escaped again for
  // the outer RPC string: three backslashes followed by the quote, all percent encoded in the form body.
  assert(strstr(body, "%5C%5C%5C%22hi%5C%5C%5C%22") != NULL);
  assert(strchr(body, '\n') == NULL);
  assert(strchr(body, '"') == NULL);
  assert(strchr(body, '\\') == NULL);
}

typedef enum hook_mode { HOOK_FALLBACK_TO_GTX, HOOK_CONSUME_DEADLINE } hook_mode_t;

typedef struct hook_state {
  hook_mode_t mode;
  int calls;
  uint32_t timeouts[4];
} hook_state_t;

static bool test_http_hook(const char* host, const char* path, const char* body, const char* content_type, char* out_buf,
                           size_t buf_size, uint32_t timeout_ms, void* user_data) {
  (void)body;
  (void)content_type;
  hook_state_t* state = (hook_state_t*)user_data;
  int call = state->calls++;
  if (call < 4) state->timeouts[call] = timeout_ms;
  if (state->mode == HOOK_CONSUME_DEADLINE) {
    test_sleep_ms(timeout_ms);
    return false;
  }
  if (call == 0) {
    assert(strcmp(host, "translate.google.com") == 0);
    assert(strstr(path, "batchexecute") != NULL);
    return false;
  }
  assert(strcmp(host, "translate.googleapis.com") == 0);
  const char* response = "[[[\"Salut lume\",\"Hello world\",null,null,1]],null,\"en\"]";
  size_t len = strlen(response);
  assert(len + 1 < buf_size);
  memcpy(out_buf, response, len + 1);
  return true;
}

static void test_real_fallback_path_with_hook(void) {
  hook_state_t state = {.mode = HOOK_FALLBACK_TO_GTX};
  vw_translate_set_test_http_hook(test_http_hook, &state);
  char out[256];
  uint8_t tier = 0;
  uint32_t latency_us = 0;
  bool ok = vw_translate_text("Hello world", "en", "ro", out, sizeof(out), &tier, &latency_us);
  vw_translate_set_test_http_hook(NULL, NULL);
  assert(ok);
  assert(strcmp(out, "Salut lume") == 0);
  assert(tier == VW_TRANSLATE_TIER_GTX);
  assert(state.calls == 2);
  assert(state.timeouts[1] <= state.timeouts[0]);
}

static void test_global_deadline(void) {
  hook_state_t state = {.mode = HOOK_CONSUME_DEADLINE};
  vw_translate_set_test_http_hook(test_http_hook, &state);
  char out[256];
  uint8_t tier = 99;
  uint32_t latency_us = 0;
  int64_t started = test_monotonic_us();
  bool ok = vw_translate_text("deadline", "en", "ro", out, sizeof(out), &tier, &latency_us);
  int64_t elapsed = test_monotonic_us() - started;
  vw_translate_set_test_http_hook(NULL, NULL);
  assert(!ok);
  assert(tier == VW_TRANSLATE_TIER_NONE);
  // One hook invocation consumes the entire remaining cue budget. A per-tier timeout implementation would invoke the
  // hook three times and take roughly 2.4 seconds.
  assert(state.calls == 1);
  assert(elapsed >= 700000LL);
  assert(elapsed < 1200000LL);
  assert(latency_us >= 700000U);
  assert(latency_us < 1200000U);
}

static void test_tier_constants(void) {
  assert(VW_TRANSLATE_TIER_NONE == 0);
  assert(VW_TRANSLATE_TIER_WEB_RPC == 1);
  assert(VW_TRANSLATE_TIER_GTX == 2);
  assert(VW_TRANSLATE_TIER_MOBILE_SCRAPE == 3);
  assert(VW_TRANSLATE_MAX_TEXT_BYTES == 1024);
  assert(VW_TRANSLATE_TIMEOUT_MS == 800);
}

int main(void) {
  test_url_encode();
  test_html_unescape();
  test_parse_rpc_response();
  test_parse_gtx_response();
  test_parse_mobile_response();
  test_rpc_request_escaping();
  test_real_fallback_path_with_hook();
  test_global_deadline();
  test_tier_constants();
  printf("All translate unit tests PASSED.\n");
  return 0;
}
