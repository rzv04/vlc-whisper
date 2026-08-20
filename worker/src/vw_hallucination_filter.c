#define _POSIX_C_SOURCE 200809L

#include "vw_hallucination_filter.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

// Check if string contains only punctuation, spaces, and non-alphanumeric chars.
// Note: UTF-8 multibyte characters (>= 0x80) represent international words (Cyrillic, CJK, etc.)
// and are never treated as isolated ASCII punctuation.
bool vw_hallucination_is_isolated_punctuation(const char* text) {
  if (text == NULL || text[0] == '\0') {
    return true;
  }
  for (const char* p = text; *p != '\0'; p++) {
    unsigned char c = (unsigned char)*p;
    // ASCII alphanumeric check
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
      return false;
    }
    // UTF-8 multibyte sequence (non-Latin languages, accented characters)
    if (c >= 0x80) {
      return false;
    }
  }
  return true;
}

// Case-insensitive string equality helper
static bool case_equals(const char* s1, const char* s2) {
  if (s1 == NULL || s2 == NULL) return false;
  while (*s1 && *s2) {
    if (tolower((unsigned char)*s1) != tolower((unsigned char)*s2)) {
      return false;
    }
    s1++;
    s2++;
  }
  return *s1 == '\0' && *s2 == '\0';
}

// Check if string consists entirely of musical notes and surrounding brackets/punctuation
static bool is_only_musical_notes(const char* str) {
  if (str == NULL || str[0] == '\0') {
    return false;
  }
  bool has_note = false;
  const unsigned char* p = (const unsigned char*)str;
  while (*p != '\0') {
    if (isspace(*p) || *p == '[' || *p == ']' || *p == '(' || *p == ')' || *p == '-' || *p == '*' || *p == '~') {
      p++;
      continue;
    }
    // Check 3-byte UTF-8 musical notes: E2 99 A9..AC (♩, ♪, ♫, ♬)
    if (p[0] == 0xE2 && p[1] == 0x99 && (p[2] >= 0xA9 && p[2] <= 0xAC)) {
      has_note = true;
      p += 3;
      continue;
    }
    // Any other character means the string contains non-note content
    return false;
  }
  return has_note;
}

// Evaluates whether text matches standalone non-speech descriptors
bool vw_hallucination_is_non_speech_tag(const char* text) {
  if (text == NULL || text[0] == '\0') {
    return true;
  }

  // 1. Trim leading and trailing whitespace
  const char* start = text;
  while (*start && isspace((unsigned char)*start)) {
    start++;
  }
  if (*start == '\0') {
    return true;
  }

  const char* end = text + strlen(text) - 1;
  while (end > start && isspace((unsigned char)*end)) {
    end--;
  }

  size_t len = (size_t)(end - start + 1);
  if (len > 128) {
    return false;  // Tags are short descriptor cues
  }

  char trimmed[129];
  memcpy(trimmed, start, len);
  trimmed[len] = '\0';

  // 2. Check if string is composed entirely of musical note symbols
  if (is_only_musical_notes(trimmed)) {
    return true;
  }

  // 3. Check bracketed/parenthesized/asterisk tags (e.g. "[music]", "(applause)", "*laughter*")
  char open = trimmed[0];
  char close = trimmed[len - 1];
  if ((open == '[' && close == ']') || (open == '(' && close == ')') || (open == '*' && close == '*')) {
    const char* inner_start = trimmed + 1;
    const char* inner_end = trimmed + len - 1;
    while (inner_start < inner_end && isspace((unsigned char)*inner_start)) inner_start++;
    while (inner_end > inner_start && isspace((unsigned char)*(inner_end - 1))) inner_end--;

    size_t inner_len = (size_t)(inner_end - inner_start);
    if (inner_len > 0 && inner_len <= 64) {
      char inner[65];
      memcpy(inner, inner_start, inner_len);
      inner[inner_len] = '\0';

      static const char* const k_inner_tags[] = {
          "music", "applause",   "laughter",          "silence",       "cheering", "screaming", "gasp",
          "sigh",  "coughing",   "sneezing",          "blank_audio",   "snicker",  "groan",     "yawn",
          "pant",  "whispering", "applause cheering", "music playing", NULL};

      for (size_t i = 0; k_inner_tags[i] != NULL; i++) {
        if (case_equals(inner, k_inner_tags[i])) {
          return true;
        }
      }
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

  // 2. Check if string is a standalone non-speech sound tag
  if (vw_hallucination_is_non_speech_tag(text)) {
    return true;
  }

  return false;
}
