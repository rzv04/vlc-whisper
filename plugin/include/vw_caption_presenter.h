#ifndef VW_CAPTION_PRESENTER_H_
#define VW_CAPTION_PRESENTER_H_

#include <stdbool.h>
#include <stdint.h>

#include "vw_protocol_types.h"

typedef struct vw_caption_presenter {
  void* p_filter_ctx;
  int spu_channel_id;           // Registered VLC SPU channel ID; -1 when unregistered or unavailable.
  bool spu_channel_registered;  // True if spu_channel_id >= 0 and successfully registered with vout.
} vw_caption_presenter_t;

// Renders timed text directly onto the active VLC video output surface using OSD rendering as a fallback path.
bool vw_caption_presenter_display(void* p_filter, const char* text, int64_t duration_us);

// Dispatches a transcription segment to VLC SPU subpicture channel (or OSD fallback) using media timestamp mapping.
bool vw_caption_presenter_show_segment(vw_caption_presenter_t* presenter, const vw_caption_segment_t* segment,
                                       int64_t input_time_us);

// Blanks active SPU and OSD caption channels while preserving the filter context, safe for mid-session seeks.
void vw_caption_presenter_blank(vw_caption_presenter_t* presenter);

// Flushes caption channels and resets filter and channel context during module teardown, never mid-session.
void vw_caption_presenter_clear(vw_caption_presenter_t* presenter);

#endif  // VW_CAPTION_PRESENTER_H_
