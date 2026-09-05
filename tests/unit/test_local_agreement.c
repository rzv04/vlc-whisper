#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vw_local_agreement.h"
#include "vw_test.h"

static vw_local_agreement_word_t word(const char* text, int64_t start, int64_t end) {
  vw_local_agreement_word_t out;
  memset(&out, 0, sizeof(out));
  snprintf(out.text_utf8, sizeof(out.text_utf8), "%s", text);
  out.start_pts_us = start;
  out.end_pts_us = end;
  return out;
}

static void test_first_pass_hidden_then_common_prefix_commits(void) {
  vw_local_agreement_t state;
  vw_local_agreement_init(&state);
  vw_local_agreement_word_t out[VW_LOCAL_AGREEMENT_MAX_WORDS];
  vw_local_agreement_word_t first[] = {word("good", 0, 300000), word("morning", 300000, 700000),
                                       word("everyone", 700000, 1100000)};
  EXPECT(vw_local_agreement_update(&state, first, 3, out, VW_LOCAL_AGREEMENT_MAX_WORDS) == 0);

  vw_local_agreement_word_t second[] = {word("good", 0, 300000), word("morning", 300000, 720000),
                                        word("everybody", 720000, 1200000)};
  size_t committed = vw_local_agreement_update(&state, second, 3, out, VW_LOCAL_AGREEMENT_MAX_WORDS);
  EXPECT(committed == 2);
  EXPECT(strcmp(out[0].text_utf8, "good") == 0);
  EXPECT(strcmp(out[1].text_utf8, "morning") == 0);
  EXPECT(state.last_committed_end_us == 720000);
}

static void test_divergence_replaces_unconfirmed_tail(void) {
  vw_local_agreement_t state;
  vw_local_agreement_init(&state);
  vw_local_agreement_word_t out[8];
  vw_local_agreement_word_t first[] = {word("alpha", 0, 300000), word("beta", 300000, 600000)};
  vw_local_agreement_word_t second[] = {word("gamma", 0, 300000), word("delta", 300000, 600000)};
  vw_local_agreement_word_t third[] = {word("gamma", 0, 310000), word("delta", 310000, 620000)};
  EXPECT(vw_local_agreement_update(&state, first, 2, out, 8) == 0);
  EXPECT(vw_local_agreement_update(&state, second, 2, out, 8) == 0);
  EXPECT(vw_local_agreement_update(&state, third, 2, out, 8) == 2);
  EXPECT(strcmp(out[0].text_utf8, "gamma") == 0);
  EXPECT(strcmp(out[1].text_utf8, "delta") == 0);
}

static void test_committed_overlap_is_not_reemitted(void) {
  vw_local_agreement_t state;
  vw_local_agreement_init(&state);
  vw_local_agreement_word_t out[8];
  vw_local_agreement_word_t first[] = {word("one", 0, 200000), word("two", 200000, 400000),
                                       word("three", 400000, 600000)};
  vw_local_agreement_word_t second[] = {word("one", 0, 210000), word("two", 210000, 420000),
                                        word("four", 420000, 650000)};
  EXPECT(vw_local_agreement_update(&state, first, 3, out, 8) == 0);
  EXPECT(vw_local_agreement_update(&state, second, 3, out, 8) == 2);

  vw_local_agreement_word_t third[] = {word("one", 200000, 410000), word("two", 410000, 620000),
                                       word("four", 620000, 850000), word("five", 850000, 1100000)};
  EXPECT(vw_local_agreement_update(&state, third, 4, out, 8) == 1);
  EXPECT(strcmp(out[0].text_utf8, "four") == 0);
}

static void test_reset_prevents_cross_epoch_confirmation(void) {
  vw_local_agreement_t state;
  vw_local_agreement_init(&state);
  vw_local_agreement_word_t out[4];
  vw_local_agreement_word_t hypothesis[] = {word("restart", 1000000, 1300000)};
  EXPECT(vw_local_agreement_update(&state, hypothesis, 1, out, 4) == 0);
  vw_local_agreement_reset(&state);
  EXPECT(vw_local_agreement_update(&state, hypothesis, 1, out, 4) == 0);
  EXPECT(state.has_committed == 0);
}

static void test_format_commit_is_bounded(void) {
  vw_local_agreement_word_t words[] = {word("hello", 100, 200), word("world", 200, 300)};
  char text[32];
  int64_t start = 0;
  int64_t end = 0;
  EXPECT(vw_local_agreement_format_commit(words, 2, text, sizeof(text), &start, &end));
  EXPECT(strcmp(text, "hello world") == 0);
  EXPECT(start == 100);
  EXPECT(end == 300);
  char tiny[5];
  EXPECT(!vw_local_agreement_format_commit(words, 2, tiny, sizeof(tiny), &start, &end));
}

int main(void) {
  test_first_pass_hidden_then_common_prefix_commits();
  test_divergence_replaces_unconfirmed_tail();
  test_committed_overlap_is_not_reemitted();
  test_reset_prevents_cross_epoch_confirmation();
  test_format_commit_is_bounded();
  printf("test_local_agreement PASSED\n");
  return 0;
}
