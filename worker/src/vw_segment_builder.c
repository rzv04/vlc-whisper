#include "vw_segment_builder.h"

#include <stdlib.h>

struct vw_segment_builder {
  uint64_t next_segment_id;
};

vw_segment_builder_t *vw_segment_builder_create(void) {
  vw_segment_builder_t *b = (vw_segment_builder_t *)calloc(1, sizeof(vw_segment_builder_t));
  return b;
}

void vw_segment_builder_free(vw_segment_builder_t *builder) {
  if (builder) {
    free(builder);
  }
}

bool vw_segment_builder_push_hypothesis(vw_segment_builder_t *builder, const char *text, int64_t start_pts_us, int64_t end_pts_us) {
  (void)builder;
  (void)text;
  (void)start_pts_us;
  (void)end_pts_us;
  return true;
}
