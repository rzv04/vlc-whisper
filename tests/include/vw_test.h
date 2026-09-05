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

// Returns the per-test translation-unit failure counter used by accumulating checks, allowing multiple independent
// contract violations to be reported before the executable exits.
static inline int* vw_test_failure_counter(void) {
  static int failures = 0;
  return &failures;
}

// Records a named failure when condition is false, preserving execution so failure-path tests can expose every violated
// contract in a single run.
static inline void vw_test_check_true(const char* name, bool condition) {
  if (!condition) {
    fprintf(stderr, "FAIL: %s\n", name);
    (*vw_test_failure_counter())++;
  }
}

// Records a named failure when condition is true, complementing the positive helper for contracts that require invalid
// states or inputs to be rejected.
static inline void vw_test_check_false(const char* name, bool condition) {
  if (condition) {
    fprintf(stderr, "FAIL: %s\n", name);
    (*vw_test_failure_counter())++;
  }
}

// Prints the final accumulated result for a test executable and returns zero on success or one when any named contract
// check failed.
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
