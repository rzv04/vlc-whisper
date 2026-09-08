# Known Issues

This ledger records the comprehensive static bug audits conducted on branch `feat/local-asr-quality-benchmark`.
The first audit (2026-09-04) covered VLC Realtime Audio Plugins, SPU Subpicture Rendering, Inference Worker & Audio DSP,
Voice Activity Detection, Translation & Model Provisioning, C17 Wire Protocol, Platform IPC Transports, and Build/Packaging.
A second full audit (2026-09-05, five independent read-only reviewer agents using the Graphify knowledge graph) re-verified
every ledger entry against current source, corrected four entries, closed two as stale/fixed, and added new findings VW-076 … VW-104.

- Open findings below are source-confirmed against `feat/local-asr-quality-benchmark`. Entries whose description was corrected by
  the 2026-09-05 audit are marked **[re-verified 2026-09-05]** with the correction noted.
- The bottom section preserves resolved, stale, or false entries as reference for future reviewers. Do not re-open them without
  fresh evidence.

## Severity

- **P1 — Critical**: Process crash, deadlock, security vulnerability, privacy leak, realtime safety violation, or complete caption failure.
- **P2 — High**: Timeline desynchronization, caption loss/truncation, state machine flaw, performance stall, or resource limits.
- **P3 — Medium-Low**: Edge-case behavior, minor resource leak, telemetry inaccuracy, or code hygiene defect.

---

## Priority 1 — Critical

### VW-001 — Permanent Subtitle Freeze on Screen During Silence via `b_ephemer = true`

- **Priority**: P1
- **Status**: Closed (Fixed in `fix/reconciled-p1-defects` via `b_ephemer = false`)
- **Affected**: `plugin/src/vw_caption_presenter.c:126` (also lines 117-135, 359-362)
- **Trigger**: Any dialogue segment finishes and is followed by silence (e.g. pause in speech, quiet movie scene).
- **Impact**: In `vw_caption_presenter_render_spu()`, setting `subpic->b_ephemer = true;` instructs VLC's SPU engine to ignore `i_stop` and keep rendering the subpicture until a subsequent subpicture arrives. When silence ensues, the previous subtitle remains permanently frozen on the screen indefinitely.
- **Required fix**: Set `subpic->b_ephemer = false;` so VLC automatically hides and destroys the subpicture when `mdate()` reaches `subpic->i_stop`.

### VW-002 — Model Download Progress Subpicture Queue Leak & Permanent "Done (100%)" Banner

- **Priority**: P1
- **Status**: Closed (Fixed in `fix/reconciled-p1-defects` via `replace_existing = true`, `b_ephemer = false`, and progress channel clear)
- **Affected**: `plugin/src/vw_caption_presenter.c:211, 126`, `plugin/src/vw_whisper_module.c:1411-1418`
- **Trigger**: Background model download in progress and reaching completion.
- **Impact**: Progress updates render with `replace_existing = false`, continuously allocating and enqueuing subpicture objects in VLC's SPU channel queue without clearing previous ones. Additionally, on DONE the module zeroes `model_download_id` (1413) *before* `vw_plugin_respawn_worker()` runs, so the clear guard at module.c:590-593 is skipped and the success path never flushes the progress channel, freezing "Model <id>: done (100%)" (rendered with `b_ephemer = true`) across the top of the video for the entire playback duration.
- **Required fix**: Render model progress with `replace_existing = true`, set `b_ephemer = false`, clear the progress channel before zeroing `model_download_id`, and explicitly call `vw_caption_presenter_clear_model_progress` upon model activation.

### VW-004 — Worker Path Resolution Skipped on Respawn, Causing Indefinite 2-Second Sender Freezes

- **Priority**: P1
- **Status**: Closed (Fixed in `fix/reconciled-p1-defects` via `vw_plugin_resolve_worker_path()` on respawn)
- **Affected**: `plugin/src/vw_whisper_module.c:622-627, 984, 1032-1043`
- **Trigger**: Worker crashes or restarts when `worker-path` config is unconfigured or empty.
- **Impact**: `vw_plugin_respawn_worker()` passes empty `sys->worker_path` (`""`) directly to `vw_plugin_launch_with_auto_retry()`, skipping `vw_plugin_resolve_worker_path()`. No worker process is spawned, and the sender thread blocks for 2,000ms every 10 seconds in `vw_ipc_connect()` retries, permanently disabling captions. The comment at module.c:623 acknowledges the empty-path case but nothing resolves it.
- **Required fix**: In `vw_plugin_respawn_worker()`, if `sys->worker_path[0] == '\0'`, call `vw_plugin_resolve_worker_path()`.

### VW-005 — Insecure Temporary File Creation in `/tmp` (Symlink Arbitrary File Overwrite)

- **Priority**: P1
- **Status**: Open (re-confirmed 2026-09-05)
- **Affected**: `plugin/src/vw_benchmark.c:69-76`
- **Trigger**: Benchmark report writing on multi-user Linux systems.
- **Impact**: `vw_benchmark_write()` creates a secondary path by appending `.next` to `/tmp/vlc-whisper-benchmark-XXXXXX` and invokes `fopen(temp_path, "w")`. An unprivileged local attacker can pre-create a symlink at that predictable path to arbitrary user files, which VLC will truncate with the user's permissions. The post-open `chmod` does not mitigate the TOCTOU.
- **Required fix**: Use `open()` with `O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW` and wrap with `fdopen()`.

### VW-006 — Unhandled `SIGPIPE` in POSIX curl Subprocess Spawning

- **Priority**: P1
- **Status**: Closed (Fixed in `fix/reconciled-p1-defects` via `sigaction` `SIG_IGN` on SIGPIPE)
- **Affected**: `worker/src/vw_translate.c:786`
- **Trigger**: Child curl process exits abruptly while worker writes payload to `pipe_in`.
- **Impact**: Writing to a broken pipe without `MSG_NOSIGNAL` raises uncaught `SIGPIPE` (signal 13), immediately terminating the entire worker process and dropping VLC captions. No `SIGPIPE`/`SIG_IGN` handling exists anywhere under `worker/`.
- **Required fix**: Set `signal(SIGPIPE, SIG_IGN)` or use `send()` with `MSG_NOSIGNAL`.

### VW-007 — Bidirectional Blocking Pipe Deadlock in Curl Transport

- **Priority**: P1
- **Status**: Closed (Fixed in `fix/reconciled-p1-defects` via `poll()` multiplexing and non-blocking I/O)
- **Affected**: `worker/src/vw_translate.c:782-822`
- **Trigger**: Curl produces early output/diagnostics or translation payload exceeds standard pipe buffer (64KB).
- **Impact**: The parent process synchronously writes the entire payload (up to ~65,526 B, near the 64 KB pipe buffer) before reading from `pipe_out`. If curl fills `pipe_out` buffer or parent blocks on full `pipe_in`, both processes deadlock permanently.
- **Required fix**: Multiplex pipe reading and writing with non-blocking I/O and `poll()`.

### VW-008 — `WinHttpSendRequest` Parameter Failure on Fallback Tiers on Windows

- **Priority**: P1
- **Status**: Closed (Fixed in `fix/reconciled-p1-defects` via explicit 0 header length for NULL headers)
- **Affected**: `worker/src/vw_translate.c:633`
- **Trigger**: Tier 1 (Web RPC) fails or times out, initiating Tier 2 (GTX) or Tier 3 (Mobile) fallback on Windows.
- **Impact**: Passing `(DWORD)-1L` for headers length when `headers == NULL` violates WinHTTP API contract and fails with `ERROR_INVALID_PARAMETER` (87), completely disabling fallback translation tiers on Windows. The GTX/mobile tiers pass `content_type=NULL` (translate.c:919, 932), so exactly the fallback tiers hit the NULL-header case. Runtime failure is API-contract dependent (unverifiable on Linux); the source defect is present.
- **Required fix**: Pass `0` for headers length when `headers == NULL`.

### VW-010 — Media Foundation Buffer Lock Leak

- **Priority**: P1
- **Status**: Closed (Fixed in `fix/reconciled-p1-defects` via guaranteed Unlock before Release)
- **Affected**: `worker/src/vw_source_decoder_mf.c:260-261, 311, 313`
- **Trigger**: Media Foundation decodes a sample where `cbCurrentLength == 0`.
- **Impact**: `IMFMediaBuffer::Lock()` is acquired, but `Unlock()` is skipped on the zero-length branch, and `Release()` is called on a locked buffer, leaking COM allocator locks and stalling subsequent decodes.
- **Required fix**: Ensure `pBuffer->Unlock()` is invoked before releasing on all exit branches.

### VW-012 — Local Denial of Service via `accept()` Connection Hijack in Unix IPC

- **Priority**: P1
- **Status**: Open (re-confirmed 2026-09-05)
- **Affected**: `protocol/src/vw_ipc_socket_linux.c:61-62, 68-75`
- **Trigger**: Unauthorized local process connects to `/tmp/vlc-whisper-*.sock` before legitimate VLC plugin.
- **Impact**: `vw_ipc_listen()` calls `accept()` and immediately closes `server_fd` (line 62) before validating peer UID. When peer validation fails, it unlinks the socket and terminates, permanently blocking the VLC plugin from connecting.
- **Required fix**: Keep `server_fd` open until peer credentials match `geteuid()`; close only the unauthorized client descriptor.

### VW-013 — Insecure Socket Creation Permissions via Umask TOCTOU Race

