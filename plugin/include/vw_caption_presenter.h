#ifndef VW_CAPTION_PRESENTER_H_
#define VW_CAPTION_PRESENTER_H_

#include <stdbool.h>
#include <stdint.h>

#include "vw_protocol_types.h"

typedef enum {
  VW_PRESENTER_MODE_AUTO = 0,
  VW_PRESENTER_MODE_SPU,
  VW_PRESENTER_MODE_OSD,
} vw_presenter_mode_t;

typedef struct vw_caption_presenter {
  void *vlc_subpicture;
} vw_caption_presenter_t;

// Displays timed text using native SPU subpicture channel or OSD fallback depending on mode and availability.
bool vw_caption_presenter_display(void *p_filter, const char *text, int64_t duration_us, vw_presenter_mode_t mode);

// Displays a caption segment using default AUTO mode for native SPU or OSD fallback presentation.
bool vw_caption_presenter_show_segment(vw_caption_presenter_t *presenter, const vw_caption_segment_t *segment);

// Clears all currently displayed subpictures or OSD caption overlays from the presentation state.
void vw_caption_presenter_clear(vw_caption_presenter_t *presenter);

#endif  // VW_CAPTION_PRESENTER_H_
