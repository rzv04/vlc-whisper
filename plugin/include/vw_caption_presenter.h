#ifndef VW_CAPTION_PRESENTER_H_
#define VW_CAPTION_PRESENTER_H_

#include <stdbool.h>
#include <stdint.h>

#include "vw_protocol_types.h"

// Minimum subtitle display duration floor (1.0 second = 1,000,000 microseconds) to eliminate unreadable sub-second
// flash cues.
#define VW_CAPTION_MIN_DISPLAY_DURATION_US 1000000LL

#define VW_PRESENTER_MAX_TEXT_BYTES 1024

typedef struct vw_caption_presenter {
  void* p_filter_ctx;
  void* p_held_vout;   // Retained vout reference ensuring lifetime safety and protecting against pointer address reuse.
  int spu_channel_id;  // Registered VLC SPU channel ID; -1 when unregistered or unavailable.
  bool spu_channel_registered;           // True if spu_channel_id >= 0 and successfully registered with current vout.
  bool has_pending;                      // True if a pending caption cue is buffered for lookahead pacing.
  vw_caption_segment_t pending_segment;  // Buffered segment awaiting successor boundary for overlap clipping.
  char pending_text[VW_PRESENTER_MAX_TEXT_BYTES];  // Static buffer storing pending segment text.
} vw_caption_presenter_t;

// Renders fallback caption text via OSD on the active vout surface when SPU is unavailable,
// safely locating the target vout thread through the parent filter hierarchy without blocking.
bool vw_caption_presenter_display(void* p_filter, const char* text, int64_t duration_us);

// Buffers a timed caption segment and dispatches any preceding cue to SPU after clipping
// its reading floor to the incoming cue start to prevent adjacent interval overlap.
bool vw_caption_presenter_show_segment(vw_caption_presenter_t* presenter, const vw_caption_segment_t* segment,
                                       int64_t input_time_us);

// Flushes the buffered pending caption cue to the registered SPU subpicture channel with standard reading
// floor duration when no subsequent overlapping cue arrives before the start display time.
bool vw_caption_presenter_flush(vw_caption_presenter_t* presenter, int64_t input_time_us);

// Blanks currently displayed caption overlays by flushing both private SPU and OSD channels while preserving filter
// context, guaranteeing clean subtitle erasure across seek jumps before upcoming segments arrive.
void vw_caption_presenter_blank(vw_caption_presenter_t* presenter);

// Flushes active caption channels, releases any held video output references, and resets presenter filter context
// exclusively during module teardown to ensure leak-free destruction without disrupting ongoing video playback.
void vw_caption_presenter_clear(vw_caption_presenter_t* presenter);

#endif  // VW_CAPTION_PRESENTER_H_
