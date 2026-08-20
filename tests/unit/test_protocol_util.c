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
  EXPECT(vw_saturating_sub_i64(INT64_MIN, INT64_MAX) == INT64_MIN);

  printf("test_protocol_util PASSED\n");
  return 0;
}