- **Priority**: P1
- **Status**: Open (re-confirmed 2026-09-05)
- **Affected**: `protocol/src/vw_ipc_socket_linux.c:38-46`
- **Trigger**: Unix domain socket creation under default system umask (`0022` or `0002`).
- **Impact**: `bind()` creates the socket file in `/tmp` with `0755` permissions before `chmod(0600)` is invoked, allowing other local users to connect during the race window.
- **Required fix**: Wrap `bind()` with `umask(0077)`.

### VW-014 — 32-Byte Secret Authentication Token Leaked via Process Command Line

- **Priority**: P1
- **Status**: Open (re-confirmed 2026-09-05)
- **Affected**: `plugin/src/vw_worker_client.c:102-104`, `worker/src/vw_worker_config.c:293-301`
- **Trigger**: Worker process launch on Linux.
- **Impact**: The secret token is passed via `argv` (`--token <hex>`) and never zeroed after `vw_token_from_hex()`; POSIX `main.c:143` keeps argv live for the process lifetime. It is globally readable by any local user via `/proc/<pid>/cmdline` or `ps`, defeating local IPC token security.
- **Required fix**: Pass the token via an inherited pipe (stdin) or immediately overwrite `argv[i]` in memory with zeroes after reading.

### VW-018 — Data Race and Type Confusion in `vw_log_set_sink`

- **Priority**: P1
- **Status**: Closed (Fixed in `fix/reconciled-p1-defects` via `g_log_mutex` synchronization and multi-instance registration)
- **Affected**: `protocol/src/vw_log.c:19-22, 56-59`
- **Trigger**: Concurrent logging while log sink is updated or cleared.
- **Impact**: Sink pointer and user data pointer are updated via separate atomic stores (log.c:20-21) and loaded separately at log.c:56-57, allowing mismatched pointer invocation or use-after-free during teardown. The plugin clears via `vw_log_set_sink(NULL, NULL)` (vw_whisper_module.c:1648/1660/1697/1731/1841) while sender/reader threads may log: a window exists where sink=`vw_plugin_log_sink` and udata=NULL, so the callback gets a NULL object.
- **Required fix**: Protect log sink updates with a mutex or atomically update a single immutable wrapper struct.

---

## Priority 2 — High

### VW-019 — Fast-Forward (>16x Playback Rate) Crashes Worker via Protocol Validation Failure

- **Priority**: P2
- **Status**: Closed (Fixed in `fix/reconciled-p1-defects` via rate clamping in client and throttling in capture)
- **Affected**: `plugin/src/vw_worker_client.c:447`, `plugin/src/vw_whisper_module.c:1117, 1128`, `protocol/src/vw_protocol_validate.c:146`
- **Trigger**: Playback rate set above 16x (e.g. 32x or 64x fast-forward in VLC).
- **Impact**: The plugin forwards `rate > 16.0f` to the worker in `VW_MSG_POSITION` (client clamps only `<=0`, not `>16`). Worker validation strictly rejects `playback_rate > 16.0f`, treats it as a fatal protocol error, and terminates.
- **Required fix**: Clamp `playback_rate` to `[0.05f, 16.0f]` in `vw_worker_client_send_position_frame()`.

### VW-020 — Unconditional Flushing of VLC System OSD (Channel 1) Destroys Native Player HUD

- **Priority**: P2
- **Status**: Closed (Fixed in `fix/reconciled-p1-defects` via conditional fallback flush preserving OSD channel 1)
- **Affected**: `plugin/src/vw_caption_presenter.c:487-488`
- **Trigger**: Any pause, resume, seek, rate change, or media blanking event.
- **Impact**: `vw_caption_presenter_blank()` unconditionally flushes VLC's native OSD channel 1, erasing native volume, mute, and speed notifications even when a private SPU channel is active.
- **Required fix**: Flush channel 1 only if private SPU channel registration failed (`!presenter->spu_channel_registered`).

### VW-021 — Model Download ID Wire-Length Mismatch Prevents Activation

- **Priority**: P2
- **Status**: Open (re-confirmed 2026-09-05)
- **Affected**: `protocol/include/vw_protocol_types.h:215,226`, `plugin/src/vw_whisper_module.c:522-523, 559, 1411-1412`, `plugin/src/vw_worker_client.c:561`
- **Trigger**: Downloading a catalog model identifier longer than 31 bytes.
- **Impact**: MODEL_CTRL and MODEL_PROGRESS carry `char model_id[32]`, while the plugin records the full identifier in its 40-byte snapshot (module.c:559) and silently truncates via `snprintf(msg.model_id, 32, ...)` (worker_client.c:561). The worker echoes the truncated wire value, so the completion `strcmp` at module.c:1412 fails and activation can remain stuck at 100%.
- **Required fix**: Reject identifiers that exceed the 31-byte wire capacity or enlarge both protocol fields and every corresponding encoder, decoder, and comparison buffer consistently.

### VW-022 — Benchmark Live-Clock Monotonic Mapping Never Re-anchored on Discontinuity/Seek

- **Priority**: P2
- **Status**: Open (re-confirmed 2026-09-05)
- **Affected**: `plugin/src/vw_benchmark.c:165-168, 183-190`
- **Trigger**: Seeking during live audio playback with benchmarking enabled.
- **Impact**: `live_pts_to_monotonic_us` is calculated once on session start (`live_clock_valid` latched) and never reset on seek; only a full `vw_benchmark_begin` re-anchors. Any seek in live mode makes `now_us - (end_pts + anchor)` diverge massively, producing corrupt latency telemetry.
- **Required fix**: Provide `vw_benchmark_reset_live_clock()` and call it on discontinuity.

### VW-025 — Translator Thread Stack Usage ~244 KB Exceeds Constrained Thread Stack Limits **[re-verified 2026-09-05 — magnitude corrected]**

- **Priority**: P2
- **Status**: Open
- **Affected**: `worker/src/vw_translate.c:549-569, 897-899`
- **Trigger**: Translation execution on systems with constrained thread stacks (e.g. musl libc 128 KB default).
- **Impact**: Stack buffers total ≈ **244 KB** (≈113 KB in `build_rpc_body` at translate.c:549-569 + ≈131 KB in `vw_translate_text` at translate.c:897-899) — the original ">254 KB" figure was slightly high, but translator threads use default stacks (vw_translate_async.c:177, NULL attr), so SIGSEGV is certain on musl 128 KB defaults; glibc 8 MB is safe.
- **Required fix**: Heap-allocate large RPC request buffers or configure worker thread stack size explicitly.

### VW-026 — Unbounded Blocking `waitpid` Violates 800ms Deadline

- **Priority**: P2
- **Status**: Open (re-confirmed 2026-09-05)
- **Affected**: `worker/src/vw_translate.c:826-832`
- **Trigger**: Curl child process hangs or network socket blocks.
- **Impact**: Synchronous `waitpid(pid, &status, 0)` freezes worker translation threads indefinitely. Partially mitigated by curl `-m`, but a curl stuck in a blocking stdout write (VW-007 scenario) is not reliably bounded by `-m`.
- **Required fix**: Use non-blocking `waitpid(pid, &status, WNOHANG)` with timed sleep/kill.

### VW-028 — Negative PTS Overwrites Audio Buffer Start PTS **[re-verified 2026-09-05 — latent]**

- **Priority**: P2 (latent; API defect real, unreachable today)
- **Status**: Open
- **Affected**: `worker/src/vw_audio_buffer.c:40-43`
- **Trigger**: Ingesting audio frames with valid negative preroll timestamps (`start_pts_us < 0`).
- **Impact**: `if (buf->count == 0 || buf->start_pts_us < 0)` overwrites the buffer start PTS on every append while `start_pts_us < 0 && count > 0`. Both current call sites block negative PTS (`vw_worker.c:805` live-path reject, `vw_worker.c:1100` source-path substitutes `decoded_pts_us`), so not reachable today — but the buffer API defect is real.
- **Required fix**: Use an explicit boolean flag `has_start_pts`.

### VW-030 — VAD Trailing Silence Calculation Adds Excessive Silence

- **Priority**: P2
- **Status**: Open (re-confirmed 2026-09-05)
- **Affected**: `worker/src/vw_vad.c:184-188`
- **Trigger**: Speech segment ends followed by silence.
- **Impact**: Case C last-segment branch computes `chosen_cut = (seg_end_sample + sample_count) / 2` with `sample_count ≤ VW_CHUNK_MAX_SAMPLES` (384000) and `seg_end_sample ≥ VW_CHUNK_MIN_SAMPLES` (96000); maximum transcribed trailing silence = 144000 samples = **exactly 9.0 s**, triggering Whisper hallucinations.
- **Required fix**: Clamp trailing silence padding to ≤300ms.

### VW-032 — FFmpeg Demuxer Pre-Roll Keyframe Audio Leak

- **Priority**: P2
- **Status**: Open (re-confirmed 2026-09-05)
- **Affected**: `worker/src/vw_source_decoder_ffmpeg.c:145-172, 192-213`
- **Trigger**: Seeking to non-keyframe timestamps.
- **Impact**: Seek sets the anchor (`current_pts_us = target`, :160) but neither seek nor `process_frame` compares `frame_pts` against the requested target; keyframe pre-roll samples are copied untrimmed via `copy_samples` (:174-190), creating pre-seek audio bleed.
- **Required fix**: Discard decoded samples whose PTS precedes requested seek PTS.

### VW-034 — `VW_MSG_START_SESSION` Ignores Requested Language

