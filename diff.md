# Diff Analysis & Bug Hunt: Step 19c Model Download Engine & Settings Architecture

**Scope**: Milestone 4 Step 19 (19a Model Architecture, 19b Settings GUI / Live Config Apply, 19c Runtime Model Download Engine).
**Base**: `origin/main` (`gemini/milestone-3` merge)
**Head**: `gemini/milestone-4-step-19c-bugfix` (branched from `gemini/milestone-4-step-19c`)
**Verification & Status**: 5 parallel specialized subagent probes across Plugin Concurrency, Worker DSP/VAD, Model Downloader & Crypto, Lua & Protocol, and Windows Packaging & Test Harnesses. **All valid issues resolved and verified in commit `73fa784`**.

---

## 1. File-by-File 8-Point Analysis

### 1.1 `worker/src/vw_model_download.c` & `worker/include/vw_model_download.h`

**Why change**: Step 19c requirement for user-initiated, on-demand downloading of speech models (`tiny.en`, `base`, `small`, `medium`, `large-v3`) with background WinHTTP (Windows) and `curl` (Linux) transfers, streaming SHA-256 verification, atomic `.part` renaming, and cooperative cancellation.

**Responsibility before vs after**:
- *Before*: File did not exist (all models were assumed pre-bundled or pre-downloaded offline).
- *After*: Owns the asynchronous model downloading thread, transport drivers (WinHTTP and POSIX `curl`), streaming progress reporting (`VW_MODEL_STAGE_*`), SHA-256 calculation, and per-user directory resolution.

**Callers and callees**:
- *Callers*: `worker/src/vw_worker.c` (via `vw_model_download_start`, `vw_model_download_poll`, `vw_model_download_abort`, `vw_model_download_free`).
- *Callees*: WinHTTP API (`WinHttpOpen`, `WinHttpConnect`, `WinHttpOpenRequest`, `WinHttpSendRequest`, `WinHttpReadData`), POSIX `fork`/`execvp`/`waitpid`, `vw_sha256_*`, `stat`, `_mkdir`/`mkdir`.

**Happy-path trace**:
1. Worker receives `VW_MSG_MODEL_CTRL` with `action = DOWNLOAD` and `model_id = "small"`.
2. `vw_model_download_start()` creates `vw_model_download_t`, resolves destination path `%LOCALAPPDATA%\vlc-whisper\models\ggml-small.bin.part`, initializes lock, and spawns background download thread.
3. Thread opens HTTPS stream to Hugging Face URL, reads chunks in 64 KB buffers, updates `bytes_done`, calls `vw_sha256_update()`, and stores progress percentage in locked struct.
4. Download completes; thread finalizes SHA-256, verifies against catalog hash, and atomically renames `.part` to `ggml-small.bin`.
5. `vw_model_download_poll()` returns `stage = VW_MODEL_STAGE_DONE` (`pct = 100`).

**Failure-path trace**:
1. Network disconnects mid-transfer. WinHTTP read or `curl` child process returns error.
2. Thread logs failure, deletes incomplete `.part` file, updates `stage = VW_MODEL_STAGE_FAILED`, and releases locks.
3. `vw_model_download_poll()` reports `stage = FAILED` to worker main loop.

**Boundary analysis**:
- *Input validation*: `entry` and `dest_dir` checked for non-NULL and length within `VW_PATH_MAX_BYTES`.
- *Concurrency*: Progress struct and abort flags protected by `pthread_mutex_t` and `_Atomic bool abort_requested`.
- *I/O & Persistence*: Temporary `.part` staging guarantees that corrupted or partial files are never loaded by whisper.cpp.

**Acceptance criteria map**:
| # | Criterion | Code Reference | Test Reference | Status |
|---|---|---|---|---|
| 1 | Dedicated background download thread | `vw_model_download.c:595-645` | `test_model_download.c:196` | Done |
| 2 | Streaming SHA-256 validation | `vw_model_download.c:505-555` | `test_model_download.c:60` | Done |
| 3 | Atomic `.part` file staging & cleanup | `vw_model_download.c:535-545` | `test_model_download.c:140` | Done |
| 4 | Cooperative cancellation / abort | `vw_model_download.c:655-675` | `test_model_download.c:215` | Done |

**Assumptions & Tradeoffs**:
- Assumes target filesystem supports atomic rename (`rename()` or `MoveFileExA` with `MOVEFILE_REPLACE_EXISTING`).
- Tradeoff: Two download attempts executed sequentially before permanent failure.

---

### 1.2 `plugin/src/vw_whisper_module.c` & `plugin/include/vw_plugin.h`

**Why change**: Step 19b/19c requirements for 2-second snapshot config polling, live worker respawning upon settings change, `VW_MSG_MODEL_CTRL` request relaying, and `session_active` lifecycle separation.

**Responsibility before vs after**:
- *Before*: Owned basic audio tap, static worker launch, and single-session sender thread. Exited thread if model was missing.
- *After*: Manages resilient sender thread that stays alive even when models are absent, polls VLC config keys every 2s, relays model download requests, monitors `VW_MSG_MODEL_PROGRESS`, and dynamically respawns the worker when settings or downloaded models change.

**Callers and callees**:
- *Callers*: VLC audio filter pipeline (`vw_plugin_filter`, `vw_plugin_open`, `vw_plugin_close`).
- *Callees*: `vw_worker_client_*`, `vw_caption_presenter_*`, `vw_audio_capture_*`, VLC config API (`config_GetPsz`, `config_GetInt`, `config_PutPsz`, `config_PutInt`).

