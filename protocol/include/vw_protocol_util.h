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
// Rejects overlong sequences, UTF-16 surrogates, and code points above U+10FFFF, consistent with
// vw_protocol_validate.c:is_valid_utf8.
static inline size_t vw_utf8_safe_len(const char* s, size_t max_bytes) {
  if (!s || max_bytes == 0) return 0;
  const uint8_t* p = (const uint8_t*)s;
  size_t len = 0;
  while (len < max_bytes && p[len] != '\0') {
    uint8_t c = p[len];
    if (c <= 0x7F) {
      len++;
      continue;
    }

    size_t extra = 0;
    uint32_t code_point = 0;
    uint32_t min_cp = 0;

    if ((c & 0xE0) == 0xC0) {
      extra = 1;
      code_point = c & 0x1F;
      min_cp = 0x80;
    } else if ((c & 0xF0) == 0xE0) {
      extra = 2;
      code_point = c & 0x0F;
      min_cp = 0x800;
    } else if ((c & 0xF8) == 0xF0) {
      extra = 3;
      code_point = c & 0x07;
      min_cp = 0x10000;
    } else {
      return len;
    }

    if (len + 1 + extra > max_bytes) break;

    bool valid = true;
    for (size_t k = 1; k <= extra; k++) {
      uint8_t next = p[len + k];
      if ((next & 0xC0) != 0x80) {
        valid = false;
        break;
      }
      code_point = (code_point << 6) | (next & 0x3F);
    }
    if (!valid) return len;

    if (code_point < min_cp) return len;
    if (code_point >= 0xD800 && code_point <= 0xDFFF) return len;
    if (code_point > 0x10FFFF) return len;

    len += 1 + extra;
  }
  return len;
}

#endif  // VW_PROTOCOL_UTIL_H_