- **Priority**: P2
- **Status**: Open (re-confirmed 2026-09-05)
- **Affected**: `worker/src/vw_worker.c:584-719`
- **Trigger**: Session starts with user-selected non-English or forced language.
- **Impact**: The worker handles `VW_MSG_START_SESSION` without reading `payload_decoded.start.language` (`char language[16]`, `vw_protocol_types.h:134`). The only `set_language` call is at worker init (vw_worker.c:344) from CLI config — session language changes are silently ignored.
- **Required fix**: Call `vw_whisper_engine_set_language(engine, payload.language)` in the START_SESSION handler.

### VW-036 — Windows Named Pipe `ERROR_MORE_DATA` Treated as Fatal Disconnection **[re-verified 2026-09-05 — line drift]**

- **Priority**: P2
- **Status**: Open
- **Affected**: `protocol/src/vw_ipc_pipe_win32.c:220-241` (fatal check now at :241)
- **Trigger**: Partial message read from message-mode pipe.
- **Impact**: `ReadFile` returns FALSE with `ERROR_MORE_DATA`; only `ERROR_IO_PENDING` is handled (:222), so `res` stays FALSE and :241 returns `VW_IPC_RECV_FATAL`, discarding the `bytes_read` partial data instead of reading remaining bytes.
- **Required fix**: Accept `ERROR_MORE_DATA` as non-fatal when `bytes_read > 0`.

### VW-037 — `SOCK_SEQPACKET` Datagram Truncation and Buffer Limits on Linux **[re-verified 2026-09-05 — latent]**

- **Priority**: P2 (latent)
- **Status**: Open
- **Affected**: `protocol/src/vw_ipc_socket_linux.c:29, 100, 142`
- **Trigger**: Frame payload larger than socket buffer or received in partial chunk.
- **Impact**: `recv()` on `SOCK_SEQPACKET` (:142) silently discards datagram excess when the read buffer is smaller (silent desync if a hostile peer coalesces header+payload into one record). No `SO_SNDBUF` is set; `VW_MAX_PAYLOAD_BYTES` (types.h:31) permits ~960 KB `pcm_bytes` (validate.c:120-125 allows 30 s duration) > default wmem → `EMSGSIZE`/send failure. Current senders use small chunks, so latent.
- **Required fix**: Switch to `SOCK_STREAM` or increase `SO_SNDBUF`/`SO_RCVBUF`.

### VW-039 — Worker HELLO Handler Ignores `min_major` and `max_major`

- **Priority**: P2
- **Status**: Open (re-confirmed 2026-09-05; grep-verified zero references in `worker/src/`)
- **Affected**: `worker/src/vw_worker.c:541-581`
- **Trigger**: Plugin connects requiring a different major protocol version.
- **Impact**: Worker verifies the token, then answers HELLO_ACK with its own major/minor. It never validates client major version bounds, proceeding even on incompatible versions.
- **Required fix**: Validate major version range and reject with `E_PROTOCOL_VERSION`.

### VW-040 — Client Rejects Same-Major Forward Minor Protocol Versions

- **Priority**: P2
- **Status**: Open (re-confirmed 2026-09-05)
- **Affected**: `plugin/src/vw_worker_client.c:230`
- **Trigger**: Connecting to worker with newer minor version (`selected_minor > VW_PROTOCOL_VERSION_MINOR`).
- **Impact**: Plugin rejects connection even though minor versions are backward-compatible, breaking independent upgrades.
- **Required fix**: Allow newer minor versions within same major version and rely on capability flags.

### VW-041 — Missing Peer Credential Verification on macOS and BSD

- **Priority**: P2
- **Status**: Open (re-confirmed 2026-09-05)
- **Affected**: `protocol/src/vw_ipc_socket_linux.c:68-76`
- **Trigger**: IPC connection on macOS or BSD systems.
- **Impact**: `SO_PEERCRED` check is guarded by `#if defined(__linux__)`, skipping peer UID validation entirely on macOS/BSD.
- **Required fix**: Implement `LOCAL_PEERCRED` / `getpeereid()` fallback.

### VW-042 — `poll()` Interruption by Signal (`EINTR`) Aborts `vw_ipc_listen`

- **Priority**: P2
- **Status**: Open (re-confirmed 2026-09-05)
- **Affected**: `protocol/src/vw_ipc_socket_linux.c:55`
- **Trigger**: Signal arrival (`SIGCHLD`, `SIGWINCH`) during the 10s accept window.
- **Impact**: `poll() <= 0` treats EINTR (-1) as timeout/error, causing `vw_ipc_listen` to unlink the socket and fail startup. The EINTR retry loop already exists in `vw_ipc_receive_timeout` (:157-159) — it is just not applied here.
- **Required fix**: Loop on `EINTR` while tracking remaining timeout.

### VW-043 — Untrusted `PATH` Search via `posix_spawnp` on Bare Executable Names

- **Priority**: P2
- **Status**: Open (re-confirmed 2026-09-05; the branch is live)
- **Affected**: `plugin/src/vw_platform_linux.c:146-151`
- **Trigger**: Worker executable path configured without directory slashes.
- **Impact**: `if (!strchr(executable_path, '/'))` → `posix_spawnp(...)` searches `PATH`, allowing binary hijacking if untrusted directories exist in `PATH`. `vw_whisper_module.c:1716` actively falls back to bare `"vlc-whisper-worker"`, so the branch is reachable. The Win32 counterpart is fail-closed (vw_platform_win32.c:50-60) — asymmetric.
- **Required fix**: Require executable path to be absolute starting with `'/'`.

### VW-044 — Parallel Test Named Pipe Collision on Windows **[re-verified 2026-09-05 — impact corrected]**

- **Priority**: P2
- **Status**: Open
- **Affected**: `tests/integration/test_worker_ipc.c:27`, `tests/integration/test_worker_lifecycle.c:48`
- **Trigger**: Same test running concurrently (`ctest -j`) on Windows, or a stale lingering listener. Correction: the two tests use *distinct* static pipe names (`test_ipc_socket` vs `test_lifecycle_socket`) and do not hit each other directly; the POSIX branches already append `getpid()` (:29/:50), Windows does not.
- **Impact**: Same-test parallel runs or stale listeners connect test cases to a mock worker instance they did not start.
- **Required fix**: Append `GetCurrentProcessId()` or GUID to test pipe endpoints on Windows.

### VW-047 — Dead Code and Invariant Contradiction in `vw_session.c`

- **Priority**: P3
- **Status**: Open (re-confirmed 2026-09-05)
- **Affected**: `plugin/src/vw_session.c:27-31`, `plugin/include/vw_audio_capture.h:32`
- **Trigger**: Discontinuity handling via legacy session file.
- **Impact**: `invalid_pts_drain_pending` is assigned once (module.c:1612) and never read (only `reset_pending` is consumed in vw_audio_capture.c:22); `vw_session.c` marks discontinuity as session failure, contradicting Directive 7. `vw_session.c` is compiled (plugin/CMakeLists.txt:4) but has zero callers — dead TU.
- **Required fix**: Remove unused field and retire or update `vw_session.c`.

### VW-060 — Source-Mode Media Swap Reuses Stale Seek Filter Anchor

- **Priority**: P2
- **Status**: Open (re-confirmed 2026-09-05)
- **Affected**: `plugin/src/vw_whisper_module.c:1076-1099, 1312-1318`
- **Trigger**: Seek in one source-mode media item, then advance to another item whose timeline starts below the previous seek position.
- **Impact**: The media-swap restart resets `last_position_us` and `paused_position_us` (1091-1092) but not the loop-local `last_source_seek_us` (775). The stale-segment filter can therefore drop every caption in the new item until its PTS exceeds the old item's seek anchor.
- **Required fix**: Reset `last_source_seek_us` to `-1` whenever a source-mode media swap starts a new session.

### VW-061 — Media Foundation Seek Returns Untrimmed Pre-Target Audio

- **Priority**: P2
- **Status**: Open (re-confirmed 2026-09-05)
- **Affected**: `worker/src/vw_source_decoder_mf.c:190-208, 236-316`
- **Trigger**: Seeking source-mode playback to a non-keyframe timestamp on Windows.
- **Impact**: `SetCurrentPosition` (:198) updates the decoder anchor, but the read loop returns every decoded sample using `llTimestamp` without dropping or trimming samples before the requested target. Pre-seek audio can be transcribed and scheduled after the seek, unlike the existing FFmpeg issue VW-032.
- **Required fix**: Retain the requested seek target and discard or trim decoded samples whose timestamp interval precedes it.

### VW-062 — FFmpeg Seek Re-Initialization Failure Leaves Decoder Partially Mutated **[re-verified 2026-09-05 — worse than filed]**

- **Priority**: P2
- **Status**: Open
- **Affected**: `worker/src/vw_source_decoder_ffmpeg.c:154-168`
- **Trigger**: `av_seek_frame` succeeds but `swr_init` fails after the resampler has been closed (:157-158).
- **Impact**: The function returns failure with demuxer and codec state repositioned and the resampler left closed; additionally the stale packet is *not* unref'd (the `pkt_pending` cleanup at :165-168 is skipped), and the worker retains the old playback anchor. Later reads send the new-position packet through a closed resampler, potentially truncating captions from the seek point onward.
- **Required fix**: Restore a usable resampler and consistent decoder state before returning failure, or invalidate the decoder so callers cannot continue with partially mutated state; unref the pending packet on all failure paths.

