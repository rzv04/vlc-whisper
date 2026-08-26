// Copyright 2026 VLC-Whisper Contributors. All rights reserved.
// Use of this source code is governed by the MIT License that can be found in the LICENSE file.

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>

#include "vw_protocol_util.h"

int main(void) {
  // 1. Normal addition
  assert(vw_saturating_add_i64(1000, 2000) == 3000);
  assert(vw_saturating_add_i64(-1000, -2000) == -3000);
  assert(vw_saturating_add_i64(-1000, 2000) == 1000);

  // 2. Positive overflow saturation
  assert(vw_saturating_add_i64(INT64_MAX, 1) == INT64_MAX);
  assert(vw_saturating_add_i64(INT64_MAX - 10, 20) == INT64_MAX);
  assert(vw_saturating_add_i64(INT64_MAX, INT64_MAX) == INT64_MAX);

  // 3. Negative overflow (underflow) saturation
  assert(vw_saturating_add_i64(INT64_MIN, -1) == INT64_MIN);
  assert(vw_saturating_add_i64(INT64_MIN + 10, -20) == INT64_MIN);
  assert(vw_saturating_add_i64(INT64_MIN, INT64_MIN) == INT64_MIN);

  // 4. Normal subtraction
  assert(vw_saturating_sub_i64(3000, 1000) == 2000);
  assert(vw_saturating_sub_i64(-3000, -1000) == -2000);
  assert(vw_saturating_sub_i64(1000, -2000) == 3000);

  // 5. Subtraction positive overflow saturation
  assert(vw_saturating_sub_i64(INT64_MAX, -1) == INT64_MAX);
  assert(vw_saturating_sub_i64(INT64_MAX - 10, -20) == INT64_MAX);
  assert(vw_saturating_sub_i64(0, INT64_MIN) == INT64_MAX);

  // 6. Subtraction negative underflow saturation
  assert(vw_saturating_sub_i64(INT64_MIN, 1) == INT64_MIN);
  assert(vw_saturating_sub_i64(INT64_MIN + 10, 20) == INT64_MIN);
  assert(vw_saturating_sub_i64(INT64_MIN, INT64_MAX) == INT64_MIN);

  printf("test_caption_timing PASSED\n");
  return 0;
}
