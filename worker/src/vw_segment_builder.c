#define _POSIX_C_SOURCE 200809L

#include "vw_segment_builder.h"

#include <stdlib.h>
#include <string.h>

#include "vw_protocol_util.h"

vw_segment_builder_t* vw_segment_builder_create(void) {
  vw_segment_builder_t* b = (vw_segment_builder_t*)calloc(1, sizeof(vw_segment_builder_t));
  if (b == NULL) {
    return NULL;
  }
  b->next_segment_id = 1;
  b->capacity = VW_SEGMENT_BUILDER_INITIAL_CAPACITY;
  b->segment_queue = (vw_caption_segment_t*)calloc(b->capacity, sizeof(vw_caption_segment_t));
  if (b->segment_queue == NULL) {
    free(b);
    return NULL;
  }
  return b;
}

void vw_segment_builder_free(vw_segment_builder_t* builder) {
  if (builder != NULL) {
    if (builder->segment_queue != NULL) {
      for (size_t i = 0; i < builder->capacity; i++) {
        free(builder->segment_queue[i].text_utf8);
      }
      free(builder->segment_queue);
    }
    free(builder);
  }
}

void vw_segment_builder_clear(vw_segment_builder_t* builder) {
  if (builder == NULL) {
    return;
  }
  if (builder->segment_queue != NULL) {
    for (size_t i = 0; i < builder->capacity; i++) {
      if (builder->segment_queue[i].text_utf8 != NULL) {
        free(builder->segment_queue[i].text_utf8);
        builder->segment_queue[i].text_utf8 = NULL;
      }
    }
  }
  builder->head = 0;
  builder->count = 0;
  builder->history_head = 0;
  builder->history_count = 0;
  memset(builder->history, 0, sizeof(builder->history));
}

// Returns pointer to the last pushed segment in the circular buffer, or NULL if empty
static const vw_caption_segment_t* vw_segment_builder_get_last_segment(const vw_segment_builder_t* builder) {
  if (builder == NULL || builder->count == 0 || builder->capacity == 0) {
    return NULL;
  }
  size_t last_idx = (builder->head + builder->capacity - 1) % builder->capacity;
  return &builder->segment_queue[last_idx];
}

