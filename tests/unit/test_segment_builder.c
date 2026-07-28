// Copyright 2026 VLC-Whisper Contributors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vw_segment_builder.h"

static void test_create_and_free(void) {
  vw_segment_builder_t *builder = vw_segment_builder_create();
  assert(builder != NULL);
  assert(builder->next_segment_id == 1);
  assert(builder->count == 0);
  assert(builder->head == 0);
  vw_segment_builder_free(builder);
}

static void test_invalid_hypothesis_rejection(void) {
  vw_segment_builder_t *builder = vw_segment_builder_create();
  assert(builder != NULL);

  // NULL builder or NULL text
  assert(!vw_segment_builder_push_hypothesis(NULL, "Hello", 0, 1000000));
  assert(!vw_segment_builder_push_hypothesis(builder, NULL, 0, 1000000));

  // Empty text
  assert(!vw_segment_builder_push_hypothesis(builder, "", 0, 1000000));

  // Invalid timestamps (negative start, or end <= start)
  assert(!vw_segment_builder_push_hypothesis(builder, "Valid text", -100, 1000000));
  assert(!vw_segment_builder_push_hypothesis(builder, "Valid text", 2000000, 1000000));
  assert(!vw_segment_builder_push_hypothesis(builder, "Valid text", 1000000, 1000000));

  // Oversized text (>1024 bytes)
  char huge_text[1100];
  memset(huge_text, 'a', 1099);
  huge_text[1099] = '\0';
  assert(!vw_segment_builder_push_hypothesis(builder, huge_text, 0, 1000000));

  vw_segment_builder_free(builder);
}

static void test_push_and_deduplication(void) {
  vw_segment_builder_t *builder = vw_segment_builder_create();
  assert(builder != NULL);

  // Push first valid segment
  assert(vw_segment_builder_push_hypothesis(builder, "The stale smell of old beer", 0, 2500000));
  assert(builder->count == 1);
  assert(builder->segment_queue[0].segment_id == 1);
  assert(strcmp(builder->segment_queue[0].text_utf8, "The stale smell of old beer") == 0);

  // Push exact duplicate text -> should be rejected by deduplication
  assert(!vw_segment_builder_push_hypothesis(builder, "The stale smell of old beer", 2500000, 5000000));
  assert(builder->count == 1);

  // Push overlapping text ("old beer lingers") -> overlap "old beer" trimmed -> "lingers" pushed
  assert(vw_segment_builder_push_hypothesis(builder, "old beer lingers", 2500000, 5000000));
  assert(builder->count == 2);
  assert(strcmp(builder->segment_queue[1].text_utf8, "lingers") == 0);

  vw_segment_builder_free(builder);
}

static void test_circular_buffer_wrap(void) {
  vw_segment_builder_t *builder = vw_segment_builder_create();
  assert(builder != NULL);

  // Push 25 distinct segments to force ring buffer wrap (capacity 20)
  for (int i = 0; i < 25; ++i) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Segment number %d", i + 1);
    int64_t start = (int64_t)i * 2000000;
    int64_t end = start + 1800000;
    assert(vw_segment_builder_push_hypothesis(builder, buf, start, end));
    (void)end;
  }

  assert(builder->count == VW_SEGMENT_BUILDER_MAX_BUFSZ);
  assert(builder->head == 5);  // 25 % 20 = 5
  assert(builder->next_segment_id == 26);

  vw_segment_builder_free(builder);
}

int main(void) {
  test_create_and_free();
  test_invalid_hypothesis_rejection();
  test_push_and_deduplication();
  test_circular_buffer_wrap();

  printf("test_segment_builder PASSED (all unit assertions verified)\n");
  return 0;
}
