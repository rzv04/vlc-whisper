#ifndef VW_SEGMENT_BUILDER_H_
#define VW_SEGMENT_BUILDER_H_

#include <stdbool.h>
#include <stdint.h>

#include "vw_protocol_types.h"

#define VW_SEGMENT_BUILDER_MAX_BUFSZ 20         // 20 caption segments max
#define VW_SEGMENT_BUILDER_MAX_TEXT_BYTES 1024  // 1 KB max text length
#define VW_AUDIO_SAMPLE_RATE 16000              // 16kHz sample rate
#define VW_HOP_DURATION_US 2000000              // 2s hop length for segmenting audio
#define VW_WINDOW_DURATION_US 8000000           // 8s max window length
#define VW_WINDOW_SAMPLES 128000                // 8s window sample count (128,000 samples at 16kHz)
#define VW_HOP_SAMPLES 32000                    // 2s hop sample count (32,000 samples at 16kHz)

typedef struct vw_segment_builder {
  uint64_t next_segment_id;
  vw_caption_segment_t* segment_queue;  // circular buffer of caption segments
  size_t head;                          // Next write position (0..19)
  size_t count;                         // Active item count (0..20)
} vw_segment_builder_t;

vw_segment_builder_t* vw_segment_builder_create(void);

void vw_segment_builder_free(vw_segment_builder_t* builder);

// Pushes a new caption segment hypothesis into the segment builder, ensuring no overlapping timestamps and valid text
// length. Returns true if the hypothesis was successfully added, false otherwise.
bool vw_segment_builder_push_hypothesis(vw_segment_builder_t* builder, const char* text, int64_t start_pts_us,
                                        int64_t end_pts_us);

// Pops the oldest caption segment from the builder queue into out. Returns true if a segment was popped, false if
// empty. Note: caller assumes ownership of out->text_utf8 and must call free() on it when done.
bool vw_segment_builder_pop(vw_segment_builder_t* builder, vw_caption_segment_t* out);

#endif  // VW_SEGMENT_BUILDER_H_