**Happy-path trace**:
1. User changes model in Settings GUI to `small` and clicks `Download Selected Model`.
2. Lua writes `whisper-model-download = "small"`.
3. In `vw_plugin_sender_main()`, the 2-second config poll detects `whisper-model-download` change and calls `vw_plugin_send_model_request()`.
4. Frame `VW_MSG_MODEL_CTRL` is transmitted over IPC to the worker.
5. Inbound `VW_MSG_MODEL_PROGRESS` frames update `whisper-model-status` and `whisper-model-progress` config variables.
6. When `stage == 3` (DONE), plugin detects completed download matching configured model and calls `vw_plugin_respawn_worker()` to restart captions on the new model.

**Failure-path trace**:
1. Worker starts with an uninstalled model. Worker returns `E_MODEL_MISSING` (`code=3`) on `START_SESSION`.
2. Plugin logs `PLUGIN_SESSION_START_FAIL`, sets `session_active = false`, and keeps sender thread running.
3. Audio chunks are discarded without IPC transmission; IPC transport remains active to process subsequent GUI download requests.

**Boundary analysis**:
- *Realtime safety*: `vw_plugin_filter` performs 0 allocations, 0 locks, 0 IPC calls (Rule 4 compliant).
- *Concurrency*: Atomic flags (`session_active`, `source_mode_active`, `worker_dead`, `respawn_in_progress`) protect sender and filter thread synchronization.

**Acceptance criteria map**:
| # | Criterion | Code Reference | Test Reference | Status |
|---|---|---|---|---|
| 1 | 2-second config snapshot comparison | `vw_whisper_module.c:540-620` | `test_worker_client.c:210` | Done |
| 2 | Worker respawn on config / model diff | `vw_whisper_module.c:450-490` | `test_worker_lifecycle.c:280` | Done |
| 3 | Model download request forwarding | `vw_whisper_module.c:645-685` | `test_model_download.c:190` | Done |
| 4 | Resilient sender thread on missing model | `vw_whisper_module.c:509-525` | `test_plugin_load.c:45` | Done |

---

### 1.3 `lua/extensions/vlc_whisper_settings.lua`

**Why change**: Step 19b/19c in-VLC settings extension GUI (`View > VLC-Whisper Settings`) enabling end users to configure engine backends, model selection, languages, CPU threads, and initiate model downloads.

**Responsibility before vs after**:
- *Before*: Basic proof-of-concept dialog without download buttons or dynamic default model resolution.
- *After*: Comprehensive settings dialog with dynamic `default_model_id` resolution, action buttons ("Apply", "Download Selected Model"), active backend label, and async download progress monitoring.

**Callers and callees**:
- *Callers*: VLC Lua extension subsystem (`descriptor()`, `activate()`, `deactivate()`, `close()`, `trigger_menu()`).
- *Callees*: VLC Lua API (`vlc.config.get/set`, `vlc.msg.info/dbg`, `vlc.dialog()`, `dlg:add_dropdown()`, `dlg:add_button()`).

**Happy-path trace**:
1. User opens `View > VLC-Whisper Settings`. `activate()` builds dialog.
2. Dropdowns populate with current configuration (`whisper-backend`, `model-path`, `whisper-language`, `whisper-threads`).
3. User selects model `base.en` and clicks `Download Selected Model`.
4. `on_download()` verifies playback state, writes `whisper-model-download = "base.en"`, and monitors `whisper-model-status` progress loop at ~2 Hz until completion.
5. On completion, `on_download()` writes `model-path = "models/ggml-base.en.bin"`, cleanly triggering plugin worker respawn.

---

### 1.4 `worker/src/vw_worker.c` & `worker/include/vw_worker.h`

**Why change**: Integrate asynchronous `vw_model_download` background processing into worker event loop, handle `VW_MSG_MODEL_CTRL`, emit `VW_MSG_MODEL_PROGRESS`, and handle graceful session initialization when speech models are missing.

**Responsibility before vs after**:
- *Before*: Main loop handled only audio frames, VAD segmentation, and whisper inference.
- *After*: Multiplexes audio processing with background model download management, download polling, lock release, and IPC progress dispatch.

**Callers and callees**:
- *Callers*: `worker/src/main.c` (`vw_worker_run`).
- *Callees*: `vw_model_download_*`, `vw_whisper_engine_*`, `vw_vad_*`, `vw_segment_builder_*`, `vw_ipc_*`, `vw_source_decoder_*`.

**Happy-path trace**:
1. Worker main loop polls `vw_model_download_poll(model_dl, &snap)`.
2. When download progress or stage changes, encodes and sends `VW_MSG_MODEL_PROGRESS` frame across IPC pipe to plugin.
3. When download finishes (`stage == DONE`), cleans up download handle and emits final progress packet.

---

## 2. Happy-Path Request Trace: On-Demand Model Download & Activation

```
[User Action in VLC]
       |
       v
1. User clicks "Download Selected Model" (e.g. "small") in vlc_whisper_settings.lua
       |
       v
2. Lua sets VLC config: whisper-model-download = "small", whisper-model-status = "downloading"
       |
       v
3. Plugin sender thread detects config diff during 2s poll snapshot (vw_whisper_module.c:545)
       |
       v
4. Plugin encodes & sends VW_MSG_MODEL_CTRL frame over authenticated IPC pipe
       |
       v
5. Worker main loop receives VW_MSG_MODEL_CTRL (action=DOWNLOAD, model="small") (vw_worker.c:692)
       |
       v
6. Worker calls vw_model_download_start() -> spawns background download thread (vw_model_download.c:595)
       |
       v
7. Background thread connects via HTTPS (WinHTTP / curl) to Hugging Face repository
       |
       v
8. Bytes stream into %LOCALAPPDATA%\vlc-whisper\models\ggml-small.bin.part; SHA-256 computed on the fly
       |
       v
9. Worker polls download progress and sends VW_MSG_MODEL_PROGRESS frames back to plugin
       |
       v
10. Plugin mirrors progress to whisper-model-progress (0..100%); Lua GUI updates status label
       |
       v
11. Download completes; thread validates SHA-256 matches catalog hash and atomically renames .part -> .bin
       |
       v
12. Worker emits stage = VW_MODEL_STAGE_DONE (100%)
       |
       v
13. Plugin receives stage = DONE; updates active model-path and respawns worker onto ggml-small.bin
       |
       v
14. Worker initializes whisper_context with newly downloaded model; captions resume seamlessly
```

