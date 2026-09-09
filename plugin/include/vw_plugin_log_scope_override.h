#ifndef VW_PLUGIN_LOG_SCOPE_OVERRIDE_H_
#define VW_PLUGIN_LOG_SCOPE_OVERRIDE_H_

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "vw_benchmark.h"
#include "vw_caption_presenter.h"
#include "vw_log.h"
#include "vw_platform.h"
#include "vw_queue.h"
#include "vw_worker_client.h"

typedef struct vlc_object_t vlc_object_t;
void vlc_object_release(vlc_object_t* obj);

// Close-path state is thread-local because VLC can destroy independent filter instances concurrently on different
// threads; each teardown must retain only its own presenter context and SHUTDOWN suppression state.
static _Thread_local vw_caption_presenter_t* vw_plugin_teardown_presenter = NULL;
static _Thread_local void* vw_plugin_teardown_filter_ctx = NULL;
static _Thread_local bool vw_plugin_teardown_shutdown_sent = false;
static _Thread_local vw_benchmark_t* vw_plugin_teardown_benchmark = NULL;
static _Thread_local uint32_t* vw_plugin_teardown_frames = NULL;
static _Thread_local uint32_t* vw_plugin_teardown_segments = NULL;
static _Thread_local uint32_t* vw_plugin_teardown_status = NULL;
static _Thread_local uint32_t* vw_plugin_teardown_errors = NULL;

// Binds instance-owned close counters after sender shutdown so synchronous worker draining updates the same accounting
// as normal reception without introducing concurrent writes.
static inline void vw_plugin_bind_close_accounting(vw_benchmark_t* benchmark, uint32_t* frames, uint32_t* segments,
                                                   uint32_t* status, uint32_t* errors) {
  vw_plugin_teardown_benchmark = benchmark;
  vw_plugin_teardown_frames = frames;
  vw_plugin_teardown_segments = segments;
  vw_plugin_teardown_status = status;
  vw_plugin_teardown_errors = errors;
}

// Maps legacy anonymous sink removal to the current plugin instance while preserving explicit registrations. This keeps
// multi-instance teardown order-independent without exposing freed VLC objects to later log callbacks.
static inline void vw_plugin_log_set_sink_scoped(vw_log_sink_fn sink, void* user_data, void* instance) {
  if (sink == NULL && user_data == NULL) {
    vw_log_set_sink(NULL, instance);
    vw_plugin_teardown_presenter = NULL;
    vw_plugin_teardown_filter_ctx = NULL;
    vw_plugin_teardown_shutdown_sent = false;
    vw_plugin_bind_close_accounting(NULL, NULL, NULL, NULL, NULL);
  } else {
    vw_log_set_sink(sink, user_data);
  }
}

// Captures the presenter's live filter context before normal teardown clears it, allowing a final MEDIA_END caption to
// be rendered later during synchronous close draining.
static inline void vw_plugin_presenter_capture_clear(vw_caption_presenter_t* presenter) {
  vw_plugin_teardown_presenter = presenter;
  vw_plugin_teardown_filter_ctx = presenter ? presenter->p_filter_ctx : NULL;
  vw_plugin_teardown_shutdown_sent = false;
  vw_caption_presenter_clear(presenter);
}

// Releases presenter-held VLC references and resets teardown state without flushing the newly queued final caption,
// allowing VLC to display it until its finite stop time.
static inline void vw_plugin_presenter_release_preserving_caption(vw_caption_presenter_t* presenter) {
  if (!presenter) return;
  vw_caption_presenter_clear_model_progress(presenter);
  presenter->has_pending = false;
  if (presenter->p_held_vout) {
    vlc_object_release((vlc_object_t*)presenter->p_held_vout);
    presenter->p_held_vout = NULL;
  }
  presenter->p_filter_ctx = NULL;
  presenter->spu_channel_id = -1;
  presenter->spu_channel_registered = false;
}

typedef void (*vw_plugin_close_audio_observer_fn)(const vw_audio_chunk_t* chunk, void* user_data);

// Drains callback-queued live PCM through the worker client before media-end teardown, preserving counters and allowing
// optional successful-send observation without touching the realtime producer.
static inline bool vw_plugin_drain_close_audio(vw_worker_client_t* client, vw_spsc_queue_t* queue,
                                               uint64_t* chunks_sent, vw_plugin_close_audio_observer_fn observer,
                                               void* user_data) {
  if (!client || !queue) return false;
  vw_audio_chunk_t chunk;
  while (vw_spsc_queue_pop(queue, &chunk)) {
    if (!vw_worker_client_send_audio(client, &chunk)) return false;
    if (chunks_sent) (*chunks_sent)++;
    if (observer) observer(&chunk, user_data);
  }
  return true;
}

// Records each final close-path PCM chunk in the active benchmark after successful worker delivery while preserving
// benchmark timing and audio-accounting consistency.
static inline void vw_plugin_record_close_audio(const vw_audio_chunk_t* chunk, void* user_data) {
  vw_benchmark_t* benchmark = (vw_benchmark_t*)user_data;
  if (!chunk || !benchmark) return;
  vw_benchmark_record_audio(benchmark, chunk->start_pts_us, chunk->duration_us, vw_platform_get_monotonic_time_us());
}

// Joins the sender thread, then drains any remaining live-session SPSC PCM to the worker while preserving chunk and
// benchmark accounting before MEDIA_END.
static inline void vw_close_join(vw_thread_t thread, vw_worker_client_t* client, vw_spsc_queue_t* queue,
                                 uint64_t* chunks_sent, vw_benchmark_t* benchmark) {
  vw_platform_thread_join(thread);
  vw_plugin_teardown_benchmark = benchmark;
  if (!client || !queue || !client->session_active || vw_worker_client_is_source_active(client)) return;
  if (!vw_plugin_drain_close_audio(client, queue, chunks_sent, vw_plugin_record_close_audio, benchmark)) {
    vw_log_event(VW_LOG_LEVEL_WARN, "PLUGIN_CLOSE_DRAIN", "failed to deliver final queued PCM before media-end stop");
  }
}

