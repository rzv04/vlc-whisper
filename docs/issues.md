# Known Issues

This ledger records the pre-MVP audits of `gemini/milestone-4-mvp-release` and PR #36, followed by the remediation
verification on branch `fix/mvp-final-release-gates` (2026-09-01). Independent reviews covered core concurrency and
protocol behavior, caption and translation timing, Windows runtime, and packaging paths.

The findings below are source-confirmed unless explicitly marked as requiring Windows runtime validation. No issue is
considered fixed merely because a roadmap or release checklist marks the corresponding feature complete.

## Severity

- **P0 — Critical**: Security or privacy failure that blocks release.
- **P1 — High**: Common or severe correctness, reliability, or installer failure that blocks release.
- **P2 — Medium**: Important edge case or contract violation that should be fixed or explicitly accepted before release.
- **P3 — Low**: Hardening, documentation, or release-hygiene debt.

## Release Blockers

### VW-001 — Predictable Windows pipe permits fake-worker impersonation and PCM disclosure

- **Priority**: P0
- **Status**: Fixed in source — Windows named-pipe hardening integrated; Windows runtime/VM validation pending
- **Affected**: `plugin/src/vw_whisper_module.c:1379`, `protocol/src/vw_ipc_pipe_win32.c:9`,
  `plugin/src/vw_worker_client.c:163`
- **Trigger**: A same-user local process creates the PID-derived named-pipe server before the real worker binds.
- **Impact**: The real worker fails to listen, while the plugin connects to the attacker's server and sends its one-time
  token in `HELLO`. A fake `HELLO_ACK` only needs the expected protocol major and PCM capability, after which the
  attacker can receive START/audio frames and inject captions.
- **Required fix**: Use a cryptographically random endpoint name, an explicit same-user ACL, first-instance protection,
  and server identity or channel binding that does not reveal the sole authenticator to an untrusted server.
- **Missing coverage**: Pre-created-pipe impersonation and connection-race tests.

### VW-002 — Windows overlapped I/O destroys state before cancellation completes

- **Priority**: P1
- **Status**: Fixed in source — CancelIoEx/reap handling integrated; Windows runtime validation pending
- **Affected**: `protocol/src/vw_ipc_pipe_win32.c:15-35,72-91,102-124`
- **Trigger**: Named-pipe listen, read, or write exceeds its timeout.
- **Impact**: `CancelIo()` is asynchronous, but the event is closed and the stack `OVERLAPPED` goes out of scope without
  reaping completion. A late kernel completion can reference invalid state, corrupt message sequencing, or crash.
- **Required fix**: Use `CancelIoEx()`, wait for and reap completion, then close the event or transport.
- **Missing coverage**: Windows timeout-then-success and repeated cancel/reuse tests for reads and writes.

### VW-003 — Live-mode seek and discontinuity handling does not create a new epoch

- **Priority**: P1
- **Status**: Fixed in source — live seek epoch/STOP-START integrated; Windows/manual seek coverage pending where flagged
- **Affected**: `plugin/src/vw_whisper_module.c:1033-1057,1098-1106,1316-1327`,
  `worker/src/vw_worker.c:702-805,1185-1213`
- **Trigger**: Seek, non-monotonic PTS, or flagged discontinuity while live audio is queued or inference is active.
- **Impact**: The plugin blanks the presenter and sends only `POSITION_FLAG_SEEK`; it does not send STOP and START a new
  session as documented. Live worker POSITION handling does not clear audio, segment-builder, or VAD state. Pre-seek
  work can therefore render after the seek, and the unchanged session ID defeats the plugin's stale-segment filter. If
  no media position is available, the worker is not reset at all.
- **Required fix**: STOP and START a fresh session epoch for live discontinuities, and make stale-output rejection
  explicit at the plugin boundary.
- **Missing coverage**: Seek during inference, queued audio, unavailable target position, and output-pipe backlog.

### VW-004 — Source-mode failure does not fall back to live PCM end-to-end

- **Priority**: P1
- **Status**: Fixed in source — source-open fallback to live PCM integrated
- **Affected**: `worker/src/vw_worker.c:641-697`, `plugin/src/vw_worker_client.c:326-374`
- **Trigger**: Media Foundation or FFmpeg cannot open the supplied local source.
- **Impact**: The worker sends recoverable `E_SOURCE_OPEN`, then `STARTED(source_active=0)`. The client returns failure on
  the ERROR and never consumes STARTED, leaving the worker session active while the plugin disables captions and sends
  no live PCM.
- **Required fix**: Treat recoverable `E_SOURCE_OPEN` as part of the START handshake and continue to STARTED, or retry an
  explicit live-only START.
- **Missing coverage**: Source-open failure followed by successful live capture.

### VW-005 — Windows auto backend does not recover from GPU executable or loader failure

- **Priority**: P1
- **Status**: Fixed in source — GPU/CPU fallback and packaging integrated; clean Windows VM validation pending
- **Affected**: `plugin/src/vw_whisper_module.c:247-269,1406-1467`, `worker/CMakeLists.txt:18-24,117-124`,
  `cmake/vw_packaging.cmake:51-60`, `README.md:245-254`
- **Trigger**: The GPU worker exists but cannot load because `vulkan-1.dll`, a driver, or another imported dependency is
  unavailable.
- **Impact**: Discovery selects the GPU worker first and never retries `vlc-whisper-worker-cpu.exe`. The installer target
  depends only on the current preset's worker, while the documented CPU copy is manual and masks failure with `|| true`.
  Captions remain unavailable on an otherwise supported CPU-only system.
- **Required fix**: Always package the CPU worker and retry it after GPU spawn or handshake failure when backend is
  `auto`; make missing package inputs fatal at build time.
