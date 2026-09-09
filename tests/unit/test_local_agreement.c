#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vw_local_agreement.h"
#include "vw_test.h"

static vw_local_agreement_word_t token(const char* text, int64_t start, int64_t end) {
  vw_local_agreement_word_t out;
  memset(&out, 0, sizeof(out));
  snprintf(out.text_utf8, sizeof(out.text_utf8), "%s", text);
  out.start_pts_us = start;
  out.end_pts_us = end;
  return out;
}

static vw_local_agreement_word_t segment_token(const char* text, int64_t start, int64_t end, uint32_t segment_index) {
  vw_local_agreement_word_t out = token(text, start, end);
  out.segment_index = segment_index;
  return out;
}

static void test_first_pass_hidden_then_common_prefix_commits(void) {
  vw_local_agreement_t state;
  vw_local_agreement_init(&state);
  vw_local_agreement_word_t out[VW_LOCAL_AGREEMENT_MAX_WORDS];
  vw_local_agreement_word_t first[] = {segment_token("good", 0, 300000, 0),
                                       segment_token(" morning", 300000, 700000, 0),
                                       segment_token(" everyone", 700000, 1100000, 1),
                                       segment_token(" today", 1100000, 1400000, 1)};
  EXPECT(vw_local_agreement_update(&state, first, 4, out, VW_LOCAL_AGREEMENT_MAX_WORDS) == 0);

  vw_local_agreement_t base = state;
  vw_local_agreement_word_t second[] = {segment_token("good", 0, 300000, 0),
                                        segment_token(" morning", 300000, 720000, 0),
                                        segment_token(" everyone", 720000, 1200000, 1),
                                        segment_token(" tomorrow", 1200000, 1500000, 1)};

  vw_local_agreement_t full = base;
  size_t committed = vw_local_agreement_update(&full, second, 4, out, VW_LOCAL_AGREEMENT_MAX_WORDS);
  EXPECT(committed == 3);
  EXPECT(out[2].segment_index == 1);

  vw_local_agreement_t partial = base;
  committed = vw_local_agreement_update(&partial, second, 4, out, 2);
  EXPECT(committed == 2);
  EXPECT(strcmp(out[0].text_utf8, "good") == 0);
  EXPECT(strcmp(out[1].text_utf8, " morning") == 0);
  EXPECT(out[0].segment_index == 0);
  EXPECT(out[1].segment_index == 0);
  EXPECT(partial.last_committed_end_us == 720000);
  EXPECT(partial.previous_count == 2);
  EXPECT(strcmp(partial.previous[0].text_utf8, " everyone") == 0);
  EXPECT(strcmp(partial.previous[1].text_utf8, " tomorrow") == 0);

  // A later inference may confirm the remaining stable segment, but the unstable suffix must not self-confirm.
  vw_local_agreement_word_t third[] = {segment_token("good", 0, 300000, 0),
                                       segment_token(" morning", 300000, 720000, 0),
                                       segment_token(" everyone", 730000, 1210000, 1),
                                       segment_token(" later", 1210000, 1510000, 1)};
  committed = vw_local_agreement_update(&partial, third, 4, out, VW_LOCAL_AGREEMENT_MAX_WORDS);
  EXPECT(committed == 1);
  EXPECT(strcmp(out[0].text_utf8, " everyone") == 0);
  EXPECT(out[0].segment_index == 1);
  EXPECT(partial.last_committed_end_us == 1210000);
}

static void test_divergence_replaces_unconfirmed_tail(void) {
  vw_local_agreement_t state;
  vw_local_agreement_init(&state);
  vw_local_agreement_word_t out[8];
  vw_local_agreement_word_t first[] = {token("alpha", 0, 300000), token(" beta", 300000, 600000)};
  vw_local_agreement_word_t second[] = {token("gamma", 0, 300000), token(" delta", 300000, 600000)};
  vw_local_agreement_word_t third[] = {token("gamma", 0, 310000), token(" delta", 310000, 620000)};
  EXPECT(vw_local_agreement_update(&state, first, 2, out, 8) == 0);
  EXPECT(vw_local_agreement_update(&state, second, 2, out, 8) == 0);
  EXPECT(vw_local_agreement_update(&state, third, 2, out, 8) == 2);
  EXPECT(strcmp(out[0].text_utf8, "gamma") == 0);
  EXPECT(strcmp(out[1].text_utf8, " delta") == 0);
}

