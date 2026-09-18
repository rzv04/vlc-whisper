#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
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
#include "vw_paused_preview_policy.h"
#include "vw_test.h"
#include "vw_test_worker_harness.h"
#include "vw_test_worker_stubs.h"

#undef vlc_object_find_name
#undef vlc_object_release
#undef vlc_object_hold
#undef vlc_list_children
#undef vlc_list_release
#undef var_Get
#undef var_Inherit

static int g_put_subpicture_calls = 0;
static int g_flush_calls = 0;
static int g_register_spu_calls = 0;
static int64_t g_mock_mdate = 100000000LL;
static char g_last_subpic_text[128];
static bool g_last_subpic_b_ephemer = false;

vlc_tick_t mdate(void) { return (vlc_tick_t)g_mock_mdate; }

int vout_RegisterSubpictureChannel(vout_thread_t* vout) {
  (void)vout;
  g_register_spu_calls++;
  return 42;
}

void vout_PutSubpicture(vout_thread_t* vout, subpicture_t* subpic) {
  (void)vout;
  g_put_subpicture_calls++;
  if (subpic) {
    g_last_subpic_b_ephemer = subpic->b_ephemer;
    if (subpic->p_region && subpic->p_region->p_text) {
      snprintf(g_last_subpic_text, sizeof(g_last_subpic_text), "%s",
               subpic->p_region->p_text->psz_text ? subpic->p_region->p_text->psz_text : "");
      text_segment_Delete(subpic->p_region->p_text);
      subpic->p_region->p_text = NULL;
    }
    if (subpic->p_region) {
      subpicture_region_Delete(subpic->p_region);
      subpic->p_region = NULL;
    }
    subpicture_Delete(subpic);
  }
}

void vout_FlushSubpictureChannel(vout_thread_t* vout, int channel) {
  (void)vout;
  (void)channel;
  g_flush_calls++;
}

subpicture_t* subpicture_New(const subpicture_updater_t* updater) {
  (void)updater;
  return calloc(1, sizeof(subpicture_t));
}

void subpicture_Delete(subpicture_t* subpic) { free(subpic); }

subpicture_region_t* subpicture_region_New(const video_format_t* p_fmt) {
  subpicture_region_t* region = calloc(1, sizeof(subpicture_region_t));
  if (region && p_fmt) region->fmt = *p_fmt;
  return region;
}

void subpicture_region_Delete(subpicture_region_t* region) { free(region); }

text_segment_t* text_segment_New(const char* text) {
  text_segment_t* segment = calloc(1, sizeof(text_segment_t));
  if (segment && text) segment->psz_text = strdup(text);
  return segment;
}