- **Missing coverage**: Clean Windows VM with a GPU worker present but no usable Vulkan loader or driver.

### VW-006 — Media Foundation is used without COM initialization

- **Priority**: P1
- **Status**: Fixed in source; Windows runtime confirmation pending — COM initialization integrated, VM confirmation still required
- **Affected**: `worker/src/vw_worker.c:301-318`, `worker/src/vw_source_decoder_mf.c:61-105`
- **Trigger**: Windows source mode opens a local media URL.
- **Impact**: The worker calls `MFStartup()` but not `CoInitializeEx()` on the thread that calls
  `MFCreateSourceReaderFromURL()`, violating the Media Foundation initialization contract and potentially disabling
  source mode.
- **Required fix**: Initialize COM with an appropriate apartment model before Media Foundation use and uninitialize it
  on every exit path.
- **Missing coverage**: Real Media Foundation startup/open test on a clean Windows VM.

### VW-007 — Translation saturation drops the source caption

- **Priority**: P1
- **Status**: Fixed in source — translation saturation fallback integrated
- **Affected**: `worker/src/vw_translate_async.c:220-237`, `worker/src/vw_worker.c:1182-1213`
- **Trigger**: `job_count + inflight_count + result_count` reaches the translation result capacity.
- **Impact**: `vw_translate_async_submit()` returns false and the worker frees the finalized caption instead of emitting
  source text. This violates the documented failure/deadline/queue-unavailable fallback.
- **Required fix**: Emit the source segment immediately with attempted/failed translation metadata when submission fails.
- **Missing coverage**: Full translation queue integration test asserting source-caption delivery.

### VW-008 — Queue sentinel collision can drop START, STOP, or SHUTDOWN

- **Priority**: P1
- **Status**: Fixed in source — queue sentinel handling integrated
- **Affected**: `worker/src/vw_worker_queue.c:88-115,131-180`
- **Trigger**: The queue is full after absolute `head` or `tail` reaches exactly `q->capacity`, naturally occurring after
  the first capacity-sized rollover.
- **Impact**: `q->capacity` is both a valid absolute queue index and the not-found sentinel. A valid audio or control
  eviction candidate can be mistaken for not found, causing required session controls or shutdown to be dropped.
- **Required fix**: Track discovery with a boolean or use `SIZE_MAX` as the sentinel.
- **Missing coverage**: Full queues where `tail == capacity` for AUDIO, START, STOP, and SHUTDOWN arrivals.

### VW-009 — Bare Windows worker fallback is executable-search hijackable

- **Priority**: P1
- **Status**: Fixed in source — Windows worker path hijack mitigation integrated; Windows validation pending
- **Affected**: `plugin/src/vw_whisper_module.c:1406-1413`, `plugin/src/vw_platform_win32.c:144`
- **Trigger**: Worker discovery fails and VLC's inherited current directory contains `vlc-whisper-worker.exe`.
- **Impact**: `CreateProcessW(NULL, ...)` resolves the bare executable through Windows search rules, including the current
  directory. An attacker-controlled worker can run with VLC's privileges and receive the authentication token.
- **Required fix**: Remove the bare fallback or resolve a trusted absolute path and pass it as the application name.
- **Missing coverage**: Discovery-failure test with an attacker-controlled current directory.

### VW-010 — Installer force-kills every VLC instance and ignores replacement failures

- **Priority**: P1
- **Status**: Fixed in source — owned worker termination, locale-independent VLC detection, and explicit abort exit integrated; Windows installer VM validation pending
- **Affected**: `cmake/vw_installer.nsi.in:150-217,356-374`
- **Trigger**: Install or uninstall while any VLC or VLC-Whisper worker process is running.
- **Impact**: `taskkill /F /IM vlc.exe /T` terminates unrelated VLC instances without prompting. Exit codes and Delete
  failures are ignored after a fixed one-second sleep, so setup can report success with a mixed old/new installation.
- **Required fix**: Prompt or abort when VLC is running, target only owned worker processes, wait for termination, and
  fail or schedule reboot replacement when files remain locked.
- **Missing coverage**: Installer acceptance tests with unrelated VLC instances and locked plugin/worker files.

## Important Correctness and Contract Issues

### VW-011 — Playback-rate changes do not re-synchronize scheduled captions

- **Priority**: P2
- **Status**: Fixed in source — playback-rate resync integrated
- **Affected**: `plugin/src/vw_whisper_module.c:915-920,965-1017`, `plugin/src/vw_caption_presenter.c:343-361`,
  `worker/src/vw_worker.c:702-707`
- **Trigger**: Playback speed changes while a caption is pending or already submitted to VLC.
- **Impact**: The sender transmits rate but never detects a transition. Already-scheduled SPUs retain start and stop ticks
  calculated with the old rate, producing drift and incorrect display duration.
- **Required fix**: Detect meaningful rate changes, blank/rebase the presenter, and re-anchor worker state.
- **Missing coverage**: `1x -> 2x` and `2x -> 0.5x` transitions with pending and displayed cues.

### VW-012 — Failed source seek is treated as a successful timeline re-anchor

- **Priority**: P2
- **Status**: Fixed in source — failed seek handling integrated
- **Affected**: `worker/src/vw_worker.c:716-724,744-750,823-831`,
  `worker/src/vw_source_decoder_mf.c:141-159`, `worker/src/vw_source_decoder_ffmpeg.c:145-171`
- **Trigger**: A corrupt, unsupported, or non-seekable source rejects a seek or resume operation.
- **Impact**: The worker ignores the false return value, sets `decoded_pts_us` to the requested target, and clears state.
  Subsequent old-position audio can be captioned against the new timeline.