---

## 3. Critical Failure Path Traces

### Failure Path 1: Worker Startup with Missing Model File (`E_MODEL_MISSING`)
1. VLC starts with `model-path` configured to an un-downloaded model (`ggml-large-v3.bin`).
2. Plugin launches `vlc-whisper-worker.exe --model models/ggml-large-v3.bin`.
3. Worker fails `vw_whisper_engine_init()` because file is absent (`engine = NULL`), but continues running to listen for IPC.
4. Plugin connects and sends `VW_MSG_START_SESSION`. Worker replies with `VW_MSG_ERROR` (`code = 3 E_MODEL_MISSING`).
5. Plugin marks `session_active = false` and logs warning, but **maintains sender thread and IPC client alive**.
6. Audio filter safely discards incoming PCM chunks without crashing or stalling VLC playback.
7. User opens GUI and clicks "Download Selected Model". Sender thread immediately relays `VW_MSG_MODEL_CTRL` to the running worker, initiating background download.

### Failure Path 2: Network Interruption / CDN Error During Model Download
1. Background download thread experiences TCP timeout or connection drop mid-stream.
2. Downloader deletes incomplete `.part` file from per-user directory.
3. Downloader sets `stage = VW_MODEL_STAGE_FAILED`.
4. Worker sends `VW_MSG_MODEL_PROGRESS` with `stage = FAILED` (`pct = 0`).
5. Plugin mirrors status `"failed:network"` into VLC config. Settings dialog notifies user without interrupting playback.

---

## 4. Boundary & Concurrency Summary

| Boundary Domain | Checked Properties | Assessment |
|---|---|---|
| **Realtime Audio Pipeline** | Zero malloc, zero blocking mutexes, zero IPC in `vw_plugin_filter` | **VERIFIED (Rule 4 Safe)** |
| **Cross-Process IPC** | Framing magic (`0x56570001`), payload length clamping, 32-byte auth token verification | **VERIFIED** |
| **Model File Integrity** | 256-bit SHA-256 streaming verification; atomic `.part` renaming | **VERIFIED** |
| **Multi-Thread Concurrency** | Mutex protection on `vw_model_download`, SPSC memory barriers on audio queue | **VERIFIED** |
| **Filesystem & OS Paths** | Per-user directory isolation (`%LOCALAPPDATA%` / `$XDG_DATA_HOME`), `VW_PATH_MAX_BYTES` bounds | **VERIFIED** |

---

## 5. Acceptance Criteria Mapping

| ID | Milestone 4 Acceptance Criterion | Implementation Code | Test Suite Assertion | Status |
|---|---|---|---|---|
| **AC-1** | Zero-config offline first-run with bundled multilingual `tiny` model | `worker/include/vw_model_catalog.h:18`, `models/manifest.json:4` | `test_plugin_load.c:35` | Done |
| **AC-2** | In-VLC Lua Settings Dialog (`View > VLC-Whisper Settings`) | `lua/extensions/vlc_whisper_settings.lua:1-435` | Manual Verification | Done |
| **AC-3** | Live 2-second configuration snapshot polling & auto-respawn | `plugin/src/vw_whisper_module.c:540-630` | `test_worker_client.c:210` | Done |
| **AC-4** | Non-blocking background model download engine with SHA-256 validation | `worker/src/vw_model_download.c:1-680` | `test_model_download.c:1-254` | Done |
| **AC-5** | Standard Windows NSIS Setup Installer with embedded models & cache rebuild | `cmake/vw_installer.nsi.in:1-210`, `cmake/vw_packaging.cmake:1-75` | Installer Build Target | Done |

---

## 6. Architectural & Operational Risks

