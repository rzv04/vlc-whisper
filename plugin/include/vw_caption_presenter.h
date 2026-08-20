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

// Renders fallback caption text via OSD on the active vout surface with minimum 1.0s display floor when SPU is
// unavailable.
bool vw_caption_presenter_display(void* p_filter, const char* text, int64_t duration_us);

// Dispatches a transcription segment to the SPU channel with rate-scaled wall-clock minimum floor, falling back to OSD.
bool vw_caption_presenter_show_segment(vw_caption_presenter_t* presenter, const vw_caption_segment_t* segment,
                                       int64_t input_time_us);

// Blanks active caption overlays by flushing SPU and OSD channels while preserving filter context across user seeks.
void vw_caption_presenter_blank(vw_caption_presenter_t* presenter);

// Flushes caption channels, releases held video output references, and resets presenter context during module teardown.
void vw_caption_presenter_clear(vw_caption_presenter_t* presenter);

#endif  // VW_CAPTION_PRESENTER_H_
