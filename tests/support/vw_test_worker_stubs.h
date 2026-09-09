#ifndef VW_TEST_WORKER_STUBS_H_
#define VW_TEST_WORKER_STUBS_H_

#include <stdbool.h>
#include <stddef.h>

typedef enum vw_test_decoder_mode {
  VW_TEST_DECODER_DATA = 0,
  VW_TEST_DECODER_AGAIN_THEN_DATA,
  VW_TEST_DECODER_ERROR,
  VW_TEST_DECODER_ERROR_AFTER_DATA,
} vw_test_decoder_mode_t;

// Resets all deterministic worker test doubles, counters, decoder behavior, and synthetic transcript state so each
// failure-path integration test begins from an isolated baseline.
void vw_test_worker_stubs_reset(void);

// Selects the deterministic decoder behavior used by worker seam tests, allowing immediate data, transient AGAIN before
// data, or surfaced decoder errors at controlled lifecycle points without real media.
void vw_test_decoder_set_mode(vw_test_decoder_mode_t mode);

// Forces subsequent stub Whisper calls to fail, exercising worker fatal-inference handling and media-end error
// reporting without downloaded models or real inference dependencies.
void vw_test_whisper_set_failure(bool enabled);

// Returns how many times the deterministic source decoder read stub has been invoked, supporting assertions about
// retry, EOF, and error handling across worker lifecycle paths.
int vw_test_decoder_read_calls(void);

// Returns how many synthetic Whisper transcription calls the worker attempted, allowing tests to verify whether
// buffered speech was processed or incorrectly discarded during lifecycle transitions.
int vw_test_whisper_transcribe_calls(void);

// Returns the concrete language selected by the most recent worker START_SESSION call, allowing lifecycle seam tests to
// prove session language changes reach the engine rather than remaining at process startup configuration.
const char* vw_test_whisper_language(void);

#endif  // VW_TEST_WORKER_STUBS_H_
