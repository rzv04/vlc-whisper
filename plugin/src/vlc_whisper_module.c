#include "vw_plugin.h"

// VLC Module entry point stubs
int vlc_whisper_Open(void *vlc_object) {
  (void)vlc_object;
  return 0;
}

void vlc_whisper_Close(void *vlc_object) { (void)vlc_object; }
