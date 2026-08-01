#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "vw_audio_capture.h"
#include "vw_queue.h"

static void test_queue_creation_and_destruction(void) {
  vw_spsc_queue_t* q = vw_spsc_queue_create(0);
  assert(q == NULL);

  q = vw_spsc_queue_create(10);
  assert(q != NULL);
  assert(q->capacity_chunks == 10);
  assert(vw_spsc_queue_get_dropped_microseconds(q) == 0);
  vw_spsc_queue_destroy(q);
}

static void test_queue_push_pop(void) {
  vw_spsc_queue_t* q = vw_spsc_queue_create(5);
  vw_audio_chunk_t chunk = {
      .start_pts_us = 1000, .duration_us = 200, .sample_rate = 16000, .channels = 1, .bytes = 1024};
  memset(chunk.pcm_data, 0xAB, 1024);

  // Push 1
  assert(vw_spsc_queue_push(q, &chunk) == true);

  // Pop 1
  vw_audio_chunk_t out;
  assert(vw_spsc_queue_pop(q, &out) != NULL);
  assert(out.start_pts_us == 1000);
  assert(out.pcm_data[0] == 0xAB);

  // Empty pop
  assert(vw_spsc_queue_pop(q, &out) == NULL);

  vw_spsc_queue_destroy(q);
}

static void test_queue_overflow(void) {
  vw_spsc_queue_t* q = vw_spsc_queue_create(2);
  vw_audio_chunk_t chunk = {.duration_us = 50000};

  // Fill the queue
  assert(vw_spsc_queue_push(q, &chunk) == true);
  assert(vw_spsc_queue_push(q, &chunk) == true);

  // Overflow the queue
  assert(vw_spsc_queue_push(q, &chunk) == false);
  assert(vw_spsc_queue_get_dropped_microseconds(q) == 50000);

  assert(vw_spsc_queue_push(q, &chunk) == false);
  assert(vw_spsc_queue_get_dropped_microseconds(q) == 100000);

  vw_spsc_queue_destroy(q);
}

static void test_queue_wraparound(void) {
  vw_spsc_queue_t* q = vw_spsc_queue_create(3);
  vw_audio_chunk_t chunk_in = {0};
  vw_audio_chunk_t chunk_out = {0};

  for (int i = 0; i < 10; ++i) {
    chunk_in.start_pts_us = i;
    assert(vw_spsc_queue_push(q, &chunk_in) == true);
    assert(vw_spsc_queue_pop(q, &chunk_out) != NULL);
    assert(chunk_out.start_pts_us == i);
  }

  vw_spsc_queue_destroy(q);
}

int main(void) {
  test_queue_creation_and_destruction();
  test_queue_push_pop();
  test_queue_overflow();
  test_queue_wraparound();
  printf("test_queue PASSED\n");
  return 0;
}
