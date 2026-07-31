#ifndef VW_CAPTION_PRESENTER_H_
#define VW_CAPTION_PRESENTER_H_

#include <stdbool.h>
#include <stdint.h>

#include "vw_protocol_types.h"

typedef struct vw_caption_presenter {
  void *p_filter_ctx;
} vw_caption_presenter_t;

// Displays timed text onto the active video output overlay using VLC OSD rendering.
bool vw_caption_presenter_display(void *p_filter, const char *text, int64_t duration_us);

// Displays a caption segment on the video output overlay.
bool vw_caption_presenter_show_segment(vw_caption_presenter_t *presenter, const vw_caption_segment_t *segment);

// Clears all currently displayed caption overlays from the presenter context state.
void vw_caption_presenter_clear(vw_caption_presenter_t *presenter);

#endif  // VW_CAPTION_PRESENTER_H_