- **Required fix**: Check the result and disable source mode, report an error, or retain the old anchor on failure.
- **Missing coverage**: Injected MF and FFmpeg seek failures for explicit seek and paused resume.

### VW-013 — Invalid VLC PTS contaminates live caption timing

- **Priority**: P2
- **Status**: Fixed in source — invalid PTS handling integrated
- **Affected**: `plugin/src/vw_whisper_module.c:1302-1307,1330-1338`, `worker/src/vw_audio_buffer.c:36-43`,
  `worker/src/vw_segment_builder.c:227-231`
- **Trigger**: VLC supplies a negative or invalid audio-block `i_pts` around startup, teardown, or discontinuity.
- **Impact**: The callback still captures the block and AUDIO validation accepts its start PTS. Negative-timestamp
  hypotheses are rejected; if valid PTS resumes, earlier buffered samples can be falsely assigned to the later PTS.
- **Required fix**: Skip invalid-PTS capture or explicitly clear and re-anchor buffered audio when valid PTS resumes.
- **Missing coverage**: Invalid-PTS startup and discontinuity fixtures through plugin, protocol, and worker.

### VW-014 — Same-session stale captions can render after source-mode seeks

- **Priority**: P2
- **Status**: Fixed in source — stale-caption epoch handling integrated
- **Affected**: `plugin/src/vw_whisper_module.c:1038-1057,1098-1106`,
  `plugin/src/vw_worker_client.c:643-672`, `worker/src/vw_translate_async.c:92-104,147-153`
- **Trigger**: A caption is already buffered in worker-to-plugin IPC when the user seeks in source mode.
- **Impact**: Presenter blanking does not remove pipe-buffered output. Source seeks retain the session ID, and no seek
  generation is carried on caption frames, so the plugin accepts and renders the old caption at the new playhead.
- **Required fix**: Use a new session ID for seek transitions or carry a caption epoch that the plugin can reject.
- **Missing coverage**: Completed source and translated caption in output IPC immediately before seek.

### VW-015 — Translation delivery holds its cancellation mutex across blocking IPC

- **Priority**: P2
- **Status**: Fixed in source — translation delivery locking integrated
- **Affected**: `worker/src/vw_translate_async.c:318-333`, `worker/src/vw_worker.c:179-218,1021-1027`
- **Trigger**: Caption delivery blocks on a congested or dead peer while seek, stop, or translation configuration arrives.
- **Impact**: Reader-side invalidation waits for one or two IPC sends, delaying controls by the transport timeout and
  widening the stale-output race. This is a bounded latency failure, not a proven circular deadlock.
- **Required fix**: Remove or copy the result under lock, unlock before IPC, and validate an epoch before delivery commit.
- **Missing coverage**: Stalled output pipe followed by seek or translation invalidation.

### VW-016 — Sequence and session validation is incomplete

- **Priority**: P2
- **Status**: Fixed in source — sequence/session validation integrated
- **Affected**: `protocol/src/vw_protocol_validate.c:3-8`, `plugin/src/vw_worker_client.c:611-622`,
  `worker/src/vw_worker.c:248-254,809-852,993-1001`
- **Trigger**: Duplicate, reordered, replayed, or stale authenticated frames.
- **Impact**: Sequence numbers are never checked. PAUSE, RESUME, STOP, and TRANSLATE controls ignore their session IDs,
  allowing a stale control to mutate a current session.
- **Required fix**: Track monotonic per-direction sequence numbers and enforce session IDs for session-scoped controls.
- **Missing coverage**: Duplicate, backward, wraparound, and wrong-session frames.

### VW-017 — Zero-length malformed controls bypass payload rejection

- **Priority**: P2
- **Status**: Fixed in source — zero-length control rejection integrated
- **Affected**: `protocol/src/vw_protocol_codec.c:219-221,293-299`, `worker/src/vw_worker.c:525-538,809-852`
- **Trigger**: An authenticated peer sends zero-payload PAUSE, RESUME, or STOP.
- **Impact**: Decode fails, but worker rejection is conditional on `payload_len > 0`. The zeroed control union reaches the
  state-changing switch; a zero-length STOP can terminate the active session. Empty SHUTDOWN is valid and must remain so.
- **Required fix**: Reject every invalid payload regardless of length, with an explicit SHUTDOWN exception.
- **Missing coverage**: Zero-length controls and unknown zero-length message types.

### VW-018 — GPU status can report GPU after actual CPU fallback

- **Priority**: P2
- **Status**: Fixed in source; runtime confirmation pending — GPU status/actual-backend handling integrated
- **Affected**: `worker/src/vw_whisper_engine.c:47-70`
- **Trigger**: A GPU or IGPU device enumerates but backend initialization or execution fails.
- **Impact**: The wrapper marks GPU active from enumeration alone, while whisper.cpp may fall back to CPU. STATUS,
  settings, and benchmarks can report the wrong backend.
- **Required fix**: Query actual initialized backend state or propagate backend initialization outcome.
- **Missing coverage**: Enumerated device whose backend cannot initialize or execute.

### VW-019 — Streaming resampler restarts source phase at every input block

- **Priority**: P2
- **Status**: Fixed in source — resampler phase handling integrated
- **Affected**: `plugin/src/vw_audio_capture.c:16-20,48-55,81-85`
- **Trigger**: Non-integer sample-rate conversion, especially 44.1 kHz to 16 kHz, across multiple callback blocks.
- **Impact**: Output-frame count carries a remainder, but source-index mapping starts from zero per block. Boundary samples
  are repeated or skipped, degrading input quality and recognition consistency.