static void test_committed_overlap_is_not_reemitted(void) {
  vw_local_agreement_t state;
  vw_local_agreement_init(&state);
  vw_local_agreement_word_t out[8];
  vw_local_agreement_word_t first[] = {token("one", 0, 200000), token(" two", 200000, 400000),
                                       token(" three", 400000, 600000)};
  vw_local_agreement_word_t second[] = {token("one", 0, 210000), token(" two", 210000, 420000),
                                        token(" four", 420000, 650000)};
  EXPECT(vw_local_agreement_update(&state, first, 3, out, 8) == 0);
  EXPECT(vw_local_agreement_update(&state, second, 3, out, 8) == 2);

  vw_local_agreement_word_t third[] = {token("one", 10000, 215000), token(" two", 215000, 425000),
                                       token(" four", 425000, 655000), token(" five", 655000, 900000)};
  EXPECT(vw_local_agreement_update(&state, third, 4, out, 8) == 1);
  EXPECT(strcmp(out[0].text_utf8, " four") == 0);
}

static void test_adjacent_repeated_token_is_preserved(void) {
  vw_local_agreement_t state;
  vw_local_agreement_init(&state);
  vw_local_agreement_word_t out[4];

  vw_local_agreement_word_t first[] = {token("no", 0, 200000)};
  vw_local_agreement_word_t second[] = {token("no", 10000, 210000)};
  EXPECT(vw_local_agreement_update(&state, first, 1, out, 4) == 0);
  EXPECT(vw_local_agreement_update(&state, second, 1, out, 4) == 1);
  EXPECT(strcmp(out[0].text_utf8, "no") == 0);

  vw_local_agreement_word_t repeated_first[] = {token(" no", 240000, 440000)};
  vw_local_agreement_word_t repeated_second[] = {token(" no", 250000, 450000)};
  EXPECT(vw_local_agreement_update(&state, repeated_first, 1, out, 4) == 0);
  EXPECT(vw_local_agreement_update(&state, repeated_second, 1, out, 4) == 1);
  EXPECT(strcmp(out[0].text_utf8, " no") == 0);
}

static void test_reset_prevents_cross_epoch_confirmation(void) {
  vw_local_agreement_t state;
  vw_local_agreement_init(&state);
  vw_local_agreement_word_t out[4];
  vw_local_agreement_word_t hypothesis[] = {token("restart", 1000000, 1300000)};
  EXPECT(vw_local_agreement_update(&state, hypothesis, 1, out, 4) == 0);
  vw_local_agreement_reset(&state);
  EXPECT(vw_local_agreement_update(&state, hypothesis, 1, out, 4) == 0);
  EXPECT(state.has_committed == 0);
}

static void test_empty_pass_breaks_consecutive_agreement(void) {
  vw_local_agreement_t state;
  vw_local_agreement_init(&state);
  vw_local_agreement_word_t out[4];
  vw_local_agreement_word_t hypothesis[] = {token("repeat", 1000000, 1300000)};
  EXPECT(vw_local_agreement_update(&state, hypothesis, 1, out, 4) == 0);
  EXPECT(vw_local_agreement_update(&state, NULL, 0, NULL, 0) == 0);
  EXPECT(state.previous_count == 0);
  EXPECT(vw_local_agreement_update(&state, hypothesis, 1, out, 4) == 0);
  EXPECT(vw_local_agreement_update(&state, hypothesis, 1, out, 4) == 1);
}

static void test_format_commit_preserves_raw_token_pieces(void) {
  vw_local_agreement_word_t english[] = {token("hello", 100, 200), token(" world", 200, 300)};
  char text[32];
  int64_t start = 0;
  int64_t end = 0;
  EXPECT(vw_local_agreement_format_commit(english, 2, text, sizeof(text), &start, &end));
  EXPECT(strcmp(text, "hello world") == 0);
  EXPECT(start == 100);
  EXPECT(end == 300);

  vw_local_agreement_word_t cjk[] = {token("你", 400, 500), token("好", 500, 600)};
  EXPECT(vw_local_agreement_format_commit(cjk, 2, text, sizeof(text), &start, &end));
  EXPECT(strcmp(text, "你好") == 0);
  EXPECT(start == 400);
  EXPECT(end == 600);

  char tiny[5];
  EXPECT(!vw_local_agreement_format_commit(english, 2, tiny, sizeof(tiny), &start, &end));
}

int main(void) {
  test_first_pass_hidden_then_common_prefix_commits();
  test_divergence_replaces_unconfirmed_tail();
  test_committed_overlap_is_not_reemitted();
  test_adjacent_repeated_token_is_preserved();
  test_reset_prevents_cross_epoch_confirmation();
  test_empty_pass_breaks_consecutive_agreement();
  test_format_commit_preserves_raw_token_pieces();
  printf("test_local_agreement PASSED\n");
  return 0;
}
