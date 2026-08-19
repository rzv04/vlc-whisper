#ifndef VW_PROTOCOL_UTIL_H_
#define VW_PROTOCOL_UTIL_H_

#include <stdint.h>

// Adds two signed 64-bit integer values using compiler overflow intrinsics, clamping to INT64_MAX or INT64_MIN to
// prevent timeline timestamp arithmetic overflow undefined behavior.
static inline int64_t vw_saturating_add_i64(int64_t a, int64_t b) {
  int64_t res;
  if (__builtin_add_overflow(a, b, &res)) {
    return (b > 0) ? INT64_MAX : INT64_MIN;
  }
  return res;
}

// Subtracts two signed 64-bit integer values using compiler overflow intrinsics, clamping to INT64_MAX or INT64_MIN to
// prevent timeline timestamp arithmetic underflow undefined behavior.
static inline int64_t vw_saturating_sub_i64(int64_t a, int64_t b) {
  int64_t res;
  if (__builtin_sub_overflow(a, b, &res)) {
    return (b < 0) ? INT64_MAX : INT64_MIN;
  }
  return res;
}

#endif  // VW_PROTOCOL_UTIL_H_
