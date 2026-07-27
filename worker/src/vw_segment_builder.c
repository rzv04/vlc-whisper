#include "vw_segment_builder.h"

#include <stdlib.h>
#include <string.h>

struct vw_segment_builder_t* vw_segment_builder_create(void) {
  struct vw_segment_builder_t* b = (struct vw_segment_builder_t*)calloc(1, sizeof(struct vw_segment_builder_t));
  b->next_segment_id = 1;
  b->segment_queue =
      (struct vw_caption_segment*)calloc(VW_SEGMENT_BUILDER_MAX_BUFSZ, sizeof(struct vw_caption_segment));
  return b;
}

void vw_segment_builder_free(struct vw_segment_builder_t* builder) {
  if (builder) {
    if (builder->segment_queue) {
      for (int i = 0; i < VW_SEGMENT_BUILDER_MAX_BUFSZ; i++) {
        free(builder->segment_queue[i].text_utf8);
      }
      free(builder->segment_queue);
    }
    free(builder);
  }
}

// Returns length of overlapping text between end of prev and start of curr
static size_t vw_find_text_overlap(const char* prev, const char* curr) {
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
  // Implementation for pushing a new caption segment hypothesis
  if (!builder || !text || start_pts_us < 0 || end_pts_us <= start_pts_us) {
    return false;
  }

  int l = strlen(text);
  if (l == 0 || l > 1024) {
    return false;
  }

  // merge last and this segment
  if (builder->count > 0) {
    size_t last_idx = (builder->head + VW_SEGMENT_BUILDER_MAX_BUFSZ - 1) % VW_SEGMENT_BUILDER_MAX_BUFSZ;
    size_t overlap = vw_find_text_overlap(builder->segment_queue[last_idx].text_utf8, text);
    if (overlap > 0) {
      const char* trimmed_text = text + overlap;
      while (*trimmed_text == ' ') trimmed_text++;  // Skip leading space
      if (*trimmed_text == '\0') return false;      // Full duplicate, skip!
      text = trimmed_text;                          // Continue with unique suffix
    }
  }

  size_t slot = builder->head;
  if (builder->segment_queue[slot].text_utf8) {
    free(builder->segment_queue[slot].text_utf8);
  }

  // 3. Write new segment
  builder->segment_queue[slot].segment_id = builder->next_segment_id++;
  builder->segment_queue[slot].start_pts_us = start_pts_us;
  builder->segment_queue[slot].end_pts_us = end_pts_us;
  builder->segment_queue[slot].is_final = true;
  builder->segment_queue[slot].text_utf8 = strdup(text);
  builder->segment_queue[slot].text_bytes = (uint16_t)l;

  // 4. Advance head index
  builder->head = (builder->head + 1) % VW_SEGMENT_BUILDER_MAX_BUFSZ;
  if (builder->count < VW_SEGMENT_BUILDER_MAX_BUFSZ) {
    builder->count++;
  }

  return true;
}