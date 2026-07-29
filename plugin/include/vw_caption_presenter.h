#ifndef VW_CAPTION_PRESENTER_H_
#define VW_CAPTION_PRESENTER_H_

#include <stdbool.h>
#include <stdint.h>

#include "vw_protocol_types.h"

typedef struct vw_caption_presenter {
  void *vlc_subpicture;
} vw_caption_presenter_t;

bool vw_caption_presenter_show_segment(vw_caption_presenter_t *presenter, const vw_caption_segment_t *segment);
void vw_caption_presenter_clear(vw_caption_presenter_t *presenter);

#endif  // VW_CAPTION_PRESENTER_H_
