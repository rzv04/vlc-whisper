#ifndef VW_PROTOCOL_UTIL_H_
#define VW_PROTOCOL_UTIL_H_

#include <stdbool.h>
#include <stddef.h>
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

// Calculates the byte length of the longest valid UTF-8 prefix within max_bytes, preventing
// multi-byte code point truncation at boundary limits.
static inline size_t vw_utf8_safe_len(const char* s, size_t max_bytes) {
  if (!s || max_bytes == 0) return 0;
  size_t len = 0;
  while (s[len] != '\0' && len < max_bytes) {
    unsigned char c = (unsigned char)s[len];
    size_t char_len = 1;
    if ((c & 0x80) == 0x00) {
      char_len = 1;
    } else if ((c & 0xE0) == 0xC0) {
      char_len = 2;
    } else if ((c & 0xF0) == 0xE0) {
      char_len = 3;
    } else if ((c & 0xF8) == 0xF0) {
      char_len = 4;
    } else {
      return len;
    }
    if (len + char_len > max_bytes) break;
    bool valid = true;
    for (size_t k = 1; k < char_len; k++) {
      if ((s[len + k] & 0xC0) != 0x80) {
        valid = false;
        break;
      }
    }
    if (!valid) return len;
    len += char_len;
  }
  return len;
}

#endif  // VW_PROTOCOL_UTIL_H_
