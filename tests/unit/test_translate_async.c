#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdatomic.h>
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
static atomic_bool g_first_request_blocked;
static atomic_bool g_first_request_entered;
static atomic_bool g_first_request_released;
static atomic_uint g_barrier_target;
static atomic_uint g_barrier_arrivals;
static atomic_bool g_barrier_released;

static vw_caption_segment_t make_segment(uint64_t id, char* text);

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
  if (atomic_exchange(&g_first_request_blocked, false)) {
    atomic_store(&g_first_request_entered, true);
    while (!atomic_load(&g_first_request_released)) sleep_ms(1);
  }

  unsigned barrier_target = atomic_load(&g_barrier_target);
  if (barrier_target > 0U) {
    unsigned arrival = atomic_fetch_add(&g_barrier_arrivals, 1U) + 1U;
    if (arrival <= barrier_target) {
      while (!atomic_load(&g_barrier_released)) sleep_ms(1);
    }
  }

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

static void test_hard_cap_inflight_fifo_ordering(void) {
  vw_translate_set_test_http_hook(async_http_hook, NULL);
  vw_translate_async_t* async = vw_translate_async_create();
  assert(async != NULL);

  atomic_store(&g_first_request_blocked, true);
  atomic_store(&g_first_request_entered, false);
  atomic_store(&g_first_request_released, false);
  atomic_store(&g_barrier_target, 0U);
  g_hook_delay_ms = 0;

  char texts[VW_TRANSLATE_ASYNC_QUEUE_CAPACITY + 2U][32];
  snprintf(texts[0], sizeof(texts[0]), "Phrase 1");
  vw_caption_segment_t first = make_segment(1, texts[0]);
  assert(vw_translate_async_submit(async, &first, "en", "ro"));
  for (int retry = 0; retry < 100 && !atomic_load(&g_first_request_entered); retry++) sleep_ms(5);
  assert(atomic_load(&g_first_request_entered));

  for (size_t i = 1; i < VW_TRANSLATE_ASYNC_QUEUE_CAPACITY + 2U; i++) {
    snprintf(texts[i], sizeof(texts[i]), "Phrase %zu", i + 1U);
    vw_caption_segment_t seg = make_segment(i + 1U, texts[i]);
    assert(vw_translate_async_submit(async, &seg, "en", "ro"));
  }

  // Later workers may complete cues while cue 1 is blocked. Ordered popping must keep them hidden behind cue 1.
  vw_translate_async_result_t result;
  assert(!vw_translate_async_try_pop(async, &result));
  atomic_store(&g_first_request_released, true);

  uint64_t expected_id = 1;
  size_t popped_count = 0;
  const size_t expected_count = VW_TRANSLATE_ASYNC_QUEUE_CAPACITY + 2U;
  for (int retry = 0; retry < 1000 && popped_count < expected_count; retry++) {
    if (!vw_translate_async_try_pop(async, &result)) {
      sleep_ms(5);
      continue;
    }
    assert(result.segment.segment_id == expected_id);
    expected_id++;
    popped_count++;
  }
  assert(popped_count == expected_count);

  vw_translate_async_destroy(async);
  vw_translate_set_test_http_hook(NULL, NULL);
}

