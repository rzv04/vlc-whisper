// Copyright 2026 VLC-Whisper Contributors. All rights reserved.
// Use of this source code is governed by the MIT License that can be found in the LICENSE file.

#ifndef VW_WORKER_LOG_POLICY_H_
#define VW_WORKER_LOG_POLICY_H_

#include <stdbool.h>
#include <stddef.h>

#include "vw_worker_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// Returns the platform-appropriate temporary directory used for default per-process worker logs.
const char* vw_worker_default_log_dir(void);

// Prunes older per-process default log files (matching "vlc-whisper-worker-*.log") in `dir`,
// ensuring at most `max_keep` files remain. Does not follow symlinks.
void vw_worker_prune_default_logs(const char* dir, size_t max_keep);

// Sets up the log file based on worker configuration. If default log is selected, prunes older
// default logs before opening the per-process log file exclusively.
void vw_worker_setup_log_file(const vw_worker_config_t* config);

// Flushes and closes the active worker log file handle, and unlinks 0-byte default logs.
void vw_worker_teardown_log_file(void);

#ifdef __cplusplus
}
#endif

#endif  // VW_WORKER_LOG_POLICY_H_