// Converts close-path STOP into MEDIA_END, orders SHUTDOWN after it, drains worker output to transport closure, and
// renders any final caption before disconnecting the client.
static inline void vw_plugin_stop_session_scoped(vw_worker_client_t* client, uint16_t reason) {
  if (!vw_plugin_teardown_presenter) {
    vw_worker_client_stop_session(client, reason);
    return;
  }

  (void)reason;
  vw_caption_presenter_t* presenter = vw_plugin_teardown_presenter;
  void* filter_ctx = vw_plugin_teardown_filter_ctx;
  bool received_tail = false;
  bool source_active = vw_worker_client_is_source_active(client);

  vw_worker_client_stop_session(client, VW_CTRL_REASON_MEDIA_END);
  vw_worker_client_shutdown(client);
  vw_plugin_teardown_shutdown_sent = true;

  int64_t started_us = vw_platform_get_monotonic_time_us();
  // IPC EOF after ordered SHUTDOWN is the completion barrier. The watchdog is only a hung-worker safety bound,
  // matching the existing quality runner's 120-second completion allowance for slow CPU/model inference.
  int64_t deadline_us = started_us >= 0 ? started_us + 120000000LL : -1;
  for (;;) {
    uint32_t timeout_us = 250000U;
    if (deadline_us >= 0) {
      int64_t now_us = vw_platform_get_monotonic_time_us();
      if (now_us < 0 || now_us >= deadline_us) break;
      int64_t remaining_us = deadline_us - now_us;
      if (remaining_us < (int64_t)timeout_us) timeout_us = (uint32_t)remaining_us;
    } else {
      break;
    }

    vw_worker_recv_t recv;
    int recv_status = vw_worker_client_receive_frame(client, timeout_us, &recv);
    if (recv_status == VW_IPC_RECV_FATAL) break;
    if (recv_status != VW_IPC_RECV_OK) continue;
    if (vw_plugin_teardown_frames) (*vw_plugin_teardown_frames)++;
    vw_benchmark_record_frame(vw_plugin_teardown_benchmark);
    if (recv.type == VW_MSG_STATUS) {
      if (vw_plugin_teardown_status) (*vw_plugin_teardown_status)++;
      vw_benchmark_update_status(vw_plugin_teardown_benchmark, &recv.status);
    } else if (recv.type == VW_MSG_ERROR) {
      if (vw_plugin_teardown_errors) (*vw_plugin_teardown_errors)++;
    }
    if (recv.type != VW_MSG_CAPTION_SEGMENT ||
        memcmp(recv.segment.session_id.bytes, client->session_id, VW_SESSION_ID_BYTES) != 0) {
      continue;
    }

    if (vw_plugin_teardown_segments) (*vw_plugin_teardown_segments)++;
    int64_t now_us = vw_platform_get_monotonic_time_us();
    vw_benchmark_record_caption_received(vw_plugin_teardown_benchmark, &recv.segment, now_us, source_active);
    if (recv.segment.translation_attempted) {
      vw_benchmark_record_translation(vw_plugin_teardown_benchmark, recv.segment.translation_tier,
                                      recv.segment.translation_latency_us, recv.segment.translated_text_bytes > 0);
    }
    presenter->p_filter_ctx = filter_ctx;
    if (vw_caption_presenter_show_segment(presenter, &recv.segment, -1, false)) {
      received_tail = true;
      vw_benchmark_record_caption_sent(vw_plugin_teardown_benchmark, now_us);
    } else {
      vw_benchmark_record_caption_filtered(vw_plugin_teardown_benchmark, false, false, true);
    }
  }

  if (received_tail) {
    presenter->p_filter_ctx = filter_ctx;
    (void)vw_caption_presenter_flush(presenter, -1, false);
    vw_plugin_presenter_release_preserving_caption(presenter);
  }

  vw_plugin_teardown_presenter = NULL;
  vw_plugin_teardown_filter_ctx = NULL;
  vw_plugin_bind_close_accounting(NULL, NULL, NULL, NULL, NULL);
}

// Suppresses the close path's duplicate SHUTDOWN after media-end draining while preserving ordinary shutdown behavior
// for respawn and teardown paths that did not perform that drain.
static inline void vw_plugin_shutdown_scoped(vw_worker_client_t* client) {
  if (vw_plugin_teardown_shutdown_sent) {
    vw_plugin_teardown_shutdown_sent = false;
    return;
  }
  vw_plugin_teardown_presenter = NULL;
  vw_plugin_teardown_filter_ctx = NULL;
  vw_worker_client_shutdown(client);
}

#ifdef VW_PLUGIN_LOG_SCOPE_OVERRIDE
#define vw_log_set_sink(sink, user_data) vw_plugin_log_set_sink_scoped((sink), (user_data), obj)
#define vw_caption_presenter_clear(presenter) vw_plugin_presenter_capture_clear((presenter))
#define vw_platform_thread_join(t) vw_close_join((t), sys->client, sys->queue, &sys->chunks_sent, &sys->benchmark)
#define vw_worker_client_stop_session(client, reason) vw_plugin_stop_session_scoped((client), (reason))
#define vw_worker_client_shutdown(client) vw_plugin_shutdown_scoped((client))
#endif

#endif  // VW_PLUGIN_LOG_SCOPE_OVERRIDE_H_