static void test_active_budget_runs_requests_concurrently(void) {
  vw_translate_set_test_http_hook(async_http_hook, NULL);
  vw_translate_async_t* async = vw_translate_async_create();
  assert(async != NULL);

  atomic_store(&g_first_request_blocked, false);
  atomic_store(&g_first_request_entered, false);
  atomic_store(&g_first_request_released, true);
  atomic_store(&g_barrier_target, VW_TRANSLATE_ASYNC_ACTIVE_BUDGET);
  atomic_store(&g_barrier_arrivals, 0U);
  atomic_store(&g_barrier_released, false);
  g_hook_delay_ms = 0;

  char texts[VW_TRANSLATE_ASYNC_ACTIVE_BUDGET + 1U][32];
  for (size_t i = 0; i < VW_TRANSLATE_ASYNC_ACTIVE_BUDGET; i++) {
    snprintf(texts[i], sizeof(texts[i]), "Budget phrase %zu", i + 1U);
    vw_caption_segment_t seg = make_segment(i + 1U, texts[i]);
    assert(vw_translate_async_submit(async, &seg, "en", "ro"));
  }

  for (int retry = 0; retry < 200 && atomic_load(&g_barrier_arrivals) < VW_TRANSLATE_ASYNC_ACTIVE_BUDGET; retry++) {
    sleep_ms(2);
  }
  assert(atomic_load(&g_barrier_arrivals) == VW_TRANSLATE_ASYNC_ACTIVE_BUDGET);

  snprintf(texts[VW_TRANSLATE_ASYNC_ACTIVE_BUDGET], sizeof(texts[VW_TRANSLATE_ASYNC_ACTIVE_BUDGET]),
           "Budget phrase %u", VW_TRANSLATE_ASYNC_ACTIVE_BUDGET + 1U);
  vw_caption_segment_t saturated =
      make_segment(VW_TRANSLATE_ASYNC_ACTIVE_BUDGET + 1U, texts[VW_TRANSLATE_ASYNC_ACTIVE_BUDGET]);
  assert(vw_translate_async_submit(async, &saturated, "en", "ro"));

  sleep_ms(10);
  assert(atomic_load(&g_barrier_arrivals) == VW_TRANSLATE_ASYNC_ACTIVE_BUDGET);
  atomic_store(&g_barrier_released, true);

  size_t popped_count = 0;
  size_t attempted_count = 0;
  vw_translate_async_result_t result;
  for (int retry = 0; retry < 1000 && popped_count < VW_TRANSLATE_ASYNC_ACTIVE_BUDGET + 1U; retry++) {
    if (!vw_translate_async_try_pop(async, &result)) {
      sleep_ms(5);
      continue;
    }
    assert(result.segment.segment_id == popped_count + 1U);
    if (result.segment.segment_id <= VW_TRANSLATE_ASYNC_ACTIVE_BUDGET) {
      assert(result.attempted);
      assert(result.success);
      attempted_count++;
    } else {
      assert(!result.attempted);
      assert(!result.success);
      assert(result.segment.translated_text_utf8 == NULL);
    }
    popped_count++;
  }
  assert(popped_count == VW_TRANSLATE_ASYNC_ACTIVE_BUDGET + 1U);
  assert(attempted_count == VW_TRANSLATE_ASYNC_ACTIVE_BUDGET);

  atomic_store(&g_barrier_target, 0U);
  vw_translate_async_destroy(async);
  vw_translate_set_test_http_hook(NULL, NULL);
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

static void test_saturation_fifo_ordering(void) {
  vw_translate_set_test_http_hook(async_http_hook, NULL);
  vw_translate_async_t* async = vw_translate_async_create();
  assert(async != NULL);

  atomic_store(&g_barrier_target, 0U);
  g_hook_delay_ms = 80;

  char texts[6][32];
  for (int i = 0; i < 6; i++) {
    snprintf(texts[i], sizeof(texts[i]), "Phrase %d", i + 1);
    vw_caption_segment_t seg = make_segment(i + 1, texts[i]);
    seg.start_pts_us = (int64_t)(i + 1) * 1000000LL;
    seg.end_pts_us = (int64_t)(i + 2) * 1000000LL;
    assert(vw_translate_async_submit(async, &seg, "en", "ro"));
  }

  // Drain all 6 results and assert strict chronological monotonicity.
  uint64_t expected_id = 1;
  int popped_count = 0;
  for (int retry = 0; retry < 400 && popped_count < 6; retry++) {
    vw_translate_async_result_t res;
    if (vw_translate_async_try_pop(async, &res)) {
      assert(res.segment.segment_id == expected_id);
      assert(res.segment.start_pts_us == (int64_t)expected_id * 1000000LL);
      if (res.success) {
        assert(strcmp(res.segment.translated_text_utf8, "Salut lume") == 0);
      } else {
        assert(res.segment.translated_text_utf8 == NULL);
      }
      expected_id++;
      popped_count++;
    } else {
      sleep_ms(10);
    }
  }
  assert(popped_count == 6);
  assert(expected_id == 7);

  vw_translate_async_destroy(async);
  vw_translate_set_test_http_hook(NULL, NULL);
}

static void test_invalidation_clears_active_budget(void) {
  vw_translate_set_test_http_hook(async_http_hook, NULL);
  vw_translate_async_t* async = vw_translate_async_create();
  assert(async != NULL);

  atomic_store(&g_first_request_blocked, true);
  atomic_store(&g_first_request_entered, false);
  atomic_store(&g_first_request_released, false);
  atomic_store(&g_barrier_target, 0U);
  g_hook_delay_ms = 0;

  char text1[] = "Old epoch phrase 1";
  vw_caption_segment_t seg1 = make_segment(1, text1);
  assert(vw_translate_async_submit(async, &seg1, "en", "ro"));
  for (int retry = 0; retry < 100 && !atomic_load(&g_first_request_entered); retry++) sleep_ms(5);
  assert(atomic_load(&g_first_request_entered));

  // Invalidate while old job is blocked in-flight.
  vw_translate_async_invalidate(async);

  // Submit new job under new epoch: it must get fresh active budget and run on another available worker.
  char text2[] = "New epoch phrase 2";
  vw_caption_segment_t seg2 = make_segment(2, text2);
  assert(vw_translate_async_submit(async, &seg2, "en", "ro"));

  vw_translate_async_result_t result;
  bool got_new = false;
  for (int retry = 0; retry < 200; retry++) {
    if (vw_translate_async_try_pop(async, &result)) {
      assert(result.segment.segment_id == 2);
      assert(result.attempted);
      got_new = true;
      break;
    }
    sleep_ms(5);
  }
  assert(got_new);

  atomic_store(&g_first_request_released, true);
  vw_translate_async_destroy(async);
  vw_translate_set_test_http_hook(NULL, NULL);
}

int main(void) {
  vw_translate_set_test_http_hook(async_http_hook, NULL);
  vw_translate_async_t* async = vw_translate_async_create();
  assert(async != NULL);

  atomic_store(&g_barrier_target, 0U);
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

  test_saturation_fifo_ordering();
  test_active_budget_runs_requests_concurrently();
  test_hard_cap_inflight_fifo_ordering();
  test_invalidation_clears_active_budget();

  printf("test_translate_async PASSED.\n");
  return 0;
}
