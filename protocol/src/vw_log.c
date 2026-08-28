// Copyright 2026 VLC-Whisper Contributors. All rights reserved.
// Use of this source code is governed by the MIT License that can be found in the LICENSE file.

#include "vw_log.h"

#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static _Atomic(vw_log_sink_fn) g_log_sink = ATOMIC_VAR_INIT(NULL);
static _Atomic(void*) g_log_user_data = ATOMIC_VAR_INIT(NULL);
static _Atomic(FILE*) g_log_file = ATOMIC_VAR_INIT(NULL);
static _Atomic bool g_log_enabled = ATOMIC_VAR_INIT(false);

void vw_log_set_enabled(bool enabled) { atomic_store(&g_log_enabled, enabled); }

void vw_log_set_sink(vw_log_sink_fn sink, void* user_data) {
  atomic_store(&g_log_user_data, user_data);
  atomic_store(&g_log_sink, sink);
}

void vw_log_set_file(FILE* file) { atomic_store(&g_log_file, file); }

static const char* vw_log_level_to_string(vw_log_level_t level) {
  switch (level) {
    case VW_LOG_LEVEL_DEBUG:
      return "DEBUG";
    case VW_LOG_LEVEL_INFO:
      return "INFO";
    case VW_LOG_LEVEL_WARN:
      return "WARN";
    case VW_LOG_LEVEL_ERROR:
      return "ERROR";
    default:
      return "UNKNOWN";
  }
}

void vw_log_event(vw_log_level_t level, const char* event_id, const char* fmt, ...) {
  if (!atomic_load(&g_log_enabled)) return;
  if (event_id == NULL) {
    event_id = "UNKNOWN_EVENT";
  }
  if (fmt == NULL) {
    fmt = "";
  }

  char message_buf[1024];
  va_list args;
  va_start(args, fmt);
  vsnprintf(message_buf, sizeof(message_buf), fmt, args);
  va_end(args);

  vw_log_sink_fn sink = atomic_load(&g_log_sink);
  void* udata = atomic_load(&g_log_user_data);
  if (sink != NULL) {
    sink(level, event_id, message_buf, udata);
  } else {
    // Default fallback sink: print to stderr with tags
    fprintf(stderr, "[%s] [%s] %s\n", vw_log_level_to_string(level), event_id, message_buf);
    fflush(stderr);
  }

  // Optional additional FILE* output (e.g. the worker's opt-in temp log file).
  FILE* log_file = atomic_load(&g_log_file);
  if (log_file != NULL) {
    fprintf(log_file, "[%s] [%s] %s\n", vw_log_level_to_string(level), event_id, message_buf);
    fflush(log_file);
  }
}
