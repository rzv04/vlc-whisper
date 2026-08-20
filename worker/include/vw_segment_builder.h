#ifndef VW_SEGMENT_BUILDER_H_
#define VW_SEGMENT_BUILDER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vw_protocol_types.h"

#define VW_SEGMENT_BUILDER_INITIAL_CAPACITY 32  // Initial pending caption queue capacity
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
  vw_caption_segment_t* segment_queue;                      // Dynamically growable circular buffer of pending segments
  size_t capacity;                                          // Allocated capacity of segment_queue
  size_t head;                                              // Next write position in segment_queue
  size_t count;                                             // Active pending item count in segment_queue
  vw_history_entry_t history[VW_SEGMENT_HISTORY_CAPACITY];  // Sliding history of committed phrases for deduplication
  size_t history_head;                                      // Next write index in history (0..15)
  size_t history_count;                                     // Count of active history entries (0..16)
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

// Borrowed token view of a single Whisper token in a candidate phrase. Supplied only on tokenized pushes; the text
// may carry a leading space as emitted by whisper, and t0_us/t1_us are absolute media PTS (the transcription window
// start plus the per-token offset). A NULL token array falls back to the legacy whole-phrase deduplication path.
typedef struct vw_phrase_token {
  const char* text;  // NUL-terminated token text (may include a leading space)
  int64_t t0_us;     // absolute media PTS of the token start
  int64_t t1_us;     // absolute media PTS of the token end
} vw_phrase_token_t;

// Pushes a phrase with optional per-token timing for accurate suffix extraction. When tokens are supplied and the
// candidate expands an already committed or queued prefix, only the new suffix is queued using the first token at or
// after the committed boundary; with NULL tokens the legacy whole-phrase behavior (including conservative superstring
// drop) is preserved. Returns true if a cue was queued or an existing cue was extended in place.
bool vw_segment_builder_push_phrase(vw_segment_builder_t* builder, const char* text, int64_t start_pts_us,
                                    int64_t end_pts_us, const vw_phrase_token_t* tokens, size_t token_count);

// Pushes a phrase hypothesis using only whole-phrase text for deduplication; a thin wrapper over
// vw_segment_builder_push_phrase that passes no token timing, so the legacy conservative superstring-drop behavior is
// preserved when a candidate merely extends an already committed or queued phrase. Returns true if a cue was queued.
bool vw_segment_builder_push_hypothesis(vw_segment_builder_t* builder, const char* text, int64_t start_pts_us,
                                        int64_t end_pts_us);

// Pops the oldest pending caption segment from the queue and transfers ownership of its allocated text buffer to the
// caller, who must free it; returns false when the queue is empty so the caller can detect exhaustion.
bool vw_segment_builder_pop(vw_segment_builder_t* builder, vw_caption_segment_t* out);

#endif  // VW_SEGMENT_BUILDER_H_
