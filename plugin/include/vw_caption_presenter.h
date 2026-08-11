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

// Blanks the current OSD overlay (empty text on the caption channel) but KEEPS the filter
// context, so later segments still render. Safe mid-session, e.g. to erase captions on a seek
// before the restarted session emits new ones. No-op when the presenter has no filter context.
void vw_caption_presenter_blank(vw_caption_presenter_t* presenter);

// Clears active caption overlays from the video output surface by issuing an empty text payload to OSD rendering,
// AND resets the presenter context (p_filter_ctx = NULL). Teardown-only: after this, show_segment/blank become
// no-ops, so call only when the module (and its filter context) is going away — never mid-session.
void vw_caption_presenter_clear(vw_caption_presenter_t* presenter);

#endif  // VW_CAPTION_PRESENTER_H_
