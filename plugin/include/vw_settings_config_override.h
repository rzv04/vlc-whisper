#ifndef VW_SETTINGS_CONFIG_OVERRIDE_H
#define VW_SETTINGS_CONFIG_OVERRIDE_H

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#endif
#include <vlc_common.h>
#include <vlc_configuration.h>

#include "vw_settings_file.h"

#undef config_GetPsz
#undef config_GetInt
#undef config_PutPsz
#undef config_PutInt

static _Thread_local char vw_settings_last_model_command[64];

static inline char* vw_settings_config_get_psz(vlc_object_t* obj, const char* key) {
  char* fallback = config_GetPsz(obj, key);
  char* value = vw_settings_override_psz(key, fallback);
  if (key && strcmp(key, "whisper-model-download") == 0) {
    if (value && value[0]) {
      snprintf(vw_settings_last_model_command, sizeof(vw_settings_last_model_command), "%s", value);
    } else {
      vw_settings_last_model_command[0] = '\0';
    }
  }
  return value;
}

static inline int64_t vw_settings_config_get_int(vlc_object_t* obj, const char* key) {
  return vw_settings_override_int(key, config_GetInt(obj, key));
}

static inline void vw_settings_config_put_psz(vlc_object_t* obj, const char* key, const char* value) {
  config_PutPsz(obj, key, value);
  vw_settings_note_psz(key, value);
  if (key && value && strcmp(key, "whisper-model-download") == 0 && value[0] == '\0' &&
      vw_settings_last_model_command[0]) {
    vw_settings_ack_model_command(vw_settings_last_model_command);
    vw_settings_last_model_command[0] = '\0';
  }
}

static inline void vw_settings_config_put_int(vlc_object_t* obj, const char* key, int64_t value) {
  config_PutInt(obj, key, value);
  vw_settings_note_int(key, value);
}

#define config_GetPsz(obj, key) vw_settings_config_get_psz((obj), (key))
#define config_GetInt(obj, key) vw_settings_config_get_int((obj), (key))
#define config_PutPsz(obj, key, value) vw_settings_config_put_psz((obj), (key), (value))
#define config_PutInt(obj, key, value) vw_settings_config_put_int((obj), (key), (value))

#endif