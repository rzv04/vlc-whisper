#include <stdint.h>
#include <stdio.h>

#include "vw_protocol_util.h"
#include "vw_test.h"

int main(void) {
  // Test vw_saturating_add_i64 normal cases
  EXPECT(vw_saturating_add_i64(10, 20) == 30);
  EXPECT(vw_saturating_add_i64(-10, -20) == -30);
  EXPECT(vw_saturating_add_i64(10, -20) == -10);
  EXPECT(vw_saturating_add_i64(0, 0) == 0);

  // Test vw_saturating_add_i64 positive overflow
  EXPECT(vw_saturating_add_i64(INT64_MAX, 1) == INT64_MAX);
  EXPECT(vw_saturating_add_i64(INT64_MAX, 1000) == INT64_MAX);
  EXPECT(vw_saturating_add_i64(INT64_MAX - 10, 20) == INT64_MAX);
  EXPECT(vw_saturating_add_i64(INT64_MAX, INT64_MAX) == INT64_MAX);

  // Test vw_saturating_add_i64 negative underflow
  EXPECT(vw_saturating_add_i64(INT64_MIN, -1) == INT64_MIN);
  EXPECT(vw_saturating_add_i64(INT64_MIN, -1000) == INT64_MIN);
  EXPECT(vw_saturating_add_i64(INT64_MIN + 10, -20) == INT64_MIN);
  EXPECT(vw_saturating_add_i64(INT64_MIN, INT64_MIN) == INT64_MIN);

  // Test vw_saturating_sub_i64 normal cases
  EXPECT(vw_saturating_sub_i64(30, 20) == 10);
  EXPECT(vw_saturating_sub_i64(-30, -20) == -10);
  EXPECT(vw_saturating_sub_i64(10, 20) == -10);
  EXPECT(vw_saturating_sub_i64(0, 0) == 0);

  // Test vw_saturating_sub_i64 positive overflow (subtracting negative)
  EXPECT(vw_saturating_sub_i64(INT64_MAX, -1) == INT64_MAX);
  EXPECT(vw_saturating_sub_i64(INT64_MAX, -1000) == INT64_MAX);
  EXPECT(vw_saturating_sub_i64(INT64_MAX - 10, -20) == INT64_MAX);
  EXPECT(vw_saturating_sub_i64(0, INT64_MIN) == INT64_MAX);
  EXPECT(vw_saturating_sub_i64(INT64_MAX, INT64_MIN) == INT64_MAX);

  // Test vw_saturating_sub_i64 negative underflow (subtracting positive)
  EXPECT(vw_saturating_sub_i64(INT64_MIN, 1) == INT64_MIN);
  EXPECT(vw_saturating_sub_i64(INT64_MIN, 1000) == INT64_MIN);
  EXPECT(vw_saturating_sub_i64(INT64_MIN + 10, 20) == INT64_MIN);
  // Test vw_utf8_safe_len
  EXPECT(vw_utf8_safe_len(NULL, 100) == 0);
  EXPECT(vw_utf8_safe_len("Hello", 0) == 0);
  EXPECT(vw_utf8_safe_len("Hello", 10) == 5);
  EXPECT(vw_utf8_safe_len("Hello", 3) == 3);

  // 2-byte UTF-8: 'é' = \xC3\xA9
  EXPECT(vw_utf8_safe_len("café", 5) == 5);
  EXPECT(vw_utf8_safe_len("café", 4) == 3);  // 4th byte is first byte of 'é', truncated safely to "caf" (3 bytes)
  EXPECT(vw_utf8_safe_len("café", 3) == 3);

  // 3-byte UTF-8: '€' = \xE2\x82\xAC
  EXPECT(vw_utf8_safe_len("10€", 5) == 5);
  EXPECT(vw_utf8_safe_len("10€", 4) == 2);
  EXPECT(vw_utf8_safe_len("10€", 3) == 2);
  EXPECT(vw_utf8_safe_len("10€", 2) == 2);

  // 4-byte UTF-8: '😀' = \xF0\x9F\x98\x80
  EXPECT(vw_utf8_safe_len("hi😀", 6) == 6);
  EXPECT(vw_utf8_safe_len("hi😀", 5) == 2);
  EXPECT(vw_utf8_safe_len("hi😀", 4) == 2);
  EXPECT(vw_utf8_safe_len("hi😀", 3) == 2);

  printf("test_protocol_util PASSED\n");
  return 0;
}
