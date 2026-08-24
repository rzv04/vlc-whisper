// Copyright 2026 VLC-Whisper Contributors. All rights reserved.
// Use of this source code is governed by the MIT License that can be found in the LICENSE file.

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

  // Non-speech descriptor tags and isolated punctuation
  assert(!vw_segment_builder_push_hypothesis(builder, "[Music]", 0, 1000000));
  assert(!vw_segment_builder_push_hypothesis(builder, "(applause)", 0, 1000000));
  assert(!vw_segment_builder_push_hypothesis(builder, "♪", 0, 1000000));
  assert(!vw_segment_builder_push_hypothesis(builder, "...", 0, 1000000));
  assert(!vw_segment_builder_push_hypothesis(builder, "---", 0, 1000000));

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
  assert(builder->segment_queue[0].start_pts_us == 0);
  assert(builder->segment_queue[0].end_pts_us == 2500000);

  // Push exact duplicate text with matching timestamps -> rejected by deduplication
  assert(!vw_segment_builder_push_hypothesis(builder, "The stale smell of old beer", 50000, 2500000));
  assert(builder->count == 1);

  // Push next discrete phrase ("Old beer lingers") -> committed in full with authentic Whisper timestamps
  assert(vw_segment_builder_push_hypothesis(builder, "Old beer lingers", 2500000, 5000000));
  assert(builder->count == 2);
  assert(strcmp(builder->segment_queue[1].text_utf8, "Old beer lingers") == 0);
  assert(builder->segment_queue[1].start_pts_us == 2500000);
  assert(builder->segment_queue[1].end_pts_us == 5000000);

  vw_segment_builder_free(builder);
}

static void vw_test_queue_grows_past_capacity(void) {
  vw_segment_builder_t *builder = vw_segment_builder_create();
  assert(builder != NULL);

  // Push 40 distinct segments to force dynamic queue growth past initial capacity (32)
  for (int i = 0; i < 40; ++i) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Segment number %d", i + 1);
    int64_t start = (int64_t)i * 2000000;
    int64_t end = start + 1800000;
    assert(vw_segment_builder_push_hypothesis(builder, buf, start, end));
    (void)end;
  }

  assert(builder->count == 40);
  assert(builder->capacity >= 40);
  assert(builder->next_segment_id == 41);

  // Pop all 40 and verify all were preserved in FIFO order without any dropped segments
  for (int i = 0; i < 40; ++i) {
    vw_caption_segment_t out;
    char expected[64];
    snprintf(expected, sizeof(expected), "Segment number %d", i + 1);
    assert(vw_segment_builder_pop(builder, &out));
    assert(strcmp(out.text_utf8, expected) == 0);
    assert(out.start_pts_us == (int64_t)i * 2000000);
    free(out.text_utf8);
  }
  assert(builder->count == 0);

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

