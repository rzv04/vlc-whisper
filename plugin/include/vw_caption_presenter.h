#ifndef VW_CAPTION_PRESENTER_H_
#define VW_CAPTION_PRESENTER_H_

#include <stdbool.h>
#include <stdint.h>

#include "vw_protocol_types.h"

// Minimum subtitle display duration floor (1.0 second = 1,000,000 microseconds) to eliminate unreadable sub-second
// flash cues.
#define VW_CAPTION_MIN_DISPLAY_DURATION_US 1000000LL

typedef struct vw_caption_presenter {
  void* p_filter_ctx;
  void* p_held_vout;   // Retained vout reference ensuring lifetime safety and protecting against pointer address reuse.
  int spu_channel_id;  // Registered VLC SPU channel ID; -1 when unregistered or unavailable.
  bool spu_channel_registered;  // True if spu_channel_id >= 0 and successfully registered with current vout.
} vw_caption_presenter_t;

// Renders fallback caption text via OSD on the active vout surface when SPU is unavailable,
// safely locating the target vout thread through the parent filter hierarchy without blocking.
bool vw_caption_presenter_display(void* p_filter, const char* text, int64_t duration_us);

// Dispatches a timed transcription segment to the registered SPU subpicture channel in the OSD clock domain,
// scaling lead time and duration by playback rate and falling back gracefully to OSDText.
bool vw_caption_presenter_show_segment(vw_caption_presenter_t* presenter, const vw_caption_segment_t* segment,
                                       int64_t input_time_us);

// Blanks currently displayed caption overlays by flushing both private SPU and OSD channels while preserving filter
// context, guaranteeing clean subtitle erasure across seek jumps before upcoming segments arrive.
void vw_caption_presenter_blank(vw_caption_presenter_t* presenter);

// Flushes active caption channels, releases any held video output references, and resets presenter filter context
// exclusively during module teardown to ensure leak-free destruction without disrupting ongoing video playback.
void vw_caption_presenter_clear(vw_caption_presenter_t* presenter);

#endif  // VW_CAPTION_PRESENTER_H_
