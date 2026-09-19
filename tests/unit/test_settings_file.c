#define _POSIX_C_SOURCE 200809L

#include "vw_settings_file.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void vw_write_settings(const char* directory, const char* payload) {
  char path[4096];
  int written = snprintf(path, sizeof(path), "%s/vlc-whisper/settings.json", directory);
  assert(written > 0 && (size_t)written < sizeof(path));
  FILE* file = fopen(path, "wb");
  assert(file);
  assert(fwrite(payload, 1, strlen(payload), file) == strlen(payload));
  assert(fclose(file) == 0);
}

int main(void) {
  char temporary[] = "/tmp/vw-settings-file-XXXXXX";
  char* root = mkdtemp(temporary);
  assert(root);
  assert(setenv("XDG_CONFIG_HOME", root, 1) == 0);

  char settings_directory[4096];
  int written = snprintf(settings_directory, sizeof(settings_directory), "%s/vlc-whisper", root);
  assert(written > 0 && (size_t)written < sizeof(settings_directory));
  assert(mkdir(settings_directory, 0700) == 0);

  vw_write_settings(root, "{\"whisper-threads\": 12");
  assert(vw_settings_override_int("whisper-threads", 7) == 4);
  char* fallback_model = malloc(32);
  assert(fallback_model);
  strcpy(fallback_model, "models/ggml-base.bin");
  char* model = vw_settings_override_psz("model-path", fallback_model);
  assert(strcmp(model, "models/ggml-tiny.bin") == 0);
  free(model);

  vw_write_settings(root, "{\"whisper-threads\": 12,]}");
  assert(vw_settings_override_int("whisper-threads", 7) == 4);

  vw_write_settings(root, "{\"whisper-threads\": 12}");
  assert(vw_settings_override_int("whisper-threads", 7) == 12);

  char settings_path[4096];
  written = snprintf(settings_path, sizeof(settings_path), "%s/settings.json", settings_directory);
  assert(written > 0 && (size_t)written < sizeof(settings_path));
  assert(unlink(settings_path) == 0);
  assert(rmdir(settings_directory) == 0);
  assert(rmdir(root) == 0);
  return 0;
}
