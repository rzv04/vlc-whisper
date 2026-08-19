#ifndef VW_PROTOCOL_UTIL_H_
#define VW_PROTOCOL_UTIL_H_

#include <stdint.h>

// Adds two int64_t values with saturation at INT64_MAX and INT64_MIN, preventing overflow UB.
static inline int64_t vw_saturating_add_i64(int64_t a, int64_t b) {
  int64_t res;
  if (__builtin_add_overflow(a, b, &res)) {
    return (b > 0) ? INT64_MAX : INT64_MIN;
  }
  return res;
}

// Subtracts two int64_t values with saturation at INT64_MAX and INT64_MIN, preventing underflow UB.
static inline int64_t vw_saturating_sub_i64(int64_t a, int64_t b) {
  int64_t res;
  if (__builtin_sub_overflow(a, b, &res)) {
    return (b < 0) ? INT64_MAX : INT64_MIN;
  }
  return res;
}

#endif  // VW_PROTOCOL_UTIL_H_