### VW-063 — Repeated Failed Source Seeks Perpetually Invalidate Translation

- **Priority**: P2
- **Status**: Open (re-confirmed 2026-09-05)
- **Affected**: `worker/src/vw_worker.c:758-791`
- **Trigger**: A source decoder repeatedly rejects a POSITION seek, such as an unseekable or failing media source.
- **Impact**: On seek failure the anchors (`current_playback_pts_us`/`decoded_pts_us`/`last_playback_pts_us`) are unchanged (fail branch skips :788-791), so `backward_jump`/`forward_past_decoded` re-evaluate true on every subsequent POSITION tick → `requires_seek` stays latched → `vw_translate_async_invalidate()` at :769 fires *before* each doomed seek attempt, repeatedly discarding in-flight translations. Even a single failing seek poisons every tick until the timeline advances past the stale anchor.
- **Required fix**: Latch a failed-seek state or bounded retry policy and suppress repeated invalidation until a new explicit seek or successful reposition occurs.

### VW-064 — macOS/BSD IPC Send Can Terminate the Host with SIGPIPE

- **Priority**: P2
- **Status**: Closed (Fixed in `fix/reconciled-p1-defects` via `MSG_NOSIGNAL` on send and `SO_NOSIGPIPE` on socket creation)
- **Affected**: `protocol/src/vw_ipc_socket_linux.c:128-130, 135`
- **Trigger**: The IPC peer closes while a macOS or BSD build sends a frame.
- **Impact**: `MSG_NOSIGNAL` is defined as zero on platforms lacking that flag, and no `SO_NOSIGPIPE` is set anywhere in the file. An `EPIPE` from `send` (:135) can therefore raise the default SIGPIPE and terminate the VLC plugin or worker.
- **Required fix**: Apply a platform-supported no-SIGPIPE mechanism, such as `SO_NOSIGPIPE` on Apple/BSD or process-level signal masking.

### VW-076 — SPU Channel Leak + Orphaned Ephemeral Caption on vout Re-registration *(new 2026-09-05)*

- **Priority**: P2
- **Status**: Open
- **Affected**: `plugin/src/vw_caption_presenter.c:321-341`
- **Trigger**: vout recreate/swap mid-playback, or `spu_channel_registered` reset while the same vout is reused.
- **Impact**: The previous channel id is never flushed on the old held vout before `vlc_object_release` (:323), and re-registering on the same vout loses the old id entirely. Combined with VW-001's `b_ephemer = true`, the last caption can remain frozen on the old (still-alive) vout and registered channel ids leak.
- **Required fix**: Call `vout_FlushSubpictureChannel(old_vout, old_id)` before release; treat re-registration on the same vout as keeping the existing id.

### VW-077 — Model Progress Banner Survives Media Swap / Discontinuity Blanking *(new 2026-09-05)*

- **Priority**: P2
- **Status**: Open
- **Affected**: `plugin/src/vw_caption_presenter.c:474-491`, `plugin/src/vw_whisper_module.c:1080, 1204`
- **Trigger**: Seek, media swap, or discontinuity while a model download is active.
- **Impact**: `vw_caption_presenter_blank()` flushes the caption SPU channel and channel 1 but not `model_progress_channel_id`, so a stale progress banner (with `b_ephemer = true`) lingers across the discontinuity and desyncs from the actual download stage.
- **Required fix**: Flush the model-progress channel in `blank()` (or clear it on discontinuity/swap).

### VW-078 — Windows Worker Log File Arbitrary-Append via Predictable `%TEMP%` Path *(new 2026-09-05)*

- **Priority**: P2
- **Status**: Open
- **Affected**: `plugin/src/vw_platform_win32.c:151-163`
- **Trigger**: Multi-user Windows host; local attacker races the fixed log name.
- **Impact**: `CreateFileA("%TEMP%\\vlc-whisper-worker.log", FILE_APPEND_DATA, ..., OPEN_ALWAYS)` on a fixed shared name lets a local user pre-create a junction/symlink so VLC appends worker output (which may include config/path diagnostics) to an attacker-chosen target. Sibling of VW-058/VW-067 — fold into VW-058's per-PID fix.
- **Required fix**: Per-PID log name plus `CREATE_NEW` semantics or reparse-point-safe handling.

### VW-079 — Media-Swap START Leaves Stale Worker Session State *(new 2026-09-05)*

- **Priority**: P2
- **Status**: Closed (2026-09-07)
- **Affected**: `worker/src/vw_worker.c:585-606`
- **Trigger**: Media swap (different session_id) while the old session was paused or had source-mode anchors set.
- **Impact**: (a) `paused` is not reset — AUDIO_PCM is ignored (:797) and source decode is gated (:1092) until the first POSITION tick flips the paused transition, delaying captions; (b) `last_playback_pts_us`/`decoded_pts_us`/`current_playback_pts_us`/`source_eof`/`eof_retry_count` survive into the new epoch — if the new item's timeline starts below the old seek anchor, the first POSITION ticks trigger spurious `backward_jump` → forced seek + translation invalidation (worker-side analog of VW-060; compounds VW-063 if seeks fail).
- **Required fix**: In the media-swap branch, reset `paused = false`, `last_playback_pts_us = -1`, `decoded_pts_us = 0`, `source_eof = false`, `eof_retry_count = 0`.

### VW-080 — Media Foundation Gap-Read Livelock *(new 2026-09-05)*

- **Priority**: P2
- **Status**: Closed (2026-09-07)
- **Affected**: `worker/src/vw_source_decoder_mf.c:251-253`
- **Trigger**: A source repeatedly returns S_OK with NULL samples (gap) and no EOF.
- **Impact**: `if (!pSample) continue;` spins the read loop indefinitely with no timeout; the worker never regains control and lookahead decode stalls.
- **Required fix**: Bound consecutive gap reads or report progress to the caller.

### VW-081 — Transient Decoder Stall Promoted to Permanent `source_eof` *(new 2026-09-05)*

- **Priority**: P2
- **Status**: Closed (2026-09-07)
- **Affected**: `worker/src/vw_worker.c:1152-1154`, `worker/src/vw_source_decoder_ffmpeg.c:309-314`
- **Trigger**: Corrupt or awkward stream where FFmpeg's `defer_no_progress >= 2` break returns 0 samples *without* `eof_reached` (decoder-side EAGAIN).
- **Impact**: The worker latches permanent `source_eof` after 3 consecutive 0-sample reads, so a transient stall ends session-wide lookahead transcription early.
- **Required fix**: Make the decoder distinguish "stalled, retryable" from EOF, or reset `eof_retry_count` on seek/position.

---

## Priority 3 — Medium-Low

### VW-015 — Latent Inner-Pointer NULL Dereferences in `vw_protocol_encode_payload`

- **Priority**: P3
- **Status**: Open (re-confirmed 2026-09-05)
- **Affected**: `protocol/src/vw_protocol_codec.c:43, 52, 61, 104`
- **Trigger**: A direct caller supplies a non-NULL payload whose variable-length field has `length > 0` and a NULL inner pointer.
- **Impact**: `ENC_BYTES` on `client_version` (:52), `worker_version` (:61), and `pcm_data` (:104) can `memcpy` from NULL for malformed HELLO, HELLO_ACK, or AUDIO_PCM payloads. The top-level NULL-payload path is already rejected at line 43, and current callers (vw_worker_client.c:180, vw_worker.c:558) validate fields first.
- **Required fix**: Add `length > 0 && !pointer` checks for each variable-length inner field before `ENC_BYTES`.

### VW-048 — Model Path Resolution Omits Default Per-User Directory

- **Priority**: P3
- **Status**: Open (re-confirmed 2026-09-05)
- **Affected**: `worker/src/vw_worker_config.c:415-448`
- **Trigger**: Relative model path supplied in configuration.
- **Impact**: Fallback probes `config->model_dir` (428-436) and the install dir (439-445) only — never `vw_model_download_default_dir()` (`~/.local/share/vlc-whisper/models`), even though the worker itself uses that function for the download dir (vw_worker.c:389). A downloaded model with no model-dir flag set fails to load.
- **Required fix**: Add per-user directory fallback probe.

### VW-049 — HTML Entity Decoding Buffer Fall-Through Text Leak

- **Priority**: P3
- **Status**: Open (re-confirmed 2026-09-05)
- **Affected**: `worker/src/vw_translate.c:149-174`
- **Trigger**: Translation output containing unhandled HTML entities.
- **Impact**: A failed numeric-entity parse falls through to translate.c:172-173, copying `'&'` then each subsequent character verbatim — e.g. `&#999999999;` or unknown named entities leak raw entity text into subtitle output. Cosmetic.
- **Required fix**: Handle unknown entities cleanly without corrupting output buffer.

### VW-050 — Non-Atomic `pipe()` + `fcntl(FD_CLOEXEC)` Race in Curl Spawning

- **Priority**: P3
- **Status**: Open (re-confirmed 2026-09-05)
- **Affected**: `worker/src/vw_translate.c:674-677, 686-691, 696-707`
- **Trigger**: Concurrent fork/exec in another thread between `pipe()` and the CLOEXEC `fcntl()`.
- **Impact**: File descriptors can leak into child processes.
- **Required fix**: Use `pipe2(fds, O_CLOEXEC)`.

### VW-051 — `status.queued_audio_us` Telemetry Never Populated

