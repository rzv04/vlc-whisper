#include <stdio.h>
#include <stdlib.h>
#include "vw_protocol.h"

#define EXPECT(cond)                                                           \
  do {                                                                         \
    if (!(cond)) {                                                             \
      fprintf(stderr, "Test failed: %s at %s:%d\n", #cond, __FILE__,           \
              __LINE__);                                                       \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)

int main(void) {
  vw_frame_header_t valid = {.magic = VW_PROTOCOL_MAGIC,
                             .major = VW_PROTOCOL_VERSION_MAJOR,
                             .type = VW_MSG_START_SESSION,
                             .payload_length = 100,
                             .sequence = 42};
  EXPECT(vw_protocol_validate_header(&valid));

  vw_frame_header_t invalid_magic = valid;
  invalid_magic.magic = 0xDEADBEEF;
  EXPECT(!vw_protocol_validate_header(&invalid_magic));

  vw_frame_header_t invalid_payload = valid;
  invalid_payload.payload_length = VW_MAX_PAYLOAD_BYTES + 1;
  EXPECT(!vw_protocol_validate_header(&invalid_payload));

  printf("test_protocol_validate PASSED\n");
  return 0;
}