// Returns true if prefix is exactly the leading run of str and the following character is a word boundary
// (whitespace or end of string). Exact equality (prefix == str) is NOT treated as a boundary prefix here; callers
// must handle exact match separately before consulting this helper.
static bool vw_segment_builder_is_word_boundary_prefix(const char* prefix, const char* str) {
  size_t plen = strlen(prefix);
  size_t slen = strlen(str);
  if (slen <= plen) {
    return false;  // exact or shorter -> not a boundary prefix (exact handled by caller)
  }
  if (memcmp(str, prefix, plen) != 0) {
    return false;
  }
  char c = str[plen];
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// Selects the first token index that begins the new suffix of an expanded phrase. Time-first: the first token whose
// start PTS is at or after the committed prefix end minus tolerance; its cumulative text offset must then land at or
// after the matched prefix end (within tolerance). Falls back to the pure text-boundary rule (first token whose start
// offset reaches the prefix end) when no time-consistent token exists. Returns true and sets *out_idx when a valid
// suffix start is found; callers should drop the candidate when this returns false.
// Selects the suffix-boundary token index for a word-aligned prefix expansion. The boundary is the
// first token whose text starts exactly at the committed prefix end — a token fully inside the
// prefix is never selected, so already-displayed words are never repeated. Any token that SPANS the
// prefix end (its text begins before the boundary and ends after it) necessarily contains the tail
// of a committed word, which cannot be split safely, so it returns false (conservative drop). The
// suffix start time is the boundary token's own authentic t0.
static bool vw_segment_builder_select_suffix_token(const vw_phrase_token_t* tokens, size_t token_count,
                                                   size_t prefix_len, size_t* out_idx) {
  size_t cum = 0;
  for (size_t k = 0; k < token_count; k++) {
    if (cum == prefix_len) {
      *out_idx = k;  // token starts exactly at the prefix end -> fully new
      return true;
    }
    size_t next_cum = cum + strlen(tokens[k].text);
    if (next_cum > prefix_len) {
      return false;  // token spans the prefix end -> cannot split without repeating committed text
    }
    cum = next_cum;
  }
  return false;  // no token extends beyond the prefix (candidate == prefix) -> exact-match drop path
}

// Allocates a text copy and appends a finalized cue to the pending queue, growing the ring buffer when full. Returns
// false only on allocation failure; the caller must not commit history when this fails.
static bool vw_segment_builder_enqueue(vw_segment_builder_t* builder, const char* text, int64_t start, int64_t end) {
  if (builder->capacity == 0 || builder->segment_queue == NULL) {
    return false;
  }
  size_t len = strlen(text);
  char* copy = (char*)malloc(len + 1);
  if (copy == NULL) {
    return false;
  }
  memcpy(copy, text, len);
  copy[len] = '\0';

  if (builder->count == builder->capacity) {
    size_t new_cap = builder->capacity * 2;
    vw_caption_segment_t* new_queue = (vw_caption_segment_t*)calloc(new_cap, sizeof(vw_caption_segment_t));
    if (new_queue == NULL) {
      free(copy);
      return false;  // Memory allocation failure: reject without committing to history
    }
    // Copy in FIFO order from oldest to newest
    for (size_t i = 0; i < builder->count; i++) {
      size_t tail = (builder->head + builder->capacity - builder->count + i) % builder->capacity;
      new_queue[i] = builder->segment_queue[tail];
    }
    free(builder->segment_queue);
    builder->segment_queue = new_queue;
    builder->head = builder->count;
    builder->capacity = new_cap;
  }

  size_t slot = builder->head;
  builder->segment_queue[slot].segment_id = builder->next_segment_id++;
  builder->segment_queue[slot].start_pts_us = start;  // Authentic Whisper start PTS
  builder->segment_queue[slot].end_pts_us = end;      // Authentic Whisper end PTS
  builder->segment_queue[slot].is_final = true;
  builder->segment_queue[slot].text_utf8 = copy;
  builder->segment_queue[slot].text_bytes = (uint16_t)len;

  builder->head = (builder->head + 1) % builder->capacity;
  builder->count++;
  return true;
}

// Records the fully expanded candidate phrase in the committed-history ring buffer. Invoked ONLY after a successful
// enqueue (or in-place replace), so history never retains a phrase that was not actually output.
static void vw_segment_builder_commit_history(vw_segment_builder_t* builder, const char* text, int64_t start,
                                              int64_t end) {
  size_t len = strlen(text);
  size_t copy_len = len < VW_SEGMENT_BUILDER_MAX_TEXT_BYTES ? len : (VW_SEGMENT_BUILDER_MAX_TEXT_BYTES - 1);
  size_t h_slot = builder->history_head;
  builder->history[h_slot].start_pts_us = start;
  builder->history[h_slot].end_pts_us = end;
  memcpy(builder->history[h_slot].text, text, copy_len);
  builder->history[h_slot].text[copy_len] = '\0';
  builder->history_head = (builder->history_head + 1) % VW_SEGMENT_HISTORY_CAPACITY;
  if (builder->history_count < VW_SEGMENT_HISTORY_CAPACITY) {
    builder->history_count++;
  }
}

bool vw_segment_builder_push_phrase(vw_segment_builder_t* builder, const char* text, int64_t start_pts_us,
                                    int64_t end_pts_us, const vw_phrase_token_t* tokens, size_t token_count) {
  if (builder == NULL || text == NULL || start_pts_us < 0 || end_pts_us <= start_pts_us) {
    return false;
  }

  // Trim leading whitespace
  while (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r') {
    text++;
  }

  // Trim trailing whitespace
  size_t len = strlen(text);
  while (len > 0 && (text[len - 1] == ' ' || text[len - 1] == '\t' || text[len - 1] == '\n' || text[len - 1] == '\r')) {
    len--;
  }

  if (len == 0 || len >= VW_SEGMENT_BUILDER_MAX_TEXT_BYTES) {
    return false;
  }

  // 1. In-window (un-emitted) check against the last queued segment. Runs before the committed-history loop so an
  //    expansion of a still-pending cue replaces it in place instead of emitting a separate suffix cue.
  const vw_caption_segment_t* last = vw_segment_builder_get_last_segment(builder);
  if (last != NULL && last->text_utf8 != NULL) {
    // Exact full match -> duplicate re-transcription from the same window -> drop.
    if (strncmp(last->text_utf8, text, len) == 0 && last->text_utf8[len] == '\0') {
      return false;
    }
    // Candidate is a fragment already covered by the pending cue -> drop.
    if (strstr(last->text_utf8, text) != NULL) {
      return false;
    }
    // Pending cue is a word-aligned PREFIX of the candidate -> extend it in place (tokenized) or conservatively drop.
    if (vw_segment_builder_is_word_boundary_prefix(last->text_utf8, text)) {
      if (tokens != NULL && token_count > 0) {
        size_t last_idx = (builder->head + builder->capacity - 1) % builder->capacity;
        vw_caption_segment_t* last_mut = &builder->segment_queue[last_idx];
        char* new_text = (char*)malloc(len + 1);
        if (new_text == NULL) {
          return false;
        }
        memcpy(new_text, text, len);
        new_text[len] = '\0';
        free(last_mut->text_utf8);
        last_mut->text_utf8 = new_text;
        last_mut->text_bytes = (uint16_t)len;
        last_mut->end_pts_us = end_pts_us;  // start_pts_us (and segment_id/count) intentionally preserved
        vw_segment_builder_commit_history(builder, text, start_pts_us, end_pts_us);
        return true;  // replaced in place: no count or next_segment_id increment
      }
      // NULL tokens -> legacy conservative superstring drop (no synthetic timing).
      return false;
    }
    // Candidate contains the pending cue mid-way (not as a prefix) -> conservative anti-repeat drop.
    if (strstr(text, last->text_utf8) != NULL) {
      return false;
    }
  }

  // 2. Committed-history loop (newest-first, same time-overlap gate as before). Cross-hop duplicates here.
  for (size_t i = 0; i < builder->history_count; i++) {
    size_t idx = (builder->history_head + VW_SEGMENT_HISTORY_CAPACITY - 1 - i) % VW_SEGMENT_HISTORY_CAPACITY;
    const vw_history_entry_t* hist = &builder->history[idx];

    int64_t start_diff = (start_pts_us >= hist->start_pts_us) ? (start_pts_us - hist->start_pts_us)
                                                              : (hist->start_pts_us - start_pts_us);
    bool time_matches = (start_diff <= VW_DEDUP_TIME_TOLERANCE_US) ||
                        (end_pts_us > hist->start_pts_us && start_pts_us < hist->end_pts_us);
    if (!time_matches) {
      continue;
    }

    // Exact full match -> duplicate re-transcription from an overlapping hop -> drop.
    if (strncmp(hist->text, text, len) == 0 && hist->text[len] == '\0') {
      return false;
    }
    // Candidate is a fragment already covered by a committed phrase -> drop.
    if (strstr(hist->text, text) != NULL) {
      return false;
    }
    // Committed phrase is a word-aligned PREFIX of the candidate -> expand, emitting only the new suffix.
    if (vw_segment_builder_is_word_boundary_prefix(hist->text, text)) {
      if (tokens != NULL && token_count > 0) {
        size_t suffix_idx = 0;
        if (!vw_segment_builder_select_suffix_token(tokens, token_count, strlen(hist->text), &suffix_idx)) {
          return false;  // No valid suffix boundary -> conservative drop (never repeat committed words).
        }
        // Concatenate the suffix token texts (leading spaces trimmed).
        size_t suffix_len = 0;
        for (size_t k = suffix_idx; k < token_count; k++) {
          suffix_len += strlen(tokens[k].text);
        }
        char* suffix_text = (char*)malloc(suffix_len + 1);
        if (suffix_text == NULL) {
          return false;
        }
        size_t off = 0;
        for (size_t k = suffix_idx; k < token_count; k++) {
          size_t tl = strlen(tokens[k].text);
          memcpy(suffix_text + off, tokens[k].text, tl);
          off += tl;
        }
        suffix_text[off] = '\0';
        // Trim leading spaces (whisper token text may carry a leading space).
        size_t lead = 0;
        while (suffix_text[lead] == ' ') {
          lead++;
        }
        if (lead > 0) {
          memmove(suffix_text, suffix_text + lead, off - lead + 1);
          off -= lead;
        }
        if (off == 0) {
          free(suffix_text);
          return false;  // Suffix collapsed to empty -> drop rather than emit a blank cue.
        }
        int64_t suffix_start = tokens[suffix_idx].t0_us;
        int64_t suffix_end = end_pts_us;
        if (!vw_segment_builder_enqueue(builder, suffix_text, suffix_start, suffix_end)) {
          free(suffix_text);
          return false;
        }
        free(suffix_text);  // enqueue copied it
        // History records the FULL candidate (suffix cue carries only the new words).
        vw_segment_builder_commit_history(builder, text, start_pts_us, end_pts_us);
        return true;
      }
      // NULL tokens -> legacy conservative superstring drop (no synthetic timing).
      return false;
    }
    // Candidate contains the committed phrase mid-way (not as a prefix) -> conservative anti-repeat drop.
    if (strstr(text, hist->text) != NULL) {
      return false;
    }
  }

  // 3. No duplicate detected -> queue the full candidate and commit it to history only after a successful enqueue.
  if (!vw_segment_builder_enqueue(builder, text, start_pts_us, end_pts_us)) {
    return false;
  }
  vw_segment_builder_commit_history(builder, text, start_pts_us, end_pts_us);
  return true;
}

bool vw_segment_builder_push_hypothesis(vw_segment_builder_t* builder, const char* text, int64_t start_pts_us,
                                        int64_t end_pts_us) {
  // Thin wrapper: no token timing -> legacy whole-phrase deduplication (conservative superstring drop preserved).
  return vw_segment_builder_push_phrase(builder, text, start_pts_us, end_pts_us, NULL, 0);
}

bool vw_segment_builder_pop(vw_segment_builder_t* builder, vw_caption_segment_t* out) {
  if (!builder || !out || builder->count == 0 || !builder->segment_queue || builder->capacity == 0) {
    return false;
  }

  size_t tail = (builder->head + builder->capacity - builder->count) % builder->capacity;
  *out = builder->segment_queue[tail];
  // Clear slot in ring buffer so ownership of text_utf8 is transferred to caller
  memset(&builder->segment_queue[tail], 0, sizeof(vw_caption_segment_t));
  builder->count--;
  return true;
}