- **Required fix**: Preserve the resampler phase and input position across blocks or use a streaming resampler.
- **Missing coverage**: Multi-block 44.1 kHz continuity and PTS tests.

### VW-020 — Unsupported transcription languages are accepted silently

- **Priority**: P2
- **Status**: Fixed in source — language validation integrated
- **Affected**: `worker/src/vw_worker_config.c:357-368`, `worker/src/vw_worker.c:344-349`,
  `worker/src/vw_whisper_engine.c:107-112`
- **Trigger**: Direct config or CLI supplies an arbitrary short language code or `auto`.
- **Impact**: CLI validation checks only length, the setter does not validate against Whisper language IDs, and its return
  value is ignored. Invalid values can silently produce failed or malformed inference prompts; rejected `auto` silently
  leaves the default language active.
- **Required fix**: Validate against `whisper_lang_id()` and surface invalid configuration before session startup.
- **Missing coverage**: Invalid and `auto` language startup/config-respawn tests.

## Windows Installer and Path Edge Cases

### VW-021 — Installer does not validate VLC executable architecture

- **Priority**: P2
- **Status**: Fixed in source — installer architecture validation integrated; Windows installer VM validation pending
- **Affected**: `cmake/vw_installer.nsi.in:55-60,66-74,89-98`
- **Trigger**: A user manually selects a 32-bit VLC directory on 64-bit Windows.
- **Impact**: Setup installs the 64-bit plugin into incompatible VLC and completes successfully, but VLC cannot load it.
- **Required fix**: Inspect `vlc.exe` PE architecture or explicitly reject known 32-bit installations.

### VW-022 — Installer can accept a stale destination worker as current package content

- **Priority**: P2
- **Status**: Fixed in source — installer worker-input validation integrated
- **Affected**: `cmake/vw_installer.nsi.in:118-127`
- **Trigger**: Both `/nonfatal` worker inputs are missing while an older worker already exists in `$INSTDIR`.
- **Impact**: The post-copy `FileExists` guard passes against the stale destination, producing a new plugin with an old or
  incompatible worker.
- **Required fix**: Make build inputs fatal before NSIS compilation and do not use destination state as package validation.

### VW-023 — Installer overwrites and uninstaller deletes VLC root license files

- **Priority**: P2
- **Status**: Fixed in source — installer license-file handling integrated
- **Affected**: `cmake/vw_installer.nsi.in:139-142,215-217`
- **Trigger**: VLC or another component owns `$INSTDIR\LICENSE` or `$INSTDIR\THIRD_PARTY_NOTICES.md`.
- **Impact**: Installation overwrites those files and uninstall deletes them.
- **Required fix**: Store project notices under an application-owned directory or with project-specific filenames.

### VW-024 — Windows model paths use ANSI filesystem APIs

- **Priority**: P2
- **Status**: Fixed in source; locale-dependent Windows validation pending — wide-API path handling integrated
- **Affected**: `worker/src/vw_model_download.c:161-168,183-197,317-427`
- **Trigger**: `%LOCALAPPDATA%`, model directory, or profile name contains characters outside the active ANSI code page.
- **Impact**: Download file creation, locking, hash verification, or atomic rename can fail.
- **Required fix**: Convert UTF-8 paths once and use wide Win32 filesystem APIs end-to-end.
- **Missing coverage**: Non-ASCII Windows profile and model directory.

### VW-025 — Windows source decoder corrupts UNC file URIs

- **Priority**: P2
- **Status**: Fixed in source — UNC handling integrated; Windows validation pending
- **Affected**: `worker/src/vw_source_decoder_mf.c:26-59`
- **Trigger**: Source URL resembles `file://server/share/video.mp4`.
- **Impact**: Normalization produces `server\share\video.mp4` instead of `\\server\share\video.mp4`, so source mode
  fails and reaches the currently broken live fallback.
- **Required fix**: Preserve the UNC authority and leading double backslash.
- **Missing coverage**: UNC source-open and seek fixtures.

### VW-026 — Elevated uninstall may remove the wrong user's model cache

- **Priority**: P2
- **Status**: Fixed in source — uninstall cache-ownership handling integrated
- **Affected**: `cmake/vw_installer.nsi.in:203-208`, `README.md:129-132`
- **Trigger**: The elevated uninstaller runs under an administrator account different from the user who downloaded models.
- **Impact**: `SetShellVarContext current` resolves the administrator's `%LOCALAPPDATA%`; the original user's app-owned
  cache remains while another account's cache may be removed.
- **Required fix**: Define installer ownership and retain the installing user's profile path for cleanup.

## Lower-Severity Hardening

### VW-027 — Runtime worker-path changes can retain a stale executable

- **Priority**: P3
- **Status**: Fixed in source — worker-path respawn handling integrated
- **Affected**: `plugin/src/vw_whisper_module.c:827-866`, `plugin/src/vw_platform_linux.c:140-145`
- **Trigger**: A runtime configuration clears, changes, or supplies an overlong worker path before respawn.
- **Impact**: The snapshot changes while `sys->worker_path` may retain the old executable. Linux also checks with
  `access()` before spawn, leaving a replacement TOCTOU window.
- **Required fix**: Resolve the executable at respawn and reject unapplied path changes explicitly.

### VW-028 — Linux monotonic-clock failure falls back to wall clock

- **Priority**: P3
- **Status**: Fixed in source — monotonic-clock handling integrated
- **Affected**: `plugin/src/vw_platform_linux.c:126-132`
- **Trigger**: `clock_gettime(CLOCK_MONOTONIC)` fails.
- **Impact**: Timeout and latency calculations use `time(NULL)` and can jump after clock adjustment.
- **Required fix**: Surface failure or use another monotonic source; never substitute civil time for monotonic timing.

