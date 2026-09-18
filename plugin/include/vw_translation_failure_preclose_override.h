#ifndef VW_TRANSLATION_FAILURE_PRECLOSE_OVERRIDE_H_
#define VW_TRANSLATION_FAILURE_PRECLOSE_OVERRIDE_H_

#include <stdint.h>

#include "vw_translation_failure_log.h"
#include "vw_worker_client.h"

// The existing log-scope preinclude defines these teardown bindings later in the same translation unit. Tentative
// declarations let the receive wrapper account for translation errors while that close-path helper is being parsed.
static _Thread_local vw_benchmark_t* vw_plugin_teardown_benchmark;
static _Thread_local uint32_t* vw_plugin_teardown_errors;

static inline int vw_plugin_receive_frame_translation_teardown(vw_worker_client_t* client, uint32_t timeout_us,
                                                               vw_worker_recv_t* out) {
  int status = vw_worker_client_receive_frame(client, timeout_us, out);
  if (status == VW_IPC_RECV_OK && out && out->type == VW_MSG_ERROR &&
      vw_plugin_record_translation_error(vw_plugin_teardown_benchmark, &out->error)) {
    if (vw_plugin_teardown_errors) (*vw_plugin_teardown_errors)++;
    // The diagnostic is fully consumed here. Hide it from the legacy generic-error branch to avoid duplicate VLC
    // logging and duplicate error counting; the following source caption remains a separate frame.
    out->type = 0;
  }
  return status;
}

#define vw_worker_client_receive_frame(client, timeout_us, out) \
  vw_plugin_receive_frame_translation_teardown((client), (timeout_us), (out))

#endif  // VW_TRANSLATION_FAILURE_PRECLOSE_OVERRIDE_H_
