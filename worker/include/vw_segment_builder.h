#ifndef VW_SEGMENT_BUILDER_H_
#define VW_SEGMENT_BUILDER_H_

#include <stdbool.h>
#include <stdint.h>

#include "vw_protocol_types.h"

#define VW_SEGMENT_BUILDER_MAX_BUFSZ 20         // 20 caption segments max in pending output queue
#define VW_SEGMENT_HISTORY_CAPACITY 16          // 16 committed caption segments in deduplication history
#define VW_SEGMENT_BUILDER_MAX_TEXT_BYTES 1024  // 1 KB max text length
#define VW_AUDIO_SAMPLE_RATE 16000              // 16kHz sample rate
#define VW_HOP_DURATION_US 2000000              // 2s hop length for segmenting audio
#define VW_WINDOW_DURATION_US 8000000           // 8s max window length
#define VW_WINDOW_SAMPLES 128000                // 8s window sample count (128,000 samples at 16kHz)
#define VW_HOP_SAMPLES 32000                    // 2s hop sample count (32,000 samples at 16kHz)
#define VW_DEDUP_TIME_TOLERANCE_US 500000LL     // 500ms timestamp tolerance for hop deduplication

// Record of a previously committed phrase retained for sliding-window deduplication across hops.
typedef struct vw_history_entry {
  int64_t start_pts_us;
  int64_t end_pts_us;
  char text[VW_SEGMENT_BUILDER_MAX_TEXT_BYTES];
} vw_history_entry_t;

typedef struct vw_segment_builder {
  uint64_t next_segment_id;
  vw_caption_segment_t* segment_queue;                      // Circular buffer of pending caption segments (0..19)
  size_t head;                                              // Next write position in segment_queue (0..19)
  size_t count;                                             // Active pending item count in segment_queue (0..20)
  vw_history_entry_t history[VW_SEGMENT_HISTORY_CAPACITY];  // Sliding history of committed phrases for deduplication
  size_t history_head;                                      // Next write index in history (0..15)
  size_t history_count;                                     // Count of active history entries (0..16)
} vw_segment_builder_t;

// Allocates and initializes a segment builder instance with circular pending output queue and sliding committed phrase
// deduplication history buffer.
vw_segment_builder_t* vw_segment_builder_create(void);

// Frees all memory associated with the segment builder, including any remaining queued pending segment text
// allocations and internal buffers.
void vw_segment_builder_free(vw_segment_builder_t* builder);

// Clears all pending queued caption segments and empties the committed history buffer on seek, pause, or session
// restart events.
void vw_segment_builder_clear(vw_segment_builder_t* builder);

// Pushes a new phrase hypothesis into the builder, deduplicating against recent history using 500ms timestamp
// proximity and committing unique phrases as final.
bool vw_segment_builder_push_hypothesis(vw_segment_builder_t* builder, const char* text, int64_t start_pts_us,
                                        int64_t end_pts_us);

// Pops the oldest pending caption segment from the queue, transferring ownership of the allocated text buffer to the
// caller.
bool vw_segment_builder_pop(vw_segment_builder_t* builder, vw_caption_segment_t* out);

#endif  // VW_SEGMENT_BUILDER_H_
