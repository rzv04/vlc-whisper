#ifndef VW_HALLUCINATION_FILTER_H_
#define VW_HALLUCINATION_FILTER_H_

#include <stdbool.h>
#include <stddef.h>

// Evaluates whether text matches standalone sound effect or music descriptors (such as [Music], (applause), or note
// glyphs), returning true if the candidate string should be suppressed.
bool vw_hallucination_is_non_speech_tag(const char* text);

// Checks if a candidate string contains zero alphanumeric characters and consists solely of isolated punctuation or
// whitespace, returning true if it lacks spoken linguistic content.
bool vw_hallucination_is_isolated_punctuation(const char* text);

// Combines non-speech descriptor and isolated punctuation checks to determine if a candidate subtitle should be
// rejected, while preserving 100% of valid spoken dialogue and sentence punctuation.
bool vw_hallucination_is_phantom_text(const char* text);

#endif  // VW_HALLUCINATION_FILTER_H_