- **Priority**: P3
- **Status**: Open (re-confirmed 2026-09-05; grep-verified zero references in worker)
- **Affected**: `worker/src/vw_worker.c:122-148`
- **Trigger**: Status polling by client.
- **Impact**: `vw_worker_send_status()` memsets the struct and sets only `inference_us`, `dropped_audio_us`, `resolved_backend`; `queued_audio_us` (`vw_msg_status_t`, types.h:186) is never written — constantly reported as 0.
- **Required fix**: Query audio buffer depth and populate field.

### VW-052 — FFmpeg Demuxer Fails to Strip `file://localhost/` URI Prefix

- **Priority**: P3
- **Status**: Open (re-confirmed 2026-09-05)
- **Affected**: `worker/src/vw_source_decoder_ffmpeg.c:41`
- **Trigger**: Media URL formatted with `file://localhost/`.
- **Impact**: Strips `file://` only; `file://localhost/x` yields `localhost/x` → `avformat_open_input` fails. (MF backend handles it: `vw_source_decoder_mf.c:45-46`.)
- **Required fix**: Normalize `file://localhost/` to `/`.

### VW-054 — Missing Semantic Validation for `START_SESSION`, `CONTROL`, and `ERROR` Payloads

- **Priority**: P3
- **Status**: Partially closed (2026-09-07: `START_SESSION` fixed; `CONTROL`, `STATUS`, and `ERROR` remain open)
- **Affected**: `protocol/src/vw_protocol_validate.c:116-117, 130-136`
- **Trigger**: Senders transmitting malformed session start or control frames.
- **Impact**: `START_SESSION` returns true unconditionally (:116-117); PAUSE/RESUME/STOP (`reason` unvalidated), STATUS, and ERROR all return true (:130-136). Mitigation: the worker re-checks `sample_rate` itself (vw_worker.c:607).
- **Required fix**: Validate struct fields against protocol contracts.

### VW-055 — Decoder Does Not Verify Complete Payload Consumption

- **Priority**: P3
- **Status**: Open (re-confirmed 2026-09-05)
- **Affected**: `protocol/src/vw_protocol_codec.c:220-417`
- **Trigger**: Frame transmitted with trailing garbage bytes.
- **Impact**: No `read_pos == buffer_size` enforcement anywhere; e.g. HELLO with trailing garbage (:232) and STATUS with trailing bytes (:345-353) decode successfully with the extra bytes silently ignored.
- **Required fix**: Enforce `read_pos == buffer_size`.

### VW-056 — `vw_protocol_validate_header` Does Not Validate Message Type Bounds

- **Priority**: P3
- **Status**: Open (re-confirmed 2026-09-05; impact limited)
- **Affected**: `protocol/src/vw_protocol_validate.c:3-9`
- **Trigger**: Frame received with undefined message type number.
- **Impact**: Header validation checks only magic/major/payload_length; `header->type` bounds unchecked. The frame is enqueued before decode/validate `default:` rejection (codec.c:413, validate.c:205) and plugin dispatch drains unknown types.
- **Required fix**: Validate `type >= VW_MSG_HELLO && type <= VW_MSG_TRANSLATE_CTRL`.

### VW-057 — Inconsistent UTF-8 Validation Between `vw_utf8_safe_len` and `is_valid_utf8`

- **Priority**: P3
- **Status**: Open (re-confirmed 2026-09-05)
- **Affected**: `protocol/include/vw_protocol_util.h:36-47`, `protocol/src/vw_protocol_validate.c:56-89`
- **Trigger**: Strings with overlong UTF-8 encodings.
- **Impact**: `vw_utf8_safe_len` accepts overlong (`C0 80`), surrogate (`ED A0 80`), and >U+10FFFF sequences; `is_valid_utf8` rejects all three. Divergent validators confirmed.
- **Required fix**: Align validation logic in utility header.

### VW-059 — NSIS CheckVLCVersion Displays Packed Version DWORDs

- **Priority**: P3
- **Status**: Open (re-confirmed 2026-09-05; gate itself verified sound — NSIS 3.09 `IntOp` supports `>>`)
- **Affected**: `cmake/vw_installer.nsi.in:96-105` (message at :103)
- **Trigger**: Selecting a non-VLC-3.x executable during installation.
- **Impact**: The major-version gate correctly rejects incompatible VLC, but the error dialog prints raw packed `GetDLLVersion` DWORDs such as `196608.1376257` instead of a readable version, obscuring diagnosis.
- **Required fix**: Decode the high DWORD into major/minor components and the low DWORD into build/revision components before interpolating the rejection message.

### VW-065 — Source Decoder Metadata Contract Mismatches Resampled Output

- **Priority**: P3
- **Status**: Open (re-confirmed 2026-09-05)
- **Affected**: `worker/include/vw_source_decoder.h:18-19`, `worker/src/vw_source_decoder_mf.c:182-183`, `worker/src/vw_source_decoder_ffmpeg.c:138-139`
- **Trigger**: A consumer uses `vw_source_decoder_info_t.sample_rate` or `.channels` as documented.
- **Impact**: The header promises "Native audio track sample rate"/"Native channel count", but both backends hardcode 16000/1. Only `duration_us`/`container_format` are consumed today (vw_worker.c:666) — latent public-contract mismatch.
- **Required fix**: Report native stream properties or change the field documentation to state that values describe the 16 kHz mono output contract.

### VW-066 — Windows Release Staging Does Not Verify PE Machine Type

- **Priority**: P3
- **Status**: Open (re-confirmed 2026-09-05)
- **Affected**: `cmake/vw_check_workers.cmake:12-33, 52-69`
- **Trigger**: A stale, misconfigured, 32-bit, or ARM64 plugin/worker artifact is present in the Windows build staging directory.
- **Impact**: Release validation checks existence and model hashes only; no PE machine validation exists in the file (nor in `vw_packaging.cmake:106-135`). A wrong-architecture DLL can silently fail to load into 64-bit VLC, and a wrong-architecture worker can fail at process creation after a clean-looking install.
- **Required fix**: Reject staged plugin and worker binaries unless their PE machine field is `IMAGE_FILE_MACHINE_AMD64` (`0x8664`), before NSIS packaging.

### VW-067 — Win32 Worker Diagnostics Break on Non-ANSI Temporary Paths

- **Priority**: P3
- **Status**: Open (re-confirmed 2026-09-05)
- **Affected**: `plugin/src/vw_platform_win32.c:152, 154, 160-163`
- **Trigger**: `%TEMP%` contains a path component not representable in the active Windows ANSI code page.
- **Impact**: `GetTempPathA` and `CreateFileA` can produce a mangled path and fail, silently disabling worker stdout/stderr capture exactly when diagnostics are needed. Worker execution continues without its log file.
- **Required fix**: Build the path and open the log with `GetTempPathW` and `CreateFileW`.

### VW-068 — CPU-Only Installer Upgrade Leaves Locked Canonical GPU Worker Active

- **Priority**: P3
- **Status**: Open (re-confirmed 2026-09-05)
- **Affected**: `cmake/vw_installer.nsi.in:312-322`
- **Trigger**: Installing a CPU-only package while the prior canonical `vlc-whisper-worker.exe` is locked by a running or recently stopped process.
- **Impact**: The first `Delete` fails, and `Delete /REBOOTOK` only schedules removal. Until reboot, runtime discovery still prefers the stale canonical GPU worker over the newly installed `vlc-whisper-worker-cpu.exe`, so the new CPU artifact is not actually selected.
- **Required fix**: Ensure discovery cannot prefer the locked old binary during the current install, or fail the upgrade until the canonical worker is removed and the CPU worker is selected.

### VW-069 — Loader-Less Windows GPU Worker Can Show a Modal DLL Error

- **Priority**: P3
- **Status**: Open (re-confirmed 2026-09-05, static)
- **Affected**: `worker/third_party/whisper.cpp/ggml/src/ggml-vulkan/CMakeLists.txt:100`, `plugin/src/vw_platform_win32.c:139-150`, `plugin/src/vw_worker_client.c:124-133`
- **Trigger**: A GPU package starts on x64 Windows without a Vulkan loader, such as a basic display driver, VM, or RDP session.
- **Impact**: `target_link_libraries(ggml-vulkan PRIVATE Vulkan::Vulkan)` makes `vulkan-1.dll` a hard import of the statically linked worker; the spawn path does only `SW_HIDE`/`CREATE_NO_WINDOW` — no loader probe, no error-mode suppression. The GPU worker can die before `main`; the plugin eventually falls back to the CPU worker, but the failed launch can produce a Windows loader dialog and adds the connection timeout to caption startup.
- **Required fix**: Probe the loader before preferring the GPU worker or use a child creation error mode that suppresses loader dialogs while preserving the CPU fallback.

### VW-070 — Model Download Lock Files Accumulate Permanently

- **Priority**: P3
- **Status**: Open (re-confirmed 2026-09-05)
- **Affected**: `worker/src/vw_model_download.c:235, 246, 251-265`
- **Trigger**: Any completed, failed, or aborted model-download attempt.
- **Impact**: Lock acquisition creates `<model>.lock` (`open(O_CREAT)` :235 POSIX, `CreateFileW OPEN_ALWAYS` :246 Win32), but release (:251-265) only unlocks and closes the descriptor; no path removes the file. Repeated downloads leave permanent zero-byte lock artifacts in the per-user model directory.
- **Required fix**: Remove the lock pathname after releasing the descriptor, preserving locking semantics for concurrent holders.

### VW-071 — POSIX Curl Diagnostics Inherit Worker Stderr

