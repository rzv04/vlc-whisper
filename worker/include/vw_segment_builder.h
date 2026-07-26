#ifndef VW_SEGMENT_BUILDER_H_
#define VW_SEGMENT_BUILDER_H_

#include <stdbool.h>
#include <stdint.h>
#include "vw_protocol_types.h"

typedef struct vw_segment_builder vw_segment_builder_t;

vw_segment_builder_t *vw_segment_builder_create(void);
void vw_segment_builder_free(vw_segment_builder_t *builder);
bool vw_segment_builder_push_hypothesis(vw_segment_builder_t *builder, const char *text, int64_t start_pts_us, int64_t end_pts_us);

#endif // VW_SEGMENT_BUILDER_H_
