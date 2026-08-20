#ifndef VW_SEGMENT_BUILDER_H_
#define VW_SEGMENT_BUILDER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vw_protocol_types.h"

#define VW_SEGMENT_BUILDER_INITIAL_CAPACITY 32        // Initial pending caption queue capacity
#define VW_SEGMENT_HISTORY_CAPACITY 16                // 16 committed caption segments in deduplication history
#define VW_SEGMENT_BUILDER_MAX_TEXT_BYTES 1024        // 1 KB max text length
#define VW_AUDIO_SAMPLE_RATE 16000                    // 16kHz sample rate
#define VW_HOP_DURATION_US 2000000                    // 2s hop length for segmenting audio
#define VW_WINDOW_DURATION_US 8000000                 // 8s max window length
#define VW_WINDOW_SAMPLES 128000                      // 8s window sample count (128,000 samples at 16kHz)
#define VW_HOP_SAMPLES 32000                          // 2s hop sample count (32,000 samples at 16kHz)
#define VW_DEDUP_TIME_TOLERANCE_US 500000LL           // 500ms timestamp tolerance for hop deduplication
#define VW_CAPTION_MIN_DISPLAY_DURATION_US 1000000LL  // 1.0s minimum reading floor for subtitle display

// Record of a previously committed phrase retained for sliding-window deduplication across hops.
typedef struct vw_history_entry {
  int64_t start_pts_us;
  int64_t end_pts_us;
  char text[VW_SEGMENT_BUILDER_MAX_TEXT_BYTES];
} vw_history_entry_t;

typedef struct vw_segment_builder {
  uint64_t next_segment_id;
  vw_caption_segment_t* segment_queue;                      // Dynamically growable circular buffer of pending segments
  size_t capacity;                                          // Allocated capacity of segment_queue
  size_t head;                                              // Next write position in segment_queue
  size_t count;                                             // Active pending item count in segment_queue
  vw_history_entry_t history[VW_SEGMENT_HISTORY_CAPACITY];  // Sliding history of committed phrases for deduplication
  size_t history_head;                                      // Next write index in history (0..15)
  size_t history_count;                                     // Count of active history entries (0..16)
  int64_t covered_end_us;  // End of the last committed cue (audio coverage frontier, -1 = none)
} vw_segment_builder_t;

// Allocates and initializes a segment builder instance, creating a circular pending-output queue and a sliding
// committed-phrase history buffer used for cross-hop deduplication, returning NULL on allocation failure.
vw_segment_builder_t* vw_segment_builder_create(void);

// Releases all memory owned by the segment builder, including every queued pending segment text buffer still held in
// the circular queue and the builder structure itself; safe to call with a NULL builder pointer.
void vw_segment_builder_free(vw_segment_builder_t* builder);

// Clears all pending queued caption segments and empties the committed-history deduplication buffer on seek, pause, or
// session-restart events, while preserving the allocated queue storage for immediate reuse.
void vw_segment_builder_clear(vw_segment_builder_t* builder);

// Pushes a phrase hypothesis as an IMMUTABLE FINAL cue with its authentic Whisper start/end PTS
// (ADR-017: final subtitles — no expansion or revision of emitted phrases). Deduplicates against
// the pending queue and committed history: exact matches, fragments, and superstrings (expanded
// re-recognitions) are all dropped. Returns true if a cue was queued.
bool vw_segment_builder_push_hypothesis(vw_segment_builder_t* builder, const char* text, int64_t start_pts_us,
                                        int64_t end_pts_us);

// Pops the oldest pending caption segment from the queue and transfers ownership of its allocated text buffer to the
// caller, who must free it; returns false when the queue is empty so the caller can detect exhaustion.
bool vw_segment_builder_pop(vw_segment_builder_t* builder, vw_caption_segment_t* out);

#endif  // VW_SEGMENT_BUILDER_H_
