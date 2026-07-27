#include <stdio.h>
#include <stdlib.h>

#include "vw_protocol.h"

#define EXPECT(cond)                                                            \
  do {                                                                          \
    if (!(cond)) {                                                              \
      fprintf(stderr, "Test failed: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
      exit(1);                                                                  \
    }                                                                           \
  } while (0)

int main(void) {
  vw_frame_header_t header = {.magic = VW_PROTOCOL_MAGIC,
                              .major = VW_PROTOCOL_VERSION_MAJOR,
                              .type = VW_MSG_HELLO,
                              .payload_length = 64,
                              .sequence = 1};

  uint8_t buffer[64];
  EXPECT(vw_protocol_encode_header(&header, buffer, sizeof(buffer)));

  vw_frame_header_t decoded = {0};
  EXPECT(vw_protocol_decode_header(buffer, sizeof(buffer), &decoded));
  EXPECT(decoded.magic == VW_PROTOCOL_MAGIC);
  EXPECT(decoded.type == VW_MSG_HELLO);
  EXPECT(decoded.sequence == 1);

  printf("test_protocol_codec PASSED\n");
  return 0;
}
