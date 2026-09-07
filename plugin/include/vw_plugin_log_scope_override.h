#ifndef VW_PLUGIN_LOG_SCOPE_OVERRIDE_H_
#define VW_PLUGIN_LOG_SCOPE_OVERRIDE_H_

#include "vw_log.h"

// Maps legacy anonymous sink removal to the current plugin instance while preserving explicit registrations. This keeps
// multi-instance teardown order-independent without exposing freed VLC objects to later log callbacks.
static inline void vw_plugin_log_set_sink_scoped(vw_log_sink_fn sink, void* user_data, void* instance) {
  if (sink == NULL && user_data == NULL) {
    vw_log_set_sink(NULL, instance);
  } else {
    vw_log_set_sink(sink, user_data);
  }
}

#ifdef VW_PLUGIN_LOG_SCOPE_OVERRIDE
#define vw_log_set_sink(sink, user_data) vw_plugin_log_set_sink_scoped((sink), (user_data), obj)
#endif

#endif  // VW_PLUGIN_LOG_SCOPE_OVERRIDE_H_
