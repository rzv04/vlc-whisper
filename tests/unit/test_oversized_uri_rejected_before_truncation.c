#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vw_test.h"

#ifndef VW_TEST_SOURCE_ROOT
#define VW_TEST_SOURCE_ROOT "."
#endif

static char* read_text_file(const char* path) {
  FILE* file = fopen(path, "rb");
  if (!file) return NULL;
  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return NULL;
  }
  long size = ftell(file);
  if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return NULL;
  }
  char* text = (char*)malloc((size_t)size + 1U);
  if (!text) {
    fclose(file);
    return NULL;
  }
  size_t read_size = fread(text, 1, (size_t)size, file);
  fclose(file);
  text[read_size] = '\0';
  return text;
}

static bool contains_before(const char* begin, const char* end, const char* needle) {
  const char* found = strstr(begin, needle);
  return found && found < end;
}

int main(void) {
  char path[1024];
  snprintf(path, sizeof(path), "%s/plugin/src/vw_whisper_module.c", VW_TEST_SOURCE_ROOT);
  char* source = read_text_file(path);
  vw_test_check_true("plugin media-swap source is readable", source != NULL);
  if (source) {
    const char* copy = strstr(source, "strncpy(normalized_uri, raw_uri");
    bool safe = (copy == NULL);
    if (copy) {
      const char* block_start = copy > source + 1600 ? copy - 1600 : source;
      bool length_checked = contains_before(block_start, copy, "strlen(raw_uri)") ||
                            contains_before(block_start, copy, "strnlen(raw_uri");
      bool destination_bound_checked = contains_before(block_start, copy, "sizeof(normalized_uri)");
      safe = length_checked && destination_bound_checked;
    }
    vw_test_check_true("media-swap rejects an oversized original URI before any truncating identity copy", safe);
    free(source);
  }
  return vw_test_finish("test_oversized_uri_rejected_before_truncation");
}
