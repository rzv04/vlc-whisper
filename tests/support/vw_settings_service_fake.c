// Deterministic backend for the real settings-service process/pipe boundary.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vw_model_download.h"
#include "vw_process_policy.h"
#include "vw_settings_service.h"
#include "vw_translate.h"

static unsigned vw_polls;
static int vw_handle;
bool vw_model_download_default_dir(char* out, size_t size) { return snprintf(out, size, "/unused") < (int)size; }
vw_model_download_t* vw_model_download_start(const vw_model_catalog_entry_t* entry, const char* dir) {
  (void)entry;
  (void)dir;
  return getenv("VW_FAKE_BUSY") ? NULL : (vw_model_download_t*)&vw_handle;
}
bool vw_model_download_poll(vw_model_download_t* dl, vw_download_progress_t* out) {
  (void)dl;
  memset(out, 0, sizeof(*out));
  out->stage = vw_polls++ == 0 ? VW_MODEL_STAGE_IDLE : VW_MODEL_STAGE_DOWNLOADING;
  if (vw_polls > 2 && !getenv("VW_FAKE_WAIT"))
    out->stage = getenv("VW_FAKE_FAIL") ? VW_MODEL_STAGE_FAILED : VW_MODEL_STAGE_DONE;
  out->pct = out->stage == VW_MODEL_STAGE_DONE ? 100 : 0;
  return true;
}
void vw_model_download_free(vw_model_download_t* dl) {
  (void)dl;
  fputs("joined\n", stderr);
}
bool vw_translate_text_detailed(const char* text, const char* from, const char* to, char* out, size_t size,
                                uint8_t* tier, uint32_t* latency, vw_translate_failure_t* failure) {
  (void)tier;
  (void)latency;
  (void)failure;
  if (getenv("VW_FAKE_FAIL")) return false;
  return snprintf(out, size, "%s -> %s: %s", from, to, text) < (int)size;
}
int main(int argc, char** argv) {
  if (!vw_process_install_worker_signal_policy()) return 2;
  return vw_settings_service_run(argc, argv);
}
