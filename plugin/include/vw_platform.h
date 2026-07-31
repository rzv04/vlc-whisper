#ifndef VW_PLATFORM_H_
#define VW_PLATFORM_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Platform abstraction layer for random bytes and time functions

// Generates cryptographically secure random bytes and fills the provided buffer with them.
bool vw_platform_get_random_bytes(void* buffer, size_t size);

// Returns the current time in microseconds since the Unix epoch (January 1, 1970).
int64_t vw_platform_get_time_us(void);

#endif  // VW_PLATFORM_H_
