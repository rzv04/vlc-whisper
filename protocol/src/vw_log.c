// Copyright 2026 VLC-Whisper Contributors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "vw_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static vw_log_sink_fn g_log_sink = NULL;
static void *g_log_user_data = NULL;

void vw_log_set_sink(vw_log_sink_fn sink, void *user_data) {
  g_log_sink = sink;
  g_log_user_data = user_data;
}

static const char *vw_log_level_to_string(vw_log_level_t level) {
  switch (level) {
    case VW_LOG_LEVEL_DEBUG: return "DEBUG";
    case VW_LOG_LEVEL_INFO:  return "INFO";
    case VW_LOG_LEVEL_WARN:  return "WARN";
    case VW_LOG_LEVEL_ERROR: return "ERROR";
    default:                 return "UNKNOWN";
  }
}

void vw_log_event(vw_log_level_t level, const char *event_id, const char *fmt, ...) {
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

  if (g_log_sink != NULL) {
    g_log_sink(level, event_id, message_buf, g_log_user_data);
  } else {
    // Default fallback sink: print to stderr with tags
    fprintf(stderr, "[%s] [%s] %s\n", vw_log_level_to_string(level), event_id, message_buf);
    fflush(stderr);
  }
}
