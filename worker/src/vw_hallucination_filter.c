#define _POSIX_C_SOURCE 200809L

#include "vw_hallucination_filter.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

// Check if string contains only punctuation, spaces, and non-alphanumeric chars
bool vw_hallucination_is_isolated_punctuation(const char* text) {
  if (text == NULL || text[0] == '\0') {
    return true;
  }
  for (const char* p = text; *p != '\0'; p++) {
    unsigned char c = (unsigned char)*p;
    if (isalnum(c)) {
      return false;  // Contains at least one alphanumeric character
    }
  }
  return true;
}

// Case-insensitive substring match helper
static bool contains_case_insensitive(const char* haystack, const char* needle) {
  if (haystack == NULL || needle == NULL || needle[0] == '\0') {
    return false;
  }
  size_t h_len = strlen(haystack);
  size_t n_len = strlen(needle);
  if (n_len > h_len) {
    return false;
  }

  for (size_t i = 0; i <= h_len - n_len; i++) {
    size_t j = 0;
    while (j < n_len && tolower((unsigned char)haystack[i + j]) == tolower((unsigned char)needle[j])) {
      j++;
    }
    if (j == n_len) {
      return true;
    }
  }
  return false;
}

// Evaluates whether text matches standalone non-speech descriptors
bool vw_hallucination_is_non_speech_tag(const char* text) {
  if (text == NULL || text[0] == '\0') {
    return false;
  }

  // Musical note symbols (UTF-8 bytes)
  if (strstr(text, "\xe2\x99\xaa") != NULL || strstr(text, "\xe2\x99\xab") != NULL ||  // ♪ or ♫
      strstr(text, "♪") != NULL || strstr(text, "♫") != NULL) {
    return true;
  }

  static const char* const k_sound_tags[] = {"[music]",     "(music)",     "[applause]",    "(applause)", "[laughter]",
                                             "(laughter)",  "[silence]",   "(silence)",     "[cheering]", "(cheering)",
                                             "[screaming]", "(screaming)", "[gasp]",        "(gasp)",     "[sigh]",
                                             "(sigh)",      "*music*",     "[blank_audio]", "(applause)", "[coughing]",
                                             "(coughing)",  "[sneezing]",  "(sneezing)",    NULL};

  for (size_t i = 0; k_sound_tags[i] != NULL; i++) {
    if (contains_case_insensitive(text, k_sound_tags[i])) {
      return true;
    }
  }

  return false;
}

bool vw_hallucination_is_phantom_text(const char* text) {
  if (text == NULL || text[0] == '\0') {
    return true;
  }

  // 1. Check if string contains zero alphanumeric characters (e.g. "...", "---", "! ! !")
  if (vw_hallucination_is_isolated_punctuation(text)) {
    return true;
  }

  // 2. Check if string is a non-speech sound tag
  if (vw_hallucination_is_non_speech_tag(text)) {
    return true;
  }

  return false;
}
