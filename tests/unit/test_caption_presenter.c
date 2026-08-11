#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <vlc_common.h>
#include <vlc_filter.h>
#include <vlc_input.h>
#include <vlc_vout.h>
#include <vlc_vout_osd.h>

#include "vw_caption_presenter.h"

#undef vlc_object_find_name
#undef vlc_object_release
#undef vlc_object_hold
#undef vlc_list_children
#undef vlc_list_release

// Stubs for VLC API symbols required when linking standalone unit test binary
vlc_object_t* vlc_object_find_name(vlc_object_t* obj, const char* name) {
  (void)obj;
  (void)name;
  return NULL;
}

void vlc_object_release(vlc_object_t* obj) { (void)obj; }

void* vlc_object_hold(vlc_object_t* obj) { return obj; }

vlc_list_t* vlc_list_children(vlc_object_t* obj) {
  (void)obj;
  return NULL;
}

void vlc_list_release(vlc_list_t* list) { (void)list; }

int input_Control(input_thread_t* p_input, int i_query, ...) {
  (void)p_input;
  (void)i_query;
  return VLC_EGENERIC;
}

void vout_OSDText(vout_thread_t* vout, int channel, int position, vlc_tick_t duration, const char* text) {
  (void)vout;
  (void)channel;
  (void)position;
  (void)duration;
  (void)text;
}

int main(void) {
  // Test 1: NULL text handling
  assert(!vw_caption_presenter_display(NULL, NULL, 1000000LL));

  // Test 2: Invalid duration handling
  assert(!vw_caption_presenter_display(NULL, "Test", 0));
  assert(!vw_caption_presenter_display(NULL, "Test", -500));

  // Test 3: Standalone display mode (without live VLC object hierarchy)
  assert(vw_caption_presenter_display(NULL, "OSD Caption", 2000000LL));

  // Test 4: Segment presenter functions
  vw_caption_presenter_t presenter = {0};
  vw_caption_segment_t segment = {.start_pts_us = 1000000LL,
                                  .end_pts_us = 3000000LL,
                                  .segment_id = 1,
                                  .is_final = true,
                                  .text_utf8 = "Hello Whisper AI"};

  assert(vw_caption_presenter_show_segment(&presenter, &segment));

  // Test 5: NULL segment handling
  assert(!vw_caption_presenter_show_segment(&presenter, NULL));

  vw_caption_segment_t empty_seg = {.text_utf8 = NULL};
  assert(!vw_caption_presenter_show_segment(&presenter, &empty_seg));

  (void)segment;
  (void)empty_seg;

  // Test 6: Clear presenter (teardown-only: blanks AND resets context)
  filter_t fake_filter = {0};  // zeroed: find_vout's walk sees NULL object_type and NULL parent
  vw_caption_presenter_t ctx_presenter = {.p_filter_ctx = &fake_filter};
  vw_caption_presenter_blank(&ctx_presenter);  // mid-session blank: keeps context
  assert(ctx_presenter.p_filter_ctx == &fake_filter);
  vw_caption_presenter_clear(&ctx_presenter);  // teardown: resets context
  assert(ctx_presenter.p_filter_ctx == NULL);

  vw_caption_presenter_clear(&presenter);
  assert(presenter.p_filter_ctx == NULL);

  return 0;
}
