#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

// clang-format off
#include <vlc_common.h>
// clang-format on
#include <vlc_es.h>
#include <vlc_filter.h>
#include <vlc_input.h>
#include <vlc_subpicture.h>
#include <vlc_text_style.h>
#include <vlc_threads.h>
#include <vlc_vout.h>
#include <vlc_vout_osd.h>

#include "vw_caption_presenter.h"

#undef vlc_object_find_name
#undef vlc_object_release
#undef vlc_object_hold
#undef vlc_list_children
#undef vlc_list_release

// Globals for tracking mock calls
static int g_flush_calls = 0;
static int g_flush_channel = -1;
static int g_register_spu_calls = 0;
static int g_mock_register_channel_return = 42;
static int g_put_subpicture_calls = 0;
static int g_osd_text_calls = 0;
static int64_t g_mock_mdate = 100000000LL;  // 100s

vlc_tick_t mdate(void) { return (vlc_tick_t)g_mock_mdate; }

int vout_RegisterSubpictureChannel(vout_thread_t* vout) {
  (void)vout;
  g_register_spu_calls++;
  return g_mock_register_channel_return;
}

void vout_PutSubpicture(vout_thread_t* vout, subpicture_t* subpic) {
  (void)vout;
  g_put_subpicture_calls++;
  if (subpic) {
    if (subpic->p_region) {
      if (subpic->p_region->p_text) {
        text_segment_Delete(subpic->p_region->p_text);
      }
      subpicture_region_Delete(subpic->p_region);
    }
    subpicture_Delete(subpic);
  }
}

void vout_FlushSubpictureChannel(vout_thread_t* vout, int channel) {
  (void)vout;
  g_flush_calls++;
  g_flush_channel = channel;
}

subpicture_t* subpicture_New(const subpicture_updater_t* updater) {
  (void)updater;
  subpicture_t* subpic = calloc(1, sizeof(subpicture_t));
  return subpic;
}

void subpicture_Delete(subpicture_t* subpic) { free(subpic); }

subpicture_region_t* subpicture_region_New(const video_format_t* p_fmt) {
  subpicture_region_t* region = calloc(1, sizeof(subpicture_region_t));
  if (region && p_fmt) {
    region->fmt = *p_fmt;
  }
  return region;
}

void subpicture_region_Delete(subpicture_region_t* region) { free(region); }

text_segment_t* text_segment_New(const char* text) {
  text_segment_t* seg = calloc(1, sizeof(text_segment_t));
  if (seg && text) {
    seg->psz_text = strdup(text);
  }
  return seg;
}

void text_segment_Delete(text_segment_t* seg) {
  if (seg) {
    free(seg->psz_text);
    free(seg);
  }
}

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
  g_osd_text_calls++;
}

int main(void) {
  // Test 1: NULL text handling
  assert(!vw_caption_presenter_display(NULL, NULL, 1000000LL));

  // Test 2: Invalid duration handling
  assert(!vw_caption_presenter_display(NULL, "Test", 0));
  assert(!vw_caption_presenter_display(NULL, "Test", -500));

  // Test 3: Standalone display mode (without live VLC object hierarchy)
  assert(vw_caption_presenter_display(NULL, "OSD Caption", 2000000LL));

  // Test 4: Segment presenter functions in standalone mode (no filter context)
  vw_caption_presenter_t presenter = {.spu_channel_id = -1, .spu_channel_registered = false};
  vw_caption_segment_t segment = {.start_pts_us = 1000000LL,
                                  .end_pts_us = 3000000LL,
                                  .segment_id = 1,
                                  .is_final = true,
                                  .text_utf8 = "Hello Whisper AI"};

  assert(vw_caption_presenter_show_segment(&presenter, &segment, 1000000LL));

  // Test 5: NULL segment handling
  assert(!vw_caption_presenter_show_segment(&presenter, NULL, 0));

  vw_caption_segment_t empty_seg = {.text_utf8 = NULL};
  assert(!vw_caption_presenter_show_segment(&presenter, &empty_seg, 0));

  // Test 6: SPU subpicture channel registration and rendering
  filter_t fake_filter = {.obj.object_type = "vout"};
  vw_caption_presenter_t spu_presenter = {
      .p_filter_ctx = &fake_filter, .spu_channel_id = -1, .spu_channel_registered = false};

  g_register_spu_calls = 0;
  g_put_subpicture_calls = 0;
  g_mock_register_channel_return = 42;

  assert(vw_caption_presenter_show_segment(&spu_presenter, &segment, 1000000LL));
  assert(g_register_spu_calls == 1);
  assert(spu_presenter.spu_channel_id == 42);
  assert(spu_presenter.spu_channel_registered == true);
  assert(g_put_subpicture_calls == 1);

  // Subsequent call reuses already-registered channel
  assert(vw_caption_presenter_show_segment(&spu_presenter, &segment, 2000000LL));
  assert(g_register_spu_calls == 1);  // No duplicate registration call
  assert(g_put_subpicture_calls == 2);

  // Test 7: SPU channel registration failure falls back to OSD gracefully
  vw_caption_presenter_t fallback_presenter = {
      .p_filter_ctx = &fake_filter, .spu_channel_id = -1, .spu_channel_registered = false};
  g_mock_register_channel_return = -1;  // Simulate SPU channel registration failure
  g_osd_text_calls = 0;
  g_put_subpicture_calls = 0;

  assert(vw_caption_presenter_show_segment(&fallback_presenter, &segment, 1000000LL));
  assert(fallback_presenter.spu_channel_id == -1);
  assert(fallback_presenter.spu_channel_registered == false);
  assert(g_osd_text_calls == 1);  // OSD fallback was used
  assert(g_put_subpicture_calls == 0);

  // Test 8: Blank presenter flushes both SPU channel and OSD channel
  g_flush_calls = 0;
  g_flush_channel = -1;
  vw_caption_presenter_blank(&spu_presenter);
  assert(spu_presenter.p_filter_ctx == &fake_filter);
  assert(g_flush_calls >= 2);  // SPU channel 42 + OSD channel 1

  // Test 9: Clear presenter resets filter context and SPU channel
  vw_caption_presenter_clear(&spu_presenter);
  assert(spu_presenter.p_filter_ctx == NULL);
  assert(spu_presenter.spu_channel_id == -1);
  assert(spu_presenter.spu_channel_registered == false);

  return 0;
}
