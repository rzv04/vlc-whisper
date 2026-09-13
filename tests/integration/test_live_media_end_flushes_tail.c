#include <string.h>

#include "../../plugin/src/vw_queue.c"
#include "vw_caption_presenter.h"
#include "vw_test.h"
#include "vw_test_worker_stubs.h"

typedef struct vlc_object_t vlc_object_t;

static int g_presenter_clear_calls = 0;
static int g_presenter_show_calls = 0;
static int g_presenter_flush_calls = 0;

void vlc_object_release(vlc_object_t* obj) { (void)obj; }

void vw_caption_presenter_clear_model_progress(vw_caption_presenter_t* presenter) {
  if (!presenter) return;
  presenter->p_model_progress_held_vout = NULL;
  presenter->model_progress_channel_id = -1;
  presenter->model_progress_channel_registered = false;
}

void vw_caption_presenter_clear(vw_caption_presenter_t* presenter) {
  g_presenter_clear_calls++;
  if (!presenter) return;
  presenter->has_pending = false;
  presenter->p_held_vout = NULL;
  presenter->p_filter_ctx = NULL;
  presenter->spu_channel_id = -1;
  presenter->spu_channel_registered = false;
}

bool vw_caption_presenter_show_segment(vw_caption_presenter_t* presenter, const vw_caption_segment_t* segment,
                                       int64_t input_time_us, bool media_timeline) {
  (void)input_time_us;
  (void)media_timeline;
  if (!presenter || !presenter->p_filter_ctx || !segment || !segment->text_utf8) return false;
  g_presenter_show_calls++;
  presenter->has_pending = true;
  presenter->pending_segment = *segment;
  return true;
}

bool vw_caption_presenter_flush(vw_caption_presenter_t* presenter, int64_t input_time_us, bool media_timeline) {
  (void)input_time_us;
  (void)media_timeline;
  if (!presenter || !presenter->has_pending) return false;
  g_presenter_flush_calls++;
  presenter->has_pending = false;
  return true;
}

#define VW_PLUGIN_LOG_SCOPE_OVERRIDE 1
#include "vw_plugin_log_scope_override.h"
#include "vw_test_worker_harness.h"

int main(void) {
  vw_test_worker_stubs_reset();
  g_presenter_clear_calls = 0;
  g_presenter_show_calls = 0;
  g_presenter_flush_calls = 0;

  vw_test_worker_fixture_t fixture;
  bool started = vw_test_worker_fixture_start(&fixture, "live-tail");
  vw_test_check_true("stub worker starts", started);
  if (started) {
    vw_test_check_true("live session starts", vw_worker_client_start_session(fixture.client, 0, "tiny", NULL));

    vw_spsc_queue_t* close_queue = vw_spsc_queue_create(4);
    vw_test_check_true("close-path SPSC queue is created", close_queue != NULL);
    if (close_queue) {
      for (int i = 0; i < 2; i++) {
        vw_audio_chunk_t chunk;
        memset(&chunk, 0, sizeof(chunk));
        chunk.start_pts_us = (int64_t)i * 512000;
        chunk.duration_us = 512000;
        chunk.sample_rate = 16000;
        chunk.channels = 1;
        chunk.bytes = 16384;
        vw_test_check_true("callback PCM enters close-path SPSC queue", vw_spsc_queue_push(close_queue, &chunk));
      }

      vw_test_check_true("queued callback audio has not reached worker before close drain",
                         vw_test_whisper_transcribe_calls() == 0);
      uint64_t close_chunks_sent = 0;
      vw_test_check_true("close path drains queued PCM through worker client",
                         vw_plugin_drain_close_audio(fixture.client, close_queue, &close_chunks_sent, NULL, NULL));
      vw_test_check_true("both queued close-path chunks are delivered", close_chunks_sent == 2);
      vw_audio_chunk_t leftover;
      vw_test_check_true("close-path SPSC queue is empty before MEDIA_END",
                         vw_spsc_queue_pop(close_queue, &leftover) == NULL);
      vw_spsc_queue_destroy(close_queue);
    }

    vw_platform_sleep_ms(75);
    vw_test_check_true("audio remains below progressive startup threshold before media end",
                       vw_test_whisper_transcribe_calls() == 0);

    vw_caption_presenter_t presenter;
    memset(&presenter, 0, sizeof(presenter));
    presenter.p_filter_ctx = (void*)0x1;
    presenter.spu_channel_id = -1;
    presenter.model_progress_channel_id = -1;

    // Mirror normal plugin close ordering after sender join/final SPSC drain: clear presenter, then issue legacy
    // STOP(0). The source-local close adapter preserves enough context to turn this into MEDIA_END and render its tail.
    vw_caption_presenter_clear(&presenter);
    vw_test_check_true("normal close clears presenter before worker stop", g_presenter_clear_calls == 1);
    vw_test_check_true("presenter context is initially cleared", presenter.p_filter_ctx == NULL);

    vw_worker_client_stop_session(fixture.client, 0);
    vw_test_check_true("normal close path flushes held-back residual speech", vw_test_whisper_transcribe_calls() > 0);
    vw_test_check_true("tail caption crosses worker-client-presenter seam", g_presenter_show_calls > 0);
    vw_test_check_true("tail caption is flushed for presentation before disconnect", g_presenter_flush_calls > 0);
    vw_test_check_true("presenter context is released after final tail delivery", presenter.p_filter_ctx == NULL);

    // Mirror the original close call that follows STOP. The adapter suppresses this duplicate because SHUTDOWN was
    // already ordered immediately after MEDIA_END to give the synchronous close drain a bounded EOF.
    vw_worker_client_shutdown(fixture.client);
  }
  if (fixture.thread_started) {
    vw_test_check_true("stub worker shuts down cleanly", vw_test_worker_fixture_shutdown(&fixture) == 0);
  }
  return vw_test_finish("test_live_media_end_flushes_tail");
}
