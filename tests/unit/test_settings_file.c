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

  vw_write_settings(root, "{\"metadata\":{\"whisper-threads\":12},\"whisper-threads\":4}");
  assert(vw_settings_override_int("whisper-threads", 7) == 4);

  vw_write_settings(root, "{\"whisper-threads\":12,\"whisper-threads\":4}");
  assert(vw_settings_override_int("whisper-threads", 7) == 4);

  vw_write_settings(root, "{\"whisper-\\u0074hreads\":5}");
  assert(vw_settings_override_int("whisper-threads", 7) == 5);

  char reset_path[4096];
  written = snprintf(reset_path, sizeof(reset_path), "%s/reset-settings", settings_directory);
  assert(written > 0 && (size_t)written < sizeof(reset_path));
  FILE* reset = fopen(reset_path, "wb");
  assert(reset);
  assert(fputs("reset\n", reset) >= 0);
  assert(fclose(reset) == 0);
  vw_write_settings(root, "{\"whisper-threads\": 12}");
  assert(vw_settings_override_int("whisper-threads", 7) == 4);
  assert(unlink(reset_path) == 0);
  assert(vw_settings_override_int("whisper-threads", 7) == 12);

  vw_write_settings(root, "{\"model-path\":\"models/ggml-base.bin\"}");
  char download_base_path[4096];
  written = snprintf(download_base_path, sizeof(download_base_path), "%s/model-path-download-base", settings_directory);
  assert(written > 0 && (size_t)written < sizeof(download_base_path));
  FILE* download_base = fopen(download_base_path, "wb");
  assert(download_base);
  assert(fputs("ggml-base.bin\nmodels/ggml-tiny.bin\n", download_base) >= 0);
  assert(fclose(download_base) == 0);
  fallback_model = malloc(32);
  assert(fallback_model);
  strcpy(fallback_model, "models/ggml-tiny.bin");
  model = vw_settings_override_psz("model-path", fallback_model);
  assert(strcmp(model, "models/ggml-tiny.bin") == 0);
  free(model);

  char activated_path[4096];
  written = snprintf(activated_path, sizeof(activated_path), "%s/ggml-base.bin", root);
  assert(written > 0 && (size_t)written < sizeof(activated_path));
  FILE* activated = fopen(activated_path, "wb");
  assert(activated);
  assert(fclose(activated) == 0);
  char earlier_path[4096];
  written = snprintf(earlier_path, sizeof(earlier_path), "%s/ggml-tiny.bin", root);
  assert(written > 0 && (size_t)written < sizeof(earlier_path));
  FILE* earlier = fopen(earlier_path, "wb");
  assert(earlier);
  assert(fclose(earlier) == 0);
  vw_settings_note_psz("model-path", earlier_path);
  assert(access(download_base_path, F_OK) == 0);
  assert(unlink(earlier_path) == 0);

  vw_settings_note_psz("model-path", activated_path);
  assert(access(download_base_path, F_OK) != 0);
  fallback_model = malloc(32);
  assert(fallback_model);
  strcpy(fallback_model, "models/ggml-tiny.bin");
  model = vw_settings_override_psz("model-path", fallback_model);
  assert(strcmp(model, activated_path) == 0);
  free(model);
  assert(unlink(activated_path) == 0);
  char active_marker_path[4096];
  written = snprintf(active_marker_path, sizeof(active_marker_path), "%s/model-path-active", settings_directory);
  assert(written > 0 && (size_t)written < sizeof(active_marker_path));
  assert(unlink(active_marker_path) == 0);

  char settings_path[4096];
  written = snprintf(settings_path, sizeof(settings_path), "%s/settings.json", settings_directory);
  assert(written > 0 && (size_t)written < sizeof(settings_path));
  assert(unlink(settings_path) == 0);
  assert(rmdir(settings_directory) == 0);
  assert(rmdir(root) == 0);
  return 0;
}