### VW-029 — Unix socket endpoint truncation is unchecked

- **Priority**: P3
- **Status**: Fixed in source — socket endpoint validation integrated
- **Affected**: `protocol/src/vw_ipc_socket_linux.c:19-29,65-74`
- **Trigger**: The requested endpoint exceeds `sockaddr_un.sun_path`.
- **Impact**: Bind/connect use a truncated path while unlink uses the original, causing connection or cleanup mismatch.
- **Required fix**: Reject overlong endpoint paths before copying.

### VW-030 — Adjacent cues can violate the one-second display floor

- **Priority**: P3
- **Status**: Fixed in source — cue display-floor handling integrated
- **Affected**: `plugin/src/vw_caption_presenter.c:388-411`
- **Trigger**: Successive cues begin less than one second apart.
- **Impact**: The preceding cue is clipped to the successor start and may flash for far less than the documented floor.
- **Required fix**: Define merge or overlap behavior, or document the floor as best-effort.

### VW-031 — Manual Windows VAD downloader does not verify SHA-256

- **Priority**: P3
- **Status**: Fixed in source — VAD downloader hash verification integrated
- **Affected**: `models/vw_download_vad_model.cmd:13-20`
- **Trigger**: The helper downloads a tampered, truncated, or wrong VAD file while curl still exits successfully.
- **Impact**: The helper reports success and leaves an unverified model that later silently falls back.
- **Required fix**: Verify the manifest hash before reporting success.

### VW-035 — Plugin state is never attached to VLC filter

- **Priority**: P1
- **Status**: Fixed in source — `vw_plugin_open` now stores its allocated state in `p_filter->p_sys` and initializes teardown atomics before launch
- **Affected**: `plugin/src/vw_whisper_module.c`
- **Trigger**: Every successful module open leaves `filter_t.p_sys` unset.
- **Impact**: The audio callback passes through without capture, and module close skips sender/client/queue/presenter cleanup, leaving workers and resources alive across Stop or playlist changes.
- **Required fix**: Attach the allocated state before returning from open and detach it at close start.
- **Missing coverage**: Windows VLC Stop, Next-item, and repeated playlist lifecycle smoke test.

### VW-036 — Relative configured model paths ignore installed bundle location

- **Priority**: P1
- **Status**: Fixed in source — worker resolves relative model filenames from the per-user model directory and adjacent installed `models/`; Windows runtime validation pending
- **Affected**: `worker/src/vw_worker_config.c`, `worker/include/vw_worker_config.h`
- **Trigger**: Lua persists `model-path=models/<filename>`, while Windows launches the worker with VLC's current directory outside the installation root.
- **Impact**: The worker misses the installed Whisper model, rejects `START` with `E_MODEL_MISSING`, and leaves captions unavailable until settings change.
- **Required fix**: Resolve the selected filename under the worker's executable-adjacent `models/` directory after the per-user model directory.
- **Missing coverage**: Clean Windows install with a persisted relative model-path setting.

### VW-037 — Windows worker console is visible during playback

- **Priority**: P3
- **Status**: Fixed in source — worker spawn uses hidden-window and no-console creation flags; Windows runtime validation pending
- **Affected**: `plugin/src/vw_platform_win32.c`
- **Trigger**: The plugin launches the console-subsystem worker without `CREATE_NO_WINDOW` or `SW_HIDE`.
- **Impact**: An implementation-detail worker console appears to the user and can flash during startup.
- **Required fix**: Apply hidden-window startup flags only to the worker process while preserving redirected diagnostics.
- **Missing coverage**: Windows VLC playback confirms no worker console/taskbar window appears.

## PR #36 Follow-up Audit (`b0117ee`, remediated 2026-09-01)

The PR head was fetched from `refs/pull/36/head` and reviewed before remediation. Every finding below is fixed in the
current branch and covered by the verification evidence at the end of this ledger.

### VW-038 — Seek-epoch regression test destroys its own executable

- **Priority**: P1
- **Status**: Fixed in source — unique absolute temporary endpoints and repeated execution verified
- **Affected**: `tests/unit/test_worker_client_seek_epoch.c:245`, `protocol/src/vw_ipc_socket_linux.c:32`
- **Trigger**: Run `test_worker_client_seek_epoch` from CTest's `build/.../tests` working directory.
- **Impact**: The test binds the relative endpoint `test_worker_client_seek_epoch`; `vw_ipc_listen()` unlinks that path
  before `bind()`, replacing the executable with a Unix socket. The first run passes, but subsequent CTest or Valgrind
  runs report `BAD_COMMAND`/permission denied until the target is rebuilt.
- **Required fix**: Use a unique endpoint under a temporary directory (and clean it up), never a path equal to the test binary.

### VW-039 — Translation invalidation can emit stale captions after the epoch changes

- **Priority**: P1
- **Status**: Fixed in source — control invalidation and delivery are serialized by the worker main loop
- **Affected**: `worker/src/vw_translate_async.c:348-389`
- **Trigger**: A translation result passes the second epoch check, then the reader thread invalidates the translator
  during the unlock-to-`deliver()` window (especially on `TRANSLATE_CTRL`, which preserves the session ID).
- **Impact**: The old translated caption is still written to IPC after invalidation. The post-send check cannot retract
  a frame already visible to the plugin.
- **Required fix**: Serialize invalidation and delivery through a cancellation-safe handoff, or attach and validate a
  generation at the actual send boundary so invalidated results cannot be emitted.

### VW-040 — Translation invalidation may delete the first result of the new epoch

