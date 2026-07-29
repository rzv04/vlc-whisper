#include <string.h>

#include "vw_platform.h"

bool vw_platform_get_random_bytes(void *buffer, size_t size) {
  if (!buffer || size == 0) {
    return false;
  }
  memset(buffer, 0x42, size);
  return true;
}

int64_t vw_platform_get_time_us(void) { return 0; }