- **Priority**: P3
- **Status**: Open (re-confirmed 2026-09-05)
- **Affected**: `worker/src/vw_translate.c:753-770`
- **Trigger**: A POSIX translation tier emits a curl error or diagnostic.
- **Impact**: The file actions only handle pipe_out/pipe_in; the child inherits worker stderr. Curl `-sS` diagnostics (endpoint/TLS errors) pollute worker/VLC logs (and the `%TEMP%` log per VW-058). The caption payload remains stdin-only, so no transcript leak.
- **Required fix**: Redirect curl stderr to `/dev/null` or a controlled non-user-visible sink while retaining structured worker error reporting.

### VW-072 — Download Cancellation Can Commit During Verification

- **Priority**: P3
- **Status**: Open (re-confirmed 2026-09-05)
- **Affected**: `worker/src/vw_model_download.c:561-622, 714-719`
- **Trigger**: Cancellation arrives after the post-download abort check but before atomic rename.
- **Impact**: The download thread does not recheck `abort_requested` between hashing (:565) and rename (:602); abort sets ABORTING (714-719) but the thread overwrites with DONE (:616-618). Cancellation during the potentially seconds-long SHA-256 of a 3 GB model commits an already hash-verified model and reports DONE instead of honoring cancellation.
- **Required fix**: Recheck cancellation immediately before rename and preserve ABORTING/IDLE state when cancellation wins the race.

### VW-073 — WinHTTP Early EOF Is Reported as a Successful Download

- **Priority**: P3
- **Status**: Open (re-confirmed 2026-09-05)
- **Affected**: `worker/src/vw_model_download.c:460-486`
- **Trigger**: WinHTTP read terminates early with a connection error after at least one byte was received.
- **Impact**: `vw_download_via_winhttp` returns `total_read > 0` (:486) without distinguishing EOF from read failure. The later SHA-256 gate prevents model corruption, but progress/status reports success until a retry or verification failure, adding latency and misleading diagnostics.
- **Required fix**: Treat a non-EOF read failure as failure and validate the expected byte count before returning success.

### VW-074 — Portable Windows Archive Bypasses Bitness and Stale-Worker Safeguards

- **Priority**: P3
- **Status**: Open (re-confirmed 2026-09-05)
- **Affected**: `README.md:46-57`, `cmake/vw_packaging.cmake:149-162`
- **Trigger**: Extracting the documented `win64.zip` over 32-bit VLC, or replacing a GPU archive with a CPU-only archive in an existing VLC directory.
- **Impact**: The archive path has no installer bitness preflight (the gate exists only in NSIS `DirectoryLeave`, nsi.in:111-119), so 32-bit VLC silently rejects the x64 plugin. CPack install rules add files only — no stale canonical-worker cleanup, no arch verification.
- **Required fix**: Add an explicit 64-bit VLC preflight to the portable-install instructions and define a safe archive upgrade/removal procedure for the canonical GPU worker.

### VW-075 — Auth-Token RNG Failure Returns a Nonfunctional Filter as Success

- **Priority**: P3
- **Status**: Closed (Fixed in `fix/reconciled-p1-defects` via fail-closed return on random token failure)
- **Affected**: `plugin/src/vw_whisper_module.c:1743-1744, 1799`
- **Trigger**: `vw_platform_get_random_bytes()` fails during plugin open.
- **Impact**: The plugin logs the RNG failure (WARN only) but still returns `VLC_SUCCESS` (:1799) without launching a worker or sender thread. VLC accepts the filter, while queued audio is never drained and captions silently remain unavailable. Fail-closed precedent exists in the pipe-name RNG path (1642-1665).
- **Required fix**: Return `VLC_EGENERIC` on auth-token generation failure, matching the existing fail-closed handling for pipe-name randomness.

### VW-082 — Signal-Interrupted `recv`/`send` Tear Down Live IPC Connections (EINTR = False Fatality) *(new 2026-09-05)*

- **Priority**: P3
- **Status**: Open
- **Affected**: `protocol/src/vw_ipc_socket_linux.c:135, 146-147, 163-167`
- **Trigger**: Any caught signal (SIGCHLD from respawned workers, SIGWINCH) landing during `recv`/`send` on the 3s-timeout socket.
- **Impact**: `recv` EINTR → `VW_IPC_RECV_FATAL` (:146-147, and again after the poll EINTR loop at :163-167); `send` EINTR → false (:135). The worker reader thread (vw_worker.c:207) or plugin sender treats the healthy connection as dead → caption loss + respawn churn.
- **Required fix**: `if (errno == EINTR) continue/retry` in all three paths (reuse the :157-159 pattern).

### VW-083 — `ENC_BYTES`/`DEC_BYTES` with Length 0 and NULL Pointer is UB (`memcpy(dst, NULL, 0)`) *(new 2026-09-05)*

- **Priority**: P3
- **Status**: Open
- **Affected**: `protocol/src/vw_protocol_codec.c:34, 126-128, 211, 216`
- **Trigger**: Encoder called with attempted-but-empty translation (`translation_attempted && bytes == 0 && ptr == NULL`).
- **Impact**: UB per C17 (implementations may trap); benign on current toolchains.
- **Required fix**: Guard `if (len) memcpy(...)` in the macros.

### VW-084 — Leaked Payload on Queue-Push Failure *(new 2026-09-05)*

- **Priority**: P3
- **Status**: Open
- **Affected**: `worker/src/vw_worker.c:278-281`
- **Trigger**: `vw_worker_queue_push` failure (queue full/eviction path).
- **Impact**: The function logs "frame dropped" but `payload_buf` is neither freed nor freed-before-NULL → heap leak per dropped frame.
- **Required fix**: `free(payload_buf)` in the failure branch.

### VW-085 — HELLO_ACK Send Results Ignored *(new 2026-09-05)*

- **Priority**: P3
- **Status**: Open
- **Affected**: `worker/src/vw_worker.c:576-577`
- **Trigger**: Pipe breaks during the two-part ACK send.
- **Impact**: Worker marks itself authenticated and continues until the next read fails; the client times out and may respawn a second worker → brief double-listener contention on the endpoint. Every other send in the file gates `running` on failure.
- **Required fix**: Check both `vw_ipc_send` returns and exit on failure like other paths.

### VW-086 — STATUS `state` Hardcoded to 1 *(new 2026-09-05)*

- **Priority**: P3
- **Status**: Open
- **Affected**: `worker/src/vw_worker.c:130`
- **Trigger**: Any STATUS poll.
- **Impact**: `status.state` always reports 1 regardless of engine load failure, paused state, or session activity — misleading telemetry for UI/diagnostics. Companion to VW-051.
- **Required fix**: Derive from actual `session_active`/`engine` state.

### VW-087 — Stale-Frame Drain Aborts Worker on Malloc Failure *(new 2026-09-05)*

- **Priority**: P3
- **Status**: Open
- **Affected**: `worker/src/vw_worker.c:228-250`
- **Trigger**: Duplicate/out-of-order frame with a large payload (≤1 MB, `VW_MAX_PAYLOAD_BYTES`) arriving while memory is tight.
- **Impact**: `malloc` failure in the *discard* path `goto fatal` kills the whole session — the frame was already rejected; killing the worker over a frame it is throwing away is disproportionate.
- **Required fix**: On drain-alloc failure, treat as fatal only if `running` is false; otherwise keep draining with a small fixed stack buffer.

### VW-088 — Forced Mid-Speech Cut at 24 s Chunk Cap *(new 2026-09-05)*

- **Priority**: P3
- **Status**: Open
- **Affected**: `worker/src/vw_vad.c:196-207` (call site `vw_worker.c:1107`)
- **Trigger**: Continuous speech > 24 s with no segment padded end landing in `[VW_CHUNK_MIN_SAMPLES, VW_CHUNK_MAX_SAMPLES]`.
- **Impact**: `chosen_cut = VW_CHUNK_MAX_SAMPLES` truncates mid-word with no boundary tolerance; transcription quality degrades on long uninterrupted monologue (lecture/music).
- **Required fix**: Prefer cutting at the last VAD probability minimum below MAX instead of a hard sample cap.

### VW-089 — Unchecked POSITION Send After Live-Epoch Restart *(new 2026-09-05)*

- **Priority**: P3
- **Status**: Open
- **Affected**: `plugin/src/vw_whisper_module.c:1229-1231`
- **Trigger**: Transport breakage during the re-anchoring `vw_worker_client_send_position` after a live epoch STOP/START.
- **Impact**: The send is unchecked; a mid-frame failure drops the transport internally but the code logs "live epoch restarted" and continues until the next receive returns FATAL, adding one respawn cycle.
- **Required fix**: Check the return and set `worker_dead` like the other send sites.

### VW-090 — Straddling Source-Mode Segments Dropped Wholesale After Seek *(new 2026-09-05)*

- **Priority**: P3
- **Status**: Open
- **Affected**: `plugin/src/vw_whisper_module.c:1312-1318`
- **Trigger**: Seek while a caption segment spans the seek target.
- **Impact**: The filter drops any segment with `start_pts_us < last_source_seek_us`, including segments that *end* well past the seek target — the first post-seek caption is swallowed on every seek.
- **Required fix**: Drop only when `end_pts_us <= last_source_seek_us`; otherwise clip start to the anchor.

### VW-091 — Discontinuity Detection Contradicts Comment; Flag Gating is Dead Code *(new 2026-09-05)*