- **Priority**: P1
- **Status**: Fixed in source — completion removal uses immutable epoch-qualified identity
- **Affected**: `worker/src/vw_translate_async.c:350-360`
- **Trigger**: An old delivery snapshots ordinal `1`; invalidation advances the epoch and a new result is queued with
  ordinal `1` before the old delivery reacquires the mutex.
- **Impact**: The stale-delivery cleanup searches by ordinal only and removes the new epoch's result, losing the first
  caption after a seek or translation-config change.
- **Required fix**: Match the queued result by both epoch and ordinal (or retain an immutable completion identity).

### VW-041 — Audio callback performs logging and can enter I/O

- **Priority**: P1
- **Status**: Fixed in source — callback logging removed
- **Affected**: `plugin/src/vw_whisper_module.c:1491-1500`, `protocol/src/vw_log.c`
- **Trigger**: Valid PTS resumes after an invalid PTS interval while diagnostic logging is enabled.
- **Impact**: The realtime VLC audio callback calls `vw_log_event()`, which can format text, lock/log, write, and flush
  a file. This violates the callback's no-blocking/no-I/O/no-allocation contract and can cause audio glitches.
- **Required fix**: Store an atomic event flag in the callback and log it from the sender thread.

### VW-042 — Failed source seek keeps a stale decoder anchor

- **Priority**: P2
- **Status**: Fixed in source — timeline anchors update only after a successful decoder seek
- **Affected**: `worker/src/vw_worker.c:764-818`
- **Trigger**: A source-mode `vw_source_decoder_seek()` fails for a new `POSITION` target.
- **Impact**: `current_playback_pts_us` and `last_playback_pts_us` advance to the requested target while
  `decoded_pts_us`/decoder data remain at the old position. Look-ahead can then emit old audio with new timeline pacing,
  producing captions with incorrect PTS.
- **Required fix**: Keep the old playback anchor on failure, or reset/terminate source mode and report an explicit
  recoverable failure instead of mixing the two timelines.

### VW-043 — Monotonic-clock failure can make IPC deadlines infinite

- **Priority**: P2
- **Status**: Fixed in source — negative and overflowing monotonic deadlines fail closed
- **Affected**: `plugin/src/vw_platform_linux.c:126-137`, `plugin/src/vw_platform_win32.c:31-45`,
  `plugin/src/vw_worker_client.c:27-46,193-197,308-407`
- **Trigger**: The monotonic clock API returns its documented `-1` error sentinel.
- **Impact**: Callers compute deadlines from `-1` and repeatedly compare later `-1` readings, so handshake/frame loops
  can retry forever instead of failing closed.
- **Required fix**: Treat a negative clock result as fatal and use a bounded fallback timeout path.

### VW-044 — Linux IPC endpoint remains predictable and is not cleaned up

- **Priority**: P2
- **Status**: Fixed in source — random private endpoints, peer credentials, permissions, and cleanup integrated
- **Affected**: `plugin/src/vw_whisper_module.c:1617`, `protocol/src/vw_ipc_socket_linux.c:32,105-111`
- **Trigger**: Any Linux plugin session uses `/tmp/vlc-whisper-<pid>.sock`.
- **Impact**: A local process can predict/connect first and consume the worker's single listener, causing a denial of
  service; default socket permissions and lack of peer-credential checks widen the attack surface. `vw_ipc_close()`
  never unlinks the path, leaving stale filesystem entries for the next startup to unlink.
- **Required fix**: Use a per-session random endpoint in a private runtime directory, set restrictive permissions, verify
  peer credentials, and unlink only an owned socket during teardown.

### VW-045 — Windows Unicode paths still use ANSI discovery/file APIs

- **Priority**: P1
- **Status**: Fixed in source — UTF-8 internals now cross wide Win32 discovery, launch, and model-loading boundaries
- **Affected**: `worker/src/vw_worker_config.c:23-29,69-71`,
  `plugin/src/vw_whisper_module.c:73-88,146-153,258-266`
- **Trigger**: VLC, the user profile, or the install directory contains characters outside the active ANSI code page.
- **Impact**: Worker/model/VAD discovery and file existence checks fail or produce malformed paths even though the
  downloader uses wide APIs. A Unicode Windows installation can therefore report a successful install but leave captions
  unavailable.
- **Required fix**: Carry UTF-8 internally and use `GetModuleFileNameW`, `GetFileAttributesW`, wide registry/environment
  access, and a wide-safe model-load path; add a Unicode-path Windows test.

### VW-046 — Direct CPack packaging does not depend on CPU fallback staging

- **Priority**: P2
- **Status**: Fixed and verified — direct GPU CPack builds the CPU fallback and emits a complete ZIP
- **Affected**: `cmake/vw_packaging.cmake:64-102,154-157`
- **Trigger**: Run CPack/install directly after a clean GPU configure instead of the `installer` target.
- **Impact**: The install rule references `vlc-whisper-worker-cpu.exe`, but only the NSIS `installer` target depends on
  `vw_cpu_worker_fallback`. CPack can fail for a missing file or produce an incomplete package.
- **Required fix**: Make the package/install target depend on CPU fallback staging, or stage and verify both workers in a
  shared packaging prerequisite.

### VW-047 — Media Foundation can discard oversized sample remainders

- **Priority**: P2
- **Status**: Fixed in source — leftover storage grows with checked bounds and preserves the complete sample
- **Affected**: `worker/src/vw_source_decoder_mf.c:269-285`
- **Trigger**: A decoded MF sample contains more than the requested output buffer plus the 4096-sample leftover ring.
- **Impact**: The remainder is clipped instead of retained, dropping source audio and shifting later caption timing.
- **Required fix**: Preserve arbitrary remainder length (or drain the sample incrementally) rather than truncating at the
  fixed 4096-sample buffer.

