#ifndef VW_SETTINGS_LAUNCHER_MODULE_OVERRIDE_H
#define VW_SETTINGS_LAUNCHER_MODULE_OVERRIDE_H

#include <vlc_common.h>
#include <vlc_plugin.h>

int vw_settings_launcher_open(vlc_object_t* object);
void vw_settings_launcher_close(vlc_object_t* object);

/* The audio-filter source owns the plugin descriptor. Append a tiny access
 * submodule so Lua can ask VLC itself to launch the standalone settings app
 * without using a command shell or adding another packaged plugin DLL. */
#undef vlc_module_end
#define vlc_module_end()                                                                                    \
  add_submodule() set_shortname("VLC-Whisper Settings Launcher")                                            \
      set_description("Shell-free VLC-Whisper Settings launcher bridge") set_capability("access", 0)        \
          set_category(CAT_INPUT) set_subcategory(SUBCAT_INPUT_ACCESS) add_shortcut("vlc-whisper-settings") \
              set_callbacks(vw_settings_launcher_open, vw_settings_launcher_close)(void) config;            \
  return 0;                                                                                                 \
  error:                                                                                                    \
  return -1;                                                                                                \
  }                                                                                                         \
  VLC_MODULE_NAME_HIDDEN_SYMBOL                                                                             \
  VLC_METADATA_EXPORTS

#endif