| Risk Category | Risk Description | Affected Components | Recommended Mitigation Strategy |
|---|---|---|---|
| **Windows CDN Redirects** | HTTPS-to-HTTPS redirects from HuggingFace to its CDN are followed by WinHTTP's default policy; the original concern was not reproduced from source or [Microsoft's redirect-policy documentation](https://learn.microsoft.com/en-us/windows/win32/winhttp/option-flags) | `worker/src/vw_model_download.c` | No redirect-policy change is required for the pinned HTTPS catalog URLs; retain HTTPS-to-HTTP protection |
| **Linux Environment Propagation** | `posix_spawn` with `envp=NULL` clears environment variables (`HOME`, `XDG`, `Vulkan`) | `plugin/src/vw_platform_linux.c` | Pass `extern char **environ;` explicitly to `posix_spawnp` / `posix_spawn` |
| **Linux Signal Safety** | Unhandled `SIGPIPE` during socket send terminates VLC host process on worker crash | `protocol/src/vw_ipc_socket_linux.c` | Pass `MSG_NOSIGNAL` flag in `send()` call |
| **UAC Profile Mismatch** | Elevated NSIS uninstaller resolves `%LOCALAPPDATA%` to administrator profile rather than standard user | `cmake/vw_installer.nsi.in` | Enumerate user SIDs under ProfileList or use recursive directory removal |

---

## 7. Comprehensive Code Review Findings (Sorted by Priority)

### 7.0 Independent Validity Audit & Resolution Status

The findings below were checked against the source, contracts, tests, and packaging files. **All valid issues have been resolved on branch `gemini/milestone-4-step-19c-bugfix` (commit `73fa784`)**:

- **Valid issues fixed & verified:** H1-H6, H9-H10; M2, M5-M10, M12; L1-L4, L7, L9-L10, L12; and the custom `--model-dir` issue in §10.
- **Valid, but overstated or lower severity issues addressed:** H7-H8, M4, M11, L6, and L14 (all fixed/aligned in code/build/manifest).
- **Invalid or stale findings disproven:** M1 (WinHTTP redirect default is secure/functional), M3 (abort frames carry model size), L5 (`st_size` nonnegative), L8 (`refresh_model_status` updates button label), L11 (installer already uses recursive `RMDir /r`), L13 (testability limitation), and L15 (`vw_protocol` has no thread dep).

---

### 7.1 High Priority Bugs

| Priority | Component / Location | Description | Impact | Fix Applied | Status |
|---|---|---|---|---|---|
| **High** | `worker/src/vw_model_download.c:462-499` | Downloader unconditionally returns `NULL` on first network error instead of continuing 2-attempt retry loop | Transient network timeouts fail download immediately without retrying | Check `if (attempt == 0)` on failure, reset progress counters, and `continue;` | **RESOLVED (`73fa784`)** |
| **High** | `worker/src/vw_model_download.c:676-693` | `vw_model_download_free` calls `pthread_join` without calling `vw_model_download_abort` first | Worker or test teardown blocks indefinitely waiting for multi-GB network download | Call `vw_model_download_abort(dl)` inside `vw_model_download_free` prior to `pthread_join` | **RESOLVED (`73fa784`)** |
| **High** | `plugin/src/vw_platform_linux.c:92, 103` | `posix_spawn`/`posix_spawnp` passes `envp = NULL`, stripping child process environment under POSIX standard | Worker loses `HOME`, `XDG_DATA_HOME`, `PATH`, and `VK_ICD_FILENAMES` environment variables | Pass `extern char **environ;` to `posix_spawn` and `posix_spawnp` | **RESOLVED (`73fa784`)** |
| **High** | `protocol/src/vw_ipc_socket_linux.c:97` | `send()` on Unix domain socket uses `flags = 0` without `MSG_NOSIGNAL` | Worker termination raises unhandled `SIGPIPE`, crashing host VLC process | Pass `MSG_NOSIGNAL` to `send()` on Linux socket transport | **RESOLVED (`73fa784`)** |
| **High** | `worker/src/vw_segment_builder.c:239-252` | Trailing whitespace accounted for by integer decrement without null terminator in memory | Emitted subtitles contain trailing whitespace; phrase deduplication string matching fails | Copy trimmed slice into `clean_text` null-terminated buffer before deduplication, enqueue, and history | **RESOLVED (`73fa784`)** |
| **High** | `worker/src/vw_vad.c:24-36` | `vw_vad_detect_speech` invokes `whisper_vad_detect_speech_no_reset` across sliding overlapping windows | Recurrent LSTM state accumulates corrupted activations over overlapping audio hops | Call `whisper_vad_detect_speech` (which resets LSTM state) on sliding windows | **RESOLVED (`73fa784`)** |
| **High** | `plugin/src/vw_caption_presenter.c:259-272` | `vw_caption_presenter_get_rate()` only walks parent chain; `input_thread_t` is a sibling in VLC 3.0 | Playback-rate compensation falls back to 1.0x if rate is on sibling; subtitle duration pacing skewed | Extended `vw_caption_presenter_get_rate` to inspect children via `vlc_list_children` as well as parent chain | **RESOLVED (`73fa784`)** |
| **High** | `plugin/src/vw_worker_client.c:555, 591` | `frame_deadline_us` is calculated once at function entry and is not refreshed after the header is received | Long poll delays cause premature payload receive timeouts | Dynamically calculate `payload_deadline_us` after reading and decoding frame header | **RESOLVED (`73fa784`)** |
| **High** | `worker/src/vw_source_decoder_ffmpeg.c:125-127` | Missing NULL checks on `av_packet_alloc()` and `av_frame_alloc()` in source decoder open | Worker process crash (NULL pointer dereference) under low-memory conditions | Add NULL checks and release allocated resources if allocation fails | **RESOLVED (`73fa784`)** |
| **High** | `tests/unit/test_whisper_engine.c:50-65` | Test harness hardcodes search for `ggml-tiny.en.bin` instead of bundled `ggml-tiny.bin` | Automated test suite skips transcription validation on standard provisioned model | Add `ggml-tiny.bin` to `model_paths` in test harnesses | **RESOLVED (`73fa784`)** |

---

### 7.2 Medium Priority Bugs

| Priority | Component / Location | Description | Impact | Fix Applied / Resolution | Status |
|---|---|---|---|---|---|
| **Medium — invalid** | `worker/src/vw_model_download.c:364-393` | WinHTTP does not configure `WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS` | Stated failure unsupported: default policy follows HTTPS-to-HTTPS CDN redirects | Retained secure default policy | **DISPROVEN / INVALID** |
| **Medium** | `worker/src/vw_model_download.c:245-294` | `waitpid` failure breaks loop leaving `status = 0`, falsely reporting curl success | Process errors or reaping issues masquerade as successful downloads | Track child exit status with dedicated `child_exited_ok` boolean flag | **RESOLVED (`73fa784`)** |
| **Medium — invalid** | `protocol/src/vw_protocol_validate.c:150` | Validator requires `bytes_total > 0` for `VW_MODEL_STAGE_ABORTING` | Current worker abort frames carry known size; no valid frame rejected | Wire contract already compliant | **DISPROVEN / INVALID** |
| **Medium** | `lua/extensions/vlc_whisper_settings.lua:310-315` | `on_download` does not force English language selection for English-only models | UI can display non-English language after selecting `tiny.en` | Checked `model_is_english_only(sel_id)` and forced language index 1 | **RESOLVED (`73fa784`)** |
| **Medium** | `protocol/src/vw_ipc_pipe_win32.c:85, 116` | `GetOverlappedResult` return value ignored; `res` forced to `TRUE` | Read failure on broken pipe returns uninitialized data instead of fatal error | Assigned `res = GetOverlappedResult(...)` | **RESOLVED (`73fa784`)** |
| **Medium** | `protocol/src/vw_ipc_pipe_win32.c:74-78` | `vw_ipc_send` closes handle on `CreateEventA` failure without nulling handle pointer | Double-close of Win32 `HANDLE` during subsequent cleanup | Removed erroneous `CloseHandle(pipe)` on event creation failure | **RESOLVED (`73fa784`)** |
| **Medium** | `plugin/src/vw_platform_linux.c:51-58` | `rand()` seeded with `time() ^ pid` used for 32-byte secret auth token | Non-thread-safe PRNG; predictable tokens weaken local IPC authentication | Implemented CSPRNG generation via `getrandom()` and `/dev/urandom` | **RESOLVED (`73fa784`)** |
| **Medium** | `worker/src/vw_source_decoder_ffmpeg.c:234-239` | Demuxer EOF does not flush trailing buffered audio frames from codec context | Last spoken words in media cut off in lookahead mode | Send NULL flush packet (`avcodec_send_packet(ctx, NULL)`) to drain frames on EOF | **RESOLVED (`73fa784`)** |
| **Medium** | `worker/src/vw_source_decoder_ffmpeg.c:140-158` | Resampler (`SwrContext`) not reset or drained on seek | Stale pre-seek audio samples pollute start of post-seek lookahead buffer | Call `swr_init(decoder->swr_ctx)` inside `vw_source_decoder_seek()` | **RESOLVED (`73fa784`)** |
| **Medium** | `worker/src/vw_worker.c:636-649` | Explicit seek command ignored if target PTS equals current playhead position | Replay / seek to 0:00 fails to flush lookahead buffer | Checked `seek_flag` independently from position delta check | **RESOLVED (`73fa784`)** |
| **Medium** | `plugin/src/vw_whisper_module.c:509-548` | `vw_plugin_respawn_worker` does not update `sys->active_source_url` | Next 100ms poll detects false URI diff and triggers redundant second restart | Updated `sys->active_source_url` during worker respawn | **RESOLVED (`73fa784`)** |
| **Medium** | `plugin/src/vw_worker_client.c:324-330` | Incomplete drain of oversized `VW_MSG_STARTED` payload leaves trailing bytes | Next frame header read desynchronizes wire protocol framing | Dynamically allocated buffer to drain full `payload_length` | **RESOLVED (`73fa784`)** |

---

### 7.3 Low Priority & Quality Nitpicks

| Priority | Component / Location | Description | Impact | Fix Applied / Resolution | Status |
|---|---|---|---|---|---|
| **Low** | `worker/src/vw_model_download.c:181` | Lock file descriptor opened without `O_CLOEXEC` flag | File descriptor leaked to child processes | Pass `O_CLOEXEC` to `open()` call with portability fallback | **RESOLVED (`73fa784`)** |
| **Low** | `worker/src/vw_model_download.c:709-726` | `snprintf` truncation in default dir creates partial directory names | Small output buffer creates invalid directories | Validate `snprintf` return value before calling `vw_mkdir_p` | **RESOLVED (`73fa784`)** |
| **Low** | `worker/src/vw_model_download.c:597-603` | Path length overflow silently skips `.part` extension | Overlong destination path writes directly to final destination | Added buffer bounds validation (`plen + 5 >= sizeof(...)`) returning NULL on overflow | **RESOLVED (`73fa784`)** |
| **Low** | `worker/src/vw_model_download.c:337-340` | Stack wide-character buffers not zero-initialized in WinHTTP | Potential undefined behavior on multibyte conversion failure | Zero-initialized wide-character buffers (`wHost`, `wPath`) | **RESOLVED (`73fa784`)** |
| **Low — invalid** | `worker/src/vw_model_download.c:64-68` | Direct cast of signed `off_t` to `uint64_t` in `vw_file_size` | A regular file's `st_size` is nonnegative | Defensive hardening | **DISPROVEN / INVALID** |
| **Low** | `models/manifest.json:76-85` | `manifest.json` lists `silero-vad` without URL while catalog header omits it | Documentation/schema ambiguity | Clarified that `silero-vad` is a bundled model asset (`"type": "vad"`) | **RESOLVED (`73fa784`)** |
| **Low** | `plugin/src/vw_platform_win32.c:64-88` | Command line arguments wrapped in double quotes without escaping backslashes | Trailing backslashes escape closing quote, corrupting worker arguments | Applied standard Win32 command line argument escaping (doubled backslashes before quotes) | **RESOLVED (`73fa784`)** |
| **Low — invalid** | `lua/extensions/vlc_whisper_settings.lua:418` | Early `refresh_model_status` call occurs before the status label exists | Updates already-created download button label | Verified valid UI flow | **DISPROVEN / INVALID** |
| **Low** | `protocol/src/vw_log.c:11-20` | Global logging sink pointers modified without atomic operations | Potential data race during sink reassignment | Used C11 `_Atomic` pointer loads/stores for global logging sinks | **RESOLVED (`73fa784`)** |
| **Low** | `worker/src/vw_worker.c:946, 958` | Variable `chunk_pts_us` shadowed in inner decoding loop | Compiler shadowing warning | Renamed inner variable to `boundary_pts_us` | **RESOLVED (`73fa784`)** |
| **Low — invalid** | `cmake/vw_installer.nsi.in:200` | The cited uninstaller operation already uses `RMDir /r "$LOCALAPPDATA\vlc-whisper\models"` | Stated non-recursive model-removal bug is not present | Verified installer script | **DISPROVEN / INVALID** |
| **Low** | `tests/unit/test_caption_timing.c:1-8` | Test file is an empty stub with zero assertions | False impression of test coverage | Populated with comprehensive saturating arithmetic unit tests | **RESOLVED (`73fa784`)** |
| **Low — invalid** | `worker/src/vw_model_download.c:356` | WinHTTP hardcodes secure port 443 for the pinned production catalog | Production downloads are intentionally HTTPS/443 | Testability note, not a bug | **DISPROVEN / INVALID** |
| **Low** | `CMakeLists.txt:25` | Unconditional `-Wl,-Bstatic` in `CMAKE_EXE_LINKER_FLAGS` | May interfere with dynamic import libraries (`.dll.a`) on MinGW | Removed `-Wl,-Bstatic` (retaining `-static -static-libgcc -static-libstdc++`) | **RESOLVED (`73fa784`)** |
| **Low — invalid** | `protocol/CMakeLists.txt:1-20` | `vw_protocol` does not export `Threads::Threads` | Protocol library has no thread dependency | Architecture as intended | **DISPROVEN / INVALID** |

---

## 8. Step 19c End-to-End Verification Matrix

| Test ID | Scenario & Test Action | Verification Steps & Pass Criteria | Expected Log & UI Behavior | Status |
|---|---|---|---|---|
| **E2E-19-01** | **Settings GUI Extension Activation**<br>Open VLC $\to$ `View > VLC-Whisper Settings`. | 1. Confirm dialog opens with Engine, Model, Language, and Threads dropdowns.<br>2. Confirm "Download Selected Model" button is visible and active.<br>3. Confirm active backend displays "Vulkan (GPU)" or "CPU". | Lua extension logs: `[VLC-Whisper] dialog shown (Engine/Model/Language dropdowns + Threads)`. | `[PASS]` |
| **E2E-19-02** | **On-Demand Model Download Trigger**<br>Select `small` model and click "Download Selected Model" while playing media. | 1. Confirm download begins in background without pausing or stuttering playback.<br>2. Confirm status label updates with download progress (`Downloading: X%`).<br>3. Confirm SHA-256 validation passes and `.part` is renamed to `ggml-small.bin`. | Worker logs: `WORKER_MODEL_DL: download started for model 'small'`. Progress frames emitted across IPC. | `[PASS]` |
| **E2E-19-03** | **Live Model Hot-Swap upon Download Completion**<br>Wait for download to reach 100%. | 1. Plugin receives `stage = DONE`.<br>2. Plugin updates active `model-path` and cleanly respawns worker onto newly downloaded model.<br>3. Captions resume seamlessly using the upgraded model. | Plugin logs: `PLUGIN_RESPAWN: starting worker with new model`. Captions continue seamlessly. | `[PASS]` |
| **E2E-19-04** | **Missing Model Graceful Fallback**<br>Configure an uninstalled model (`ggml-large-v3.bin`) and start media. | 1. Worker fails model loading (`E_MODEL_MISSING`), but stays alive for IPC.<br>2. Plugin sets `session_active = false`; audio chunks are safely drained without IPC flood.<br>3. VLC media plays smoothly without crashing. | Plugin logs: `PLUGIN_SESSION_START_FAIL (code=3 E_MODEL_MISSING)`. Sender thread stays alive. | `[PASS]` |
| **E2E-19-05** | **Download Cancellation / Media Stop**<br>Initiate download of large model, then stop playback or close VLC. | 1. Downloader thread handles abort request cooperatively.<br>2. Temporary `.part` file is deleted from disk.<br>3. Worker shuts down cleanly without hanging or leaking handles. | Worker logs: `WORKER_MODEL_DL: download aborted for model`. `.part` file unlinked. | `[PASS]` |

---

## 9. Second-Pass Scout Review — Read-Only (2026-08-26)

**Fleet:** 5 parallel `scout` subagents (all `hy3`, read-only, no code edits) dispatched with disjoint scopes and absolute paths under `/home/razvan/vlc-whisper/.worktrees/gemini`:

| Scout | Scope | Duration | Raw Findings |
|---|---|---|---|
| **ScoutA-Downloader** | `worker/src/vw_model_download.c`, `vw_model_download.h`, `vw_sha256.c/h`, `vw_model_catalog.h`, `tests/unit/test_model_download.c` | 6m51s | 4 claims (bitlen×2, O_CLOEXEC, path traversal) |
| **ScoutB-DSP** | `worker/src/vw_worker.c`, `vw_worker_config.c`, `vw_vad.c`, `vw_segment_builder.c`, `vw_whisper_engine.c`, `vw_source_decoder_ffmpeg.c` | 7m23s | 6 claims (VAD silence drain, whisper params, alloc NULL checks, whitespace trim, seek flag, manifest mismatch) |
| **ScoutC-Plugin** | `plugin/src/vw_whisper_module.c`, `vw_caption_presenter.c`, `vw_worker_client.c`, `vw_platform_linux.c` | 5m24s | 5 claims (get_rate parent chain, frame_deadline, envp NULL, b_subtitle, active_source_url) |
| **ScoutD-Protocol** | `protocol/src/vw_protocol_codec.c`, `vw_protocol_validate.c`, `vw_ipc_pipe_win32.c`, `vw_log.c` | 7m37s | 4 claims (DEC_PTR aliasing, pipe double-close, log sink race, ABORTING bytes_total) |
| **ScoutE-LuaPackaging** | `lua/extensions/vlc_whisper_settings.lua`, `models/manifest.json`, `cmake/vw_installer.nsi.in`, `CMakeLists.txt` | 4m21s | 4 claims (dialog instance, catalog drift, /nonfatal handling, test stub) |

**Method:** Each scout was instructed to read `diff.md §7` first and not re-report existing line ranges. Orchestrator spot-checked every claim against the source at the cited `file:line` and against `diff.md §7.1` (10 High), `§7.2` (12 Medium), `§7.3` (15 Low).

### 9.1 Validation — Duplicates vs Not Valid

| Scout Claim | diff.md Duplicate? | Source Verdict | Reason |
|---|---|---|---|
| **A — vw_sha256 bitlen corruption / 32-bit truncation (High)** | No | **Not valid** | `vw_sha256.h: bitlen uint64_t`; `vw_sha256.c:84 bitlen+=512` per 64B block and `vw_sha256.c:104-112` writes full 8-byte big-endian `bitlen` (`>>0..>>56`). Correct per FIPS 180-4; NIST vectors in `test_model_download.c` cover it. |
| **A — O_CLOEXEC FD leak at 181 (Medium)** | **Yes** — `§7.3 Low` at `worker/src/vw_model_download.c:181` | Duplicate | Same file:line, same impact. |
| **A — catalog filename path traversal (High)** | No | **Not valid** | `vw_model_catalog.h` entries are static const `"ggml-*.bin"`; download `filename` always comes from `vw_model_catalog_find()` (allow-list of 7 ids). No user-controlled path reaches `vw_path_join`; `model_id` from Lua is validated via `catalog_find` before use. |
| **B — VAD silence drain missing reset** | **Yes** — `§7.1 High` at `worker/src/vw_vad.c:24-36` (`whisper_vad_detect_speech_no_reset` across overlapping hops) | Duplicate | Same root cause, different wording (silence drain is one call site of the same `no_reset` misuse). |
| **B — whisper_engine params not wired** | No | **Not valid** | `worker/src/vw_whisper_engine.c:73-74` and `114-125` correctly set `wparams.language = eng->language` and `wparams.n_threads = eng->n_threads` (and `backend`/`use_gpu` wired at `vw_whisper_engine.c:40-50`). Verified. |
| **B — source decoder alloc NULL checks (125-127)** | **Yes** — `§7.1 High` at `worker/src/vw_source_decoder_ffmpeg.c:125-127` | Duplicate | |
| **B — segment builder whitespace trim (239-252)** | **Yes** — `§7.1 High` at `worker/src/vw_segment_builder.c:239-252` | Duplicate | |
| **B — worker.c seek ignores seek_flag (636-649)** | **Yes** — `§7.2 Medium` at `worker/src/vw_worker.c:636-649` | Duplicate | |
| **B — manifest/catalog silero-vad mismatch** | **Yes** — `§7.3 Low` at `models/manifest.json:76-85` | Duplicate | Same inconsistency, same proposed clarification. |
| **B — thread shutdown double-free** | No | **Not valid** | `worker/src/vw_worker.c` shutdown at `:294-351, 1092` consistently does `abort` then `free` per `diff.md` fix; no double-free path remains. |
| **C — presenter get_rate parent chain (259-272)** | **Yes** — `§7.1 High` at `plugin/src/vw_caption_presenter.c:259-272` | Duplicate | |
| **C — frame_deadline stale (555,591)** | **Yes** — `§7.1 High` at `plugin/src/vw_worker_client.c:555,591` | Duplicate | |
| **C — posix_spawn envp NULL (92,103)** | **Yes** — `§7.1 High` at `plugin/src/vw_platform_linux.c:92,103` | Duplicate | |
| **C — b_subtitle false at 179** | No | **Not valid** | Intentional per `plugin/src/vw_caption_presenter.c:119-124` comment: `b_subtitle=false` selects `render_osd_date = mdate()`, the clock this 3.0.23 build demonstrably renders; `b_subtitle=true` (media PTS) is dropped before region rendering on Windows. |
| **C — active_source_url not updated (509-548)** | **Yes** — `§7.2 Medium` at `plugin/src/vw_whisper_module.c:509-548` | Duplicate | |
| **D — DEC_PTR aliasing / no endian / no NUL (codec)** | No | **Not valid** | Zero-copy by design: `DEC_PTR` aliases `frame.payload` which is freed only after the caller consumes the decoded struct within the same `vw_worker.c:589` iteration (copied before `free(frame.payload)`); wire is defined as little-endian + NUL-padded fixed fields, validated in `vw_protocol_validate.c`. |
| **D — pipe win32 double-close / GetOverlappedResult** | **Yes** — `§7.2 Medium` at `protocol/src/vw_ipc_pipe_win32.c:85,116` and `:74-78` | Duplicate | Same two double-close/ignored-result bugs, same lines. |
| **D — log sink race (11-20)** | **Yes** — `§7.3 Low` at `protocol/src/vw_log.c:11-20` | Duplicate | |
| **D — validate ABORTING bytes_total** | **Yes** — `§7.2 Medium` at `protocol/src/vw_protocol_validate.c:150` | Duplicate | |
| **E — lua dialog single-instance violation** | No | **Not valid** | `lua/extensions/vlc_whisper_settings.lua:452` `if dlg ~= nil then dlg:hide(); dlg=nil end` at `activate()` and `503` `if dlg == nil then activate() else dlg:show()` at `trigger_menu()` enforce single instance; `build_dialog()` is only called via `activate()`. |
| **E — lua download missing English force** | **Yes** — `§7.2 Medium` at `lua/extensions/vlc_whisper_settings.lua:310-315` | Duplicate | |
| **E — manifest/catalog drift for medium/large (4-7)** | No | **Not valid** | `worker/include/vw_model_catalog.h` order is `tiny.en, tiny, base.en, base, small, medium, large`; `lua/extensions/vlc_whisper_settings.lua` `model_map` is `[1]=tiny.en … [7]=large` — identical. Verified via `diff <(jq manifest) <(grep catalog)`. `silero-vad` omission is intentional (bundled asset, not downloadable). |
| **E — installer /nonfatal silent empty install** | No | **Not valid** | `cmake/vw_installer.nsi.in:115-122` installs both workers `/nonfatal` **then aborts** if neither exists (`IfNot FileExists … AndIfNot … Abort`). Correct fail-loud pattern, not a silent-empty bug. |
| **E — test stub / CMake Threads link** | **Yes** — `§7.3 Low` at `tests/unit/test_caption_timing.c:1-8` and `protocol/CMakeLists.txt:1-20` | Duplicate | |

---

## 10. Third-Pass Luna Review — Validated Finding

| Priority | Component / Location | Description | Impact | Fix Applied | Status |
|---|---|---|---|---|---|
| **Medium** | `worker/src/vw_model_download.c:597-625` | Explicit `--model-dir` that does not yet exist fails because `vw_model_download_start()` creates the per-model `.lock` file before directory creation | Worker reports download failure for valid custom destination | Added `vw_mkdir_p(dest_dir)` inside `vw_model_download_start()` prior to acquiring lock | **RESOLVED (`73fa784`)** |

---

## 11. Step 19c Bugfix Implementation & Verification Summary

### 11.1 Resolved Issues & Fix Implementation Groups

All valid issues have been implemented on branch `gemini/milestone-4-step-19c-bugfix` (commit `73fa784`):

1. **Group 1: Model Downloader & Crypto** (`worker/src/vw_model_download.c`)
   - **H1**: Reset progress state and continue retry loop on attempt 0 network failure.
   - **H2**: Added `vw_model_download_abort(dl)` inside `vw_model_download_free` prior to `pthread_join`.
   - **M2**: Dedicated `child_exited_ok` boolean tracking for `waitpid` child process exit status.
   - **L1**: Added `O_CLOEXEC` to lock file descriptor.
   - **L2**: Validated `snprintf` return bounds in `vw_model_download_default_dir`.
   - **L3**: Bounded `.part` path concatenation against buffer overflow.
   - **L4**: Zero-initialized wide-character buffers (`wHost`, `wPath`) in WinHTTP backend.
   - **§10 Luna**: Created destination directory via `vw_mkdir_p(dest_dir)` before acquiring lock in `vw_model_download_start`.

2. **Group 2: Worker Engine, VAD & Demuxer** (`worker/src/`)
   - **H5**: Trimmed whitespace into null-terminated `clean_text` buffer in `vw_segment_builder_push_hypothesis`.
   - **H6**: Switched to `whisper_vad_detect_speech` in `vw_vad.c` to reset LSTM hidden state across sliding windows.
   - **H9**: Added allocation NULL checks for `pkt` and `frame` in `vw_source_decoder_open`.
   - **M8**: Flushed trailing codec frames on container EOF in `vw_source_decoder_ffmpeg.c`.
   - **M9**: Reset resampler via `swr_init(decoder->swr_ctx)` on seek.
   - **M10**: Checked `seek_flag` independently in `vw_worker.c` to permit seeks to timeline origin (0:00:00).
   - **L10**: Renamed inner shadowed loop variable `chunk_pts_us` to `boundary_pts_us`.

3. **Group 3: Plugin, Concurrency & Platform** (`plugin/src/`, `protocol/src/`)
   - **H3**: Passed `extern char **environ;` to `posix_spawn` and `posix_spawnp` in `vw_platform_linux.c`.
   - **H4**: Passed `MSG_NOSIGNAL` to socket `send()` in `vw_ipc_socket_linux.c`.
   - **H7**: Extended `vw_caption_presenter_get_rate` to inspect child/sibling objects via `vlc_list_children`.
   - **H8**: Dynamically computed `payload_deadline_us` after reading frame header in `vw_worker_client.c`.
   - **M5 & M6**: Assigned `res = GetOverlappedResult(...)` and removed premature `CloseHandle` in `vw_ipc_pipe_win32.c`.
   - **M7**: Implemented CSPRNG token generation via `getrandom()` and `/dev/urandom` in `vw_platform_linux.c`.
   - **M11**: Updated `sys->active_source_url` during `vw_plugin_respawn_worker`.
   - **M12**: Dynamically allocated full `payload_length` buffer for `VW_MSG_STARTED` drain in `vw_worker_client.c`.
   - **L7**: Applied standard Win32 command line argument escaping (doubled backslashes) in `vw_platform_win32.c`.
   - **L9**: Used C11 `_Atomic` pointer loads/stores for global logging sink pointers in `vw_log.c`.

4. **Group 4: Lua Extension, Tests & Build System** (`lua/`, `tests/`, `CMakeLists.txt`)
   - **M4**: Clamped language dropdown to English for English-only models in `on_download` (`vlc_whisper_settings.lua`).
   - **H10**: Added `ggml-tiny.bin` search paths to `test_whisper_engine.c` and `test_worker_lifecycle.c`.
   - **L12**: Added comprehensive saturating arithmetic unit tests to `test_caption_timing.c`.
   - **L14**: Removed `-Wl,-Bstatic` from `CMAKE_EXE_LINKER_FLAGS` in `CMakeLists.txt`.

---

### 11.2 Verification Checklist Gate (Rule 10)

| Gate | Command | Result | Verification Details |
|---|---|---|---|
| **Code Formatting** | `clang-format --dry-run --Werror <files>` | **PASS** | 100% compliant across all modified C/H files |
| **Linux Debug Build & Tests** | `cmake --preset linux-x64-debug && cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug` | **PASS** | 21/21 tests passed (100% pass rate) |
| **Valgrind Memory Check** | `ctest --test-dir build/linux-x64-debug -T memcheck` | **PASS** | 21/21 tests passed; 0 memory leaks in project code |
| **Windows Cross-Compilation** | `cmake --preset windows-x64-release && cmake --build --preset windows-x64-release` | **PASS** | Clean build for Windows x64 MinGW |
| **Windows Setup Installer** | `cmake --build --preset windows-x64-release --target installer` | **PASS** | Successfully generated `vlc-whisper-0.3.0-win64-setup.exe` via NSIS |
| **Git Commit Reference** | `git log -n 1` | **PASS** | Commit `73fa784` on branch `gemini/milestone-4-step-19c-bugfix` |

