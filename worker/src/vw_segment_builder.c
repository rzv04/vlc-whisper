#define _POSIX_C_SOURCE 200809L

#include "vw_segment_builder.h"

#include <stdlib.h>
#include <string.h>

struct vw_segment_builder_t* vw_segment_builder_create(void) {
  struct vw_segment_builder_t* b = (struct vw_segment_builder_t*)calloc(1, sizeof(struct vw_segment_builder_t));
  if (b == NULL) {
    return NULL;
  }
  b->next_segment_id = 1;
  b->segment_queue =
      (struct vw_caption_segment*)calloc(VW_SEGMENT_BUILDER_MAX_BUFSZ, sizeof(struct vw_caption_segment));
  if (b->segment_queue == NULL) {
    free(b);
    return NULL;
  }
  return b;
}

void vw_segment_builder_free(struct vw_segment_builder_t* builder) {
  if (builder != NULL) {
    if (builder->segment_queue != NULL) {
      for (size_t i = 0; i < VW_SEGMENT_BUILDER_MAX_BUFSZ; i++) {
        free(builder->segment_queue[i].text_utf8);
      }
      free(builder->segment_queue);
    }
    free(builder);
  }
}

// Returns pointer to the last pushed segment in the circular buffer, or NULL if empty
static const vw_caption_segment_t* vw_segment_builder_get_last_segment(const struct vw_segment_builder_t* builder) {
  if (builder == NULL || builder->count == 0) {
    return NULL;
  }
  size_t last_idx = (builder->head + VW_SEGMENT_BUILDER_MAX_BUFSZ - 1) % VW_SEGMENT_BUILDER_MAX_BUFSZ;
  return &builder->segment_queue[last_idx];
}

// Writes a segment entry into the current ring buffer slot and advances head & count
// The text is not validated for length or content; caller must ensure it is valid.
// Also, the text is strdup'd, and the caller is responsible for freeing the text in the segment when done.
static void vw_segment_builder_write_slot(struct vw_segment_builder_t* builder, const char* text, int64_t start_pts_us,
                                          int64_t end_pts_us) {
  size_t slot = builder->head;
  if (!builder->segment_queue) {
    return;  // Safety check
  }

  if (builder->segment_queue[slot].text_utf8 != NULL) {
    free(builder->segment_queue[slot].text_utf8);
  }

  size_t len = strlen(text);
  builder->segment_queue[slot].segment_id = builder->next_segment_id++;
  builder->segment_queue[slot].start_pts_us = start_pts_us;
  builder->segment_queue[slot].end_pts_us = end_pts_us;
  builder->segment_queue[slot].is_final = true;  // TODO: Determine if this segment is final based on context
  builder->segment_queue[slot].text_utf8 = strdup(text);
  builder->segment_queue[slot].text_bytes = (uint16_t)len;

  builder->head = (builder->head + 1) % VW_SEGMENT_BUILDER_MAX_BUFSZ;
  if (builder->count < VW_SEGMENT_BUILDER_MAX_BUFSZ) {
    builder->count++;
  }
}

// Returns length of overlapping text between end of prev and start of curr
static size_t vw_find_text_overlap(const char* prev, const char* curr) {
  if (prev == NULL || curr == NULL) {
    return 0;
  }
  size_t prev_len = strlen(prev);
  size_t curr_len = strlen(curr);
  size_t max_check = (prev_len < curr_len) ? prev_len : curr_len;

  for (size_t len = max_check; len > 0; --len) {
    if (strncmp(prev + (prev_len - len), curr, len) == 0) {
      return len;  // Character overlap length
    }
  }
  return 0;  // No overlap
}

bool vw_segment_builder_push_hypothesis(struct vw_segment_builder_t* builder, const char* text, int64_t start_pts_us,
                                        int64_t end_pts_us) {
  if (builder == NULL || text == NULL || start_pts_us < 0 || end_pts_us <= start_pts_us) {
    return false;
  }

  size_t len = strlen(text);
  if (len == 0 || len > VW_SEGMENT_BUILDER_MAX_TEXT_BYTES) {
    return false;
  }

  // 1. Check last segment for overlap deduplication
  const vw_caption_segment_t* last = vw_segment_builder_get_last_segment(builder);
  if (last != NULL && last->text_utf8 != NULL) {
    size_t overlap = vw_find_text_overlap(last->text_utf8, text);
    if (overlap > 0) {
      const char* trimmed_text = text + overlap;
      while (*trimmed_text == ' ') {
        trimmed_text++;  // Skip leading space
      }
      if (*trimmed_text == '\0') {
        return false;  // Full duplicate, skip!
      }
      text = trimmed_text;  // Continue with unique suffix
    }
  }

  // 2. Write new segment to ring buffer slot
  vw_segment_builder_write_slot(builder, text, start_pts_us, end_pts_us);
  return true;
}