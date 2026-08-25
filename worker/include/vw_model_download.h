#ifndef VW_MODEL_DOWNLOAD_H_
#define VW_MODEL_DOWNLOAD_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vw_model_catalog.h"

#ifdef __cplusplus
extern "C" {
#endif

// Download stage constants mirror protocol VW_MODEL_STAGE_* values (0..5) but
// are defined here to keep worker independent from protocol headers on Windows.
#define VW_MODEL_STAGE_IDLE 0
#define VW_MODEL_STAGE_DOWNLOADING 1
#define VW_MODEL_STAGE_VERIFYING 2
#define VW_MODEL_STAGE_DONE 3
#define VW_MODEL_STAGE_FAILED 4
#define VW_MODEL_STAGE_ABORTING 5

// Snapshot of current download progress guarded by internal mutex; model_id is
// NUL-terminated catalog identifier copied from the catalog entry at start.
typedef struct vw_download_progress {
  int stage;
  int pct;
  uint64_t bytes_done;
  uint64_t bytes_total;
  char model_id[32];
} vw_download_progress_t;

typedef struct vw_model_download vw_model_download_t;

// Starts an asynchronous single-flight model download in a dedicated thread
// copying the catalog entry and destination directory without performing network
// I/O synchronously; returns handle or NULL on invalid args or thread failure.
vw_model_download_t* vw_model_download_start(const vw_model_catalog_entry_t* entry, const char* dest_dir);

// Requests asynchronous abort of an in-flight download; sets abort flag, kills
// platform backend handle or curl child, and lets the thread clean up safely.
void vw_model_download_abort(vw_model_download_t* dl);

// Copies the current progress snapshot under mutex protection into out; returns
// false when download handle or output pointer is NULL for safe polling.
bool vw_model_download_poll(vw_model_download_t* dl, vw_download_progress_t* out);

// Joins the download thread if running, reaps any curl child process, frees
// internal mutex and heap allocations, and is safe to call with NULL.
void vw_model_download_free(vw_model_download_t* dl);

// Resolves per-user model directory (%LOCALAPPDATA%\vlc-whisper\models on Windows
// else $XDG_DATA_HOME/vlc-whisper/models or ~/.local/share/vlc-whisper/models) and
// ensures it exists with mkdir -p semantics; returns true on success.
bool vw_model_download_default_dir(char* out, size_t out_size);

// Deletes any stale *.part temporary files left from previous interrupted
// downloads inside dest_dir; invoked once at worker startup for cleanliness.
void vw_model_download_cleanup_partial(const char* dest_dir);

// Pure helper computing saturated progress percentage bytes_done*100/bytes_total,
// clamping to 100 and handling zero total without division by zero.
uint8_t vw_model_download_pct(uint64_t bytes_done, uint64_t bytes_total);

#ifdef __cplusplus
}
#endif

#endif  // VW_MODEL_DOWNLOAD_H_
