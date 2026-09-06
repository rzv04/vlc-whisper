#ifndef VW_TEST_WORKER_STUBS_H_
#define VW_TEST_WORKER_STUBS_H_

#include <stddef.h>

typedef enum vw_test_decoder_mode {
  VW_TEST_DECODER_DATA = 0,
  VW_TEST_DECODER_AGAIN_THEN_DATA,
  VW_TEST_DECODER_ERROR,
} vw_test_decoder_mode_t;

void vw_test_worker_stubs_reset(void);
void vw_test_decoder_set_mode(vw_test_decoder_mode_t mode);
int vw_test_decoder_read_calls(void);
int vw_test_whisper_transcribe_calls(void);

#endif  // VW_TEST_WORKER_STUBS_H_
