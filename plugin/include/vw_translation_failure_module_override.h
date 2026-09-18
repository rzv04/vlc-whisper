#ifndef VW_TRANSLATION_FAILURE_MODULE_OVERRIDE_H_
#define VW_TRANSLATION_FAILURE_MODULE_OVERRIDE_H_

#include <stdint.h>

#include "vw_translation_failure_log.h"
#include "vw_worker_client.h"

#ifdef vw_worker_client_receive_frame
#undef vw_worker_client_receive_frame
#endif

static inline int vw_plugin_receive_frame_translation_live(vw_worker_client_t* client, uint32_t timeout_us,
                                                           vw_worker_recv_t* out, vw_benchmark_t* benchmark,
                                                           uint32_t* errors_received) {
  int status = vw_worker_client_receive_frame(client, timeout_us, out);
  if (status == VW_IPC_RECV_OK && out && out->type == VW_MSG_ERROR &&
      vw_plugin_record_translation_error(benchmark, &out->error)) {
    if (errors_received) (*errors_received)++;
    // Prevent the legacy generic worker-error branch from emitting a second VLC message for the same recoverable
    // translation diagnostic. The source-caption fallback arrives independently and is processed normally.
    out->type = 0;
  }
  return status;
}

#define vw_worker_client_receive_frame(client, timeout_us, out) \
  vw_plugin_receive_frame_translation_live((client), (timeout_us), (out), &sys->benchmark, &sys->errors_received)

#endif  // VW_TRANSLATION_FAILURE_MODULE_OVERRIDE_H_
