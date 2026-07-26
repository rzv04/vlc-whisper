#include "vw_log.h"

#include <stdarg.h>
#include <stdio.h>

void vw_log_event(vw_log_level_t level, const char *event_id, const char *fmt, ...) {
  (void)level;
  (void)event_id;
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
  printf("\n");
}
