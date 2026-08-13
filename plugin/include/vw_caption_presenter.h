#ifndef VW_CAPTION_PRESENTER_H_
#define VW_CAPTION_PRESENTER_H_

#include <stdbool.h>
#include <stdint.h>

#include "vw_protocol_types.h"

typedef struct vw_caption_presenter {
  void* p_filter_ctx;
} vw_caption_presenter_t;

// Renders timed subtitle text directly onto the active VLC video output surface using OSD rendering, walking the
// parent object hierarchy to locate the target vout thread.
bool vw_caption_presenter_display(void* p_filter, const char* text, int64_t duration_us);

// Dispatches a validated timed transcription segment structure to the video output overlay, extracting its
// microsecond duration bounds and formatting UTF-8 text for display.
bool vw_caption_presenter_show_segment(vw_caption_presenter_t* presenter, const vw_caption_segment_t* segment);

// Blanks the active OSD overlay while retaining the filter context, allowing later caption
// segments to render after a mid-session seek. No-op when no filter context is set.
void vw_caption_presenter_blank(vw_caption_presenter_t* presenter);

// Blanks the active OSD overlay and resets the filter context during module teardown,
// preventing subsequent segment rendering through the presenter. Never mid-session.
void vw_caption_presenter_clear(vw_caption_presenter_t* presenter);

#endif  // VW_CAPTION_PRESENTER_H_
