#ifndef VW_CAPTION_PRESENTER_H_
#define VW_CAPTION_PRESENTER_H_

#include <stdbool.h>
#include <stdint.h>

#include "vw_protocol_types.h"

typedef struct vw_caption_presenter {
  void* p_filter_ctx;
  void* p_held_vout;   // Retained vout reference ensuring lifetime safety and protecting against pointer address reuse.
  int spu_channel_id;  // Registered VLC SPU channel ID; -1 when unregistered or unavailable.
  bool spu_channel_registered;  // True if spu_channel_id >= 0 and successfully registered with current vout.
} vw_caption_presenter_t;

// Renders timed fallback caption text directly onto the active VLC video output surface using OSD rendering,
// locating the target vout thread safely through the parent filter hierarchy when SPU presentation is unavailable.
bool vw_caption_presenter_display(void* p_filter, const char* text, int64_t duration_us);

// Dispatches a transcription segment to the VLC SPU channel as timed text in the OSD clock domain
// (i_start = mdate()), falling back gracefully to vout_OSDText if SPU channel registration fails.
bool vw_caption_presenter_show_segment(vw_caption_presenter_t* presenter, const vw_caption_segment_t* segment,
                                       int64_t input_time_us);

// Blanks active caption overlays by flushing both private SPU and OSD channels while preserving the filter context,
// ensuring safe subtitle erasure across user seeks before upcoming segments arrive.
void vw_caption_presenter_blank(vw_caption_presenter_t* presenter);

// Flushes active caption channels, releases any held video output references, and resets filter and channel context
// exclusively during module teardown, never during mid-session seeks or timeline resets.
void vw_caption_presenter_clear(vw_caption_presenter_t* presenter);

#endif  // VW_CAPTION_PRESENTER_H_