### VW-048 — Windows model-download abort races WinHTTP handle use

- **Priority**: P2
- **Status**: Fixed in source — the download thread exclusively owns WinHTTP handles and polls bounded cancellation
- **Affected**: `worker/src/vw_model_download.c:351-363,412-483,699-713`
- **Trigger**: Abort/free closes `hRequest` while the download thread is in `WinHttpReadData()` or another WinHTTP call.
- **Impact**: The handle is cleared under the mutex but used from an unlocked local copy, allowing cancellation-time
  use-after-close or undefined WinHTTP behavior.
- **Required fix**: Serialize WinHTTP calls and handle closure, or use a documented cancellation mechanism and stress test
  abort during each request phase.

### VW-049 — START handshake accepts an uncorrelated STARTED frame

- **Priority**: P2
- **Status**: Fixed in protocol 1.6 — STARTED carries the session ID with legacy-minor decode compatibility
- **Affected**: `plugin/src/vw_worker_client.c:308-353`
- **Trigger**: A stale/malformed `STARTED` frame arrives during a new start handshake on an authenticated transport.
- **Impact**: The client accepts the first `STARTED` without checking its worker sequence or a session correlation (the
  `STARTED` payload has no session ID), then marks the new session active.
- **Required fix**: Validate the handshake sequence against the HELLO_ACK baseline and include/check the requested
  session ID in the start confirmation.

### VW-050 — Worker error/model-progress send failures are ignored

- **Priority**: P2
- **Status**: Fixed in source — header/payload failures terminate the worker transport loop
- **Affected**: `worker/src/vw_worker.c:67-124,1146-1151`
- **Trigger**: IPC closes or a write partially fails while sending `ERROR` or `MODEL_PROGRESS`.
- **Impact**: The worker reports success and continues, leaving the peer potentially mid-frame/desynchronized; the plugin
  can miss the only diagnostic or terminal download state.
- **Required fix**: Propagate both header and payload write results and drop/terminate the transport on failure.

### VW-051 — Windows installer accepts unsupported VLC versions

- **Priority**: P2
- **Status**: Fixed in source — installer requires a 64-bit VLC 3 executable
- **Affected**: `cmake/vw_installer.nsi.in:97-104`
- **Trigger**: The user selects any existing 64-bit `vlc.exe`, including an unsupported VLC major/version.
- **Impact**: The installer reports success while placing a module built for the vendored VLC 3.0.23 ABI into a VLC
  version that may reject or fail to load it.
- **Required fix**: Validate the supported VLC major/version (or make compatibility explicit) before installation.

### VW-052 — Windows installer command lines are not VERBATIM-safe for spaces

- **Priority**: P2
- **Status**: Fixed and verified — custom commands are VERBATIM and a space-containing build produced an installer
- **Affected**: `cmake/vw_packaging.cmake:104-130`
- **Trigger**: Checkout/build/model paths contain spaces.
- **Impact**: The outer `installer` custom target omits `VERBATIM`; generated `-D...` arguments or the NSIS script path
  can split and make packaging fail or validate the wrong path.
- **Required fix**: Add `VERBATIM` and test a space-containing source/build path.

### VW-053 — Packaging verification and copy have a TOCTOU window

- **Priority**: P2
- **Status**: Fixed in source — workers, plugin, and verified model snapshots are packaged from a staging directory
- **Affected**: `cmake/vw_packaging.cmake:117-126`, `cmake/vw_check_workers.cmake`
- **Trigger**: A local process replaces a worker, plugin, or model after validation but before `makensis`/CPack reads it.
- **Impact**: The produced artifact can contain bytes different from the hash/existence checks that authorized it.
- **Required fix**: Stage immutable verified copies and package those, or hash/verify the final archive/installer inputs
  immediately before emission.

### VW-054 — Uninstaller removes an unowned English model

- **Priority**: P2
- **Status**: Fixed in source — uninstaller removes only the model shipped by this installer
- **Affected**: `cmake/vw_installer.nsi.in:456-460`
- **Trigger**: A user or older installation has manually placed `models\\ggml-tiny.en.bin` under the VLC directory.
- **Impact**: Uninstall deletes a model this installer never installed and can destroy user data.
- **Required fix**: Delete only files recorded as installer-owned, or leave pre-existing model files untouched.

### VW-055 — Oversized source URLs are silently truncated

- **Priority**: P2
- **Status**: Fixed and tested — oversized URLs fail before START is sent
- **Affected**: `plugin/src/vw_worker_client.c:279-286`
- **Trigger**: A local source URL reaches the fixed protocol field limit.
- **Impact**: The client logs a warning but starts with a truncated URL, which may resolve to a different path or silently
  force live fallback.
- **Required fix**: Reject the session before sending `START` when the URL does not fit.

### VW-056 — Release tests compile assertions and test actions out with `NDEBUG`

- **Priority**: P1
- **Status**: Fixed in source — project tests undefine `NDEBUG` on supported GNU/Clang release builds
- **Affected**: `tests/unit/*.c`, `tests/integration/*.c`, release CMake presets
- **Trigger**: Build the Windows CPU/GPU release presets, which define `NDEBUG` while `BUILD_TESTING=ON`.
- **Impact**: Tests use side-effectful `assert(call())`; Release removes the calls, so the suite can report false passes
  and the seek-epoch test reaches `pthread_join()` with an uninitialized thread. The MinGW release build emitted unused
  variables/functions for exactly this reason.
- **Required fix**: Use an always-on test assertion macro (or compile tests with `-UNDEBUG`) and run the release test
  binaries in an appropriate Windows environment.

### VW-057 — Current PR still fails format/diff hygiene checks