- **Priority**: P3
- **Status**: Open
- **Affected**: `plugin/src/vw_whisper_module.c:1550-1560`
- **Trigger**: Any ≥5 s PTS jump, flagged or not.
- **Impact**: `(is_forward_seek && is_flagged) || (diff >= VW_INPUT_JUMP_DISCONTINUITY_US)` — the third term subsumes both `is_flagged` variants, so an *unflagged* ≥5 s jump always triggers a full live-epoch STOP/START (≈1 s caption blank). The header comment (1543-1545) implies only flagged forward seeks trigger; possible spurious restarts at exactly-threshold network jitter.
- **Required fix**: Drop the redundant term or restore intended flag gating.

### VW-092 — POSIX Model-Dir Resolution Silently Truncates and Falls Back to World-Writable `/tmp` *(new 2026-09-05)*

- **Priority**: P3
- **Status**: Open
- **Affected**: `plugin/src/vw_whisper_module.c:128-140`
- **Trigger**: Very long `XDG_DATA_HOME`, or stripped HOME/XDG environment.
- **Impact**: The Windows path rejects overflow (115, 125); the POSIX `snprintf(out, out_size, "%s", tmp)` truncates silently. With no HOME/XDG the dir becomes `/tmp/vlc-whisper/models` — a world-writable location the worker will use for downloads/activation (model-dir poisoning edge).
- **Required fix**: Mirror the Windows overflow rejection and drop the /tmp fallback (fail closed).

### VW-093 — mkstemp FD Closed, Leaving an Empty Predictable File for the Whole Session *(new 2026-09-05)*

