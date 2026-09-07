#ifndef VW_PLUGIN_LOG_SCOPE_OVERRIDE_H_
#define VW_PLUGIN_LOG_SCOPE_OVERRIDE_H_

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#endif
// clang-format off
#include <vlc_common.h>
// clang-format on

#include "vw_caption_presenter.h"
#include "vw_log.h"
#include "vw_platform.h"
#include "vw_worker_client.h"

// Close-path state is thread-local because VLC can destroy independent filter instances concurrently on different
// threads; each teardown must retain only its own presenter context and SHUTDOWN suppression state.
static _Thread_local vw_caption_presenter_t* vw_plugin_teardown_presenter = NULL;
static _Thread_local void* vw_plugin_teardown_filter_ctx = NULL;
static _Thread_local bool vw_plugin_teardown_shutdown_sent = false;

// Maps legacy anonymous sink removal to the current plugin instance while preserving explicit registrations. This keeps
// multi-instance teardown order-independent without exposing freed VLC objects to later log callbacks.
static inline void vw_plugin_log_set_sink_scoped(vw_log_sink_fn sink, void* user_data, void* instance) {
  if (sink == NULL && user_data == NULL) {
    vw_log_set_sink(NULL, instance);
    vw_plugin_teardown_presenter = NULL;
    vw_plugin_teardown_filter_ctx = NULL;
    vw_plugin_teardown_shutdown_sent = false;
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

  vw_worker_client_stop_session(client, VW_CTRL_REASON_MEDIA_END);
  vw_worker_client_shutdown(client);
  vw_plugin_teardown_shutdown_sent = true;

  int64_t started_us = vw_platform_get_monotonic_time_us();
  int64_t deadline_us = started_us >= 0 ? started_us + 3000000LL : -1;
  for (unsigned int frames = 0; frames < 64U; frames++) {
    uint32_t timeout_us = 250000U;
    if (deadline_us >= 0) {
      int64_t now_us = vw_platform_get_monotonic_time_us();
      if (now_us < 0 || now_us >= deadline_us) break;
      int64_t remaining_us = deadline_us - now_us;
      if (remaining_us < (int64_t)timeout_us) timeout_us = (uint32_t)remaining_us;
    } else if (frames > 0U) {
      break;
    }

    vw_worker_recv_t recv;
    int recv_status = vw_worker_client_receive_frame(client, timeout_us, &recv);
    if (recv_status == VW_IPC_RECV_FATAL) break;
    if (recv_status != VW_IPC_RECV_OK) continue;
    if (recv.type != VW_MSG_CAPTION_SEGMENT ||
        memcmp(recv.segment.session_id.bytes, client->session_id, VW_SESSION_ID_BYTES) != 0) {
      continue;
    }

    presenter->p_filter_ctx = filter_ctx;
    if (vw_caption_presenter_show_segment(presenter, &recv.segment, -1, false)) {
      received_tail = true;
    }
  }

  if (received_tail) {
    presenter->p_filter_ctx = filter_ctx;
    (void)vw_caption_presenter_flush(presenter, -1, false);
    vw_plugin_presenter_release_preserving_caption(presenter);
  }

  vw_plugin_teardown_presenter = NULL;
  vw_plugin_teardown_filter_ctx = NULL;
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
#define vw_worker_client_stop_session(client, reason) vw_plugin_stop_session_scoped((client), (reason))
#define vw_worker_client_shutdown(client) vw_plugin_shutdown_scoped((client))
#endif

#endif  // VW_PLUGIN_LOG_SCOPE_OVERRIDE_H_