- **Priority**: P3
- **Status**: Fixed and verified — clang-format and git diff whitespace gates pass
- **Affected**: `tests/unit/test_translate_async.c:157-158`, `tests/unit/test_worker_client_seek_epoch.c:59,150-152`,
  `docs/decisions.md:490`, `samples/snippets/script.py:185`
- **Trigger**: Run `clang-format --dry-run --Werror` over changed C/H files or `git diff --check origin/main...HEAD`.
- **Impact**: The required verification gates fail despite the previous ledger marking VW-033 fixed.
- **Required fix**: Format the reported test files and remove the trailing whitespace/extra EOF blank line.

### VW-058 — Follow-up documentation metadata is stale

- **Priority**: P3
- **Status**: Fixed — this ledger records the 26-test suite and current remediation evidence
- **Affected**: `docs/issues.md:1-4,456-462`
- **Trigger**: Read the issue ledger on PR head `b0117ee`.
- **Impact**: The header still identifies the old `b59214a` audit, and the summary says a 25-test suite passed while the
  current suite has 26 tests. This can mislead release sign-off and makes the status claims hard to audit.
- **Required fix**: Record the current commit, test count, exact memcheck result, and open follow-up findings.

## Documentation and Release-Gate Drift

### VW-032 — Release documentation marks unimplemented or stale behavior complete

- **Priority**: P2
- **Status**: Fixed in source — release documentation reconciled; clean Windows VM/manual acceptance remains pending
- **Affected**: `docs/roadmap.md:51-59,82-95`, `docs/architecture.md:75-77,94,208`,
  `lua/README_SETTINGS.md:1-5,122-127`, `docs/issues.md` before this audit
- **Examples**:
  - Roadmap step 17 says live seeks STOP and START a new session, but current code sends only POSITION.
  - Architecture requires random pipe names, but current endpoints are PID-derived.
  - Roadmap still names a `0.3.0` installer and automatic `vlc-cache-gen`; the branch is `0.1.0` and installer code does
    not run the cache generator.
  - `lua/README_SETTINGS.md` says translation does not exist even though translation is implemented.
  - The former Issue #1 claimed Linux ancestor probing began at `up = 1`; current code probes from the plugin path and
    the record was stale.
  - The clean release build, smoke test, and CI checklist entries are marked complete without current-commit evidence
    in this audit.
- **Required fix**: Reconcile documentation with code and attach current clean-build/VM evidence before tagging.

### VW-033 — Branch fails `git diff --check`

- **Priority**: P3
- **Status**: Fixed — `git diff --check` clean per parent observation
- **Affected**: `assets/vlc-whisper-logo.svg`, `diff.md`, `docs/decisions.md`,
  `docs/plans/subtitle_quality_enhancements_plan.md`, `samples/snippets/script.py`
- **Impact**: The branch contains trailing whitespace or extra blank lines at EOF and does not pass basic diff hygiene.
- **Required fix**: Remove the reported whitespace defects before release.

### VW-034 — Valgrind gate exits successfully while reporting memory defects

- **Priority**: P2
- **Status**: Fixed — CPU-forced Valgrind memcheck and strict defect gate pass on the current tree; Windows runtime validation remains pending
- **Affected**: `CMakeLists.txt:45`, `tests/integration/test_worker_ipc.c`,
  `tests/integration/test_worker_lifecycle.c`, `docs/test-strategy.md:52,113`
- **Trigger**: Run `ctest --test-dir build/linux-x64-debug -T memcheck` against the Vulkan-enabled debug preset.
- **Impact**: CTest reports all tests passed and exits zero while its memory-check summary reports 2 memory leaks and
  6,071 potential memory leaks. Each worker integration test reports 448 definitely lost bytes and 13,728 possibly lost
  bytes; the inspected stacks concentrate in Vulkan loader/LLVM initialization. The release gate can therefore appear
  green despite reported defects, contradicting the documented clean-memcheck requirement.
- **Required fix**: Triage the reports, run worker memcheck with the CPU backend when appropriate, add narrowly justified
  third-party suppressions, and make the gate fail on remaining unsuppressed project-owned defects.
- **Missing coverage**: A CI assertion that parses the CTest memcheck defect summary rather than trusting only exit status.

## Audit Boundaries and Remaining Validation

- No additional undisclosed project-source `TODO`, `FIXME`, placeholder, or unimplemented MVP stub was found. The
  documented Whisper beam-search patience limitation is not treated as an MVP blocker.
- Media Foundation timestamp-origin behavior remains uncertain: the Windows decoder uses raw sample timestamps while
  FFmpeg subtracts the stream start offset. Test a Windows fixture with a nonzero presentation start.
- Signed timeline arithmetic should receive sanitizer and extreme-PTS coverage even though ordinary protocol position
  bounds keep normal inputs in range.
- The dependency graph was generated from commit `13aeea64`, not the remediation branch; every finding was checked
  against current source rather than accepted from the stale graph.
- Source fixes for VW-001 through VW-058 are integrated. The native debug configure/build and complete 26-test CTest
  suite pass. Valgrind executes 25 tests successfully, skips only the model-heavy Whisper engine test by documented
  policy, reports no defects, and the strict gate confirms all 26 MemoryChecker logs are clean.
- Windows CPU and GPU release cross-builds pass. CPU and GPU NSIS installers pass with staged input/hash validation;
  direct GPU CPack emits a ZIP with its CPU fallback dependency; a CPU-only installer also builds from a path containing
  spaces. A clean Windows VM installer/VLC run, Media Foundation fixture validation, Unicode-profile smoke test, and
  Windows timeout/cancellation stress test remain native acceptance limitations rather than source-known defects.
