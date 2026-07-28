#ifndef VW_TEST_H_
#define VW_TEST_H_

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

#endif  // VW_TEST_H_
