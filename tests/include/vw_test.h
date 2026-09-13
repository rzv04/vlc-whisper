#ifndef VW_TEST_H_
#define VW_TEST_H_

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXPECT(cond)                                                            \
  do {                                                                          \
    if (!(cond)) {                                                              \
      fprintf(stderr, "Test failed: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
      exit(1);                                                                  \
    }                                                                           \
  } while (0)

#define EXPECT_EQ_STR(a, b)                                                                  \
  do {                                                                                       \
    if (strcmp((a), (b)) != 0) {                                                             \
      fprintf(stderr, "Test failed: '%s' != '%s' at %s:%d\n", (a), (b), __FILE__, __LINE__); \
      exit(1);                                                                               \
    }                                                                                        \
  } while (0)

// Returns this test translation unit's accumulating contract-failure counter.
static inline int* vw_test_failure_counter(void) {
  static int failures = 0;
  return &failures;
}

// Records a named failed expectation without aborting the remaining contract checks.
static inline void vw_test_check_true(const char* name, bool condition) {
  if (!condition) {
    fprintf(stderr, "FAIL: %s\n", name);
    (*vw_test_failure_counter())++;
  }
}

// Records a named expectation that required the supplied condition to remain false.
static inline void vw_test_check_false(const char* name, bool condition) {
  if (condition) {
    fprintf(stderr, "FAIL: %s\n", name);
    (*vw_test_failure_counter())++;
  }
}

// Prints one final test result after all independent named checks have executed.
static inline int vw_test_finish(const char* test_name) {
  int failures = *vw_test_failure_counter();
  if (failures != 0) {
    fprintf(stderr, "%s: %d contract failure(s)\n", test_name, failures);
    return 1;
  }

  printf("%s PASSED\n", test_name);
  return 0;
}

#endif  // VW_TEST_H_
