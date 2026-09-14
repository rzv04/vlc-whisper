#ifndef VW_TRANSLATION_FAILURE_LOG_H_
#define VW_TRANSLATION_FAILURE_LOG_H_

#include <stdbool.h>
#include <stdint.h>

#include "vw_benchmark.h"
#include "vw_protocol_types.h"

static inline bool vw_plugin_is_translation_error_code(uint32_t error_code) {
  return error_code == E_TRANSLATION_PROVIDER || error_code == E_TRANSLATION_TRANSPORT ||
         error_code == E_TRANSLATION_PARSE || error_code == E_TRANSLATION_DEADLINE || error_code == E_TRANSLATION_LOCAL;
}

// Routes worker-owned translation diagnostics into the plugin benchmark/logger path. Translation failures stay
// recoverable and are not reclassified from latency in the plugin.
static inline bool vw_plugin_record_translation_error(vw_benchmark_t* benchmark, const vw_msg_error_t* error) {
  if (!error || !error->recoverable || !vw_plugin_is_translation_error_code(error->error_code)) return false;
  vw_benchmark_record_translation_failure(benchmark, error->error_code, error->message);
  return true;
}

#endif  // VW_TRANSLATION_FAILURE_LOG_H_
