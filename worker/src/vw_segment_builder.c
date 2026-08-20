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

bool vw_segment_builder_push_hypothesis(vw_segment_builder_t* builder, const char* text, int64_t start_pts_us,
                                        int64_t end_pts_us) {
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

  // 1. Whole-Phrase Deduplication against Committed History (Zero Text Slicing / Zero Synthetic Timestamps)
  for (size_t i = 0; i < builder->history_count; i++) {
    size_t idx = (builder->history_head + VW_SEGMENT_HISTORY_CAPACITY - 1 - i) % VW_SEGMENT_HISTORY_CAPACITY;
    const vw_history_entry_t* hist = &builder->history[idx];

    int64_t start_diff = (start_pts_us >= hist->start_pts_us) ? (start_pts_us - hist->start_pts_us)
                                                              : (hist->start_pts_us - start_pts_us);
    bool time_matches = (start_diff <= VW_DEDUP_TIME_TOLERANCE_US) ||
                        (end_pts_us > hist->start_pts_us && start_pts_us < hist->end_pts_us);

    if (time_matches) {
      // Exact full match -> duplicate re-transcription from overlapping hop -> drop
      if (strncmp(hist->text, text, len) == 0 && hist->text[len] == '\0') {
        return false;
      }
      // Substring match -> candidate is a fragment already covered by a committed phrase -> drop
      if (strstr(hist->text, text) != NULL) {
        return false;
      }
      // Superstring match -> candidate expands an already committed phrase at the same timestamp -> drop
      if (strstr(text, hist->text) != NULL) {
        return false;
      }
    }
  }

  // 2. In-window duplicate check against last queued segment
  const vw_caption_segment_t* last = vw_segment_builder_get_last_segment(builder);
  if (last != NULL && last->text_utf8 != NULL) {
    if (strncmp(last->text_utf8, text, len) == 0 && last->text_utf8[len] == '\0') {
      return false;
    }
    if (strstr(last->text_utf8, text) != NULL) {
      return false;
    }
    if (strstr(text, last->text_utf8) != NULL) {
      return false;
    }
  }

  // 3. Ensure queue capacity (dynamically grow if full)
  if (!builder->segment_queue || builder->capacity == 0) {
    return false;
  }

  char* text_copy = (char*)malloc(len + 1);
  if (!text_copy) {
    return false;
  }
  memcpy(text_copy, text, len);
  text_copy[len] = '\0';

  if (builder->count == builder->capacity) {
    size_t new_cap = builder->capacity * 2;
    vw_caption_segment_t* new_queue = (vw_caption_segment_t*)calloc(new_cap, sizeof(vw_caption_segment_t));
    if (!new_queue) {
      free(text_copy);
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
  builder->segment_queue[slot].start_pts_us = start_pts_us;  // Authentic Whisper start PTS
  builder->segment_queue[slot].end_pts_us = end_pts_us;      // Authentic Whisper end PTS
  builder->segment_queue[slot].is_final = true;
  builder->segment_queue[slot].text_utf8 = text_copy;
  builder->segment_queue[slot].text_bytes = (uint16_t)len;

  builder->head = (builder->head + 1) % builder->capacity;
  builder->count++;

  // 4. Record whole phrase in committed history ring buffer ONLY AFTER successful output allocation & queueing
  size_t h_slot = builder->history_head;
  builder->history[h_slot].start_pts_us = start_pts_us;
  builder->history[h_slot].end_pts_us = end_pts_us;
  memcpy(builder->history[h_slot].text, text, len);
  builder->history[h_slot].text[len] = '\0';
  builder->history_head = (builder->history_head + 1) % VW_SEGMENT_HISTORY_CAPACITY;
  if (builder->history_count < VW_SEGMENT_HISTORY_CAPACITY) {
    builder->history_count++;
  }
  return true;
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