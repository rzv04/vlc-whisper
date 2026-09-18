#ifndef VW_SETTINGS_CONFIG_OVERRIDE_H
#define VW_SETTINGS_CONFIG_OVERRIDE_H

#ifdef _WIN32
#include <winsock2.h>
#endif
#include <vlc_block.h>
#include <vlc_common.h>
#include <vlc_configuration.h>
#include <vlc_filter.h>
#include <vlc_input.h>
#include <vlc_plugin.h>

#include "vw_settings_file.h"

static inline char* vw_settings_config_get_psz(vlc_object_t* obj, const char* key) {
  char* fallback = config_GetPsz(obj, key);
  return vw_settings_override_psz(key, fallback);
}

static inline int64_t vw_settings_config_get_int(vlc_object_t* obj, const char* key) {
  return vw_settings_override_int(key, config_GetInt(obj, key));
}

static inline void vw_settings_config_put_psz(vlc_object_t* obj, const char* key, const char* value) {
  config_PutPsz(obj, key, value);
  vw_settings_note_psz(key, value);
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
