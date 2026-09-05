#include "vw_local_agreement.h"

#include <string.h>

static int vw_local_agreement_word_valid(const vw_local_agreement_word_t* word) {
  return word && word->text_utf8[0] != '\0' && word->start_pts_us >= 0 && word->end_pts_us >= word->start_pts_us;
}

static void vw_local_agreement_copy_words(vw_local_agreement_word_t* dst, const vw_local_agreement_word_t* src,
                                          size_t count) {
  if (count > 0) memcpy(dst, src, count * sizeof(*dst));
}

static int vw_local_agreement_words_equal(const vw_local_agreement_word_t* a, const vw_local_agreement_word_t* b) {
  return a && b && strcmp(a->text_utf8, b->text_utf8) == 0;
}

static size_t vw_local_agreement_strip_committed_overlap(const vw_local_agreement_t* state,
                                                         const vw_local_agreement_word_t* words, size_t count) {
  if (!state || !words || count == 0 || !state->has_committed || state->committed_tail_count == 0) return 0;
  int64_t distance = words[0].start_pts_us - state->last_committed_end_us;
  if (distance < 0) distance = -distance;
  if (distance > VW_LOCAL_AGREEMENT_OVERLAP_US) return 0;

  size_t limit = count;
  if (limit > state->committed_tail_count) limit = state->committed_tail_count;
  if (limit > VW_LOCAL_AGREEMENT_COMMITTED_TAIL_WORDS) limit = VW_LOCAL_AGREEMENT_COMMITTED_TAIL_WORDS;
  for (size_t n = limit; n > 0; n--) {
    size_t tail_start = state->committed_tail_count - n;
    int match = 1;
    for (size_t i = 0; i < n; i++) {
      if (!vw_local_agreement_words_equal(&state->committed_tail[tail_start + i], &words[i])) {
        match = 0;
        break;
      }
    }
    if (match) return n;
  }
  return 0;
}

static void vw_local_agreement_append_tail(vw_local_agreement_t* state, const vw_local_agreement_word_t* words,
                                           size_t count) {
  if (!state || !words || count == 0) return;
  for (size_t i = 0; i < count; i++) {
    if (state->committed_tail_count < VW_LOCAL_AGREEMENT_COMMITTED_TAIL_WORDS) {
      state->committed_tail[state->committed_tail_count++] = words[i];
    } else {
      memmove(&state->committed_tail[0], &state->committed_tail[1],
              (VW_LOCAL_AGREEMENT_COMMITTED_TAIL_WORDS - 1U) * sizeof(state->committed_tail[0]));
      state->committed_tail[VW_LOCAL_AGREEMENT_COMMITTED_TAIL_WORDS - 1U] = words[i];
    }
  }
}

void vw_local_agreement_init(vw_local_agreement_t* state) { vw_local_agreement_reset(state); }

void vw_local_agreement_reset(vw_local_agreement_t* state) {
  if (!state) return;
  memset(state, 0, sizeof(*state));
  state->last_committed_end_us = -1;
}

size_t vw_local_agreement_update(vw_local_agreement_t* state, const vw_local_agreement_word_t* hypothesis,
                                 size_t hypothesis_count, vw_local_agreement_word_t* output, size_t output_capacity) {
  if (!state || (!hypothesis && hypothesis_count > 0) || (!output && output_capacity > 0)) return 0;

  vw_local_agreement_word_t filtered[VW_LOCAL_AGREEMENT_MAX_WORDS];
  size_t filtered_count = 0;
  for (size_t i = 0; i < hypothesis_count && filtered_count < VW_LOCAL_AGREEMENT_MAX_WORDS; i++) {
    if (!vw_local_agreement_word_valid(&hypothesis[i])) continue;
    if (state->has_committed &&
        hypothesis[i].start_pts_us <= state->last_committed_end_us - VW_LOCAL_AGREEMENT_RETAIN_US) {
      continue;
    }
    filtered[filtered_count++] = hypothesis[i];
  }

  size_t overlap = vw_local_agreement_strip_committed_overlap(state, filtered, filtered_count);
  if (overlap > 0) {
    memmove(filtered, filtered + overlap, (filtered_count - overlap) * sizeof(filtered[0]));
    filtered_count -= overlap;
  }

  if (state->previous_count == 0) {
    vw_local_agreement_copy_words(state->previous, filtered, filtered_count);
    state->previous_count = filtered_count;
    return 0;
  }

  size_t common_count = state->previous_count < filtered_count ? state->previous_count : filtered_count;
  size_t confirmed_count = 0;
  while (confirmed_count < common_count &&
         vw_local_agreement_words_equal(&state->previous[confirmed_count], &filtered[confirmed_count])) {
    confirmed_count++;
  }
  if (confirmed_count > output_capacity) confirmed_count = output_capacity;

  if (confirmed_count > 0) {
    vw_local_agreement_copy_words(output, filtered, confirmed_count);
    vw_local_agreement_append_tail(state, filtered, confirmed_count);
    state->last_committed_end_us = filtered[confirmed_count - 1U].end_pts_us;
    state->has_committed = 1;
  }

  size_t remainder = filtered_count - confirmed_count;
  if (remainder > 0) memmove(state->previous, filtered + confirmed_count, remainder * sizeof(state->previous[0]));
  state->previous_count = remainder;
  return confirmed_count;
}

int vw_local_agreement_format_commit(const vw_local_agreement_word_t* words, size_t word_count, char* text_out,
                                     size_t text_capacity, int64_t* start_pts_us, int64_t* end_pts_us) {
  if (!words || word_count == 0 || !text_out || text_capacity == 0 || !start_pts_us || !end_pts_us) return 0;
  size_t written = 0;
  text_out[0] = '\0';
  for (size_t i = 0; i < word_count; i++) {
    if (!vw_local_agreement_word_valid(&words[i])) return 0;
    size_t len = strlen(words[i].text_utf8);
    size_t needed = len + (i > 0 ? 1U : 0U);
    if (written + needed + 1U > text_capacity) return 0;
    if (i > 0) text_out[written++] = ' ';
    memcpy(text_out + written, words[i].text_utf8, len);
    written += len;
    text_out[written] = '\0';
  }
  *start_pts_us = words[0].start_pts_us;
  *end_pts_us = words[word_count - 1U].end_pts_us;
  return *end_pts_us >= *start_pts_us;
}
