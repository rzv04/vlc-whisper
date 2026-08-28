#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "vw_translate.h"
#include "vw_translate_async.h"

static uint32_t g_hook_delay_ms = 0;

static int64_t monotonic_us(void) {
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

static void sleep_ms(uint32_t ms) {
#ifdef _WIN32
  Sleep(ms);
#else
  struct timespec ts = {.tv_sec = (time_t)(ms / 1000U), .tv_nsec = (long)(ms % 1000U) * 1000000L};
  while (nanosleep(&ts, &ts) != 0) {
  }
#endif
}

static bool async_http_hook(const char* host, const char* path, const char* body, const char* content_type,
                            char* out_buf, size_t buf_size, uint32_t timeout_ms, void* user_data) {
  (void)body;
  (void)content_type;
  (void)user_data;
  uint32_t delay = g_hook_delay_ms;
  if (delay > timeout_ms) delay = timeout_ms;
  if (delay) sleep_ms(delay);
  if (strstr(path, "batchexecute")) return false;
  if (strcmp(host, "translate.googleapis.com") == 0) {
    const char* response = "[[[\"Salut lume\",\"Hello world\",null,null,1]],null,\"en\"]";
    size_t len = strlen(response);
    assert(len + 1 < buf_size);
    memcpy(out_buf, response, len + 1);
    return true;
  }
  return false;
}

static vw_caption_segment_t make_segment(uint64_t id, char* text) {
  vw_caption_segment_t seg;
  memset(&seg, 0, sizeof(seg));
  seg.segment_id = id;
  seg.start_pts_us = 1000000LL;
  seg.end_pts_us = 2000000LL;
  seg.is_final = true;
  seg.text_utf8 = text;
  seg.text_bytes = (uint16_t)strlen(text);
  seg.session_id.bytes[0] = 0x42;
  return seg;
}

int main(void) {
  vw_translate_set_test_http_hook(async_http_hook, NULL);
  vw_translate_async_t* async = vw_translate_async_create();
  assert(async != NULL);

  char first_text[] = "Hello world";
  vw_caption_segment_t first = make_segment(1, first_text);
  g_hook_delay_ms = 150;
  int64_t submit_started = monotonic_us();
  assert(vw_translate_async_submit(async, &first, "en", "ro"));
  int64_t submit_elapsed = monotonic_us() - submit_started;
  assert(submit_elapsed < 50000LL);  // queueing must not inherit network latency

  sleep_ms(20);
  vw_translate_async_invalidate(async);  // model an arriving seek/session epoch change
  sleep_ms(400);
  assert(!vw_translate_async_has_result(async));
  vw_translate_async_result_t result;
  assert(!vw_translate_async_try_pop(async, &result));

  char second_text[] = "Hello world";
  vw_caption_segment_t second = make_segment(2, second_text);
  second.session_id.bytes[0] = 0x43;
  g_hook_delay_ms = 0;
  assert(vw_translate_async_submit(async, &second, "en", "ro"));

  bool received = false;
  for (int i = 0; i < 200; i++) {
    if (vw_translate_async_try_pop(async, &result)) {
      received = true;
      break;
    }
    sleep_ms(5);
  }
  assert(received);
  assert(result.success);
  assert(result.segment.segment_id == 2);
  assert(result.segment.session_id.bytes[0] == 0x43);
  assert(strcmp(result.segment.text_utf8, "Hello world") == 0);
  assert(strcmp(result.segment.translated_text_utf8, "Salut lume") == 0);
  assert(result.segment.translation_tier == VW_TRANSLATE_TIER_GTX);

  vw_translate_async_destroy(async);
  vw_translate_set_test_http_hook(NULL, NULL);
  printf("test_translate_async PASSED.\n");
  return 0;
}
