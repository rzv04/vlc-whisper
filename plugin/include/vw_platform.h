#ifndef VW_PLATFORM_H_
#define VW_PLATFORM_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool vw_platform_get_random_bytes(void *buffer, size_t size);
int64_t vw_platform_get_time_us(void);

#endif  // VW_PLATFORM_H_