static void vw_test_history_commit_after_push(void) {
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

static void vw_test_discrete_phrase_authentic_timing(void) {
  vw_segment_builder_t *builder = vw_segment_builder_create();
  assert(builder != NULL);

  // Push discrete sentence: "Where are you from, Victoria?" from 0.5s to 2.8s
  assert(vw_segment_builder_push_hypothesis(builder, "Where are you from, Victoria?", 500000LL, 2800000LL));

  vw_caption_segment_t out;
  assert(vw_segment_builder_pop(builder, &out));
  assert(strcmp(out.text_utf8, "Where are you from, Victoria?") == 0);
  assert(out.start_pts_us == 500000LL);
  assert(out.end_pts_us == 2800000LL);
  free(out.text_utf8);

  // In next window hop: exact same phrase is re-transcribed with slight acoustic jitter (30ms)
  // It must be cleanly dropped as duplicate without modifying timestamps or text
  assert(!vw_segment_builder_push_hypothesis(builder, "Where are you from, Victoria?", 530000LL, 2810000LL));

  // Next discrete sentence: "I'm from Germany," from 3.4s to 5.2s
  // Preserves its authentic acoustic timestamps and full text without word amputation
  assert(vw_segment_builder_push_hypothesis(builder, "I'm from Germany,", 3400000LL, 5200000LL));

  assert(vw_segment_builder_pop(builder, &out));
  assert(strcmp(out.text_utf8, "I'm from Germany,") == 0);
  assert(out.start_pts_us == 3400000LL);
  assert(out.end_pts_us == 5200000LL);
  free(out.text_utf8);

  // If a later overlapping window expands a committed phrase beyond the coverage frontier
  // (e.g. "I'm from Germany, indeed" extends to 5.8s), the tail-prefix trim emits only the NEW
  // remainder ("indeed") starting at the frontier — no repeated committed words, no lost text.
  assert(vw_segment_builder_push_hypothesis(builder, "I'm from Germany, indeed", 3420000LL, 5800000LL));
  assert(vw_segment_builder_pop(builder, &out));
  assert(strcmp(out.text_utf8, "indeed") == 0);
  assert(out.start_pts_us == 5200000LL);  // clamped to the coverage frontier
  assert(out.end_pts_us == 5800000LL);
  free(out.text_utf8);

  vw_segment_builder_free(builder);
}

// Final-subtitles policy (ADR-017): an expanded re-recognition of a committed phrase is DROPPED
// wholesale — emitted subtitles are immutable and never revised or extended.
static void vw_test_expansion_dropped_final_subtitles(void) {
  vw_segment_builder_t *builder = vw_segment_builder_create();
  assert(builder != NULL);
  assert(vw_segment_builder_push_hypothesis(builder, "jumps", 10000000LL, 20000000LL));
  vw_caption_segment_t out;
  assert(vw_segment_builder_pop(builder, &out));
  assert(strcmp(out.text_utf8, "jumps") == 0);
  free(out.text_utf8);
  // Expanded re-recognition ("jumps quickly") is dropped: no suffix cue, no revision.
  assert(!vw_segment_builder_push_hypothesis(builder, "jumps quickly", 10000000LL, 30000000LL));
  assert(builder->count == 0);
  vw_segment_builder_free(builder);
}

static void vw_test_last_queued_expansion_dropped(void) {
  vw_segment_builder_t *builder = vw_segment_builder_create();
  assert(builder != NULL);
  assert(vw_segment_builder_push_hypothesis(builder, "jumps", 10000000LL, 20000000LL));
  // Expansion of the still-pending cue is also dropped (no in-place revision).
  assert(!vw_segment_builder_push_hypothesis(builder, "jumps quickly", 10000000LL, 30000000LL));
  assert(builder->count == 1);
  vw_caption_segment_t out;
  assert(vw_segment_builder_pop(builder, &out));
  assert(strcmp(out.text_utf8, "jumps") == 0);
  assert(out.end_pts_us == 20000000LL);
  free(out.text_utf8);
  vw_segment_builder_free(builder);
}

static void vw_test_mid_containment_dropped(void) {
  vw_segment_builder_t *builder = vw_segment_builder_create();
  assert(builder != NULL);
  assert(vw_segment_builder_push_hypothesis(builder, "a b c", 10000000LL, 30000000LL));
  vw_caption_segment_t out;
  assert(vw_segment_builder_pop(builder, &out));
  free(out.text_utf8);
  // Candidate contains a committed phrase mid-way -> dropped (no repetition).
  assert(!vw_segment_builder_push_hypothesis(builder, "x a b c y", 10000000LL, 40000000LL));
  assert(builder->count == 0);
  vw_segment_builder_free(builder);
}

static void vw_test_superstring_dropped(void) {
  vw_segment_builder_t *builder = vw_segment_builder_create();
  assert(builder != NULL);
  assert(vw_segment_builder_push_hypothesis(builder, "hello world", 10000000LL, 20000000LL));
  vw_caption_segment_t out;
  assert(vw_segment_builder_pop(builder, &out));
  assert(strcmp(out.text_utf8, "hello world") == 0);
  free(out.text_utf8);
  // Whole-phrase superstring extending past the frontier (2+ word cue): the new word is RECOVERED
  // via the tail-prefix trim instead of being lost — emitted at the covered frontier.
  assert(vw_segment_builder_push_hypothesis(builder, "hello world again", 10000000LL, 30000000LL));
  assert(vw_segment_builder_pop(builder, &out));
  assert(strcmp(out.text_utf8, "again") == 0);
  assert(out.start_pts_us == 20000000LL);
  assert(out.end_pts_us == 30000000LL);
  free(out.text_utf8);
  vw_segment_builder_free(builder);
}

static void vw_test_short_prefix_expansion_dropped(void) {
  vw_segment_builder_t *builder = vw_segment_builder_create();
  assert(builder != NULL);
  assert(vw_segment_builder_push_hypothesis(builder, "jumps", 10000000LL, 20000000LL));
  vw_caption_segment_t out;
  assert(vw_segment_builder_pop(builder, &out));
  free(out.text_utf8);
  // Short-prefix expansion is dropped too: no repetition, no suffix, no revision.
  assert(!vw_segment_builder_push_hypothesis(builder, "jumps quickly", 10000000LL, 30000000LL));
  assert(builder->count == 0);
  vw_segment_builder_free(builder);
}

static void vw_test_pending_dedup_time_gated(void) {
  vw_segment_builder_t *builder = vw_segment_builder_create();
  assert(builder != NULL);
  // First pending cue at [10s,14s].
  assert(vw_segment_builder_push_hypothesis(builder, "the cat sat", 10000000LL, 14000000LL));
  // Textually contained BUT time-distinct (non-overlapping) cue in the same window: a repeated
  // phrase later in the window is legitimate and must NOT be dropped by textual dedup.
  assert(vw_segment_builder_push_hypothesis(builder, "the cat", 20000000LL, 24000000LL));
  assert(builder->count == 2);
  vw_caption_segment_t out;
  assert(vw_segment_builder_pop(builder, &out));
  assert(strcmp(out.text_utf8, "the cat sat") == 0);
  free(out.text_utf8);
  assert(vw_segment_builder_pop(builder, &out));
  assert(strcmp(out.text_utf8, "the cat") == 0);
  assert(out.start_pts_us == 20000000LL);
  free(out.text_utf8);
  vw_segment_builder_free(builder);
}

static void vw_test_pending_dedup_overlapping_time_dropped(void) {
  vw_segment_builder_t *builder = vw_segment_builder_create();
  assert(builder != NULL);
  assert(vw_segment_builder_push_hypothesis(builder, "the cat sat", 10000000LL, 14000000LL));
  // Textually contained AND time-overlapping -> re-recognition of the same audio -> dropped.
  assert(!vw_segment_builder_push_hypothesis(builder, "the cat", 10000000LL, 12000000LL));
  assert(builder->count == 1);
  vw_caption_segment_t out;
  assert(vw_segment_builder_pop(builder, &out));
  assert(strcmp(out.text_utf8, "the cat sat") == 0);
  free(out.text_utf8);
  vw_segment_builder_free(builder);
}

static void vw_test_partial_overlap_prefix_trimmed(void) {
  vw_segment_builder_t *builder = vw_segment_builder_create();
  assert(builder != NULL);
  // Long caption A (window N); its tail re-appears as the prefix of continuation B (window N+1).
  assert(vw_segment_builder_push_hypothesis(
      builder, "england so i used to skype very often or go and look films on the internet or listen to music more",
      10000000LL, 14000000LL));
  vw_caption_segment_t out;
  assert(vw_segment_builder_pop(builder, &out));
  free(out.text_utf8);
  // B overlaps A in time and starts with A's tail -> only the NEW remainder is emitted, starting at
  // A's end (each word appears once, no embedded-context duplication).
  assert(vw_segment_builder_push_hypothesis(builder, "or listen to music more because i have more free time now",
                                            13000000LL, 16000000LL));
  assert(vw_segment_builder_pop(builder, &out));
  assert(strcmp(out.text_utf8, "because i have more free time now") == 0);
  assert(out.start_pts_us == 14000000LL);  // A's end
  assert(out.end_pts_us == 16000000LL);
  free(out.text_utf8);
  vw_segment_builder_free(builder);
}

static void vw_test_partial_overlap_whole_tail_dropped(void) {
  vw_segment_builder_t *builder = vw_segment_builder_create();
  assert(builder != NULL);
  assert(vw_segment_builder_push_hypothesis(builder, "go and look films on the internet", 10000000LL, 14000000LL));
  vw_caption_segment_t out;
  assert(vw_segment_builder_pop(builder, &out));
  free(out.text_utf8);
  // The candidate is entirely the cue's tail -> dropped (already shown).
  assert(!vw_segment_builder_push_hypothesis(builder, "on the internet", 13000000LL, 14000000LL));
  assert(builder->count == 0);
  vw_segment_builder_free(builder);
}

static void vw_test_partial_overlap_far_time_not_trimmed(void) {
  vw_segment_builder_t *builder = vw_segment_builder_create();
  assert(builder != NULL);
  assert(vw_segment_builder_push_hypothesis(builder, "the store was closed", 10000000LL, 14000000LL));
  vw_caption_segment_t out;
  assert(vw_segment_builder_pop(builder, &out));
  free(out.text_utf8);
  // Same tail words but far later in time (not adjacent/overlapping): genuinely new speech -> kept whole.
  assert(vw_segment_builder_push_hypothesis(builder, "the store was crowded", 50000000LL, 54000000LL));
  assert(vw_segment_builder_pop(builder, &out));
  assert(strcmp(out.text_utf8, "the store was crowded") == 0);
  assert(out.start_pts_us == 50000000LL);
  free(out.text_utf8);
  vw_segment_builder_free(builder);
}

static void vw_test_prefix_change_retranscription_dropped(void) {
  vw_segment_builder_t *builder = vw_segment_builder_create();
  assert(builder != NULL);
  assert(vw_segment_builder_push_hypothesis(builder, "i'm from peru, i live in the capital", 0, 3000000));
  vw_caption_segment_t out;
  assert(vw_segment_builder_pop(builder, &out));
  free(out.text_utf8);
  // Whisper re-transcribes the same audio with a CHANGED PREFIX (text not contained). The audio is
  // already covered -> dropped by time coverage, regardless of the text difference.
  assert(!vw_segment_builder_push_hypothesis(builder, "i live in the capital", 1500000, 3000000));
  assert(builder->count == 0);
  vw_segment_builder_free(builder);
}

static void vw_test_suffix_swap_retranscription_dropped(void) {
  vw_segment_builder_t *builder = vw_segment_builder_create();
  assert(builder != NULL);
  assert(vw_segment_builder_push_hypothesis(builder, "capital lima is near the coast", 10000000, 14000000));
  vw_caption_segment_t out;
  assert(vw_segment_builder_pop(builder, &out));
  free(out.text_utf8);
  // Re-transcription with a swapped suffix ending within covered audio -> dropped.
  assert(!vw_segment_builder_push_hypothesis(builder, "i'm from peru capital lima", 11000000, 13000000));
  assert(builder->count == 0);
  vw_segment_builder_free(builder);
}

static void vw_test_boundary_extension_clamped_no_overwrite(void) {
  vw_segment_builder_t *builder = vw_segment_builder_create();
  assert(builder != NULL);
  assert(vw_segment_builder_push_hypothesis(builder, "long caption phrase", 10000000, 14000000));
  vw_caption_segment_t out;
  assert(vw_segment_builder_pop(builder, &out));
  free(out.text_utf8);
  // Boundary-spanning continuation: starts before the covered end (would overwrite the showing cue
  // mid-display) but extends past it. Emit only the new remainder, starting at the covered end.
  assert(vw_segment_builder_push_hypothesis(builder, "long caption phrase continues", 13000000, 16000000));
  assert(vw_segment_builder_pop(builder, &out));
  assert(strcmp(out.text_utf8, "continues") == 0);
  assert(out.start_pts_us == 14000000LL);  // clamped to the coverage frontier (previous cue end)
  assert(out.end_pts_us == 16000000LL);
  free(out.text_utf8);
  vw_segment_builder_free(builder);
}

static void vw_test_new_audio_after_coverage_emitted(void) {
  vw_segment_builder_t *builder = vw_segment_builder_create();
  assert(builder != NULL);
  assert(vw_segment_builder_push_hypothesis(builder, "first utterance", 10000000, 14000000));
  vw_caption_segment_t out;
  assert(vw_segment_builder_pop(builder, &out));
  free(out.text_utf8);
  // Genuinely new audio well past the covered range -> emitted whole (no trim, no drop).
  assert(vw_segment_builder_push_hypothesis(builder, "second utterance after silence", 20000000, 24000000));
  assert(vw_segment_builder_pop(builder, &out));
  assert(strcmp(out.text_utf8, "second utterance after silence") == 0);
  assert(out.start_pts_us == 20000000LL);
  free(out.text_utf8);
  vw_segment_builder_free(builder);
}

static void vw_test_edge_jittered_fragment_past_cue_end_dropped(void) {
  vw_segment_builder_t *builder = vw_segment_builder_create();
  assert(builder != NULL);
  assert(vw_segment_builder_push_hypothesis(builder, "the cat sat on the mat", 10000000, 20000000));
  vw_caption_segment_t out;
  assert(vw_segment_builder_pop(builder, &out));
  free(out.text_utf8);
  // Fragment whose time jitters PAST the cue's end and the covered frontier: all words are still
  // covered -> dropped (no new text to emit).
  assert(!vw_segment_builder_push_hypothesis(builder, "cat sat on the", 12000000, 21000000));
  assert(builder->count == 0);
  vw_segment_builder_free(builder);
}

static void vw_test_edge_hallucinated_retranscription_dropped(void) {
  vw_segment_builder_t *builder = vw_segment_builder_create();
  assert(builder != NULL);
  assert(vw_segment_builder_push_hypothesis(builder, "the cat sat on the mat", 10000000, 20000000));
  vw_caption_segment_t out;
  assert(vw_segment_builder_pop(builder, &out));
  free(out.text_utf8);
  // Same audio re-transcribed with COMPLETELY different text within the covered range: the audio is
  // authoritative -> dropped (text-independent coverage dedup).
  assert(!vw_segment_builder_push_hypothesis(builder, "completely different words", 11000000, 19000000));
  assert(builder->count == 0);
  vw_segment_builder_free(builder);
}

static void vw_test_edge_tolerance_boundary(void) {
  vw_segment_builder_t *builder = vw_segment_builder_create();
  assert(builder != NULL);
  assert(vw_segment_builder_push_hypothesis(builder, "base phrase", 10000000, 20000000));
  vw_caption_segment_t out;
  assert(vw_segment_builder_pop(builder, &out));
  free(out.text_utf8);
  // Exactly at covered + VW_DEDUP_TIME_TOLERANCE_US (500ms) -> dropped (<=).
  assert(!vw_segment_builder_push_hypothesis(builder, "late retranscription", 11000000, 20500000));
  assert(builder->count == 0);
  // One microsecond past the tolerance -> emitted (new audio), start clamped to the frontier.
  assert(vw_segment_builder_push_hypothesis(builder, "past tolerance phrase", 11000000, 20500001));
  assert(vw_segment_builder_pop(builder, &out));
  assert(out.start_pts_us == 20000000LL);  // clamped, never overwrites the showing cue
  assert(out.end_pts_us == 20500001LL);
  free(out.text_utf8);
  vw_segment_builder_free(builder);
}

static void vw_test_edge_same_start_time_extension_trimmed(void) {
  vw_segment_builder_t *builder = vw_segment_builder_create();
  assert(builder != NULL);
  assert(vw_segment_builder_push_hypothesis(builder, "first phrase", 10000000, 13000000));
  vw_caption_segment_t out;
  assert(vw_segment_builder_pop(builder, &out));
  free(out.text_utf8);
  // Whisper sometimes stamps consecutive segments at the same start; the extension of a 2+ word
  // cue is recovered via the tail-prefix trim (prefix removed, remainder emitted at the frontier).
  assert(vw_segment_builder_push_hypothesis(builder, "first phrase continued", 10000000, 14000000));
  assert(vw_segment_builder_pop(builder, &out));
  assert(strcmp(out.text_utf8, "continued") == 0);
  assert(out.start_pts_us == 13000000LL);
  assert(out.end_pts_us == 14000000LL);
  free(out.text_utf8);
  vw_segment_builder_free(builder);
}

static void vw_test_edge_backward_time_candidate_dropped(void) {
  vw_segment_builder_t *builder = vw_segment_builder_create();
  assert(builder != NULL);
  assert(vw_segment_builder_push_hypothesis(builder, "forward phrase", 10000000, 14000000));
  vw_caption_segment_t out;
  assert(vw_segment_builder_pop(builder, &out));
  free(out.text_utf8);
  // A candidate stamped earlier than the frontier but ending inside covered audio -> dropped.
  assert(!vw_segment_builder_push_hypothesis(builder, "backward stamp", 5000000, 9000000));
  assert(builder->count == 0);
  vw_segment_builder_free(builder);
}

static void vw_test_edge_one_word_expansion_dropped_adr018(void) {
  vw_segment_builder_t *builder = vw_segment_builder_create();
  assert(builder != NULL);
  assert(vw_segment_builder_push_hypothesis(builder, "jumps", 10000000, 20000000));
  vw_caption_segment_t out;
  assert(vw_segment_builder_pop(builder, &out));
  free(out.text_utf8);
  // One-word cue cannot be safely trimmed (>=2 word rule) -> expansion dropped (ADR-018 preserved).
  assert(!vw_segment_builder_push_hypothesis(builder, "jumps quickly", 10000000, 30000000));
  assert(builder->count == 0);
  vw_segment_builder_free(builder);
}

static void vw_test_edge_two_word_expansion_recovered(void) {
  vw_segment_builder_t *builder = vw_segment_builder_create();
  assert(builder != NULL);
  assert(vw_segment_builder_push_hypothesis(builder, "the cat", 10000000, 20000000));
  vw_caption_segment_t out;
  assert(vw_segment_builder_pop(builder, &out));
  free(out.text_utf8);
  // Two-word cue prefix-extension: the new word is recovered (not lost) via the tail-prefix trim.
  assert(vw_segment_builder_push_hypothesis(builder, "the cat sat", 10000000, 30000000));
  assert(vw_segment_builder_pop(builder, &out));
  assert(strcmp(out.text_utf8, "sat") == 0);
  assert(out.start_pts_us == 20000000LL);
  assert(out.end_pts_us == 30000000LL);
  free(out.text_utf8);
  vw_segment_builder_free(builder);
}

static void vw_test_edge_history_wrap_retranscription_dropped(void) {
  vw_segment_builder_t *builder = vw_segment_builder_create();
  assert(builder != NULL);
  // Fill and evict the 16-entry history ring with 20 sequential cues.
  for (int i = 0; i < 20; i++) {
    char buf[64];
    snprintf(buf, sizeof(buf), "cue number %d", i + 1);
    int64_t start = (int64_t)i * 2000000;
    assert(vw_segment_builder_push_hypothesis(builder, buf, start, start + 1800000));
    vw_caption_segment_t out;
    assert(vw_segment_builder_pop(builder, &out));
    free(out.text_utf8);
  }
  // Cue #1 has long left the history ring, but the coverage frontier still covers its audio ->
  // re-transcription is dropped (frontier survives history eviction).
  assert(!vw_segment_builder_push_hypothesis(builder, "cue number 1", 0, 1800000));
  assert(builder->count == 0);
  vw_segment_builder_free(builder);
}

static void vw_test_edge_unrelated_boundary_extension_clamped(void) {
  vw_segment_builder_t *builder = vw_segment_builder_create();
  assert(builder != NULL);
  assert(vw_segment_builder_push_hypothesis(builder, "first phrase", 10000000, 13000000));
  vw_caption_segment_t out;
  assert(vw_segment_builder_pop(builder, &out));
  free(out.text_utf8);
  // Boundary-spanning candidate with unrelated text: no trim applies, but the start is clamped to
  // the frontier so it never overwrites the showing cue mid-display.
  assert(vw_segment_builder_push_hypothesis(builder, "unrelated words", 10000000, 14000000));
  assert(vw_segment_builder_pop(builder, &out));
  assert(strcmp(out.text_utf8, "unrelated words") == 0);
  assert(out.start_pts_us == 13000000LL);
  assert(out.end_pts_us == 14000000LL);
  free(out.text_utf8);
  vw_segment_builder_free(builder);
}

static void vw_test_edge_repeat_phrase_distinct_times_kept(void) {
  vw_segment_builder_t *builder = vw_segment_builder_create();
  assert(builder != NULL);
  assert(vw_segment_builder_push_hypothesis(builder, "repeat phrase", 10000000, 12000000));
  vw_caption_segment_t out;
  assert(vw_segment_builder_pop(builder, &out));
  free(out.text_utf8);
  // The same words genuinely spoken again much later (far outside coverage/tolerance) -> kept.
  assert(vw_segment_builder_push_hypothesis(builder, "repeat phrase", 20000000, 22000000));
  assert(vw_segment_builder_pop(builder, &out));
  assert(strcmp(out.text_utf8, "repeat phrase") == 0);
  assert(out.start_pts_us == 20000000LL);
  free(out.text_utf8);
  vw_segment_builder_free(builder);
}

static void vw_test_edge_trailing_distinct_subsegment_kept(void) {
  vw_segment_builder_t *builder = vw_segment_builder_create();
  assert(builder != NULL);
  assert(vw_segment_builder_push_hypothesis(builder, "the cat sat", 10000000, 14000000));
  vw_caption_segment_t out;
  assert(vw_segment_builder_pop(builder, &out));
  free(out.text_utf8);
  // A DISTINCT non-overlapping trailing sub-segment that starts after the frontier but ends within
  // the 500ms tolerance must NOT be coverage-dropped (it is new audio, not a re-transcription).
  assert(vw_segment_builder_push_hypothesis(builder, "on the mat", 14100000, 14400000));
  assert(vw_segment_builder_pop(builder, &out));
  assert(strcmp(out.text_utf8, "on the mat") == 0);
  assert(out.start_pts_us == 14100000LL);
  assert(out.end_pts_us == 14400000LL);
  free(out.text_utf8);
  vw_segment_builder_free(builder);
}

int main(void) {
  vw_test_create_and_free();
  vw_test_invalid_hypothesis_rejection();
  vw_test_queue_grows_past_capacity();
  vw_test_push_and_deduplication();
  vw_test_pop();
  vw_test_multi_phrase_per_window();
  vw_test_history_commit_after_push();
  vw_test_silence_gap_preservation();
  vw_test_clear_resets_history_and_queue();
  vw_test_discrete_phrase_authentic_timing();
  vw_test_expansion_dropped_final_subtitles();
  vw_test_last_queued_expansion_dropped();
  vw_test_pending_dedup_time_gated();
  vw_test_pending_dedup_overlapping_time_dropped();
  vw_test_partial_overlap_prefix_trimmed();
  vw_test_partial_overlap_whole_tail_dropped();
  vw_test_partial_overlap_far_time_not_trimmed();
  vw_test_prefix_change_retranscription_dropped();
  vw_test_suffix_swap_retranscription_dropped();
  vw_test_boundary_extension_clamped_no_overwrite();
  vw_test_new_audio_after_coverage_emitted();
  vw_test_edge_jittered_fragment_past_cue_end_dropped();
  vw_test_edge_hallucinated_retranscription_dropped();
  vw_test_edge_tolerance_boundary();
  vw_test_edge_same_start_time_extension_trimmed();
  vw_test_edge_backward_time_candidate_dropped();
  vw_test_edge_one_word_expansion_dropped_adr018();
  vw_test_edge_two_word_expansion_recovered();
  vw_test_edge_history_wrap_retranscription_dropped();
  vw_test_edge_unrelated_boundary_extension_clamped();
  vw_test_edge_repeat_phrase_distinct_times_kept();
  vw_test_edge_trailing_distinct_subsegment_kept();
  vw_test_mid_containment_dropped();
  vw_test_superstring_dropped();
  vw_test_short_prefix_expansion_dropped();

  printf("test_segment_builder PASSED (all unit assertions verified)\n");
  return 0;
}
