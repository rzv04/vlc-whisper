// Copyright 2026 VLC-Whisper Contributors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vw_segment_builder.h"

static void vw_test_create_and_free(void) {
  vw_segment_builder_t *builder = vw_segment_builder_create();
  assert(builder != NULL);
  assert(builder->next_segment_id == 1);
  assert(builder->count == 0);
  assert(builder->head == 0);
  vw_segment_builder_free(builder);
}

static void vw_test_invalid_hypothesis_rejection(void) {
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

static void vw_test_push_and_deduplication(void) {
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

static void vw_test_circular_buffer_wrap(void) {
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

static void vw_test_pop(void) {
  vw_segment_builder_t *builder = vw_segment_builder_create();
  assert(builder != NULL);

  vw_caption_segment_t out;
  assert(!vw_segment_builder_pop(builder, &out));

  assert(vw_segment_builder_push_hypothesis(builder, "First segment", 0, 2000000));
  assert(vw_segment_builder_push_hypothesis(builder, "Second segment", 2000000, 4000000));

  assert(vw_segment_builder_pop(builder, &out));
  assert(strcmp(out.text_utf8, "First segment") == 0);
  assert(out.start_pts_us == 0);
  assert(out.is_final == true);
  free(out.text_utf8);

  assert(vw_segment_builder_pop(builder, &out));
  assert(strcmp(out.text_utf8, "Second segment") == 0);
  assert(out.start_pts_us == 2000000);
  assert(out.is_final == true);
  free(out.text_utf8);

  assert(!vw_segment_builder_pop(builder, &out));

  vw_segment_builder_free(builder);
}

static void vw_test_multi_phrase_per_window(void) {
  vw_segment_builder_t *builder = vw_segment_builder_create();
  assert(builder != NULL);

  // Push 3 distinct phrases within an 8-second window
  assert(vw_segment_builder_push_hypothesis(builder, "Where are you from, Victoria?", 500000LL, 2800000LL));
  assert(vw_segment_builder_push_hypothesis(builder, "I'm from Germany,", 3400000LL, 5200000LL));
  assert(vw_segment_builder_push_hypothesis(builder, "from the north coast of Germany.", 5400000LL, 7100000LL));

  assert(builder->count == 3);

  vw_caption_segment_t seg1, seg2, seg3;
  assert(vw_segment_builder_pop(builder, &seg1));
  assert(vw_segment_builder_pop(builder, &seg2));
  assert(vw_segment_builder_pop(builder, &seg3));

  assert(strcmp(seg1.text_utf8, "Where are you from, Victoria?") == 0);
  assert(seg1.start_pts_us == 500000LL);
  assert(seg1.end_pts_us == 2800000LL);
  assert(seg1.is_final == true);

  assert(strcmp(seg2.text_utf8, "I'm from Germany,") == 0);
  assert(seg2.start_pts_us == 3400000LL);
  assert(seg2.end_pts_us == 5200000LL);
  assert(seg2.is_final == true);

  assert(strcmp(seg3.text_utf8, "from the north coast of Germany.") == 0);
  assert(seg3.start_pts_us == 5400000LL);
  assert(seg3.end_pts_us == 7100000LL);
  assert(seg3.is_final == true);

  free(seg1.text_utf8);
  free(seg2.text_utf8);
  free(seg3.text_utf8);

  vw_segment_builder_free(builder);
}

static void vw_test_hop_deduplication_with_history_persistence(void) {
  vw_segment_builder_t *builder = vw_segment_builder_create();
  assert(builder != NULL);

  // Window 0 (0-8s): push Phrase A and Phrase B
  assert(vw_segment_builder_push_hypothesis(builder, "Hello world", 500000LL, 2000000LL));
  assert(vw_segment_builder_push_hypothesis(builder, "Welcome to the show", 3400000LL, 5200000LL));

  // Drain output queue completely (simulating worker loop IPC transmission)
  vw_caption_segment_t out;
  while (vw_segment_builder_pop(builder, &out)) {
    free(out.text_utf8);
  }
  assert(builder->count == 0);

  // Window 1 (2-10s): next hop sees Phrase B again (slight timestamp delta 50ms) and new Phrase C
  // Phrase B should be deduplicated against committed history even though output queue is empty!
  assert(!vw_segment_builder_push_hypothesis(builder, "Welcome to the show", 3450000LL, 5200000LL));
  assert(builder->count == 0);

  // Phrase C is unique and should be accepted
  assert(vw_segment_builder_push_hypothesis(builder, "Next topic for today", 6500000LL, 8200000LL));
  assert(builder->count == 1);

  assert(vw_segment_builder_pop(builder, &out));
  assert(strcmp(out.text_utf8, "Next topic for today") == 0);
  assert(out.is_final == true);
  free(out.text_utf8);

  vw_segment_builder_free(builder);
}

static void vw_test_silence_gap_preservation(void) {
  vw_segment_builder_t *builder = vw_segment_builder_create();
  assert(builder != NULL);

  assert(vw_segment_builder_push_hypothesis(builder, "First speaker line.", 500000LL, 2800000LL));
  assert(vw_segment_builder_push_hypothesis(builder, "Second speaker response.", 3400000LL, 7100000LL));

  vw_caption_segment_t seg1, seg2;
  assert(vw_segment_builder_pop(builder, &seg1));
  assert(vw_segment_builder_pop(builder, &seg2));

  // 0.6s silence gap: seg2 starts at 3.4s, seg1 ends at 2.8s
  int64_t silence_gap_us = seg2.start_pts_us - seg1.end_pts_us;
  assert(silence_gap_us == 600000LL);

  free(seg1.text_utf8);
  free(seg2.text_utf8);
  vw_segment_builder_free(builder);
}

static void vw_test_clear_resets_history_and_queue(void) {
  vw_segment_builder_t *builder = vw_segment_builder_create();
  assert(builder != NULL);

  assert(vw_segment_builder_push_hypothesis(builder, "Sample subtitle", 1000000LL, 3000000LL));
  assert(builder->count == 1);
  assert(builder->history_count == 1);

  vw_segment_builder_clear(builder);
  assert(builder->count == 0);
  assert(builder->head == 0);
  assert(builder->history_count == 0);
  assert(builder->history_head == 0);

  // After clear (seek/reset), pushing the same phrase should now succeed because history is wiped
  assert(vw_segment_builder_push_hypothesis(builder, "Sample subtitle", 1000000LL, 3000000LL));
  assert(builder->count == 1);

  vw_caption_segment_t out;
  assert(vw_segment_builder_pop(builder, &out));
  free(out.text_utf8);

  vw_segment_builder_free(builder);
}

static void vw_test_trimmed_caption_timing(void) {
  vw_segment_builder_t *builder = vw_segment_builder_create();
  assert(builder != NULL);

  // Push full sentence: "the quick brown fox" from 0 to 4s
  assert(vw_segment_builder_push_hypothesis(builder, "the quick brown fox", 0LL, 4000000LL));

  vw_caption_segment_t out;
  assert(vw_segment_builder_pop(builder, &out));
  assert(strcmp(out.text_utf8, "the quick brown fox") == 0);
  assert(out.start_pts_us == 0LL);
  free(out.text_utf8);

  // Next window outputs overlapping phrase: "brown fox jumps" from 2s to 6s
  // "brown fox" is trimmed -> "jumps" remains
  // The start timestamp of "jumps" must be shifted forward (>= 4s, the end of prior history)
  assert(vw_segment_builder_push_hypothesis(builder, "brown fox jumps", 2000000LL, 6000000LL));

  assert(vw_segment_builder_pop(builder, &out));
  assert(strcmp(out.text_utf8, "jumps") == 0);
  assert(out.start_pts_us >= 4000000LL);  // Start shifted to or past previous end PTS
  assert(out.end_pts_us == 6000000LL);
  free(out.text_utf8);

  vw_segment_builder_free(builder);
}

int main(void) {
  vw_test_create_and_free();
  vw_test_invalid_hypothesis_rejection();
  vw_test_push_and_deduplication();
  vw_test_circular_buffer_wrap();
  vw_test_pop();
  vw_test_multi_phrase_per_window();
  vw_test_hop_deduplication_with_history_persistence();
  vw_test_silence_gap_preservation();
  vw_test_clear_resets_history_and_queue();
  vw_test_trimmed_caption_timing();

  printf("test_segment_builder PASSED (all unit assertions verified)\n");
  return 0;
}