- **Priority**: P3
- **Status**: Open
- **Affected**: `plugin/src/vw_benchmark.c:26-29`
- **Trigger**: Any benchmark report creation.
- **Impact**: `create_report` closes the mkstemp descriptor immediately; the empty 0600 file sits at a fixed, guessable path all session (same predictability class as VW-005's `.next`).
- **Required fix**: Keep the fd and `fopen` via `fdopen` on `report_path`, or unlink+recreate with `O_EXCL`.

### VW-094 — Curl Children Inherit Worker IPC FD (No CLOEXEC on IPC FDs) *(new 2026-09-05)*

- **Priority**: P3
- **Status**: Open
- **Affected**: `protocol/src/vw_ipc_socket_linux.c:29, 100`, `worker/src/vw_translate.c:753-772`, `worker/src/vw_model_download.c:279-292`
- **Trigger**: Any translate curl child (~800 ms) or model-download curl child (`--max-time 1800`).
- **Impact**: `socket()` without `SOCK_CLOEXEC`; spawn paths only action the pipe fds. Children inherit the worker's IPC connection fd. After worker death/respawn, a long-lived download curl keeps the old connection endpoint open for up to 30 minutes, delaying plugin failure detection and leaking fds into unrelated processes.
- **Required fix**: Open IPC fds with `SOCK_CLOEXEC`/`fcntl`, or `close_range()`/file-action close from fd 3 up in the child.

### VW-095 — `fork`+`execvp` in Multithreaded Worker; PDEATHSIG Linux-Only *(new 2026-09-05)*

- **Priority**: P3
- **Status**: Open
- **Affected**: `worker/src/vw_model_download.c:279-292`
- **Trigger**: Model download in the multithreaded worker (reader thread, translator pool active).
- **Impact**: `execvp` is not async-signal-safe (may run `malloc`/`getenv` in the forked child → possible child deadlock). `PR_SET_PDEATHSIG` (:286) is `#ifdef __linux__`, so on macOS/BSD an orphan curl can keep writing `.part` for ≤30 min after worker death, racing the next worker's hash→rename verification window.
- **Required fix**: Use `posix_spawn` (as vw_translate.c does) with a PDEATHSIG replacement / kill-on-parent-death per platform.

### VW-096 — ABORTING Stage Instantly Overwritten to IDLE *(new 2026-09-05)*

- **Priority**: P3
- **Status**: Open
- **Affected**: `worker/src/vw_model_download.c:521-528`
- **Trigger**: Abort racing the download thread's post-download abort branch.
- **Impact**: ABORTING is set and immediately replaced by IDLE in consecutive lock sections, so pollers (vw_worker.c:1056-1079) can never observe ABORTING on this path; plugin cancel feedback/last_stage transitions are skipped.
- **Required fix**: Collapse into a single IDLE transition or keep ABORTING for one poll interval.

### VW-097 — Win32 Attribute-List Failure Silently Discards Worker Log *(new 2026-09-05)*

- **Priority**: P3
- **Status**: Open
- **Affected**: `plugin/src/vw_platform_win32.c:160-181, 192-193`
- **Trigger**: `malloc`/`InitializeProcThreadAttributeList`/`UpdateProcThreadAttribute` failure at worker spawn.
- **Impact**: `hLog`/`hStdin` are opened before the attribute-list setup; on attribute failure the worker launches with `inherit = FALSE` and the opened log is silently discarded — diagnostics vanish with no warning.
- **Required fix**: Log a warning or fall back to plain `STARTF_USESTDHANDLES`.

### VW-098 — `$INSTDIR` Raw-Interpolated into PowerShell Worker-Kill Command *(new 2026-09-05)*

- **Priority**: P3
- **Status**: Open
- **Affected**: `cmake/vw_installer.nsi.in:198, 205`
- **Trigger**: Install path containing `'` or `$`.
- **Impact**: `$INSTDIR` is interpolated raw into a PowerShell command with doubled quotes; the CIM query breaks, `StopOwnedWorkers` returns 2 (treated as soft failure), and owned workers may keep running and lock binaries during upgrade. Compounds VW-068.
- **Required fix**: Escape the path or use a short 8.3/executable-dir strategy.

### VW-099 — CPack ZIP Path Bypasses Worker/Model Verification and CPU-Worker Dependency Guard *(new 2026-09-05)*

- **Priority**: P3
- **Status**: Open
- **Affected**: `cmake/vw_packaging.cmake:149-186`
- **Trigger**: Building the portable ZIP archive.
- **Impact**: The ZIP/CPack path has no equivalent of `vw_check_workers.cmake` — no model/worker re-verification of staged bytes — and `cpack` invoked directly (not via the `package` target) bypasses the `add_dependencies(package vw_cpu_worker_fallback)` guard at :184-186, allowing a stale/missing CPU worker into the archive. Extends VW-066/VW-074.
- **Required fix**: Add staged-artifact verification to the ZIP path and route packaging through the guarded target.

### VW-100 — FFmpeg Negative Frame PTS Clamped to 0; Silent Resampler Error *(new 2026-09-05)*

- **Priority**: P3
- **Status**: Open
- **Affected**: `worker/src/vw_source_decoder_ffmpeg.c:202, 209-212`
- **Trigger**: Pre-`start_time` head padding frames; resampler error.
- **Impact**: Negative frame PTS is clamped to 0, so multiple pre-roll frames all stamp 0 and `current_pts_us` can go backward/duplicate at t=0. Additionally a negative `swr_convert` return (:209-212) is silently ignored — frame dropped without the WARN logging every other error path uses.
- **Required fix**: Track and skip head-padding samples instead of collapsing their PTS; log resampler errors at least once.

### VW-101 — Control Reason Codes Indistinguishable on the Wire *(new 2026-09-05)*

- **Priority**: P3
- **Status**: Open
- **Affected**: `protocol/include/vw_protocol_types.h:177-179`, `protocol/src/vw_protocol_validate.c:130-132`
- **Trigger**: Any CONTROL frame.
- **Impact**: `VW_CTRL_REASON_USER_PAUSE`, `_USER_RESUME`, and `_USER_STOP` all equal `1U`; the validator does not check `reason` either. Receivers cannot distinguish pause/resume/stop by payload and rely purely on frame `type`. Contract smell, no current misbehavior.
- **Required fix**: Distinct codes + validator bound.

### VW-102 — Frame Header Serialized in Host Byte Order *(new 2026-09-05)*

- **Priority**: P3
- **Status**: Open
- **Affected**: `protocol/src/vw_protocol_codec.c:8-20`, `protocol/include/vw_protocol_types.h:94-100`
- **Trigger**: Any frame encode/decode.
- **Impact**: Header/payload structs are memcpy'd raw — fine for same-host IPC (both peers share endianness) but silently breaks if the endpoint is ever used cross-machine or the file is reused for another transport; `vw_frame_header_t.type` is `uint16_t` while the enum is int-sized (masking handled only by struct choice).
- **Required fix**: Document as host-order-local-only, or use explicit little-endian encode/decode.

### VW-103 — Odd-Length AUDIO_PCM Payload Silently Loses Trailing Byte *(new 2026-09-05)*

- **Priority**: P3
- **Status**: Open
- **Affected**: `worker/src/vw_worker.c:802-803`, `protocol/src/vw_protocol_validate.c:126`
- **Trigger**: `pcm_bytes` odd (validator explicitly tolerates ±1 byte vs expected).
- **Impact**: `pcm_bytes / sizeof(int16_t)` truncates the half-sample, so the buffer's timeline advances one sample short of the wire `duration_us` — cumulative sub-µs PTS skew on the affected frame. Inconsistent with the validator's documented "whole-sample rounding" contract.
- **Required fix**: Reject odd `pcm_bytes` in validation or round the sample count per contract.

### VW-104 — Code Hygiene: Feature-Test Macros, Includes, and Dead Code *(new 2026-09-05)*

- **Priority**: P3
- **Status**: Open
- **Affected**: `tests/integration/test_worker_ipc.c:6`, `tests/integration/test_worker_lifecycle.c:6`, `protocol/src/vw_protocol_validate.c:11-15`, `plugin/src/vw_whisper_module.c:997-999, 1371-1391, 1764-1769`, `plugin/src/vw_benchmark.c:70`, `tests/integration/test_worker_ipc.c:65-66, 111`
- **Trigger**: Build/read of affected files.
- **Impact**: (a) `#define _XOPEN_SOURCE 500` placed *after* standard includes in both integration tests (must precede the first include); (b) `vw_ipc_send` returns unchecked in test_worker_ipc.c, turning failed sends into slow 3 s receive timeouts; (c) mid-file include block in the validator (after a function definition) violates repo C17/clang-format conventions; (d) dead code/drift in the module: empty if-block (:1764-1769), orphaned comment fragment (:997-999), magic-number stage mapping duplicating `VW_MODEL_STAGE_*` (:1371-1391, compare presenter.c:185-203), unused `path_length` result (benchmark.c:70).
- **Required fix**: Move feature-test macros and includes to file top; check send returns; delete dead code; map stages via a shared table.

---

## Resolved, Stale, or False — Reference Archive

The following entries are preserved from the 2026-09-04 first-pass audit (plus the 2026-09-05 re-verification) for reference only.
They were struck through as false, confirmed fixed, or found stale/non-existent by the 2026-09-05 five-agent re-audit. Do not
re-open without fresh source evidence.

### VW-003 — ~~Use-After-Free and Crash in Unheld VLC Object Hierarchy Traversal~~

- **Priority**: P1
- **Status**: False on current branch (upheld 2026-09-05)
- **Evidence**: Vendored VLC 3.0.23 `src/misc/objects.c:506-533` — `vlc_list_children()` holds every returned child until `vlc_list_release()`. `vw_plugin_find_input` (module.c:1474) holds before releasing the list; `find_vout` (presenter.c:63-68) holds the vout child before release; the parent walk is safe because each object holds its parent. The first-pass UAF trigger is not reachable.

### VW-009 — ~~Private Caption Transcript Leaks to Stderr / Logs on Curl Failure~~

- **Priority**: P1
- **Status**: False as stated; residual stderr inheritance tracked as VW-071 (upheld 2026-09-05)
- **Evidence**: POSIX translation passes caption text through curl stdin (`--data-binary @-` / `q@-`, translate.c:739-748, 857-860, 915-917, 930); the URL contains only endpoint and language parameters. Curl's `-sS` diagnostics do not echo the stdin payload.

### VW-011 — ~~Inverted `strstr` Subtitle Drop in Segment Builder~~

- **Priority**: P1
- **Status**: False as stated (upheld 2026-09-05)
- **Evidence**: The `strstr(clean_text, hist->text)` check (vw_segment_builder.c:298, 332) is an intentional, time-gated mid-containment drop after word-aligned tail trimming. The segment-builder comments and ADR-017 define exact, fragment, and expanded re-recognition suppression; the reported "Good" example is only dropped when it overlaps the historical cue.

### VW-016 — ~~Out-of-Bounds Memory Reads from Non-NUL-Terminated Wire Strings~~

- **Priority**: P1
- **Status**: False on current branch (upheld 2026-09-05)
- **Evidence**: All `DEC_PTR` string consumers copy with explicit length then NUL-terminate (`vw_worker_client.c:785-801, 427-441`; `vw_worker.c:541-580`; `vw_caption_presenter.c:427-441`). `hello.client_version` is never read by the worker after the token check; `ack.worker_version` is never consumed by the plugin (only `capability_flags`/`selected_minor` at :233-236). No OOB path.

### VW-017 — ~~Unchecked CSPRNG Failure Leaves `auth_token` Uninitialized~~

- **Priority**: P1
- **Status**: False as stated; the actual fail-closed gap is tracked as VW-075 (upheld 2026-09-05)
- **Evidence**: `sys` is `calloc`-initialized, and worker launch plus sender-thread creation are inside the RNG-success `else` branch. On RNG failure no client or respawn path can consume the token. The filter nevertheless returns success without a functioning sender — see VW-075.

### VW-023 — ~~Aborted Model Download Still Commits to Disk~~

- **Priority**: P2
- **Status**: False as stated; residual cancellation race tracked as VW-072 (upheld 2026-09-05)
- **Evidence**: The download thread checks `abort_requested` before verification (vw_model_download.c:508, 515-520) and removes the `.part` file. A file reaches rename only after the pinned SHA-256 digest matches (565-601, rename at 602), so truncated data cannot be committed as a complete model.

### VW-024 — ~~Premature Success on Truncated WinHTTP Download~~

- **Priority**: P2
- **Status**: False as a model-integrity bug; residual status-reporting defect tracked as VW-073 (upheld 2026-09-05)
- **Evidence**: `vw_download_via_winhttp()` (vw_model_download.c:460-486) can return true after an early read-loop exit, but `vw_download_thread()` always hashes the `.part` file against the pinned catalog digest before rename (565-601). Truncated data is deleted and retried or failed.

### VW-027 — ~~Epoch Invalidation Advances Ordinal and Freezes Translation Queue~~

- **Priority**: P2
- **Status**: False on current branch (upheld 2026-09-05)
- **Evidence**: `vw_translate_async_invalidate()` re-anchors both epochs and ordinals and clears both rings (vw_translate_async.c:214-228). Completion threads drop old-epoch results before insertion (151-153) and `try_deliver` discards stale results (338-341), so the stale-result branch cannot advance `next_result_ordinal` for a later valid epoch. No freeze path.

### VW-029 — ~~Silero VAD LSTM State Never Cleared Across Passes~~

- **Priority**: P2
- **Status**: False on current branch (upheld 2026-09-05)
- **Evidence**: Vendored `whisper_vad_detect_speech()` calls `whisper_vad_reset_state(vctx)` on every detection pass (whisper.cpp:5183-5188), and the worker additionally resets state on seek, pause, resume, stop, and session transitions (vw_worker.c:604, 633, 734, 750, 785, 886, 912, 941). No state leakage path.

### VW-031 — ~~FFmpeg Demuxer Fixed Leftover Buffer Truncation~~

- **Priority**: P2
- **Status**: False on current branch (upheld 2026-09-05)
- **Evidence**: Both swr paths cap output at `resample_buf[4096]` (vw_source_decoder_ffmpeg.c:207-212, 217-223), so `remainder = count - to_copy ≤ 4096` always fits the 4096-sample leftover capacity; the clamp at :186 can never truncate in current callers.

### VW-033 — Whisper Engine Reports False-Positive GPU Active

- **Priority**: P2
- **Status**: **Fixed in current source — verified 2026-09-05; entry closed**
- **Evidence**: After Whisper initialization, the engine re-derives the selected GPU/IGPU device and requires a successful `ggml_backend_dev_init()` (vw_whisper_engine.c:96-125) before `gpu_active = true`; init failure degrades to CPU reporting. STATUS uses `vw_whisper_engine_is_gpu_active` (vw_worker.c:134). Residual nuance (not re-opening): the mirror is a re-derivation, not a read of whisper's actual backend, so a theoretical init-success/inference-fail divergence remains possible.

### VW-035 — ~~Missing Translation Queue Invalidation on Position Pause~~

- **Priority**: P2
- **Status**: False on current branch (upheld 2026-09-05)
- **Evidence**: The primary `VW_MSG_PAUSE` path invalidates the translation queue (vw_worker.c:636), and the plugin drops segments while paused instead of rendering them. The remaining POSITION-paused branch can retain work briefly but does not surface stale captions under the current caller flow.

### VW-038 — ~~Partial Send Failure in `vw_ipc_send` Corrupts Stream~~

- **Priority**: P2
- **Status**: False on current transports (upheld 2026-09-05)
- **Evidence**: Linux uses `SOCK_SEQPACKET` (records atomic; send is all-or-error with `EMSGSIZE` otherwise, vw_ipc_socket_linux.c:132-137) and Windows uses message-mode named pipes (`WriteFile` on `PIPE_TYPE_MESSAGE` is message-atomic, vw_ipc_pipe_win32.c:176-206). `vw_ipc_send` returns true only on full count. Partial stream fragments cannot occur unless the transport changes to `SOCK_STREAM` or byte-mode pipes.

### VW-046 — ~~Abandoned Temporary Benchmark File Leak on Redundant `begin`~~

- **Priority**: P3
- **Status**: False on current branch (upheld 2026-09-05)
- **Evidence**: The sender starts benchmarking once (vw_whisper_module.c:758, `benchmark.active` false from `calloc`), and the only other `vw_benchmark_begin()` call is guarded by `!sys->benchmark.active` in the respawn path (:676). Media swaps (:1087) and discontinuities do not call begin. No reachable redundant-`begin` path, hence no leak as described.

### VW-053 — Inverted Pointer Subtraction in Hallucination Filter

- **Priority**: P3
- **Status**: **Stale / non-existent — found and closed 2026-09-05; entry was marked Open but matches no current code**
- **Evidence**: `worker/src/vw_hallucination_filter.c:103-112` (and the entire file) contains no inverted pointer subtraction. All subtractions (:88, :111) are `end − start`/`inner_end − inner_start` with start ≤ end invariants enforced (early return at :79 for all-whitespace; `inner_end` only decremented while `> inner_start`). The SIZE_MAX edge on single-char `"*"` at :111 is correctly rejected by the `inner_len <= 64` guard at :112. Re-derive or retire.