void text_segment_Delete(text_segment_t* segment) {
  if (!segment) return;
  free(segment->psz_text);
  free(segment);
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

int input_Control(input_thread_t* input, int query, ...) {
  (void)input;
  (void)query;
  return VLC_EGENERIC;
}

int var_Get(vlc_object_t* obj, const char* name, vlc_value_t* value) {
  (void)obj;
  (void)name;
  if (value) value->f_float = 1.0f;
  return VLC_SUCCESS;
}

int var_Inherit(vlc_object_t* obj, const char* name, int type, vlc_value_t* value) {
  (void)obj;
  (void)name;
  (void)type;
  if (value) value->i_int = 1;
  return VLC_SUCCESS;
}

void vout_OSDText(vout_thread_t* vout, int channel, int position, vlc_tick_t duration, const char* text) {
  (void)vout;
  (void)channel;
  (void)position;
  (void)duration;
  (void)text;
}

static bool vw_test_receive_caption_at_or_after(vw_worker_client_t* client, int64_t target_us, vw_worker_recv_t* recv) {
  if (!client || !recv) return false;
  for (int attempt = 0; attempt < 30; attempt++) {
    int status = vw_worker_client_receive_frame(client, 100000, recv);
    if (status == VW_IPC_RECV_FATAL) return false;
    if (status != VW_IPC_RECV_OK || recv->type != VW_MSG_CAPTION_SEGMENT) continue;
    if (memcmp(recv->segment.session_id.bytes, client->session_id, VW_SESSION_ID_BYTES) != 0) continue;
    if (recv->segment.start_pts_us < target_us) continue;
    return true;
  }
  return false;
}

int main(void) {
  vw_paused_preview_state_t preview_state;
  vw_paused_preview_state_init(&preview_state);

  int64_t target_us = -1;
  vw_test_check_false("unavailable pause position does not create a stale preview target",
                      vw_paused_preview_capture_target(true, -1, &target_us));
  vw_paused_preview_arm(&preview_state);
  vw_paused_preview_state_reset_session(&preview_state);
  vw_test_check_false("new session clears any one-shot paused preview token", vw_paused_preview_take(&preview_state));

  vw_test_worker_stubs_reset();
  vw_test_worker_fixture_t fixture;
  bool started = vw_test_worker_fixture_start(&fixture, "paused-source-preview");
  vw_test_check_true("stub worker starts for paused source preview", started);

  if (started) {
    bool source_started =
        vw_worker_client_start_session(fixture.client, 0, "tiny", "file:///vw-paused-preview-stub.wav");
    vw_test_check_true("local source session starts", source_started);
    if (source_started) {
      vw_test_check_true("local source look-ahead is active", vw_worker_client_is_source_active(fixture.client));

      const int64_t seek_target_us = 9000000LL;
      uint8_t flags = vw_paused_preview_seek_flags(true, true);
      vw_test_check_true("paused source preview seek carries SEEK", (flags & VW_POSITION_FLAG_SEEK) != 0);
      vw_test_check_false("paused source preview keeps worker look-ahead unpaused",
                          (flags & VW_POSITION_FLAG_PAUSED) != 0);
      vw_test_check_true("paused source preview seek reaches worker",
                         vw_worker_client_send_position(fixture.client, seek_target_us, seek_target_us, 1.0f, flags));

      vw_worker_recv_t recv;
      memset(&recv, 0, sizeof(recv));
      bool received = vw_test_receive_caption_at_or_after(fixture.client, seek_target_us, &recv);
      vw_test_check_true("worker regenerates a cue at the paused seek target", received);
      if (received) {
        filter_t fake_filter = {.obj.object_type = "vout"};
        vw_caption_presenter_t presenter = {
            .p_filter_ctx = &fake_filter, .spu_channel_id = -1, .spu_channel_registered = false};
        g_put_subpicture_calls = 0;
        g_flush_calls = 0;
        g_register_spu_calls = 0;
        g_last_subpic_text[0] = '\0';
        g_last_subpic_b_ephemer = false;

        vw_paused_preview_arm(&preview_state);
        vw_test_check_true("first regenerated cue consumes paused preview token", vw_paused_preview_take(&preview_state));
        vw_test_check_true("regenerated source cue renders through paused presenter",
                           vw_caption_presenter_show_paused(&presenter, &recv.segment));
        vw_test_check_true("paused presenter renders exactly one replacement", g_put_subpicture_calls == 1);
        vw_test_check_true("paused replacement uses persistent SPU semantics", g_last_subpic_b_ephemer);
        vw_test_check_true("paused replacement carries regenerated worker text",
                           strcmp(g_last_subpic_text, "stub speech") == 0);
        vw_caption_presenter_clear(&presenter);
      }

      if (fixture.client->session_active) {
        vw_worker_client_stop_session(fixture.client, VW_CTRL_REASON_USER_STOP);
      }
    }
  }

  if (fixture.thread_started) {
    vw_test_check_true("stub worker shuts down cleanly", vw_test_worker_fixture_shutdown(&fixture) == 0);
  }
  return vw_test_finish("test_paused_source_preview");
}
