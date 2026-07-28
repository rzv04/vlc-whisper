#ifndef VW_SEGMENT_BUILDER_H_
#define VW_SEGMENT_BUILDER_H_

#include <stdbool.h>
#include <stdint.h>

#include "vw_protocol_types.h"

#define VW_SEGMENT_BUILDER_MAX_BUFSZ 20         // 20 caption segments max
#define VW_SEGMENT_BUILDER_MAX_TEXT_BYTES 1024  // 1 KB max text length
#define VW_HOP_DURATION_US 2000000              // 2s hop length for segmenting audio
#define VW_WINDOW_DURATION_US 8000000           // 8s max window length

typedef struct vw_segment_builder {
  uint64_t next_segment_id;
  vw_caption_segment_t* segment_queue;  // circular buffer of caption segments
  size_t head;                               // Next write position (0..19)
  size_t count;                              // Active item count (0..20)
} vw_segment_builder_t;

vw_segment_builder_t* vw_segment_builder_create(void);

void vw_segment_builder_free(vw_segment_builder_t* builder);

// Pushes a new caption segment hypothesis into the segment builder, ensuring no overlapping timestamps and valid text
// length. Returns true if the hypothesis was successfully added, false otherwise.
bool vw_segment_builder_push_hypothesis(vw_segment_builder_t* builder, const char* text, int64_t start_pts_us,
                                        int64_t end_pts_us);
#endif  // VW_SEGMENT_BUILDER_H_
