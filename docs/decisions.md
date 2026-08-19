# Architecture Decisions

## ADR-001: External local worker

**Status:** Accepted.

Use a separate worker executable for inference. This contains whisper.cpp and model failures outside VLC, permits independent worker tests, and makes GPU backends later packaging variants rather than plugin dependencies. whisper.cpp supports a C-style API, Windows and Linux, CPU-only inference, VAD, and several optional accelerators, so the worker can remain a C-authored host over a pinned dependency. [page:0]

Consequence: define and test IPC now. The plugin must tolerate worker absence/crash without affecting media playback.

## ADR-002: C17 authored code

**Status:** Accepted.

All VLC integration, IPC, worker host, tests, and tooling authored here use C17. whisper.cpp remains third-party C/C++; link the worker with the appropriate C++ linker/runtime while calling only its C header surface. VLC itself is mainly developed in C, which aligns with this constraint. [page:1]

## ADR-003: Pin VLC build

**Status:** Accepted.

Support one exact Windows VLC 3.x distribution/build at a time. Native modules must be built/tested with matching headers, libraries, compiler/runtime conventions, and module conventions. VLC's developer site explicitly says the project evolves quickly and directs developers to source and current wiki material. [page:2]

Consequence: release manifests and CI artifacts include VLC version/commit and ABI assumptions. Compatibility with VLC 4 is a separate port, not an upgrade checkbox.

## ADR-004: Offline-only local IPC

**Status:** Accepted.

Use authenticated, current-user-only named pipes on Windows and Unix-domain `SOCK_SEQPACKET` on Linux. No localhost TCP, WebSocket, HTTP server, cloud fallback, telemetry, or auto-download. This meets the privacy claim even when a firewall or another local process is misconfigured.

## ADR-005: Final-only captions first

**Status:** Accepted.

MVP renders final segments only. This reduces flicker and avoids requiring subtitle replacement semantics before the VLC presentation spike is proven. Keep segment IDs and reserved `replace` capability so later partial hypotheses can revise a rolling caption area.

## ADR-006: Seeking and Play/Pause lifecycle in MVP

**Status:** Accepted (Updated).

Seeking and Play/Pause lifecycle are IN-SCOPE for the MVP (Milestone 3):
1. **Play/Pause**: When VLC pauses playback, the plugin sends a `PAUSE` control frame over IPC and suspends audio forwarding. Resuming sends `RESUME` and resumes timeline PTS sync.
2. **Seeking / Discontinuity**: When VLC seeks (`BLOCK_FLAG_DISCONTINUITY` or non-monotonic PTS jump), the plugin clears active presenter captions, sends a `STOP` (`SEEK_DISCONTINUITY`) control frame over IPC, flushes the SPSC queue & VAD state, and initializes a new session epoch (`timeline_origin_pts_us`) seamlessly without disabling captions or interrupting VLC media playback.

## ADR-007: Model policy

**Status:** Accepted.

Ship/support only local `tiny.en` CPU for MVP. Expose a model manifest abstraction (`models/manifest.json`): ID, file name, language scope, model SHA-256 hash, disk size, and RAM estimate. Do not expose model dropdowns until install, validation, benchmarking, and failure UX exist.

### Manifest Verification Sequence & Runtime Policy

1. **Manifest Parsing**: Upon worker launch or receiving a `START` frame, `vlc-whisper-worker` reads `models/manifest.json` and matches the requested `model_id` (`tiny.en`).
2. **SHA-256 Verification**: Worker computes the SHA-256 hash of `models/ggml-tiny.en.bin` prior to passing the path to `whisper_init_from_file_with_params()`.
3. **Pre-allocation Memory Check**: Compare `ram_bytes_estimate` against available system RAM to prevent OOM panics in the process tree.
4. **Error Behavior**: If the manifest or binary model is missing or corrupt (SHA-256 mismatch), the worker emits `E_MODEL_MISSING` or `E_MODEL_INVALID` to the plugin. The caption session disables gracefully while VLC media playback continues uninterrupted.

## ADR-008: Bounded loss over playback impact

**Status:** Accepted.

When inference cannot keep up, drop new unprocessed audio and make this measurable. Never block VLC's audio path or slow playback. This produces caption gaps under load but preserves the media player's core responsibility.

## ADR-009: No database

**Status:** Accepted.

MVP persists no audio, transcript, playback history, or database. A future GUI may store settings in an OS-appropriate per-user configuration file with schema/version migration; it must not create a hidden transcript archive by default.

## ADR-010: Build strategy

**Status:** Accepted.

Use CMake presets/toolchain files and cross-compile Windows x64 worker artifacts from Ubuntu. All MinGW runtime dependencies (`libgcc`, `libstdc++`, `libgomp`, `libwinpthread`) are statically linked into target binaries (`vlc-whisper-worker.exe`, sample binaries) to ensure output executables are fully self-contained and run on Windows without missing DLL errors. The VLC native-module build is a risk-managed exception: first prove whether exact SDK/out-of-tree compilation is sufficient; otherwise maintain a small pinned VLC source patch/in-tree module build. A clean out-of-tree experience is desirable, but not allowed to overrule reliability.

## ADR-011: Standalone settings GUI binary

**Status:** Accepted.

The post-MVP settings and model-management GUI (`vlc-whisper-settings.exe`) will be authored and packaged as a standalone executable rather than embedded inside the VLC plugin DLL (`libvlc_whisper_plugin.dll`).

Consequences:

1. **Crash & Thread Isolation**: Keeps UI event loop execution out of VLC's process space, preventing UI freezes or thread deadlocks with VLC's main Qt window thread.
2. **Independent Execution**: Allows end-users to launch settings and pre-download models directly from the Start Menu without opening VLC or playing media first.
3. **VLC Menu Invocation**: The VLC plugin DLL registers a lightweight menu item under `Tools -> VLC-Whisper Settings...` which invokes `vlc-whisper-settings.exe` out-of-process (`CreateProcess()` / `exec()`).

## ADR-012: Out-of-tree packaging over custom VLC build

**Status:** Accepted.

The VLC-Whisper plugin will be shipped as an external out-of-tree plugin with a standalone installer, rather than distributing a custom build of VLC.

Consequences:
1. **Compatibility**: We must strictly target the ABI of a pinned VLC release (e.g. 3.x series) to ensure the DLL loads correctly in user installations.
2. **Installation**: The installer will locate the user's existing VLC installation directory and copy `libvlc_whisper_plugin.dll` into the `plugins/` subdirectory.
3. **No Engine Forks**: We cannot modify VLC core player behavior or patch VLC itself to accommodate our subtitle or audio timing needs. We must use public VLC APIs and handle synchronization carefully within the plugin boundaries.

## ADR-013: Decoupled Worker IPC Reader Thread

**Status:** Accepted.

The worker process receives incoming `VW_MSG_AUDIO_PCM` frames across the IPC transport. Running `whisper_full()` inference directly on the worker main/reader loop introduces 200–500 ms of blocking delay per window. If the pipe is not drained continuously, backpressure travels back to the plugin's sender thread.

Consequences:
1. **Thread Decoupling**: Decouple IPC frame receiving from worker main loop using a dedicated worker IPC reader thread (`vw_worker_reader_main`) and a bounded SPSC queue (`vw_spsc_queue_t`).
2. **Pipe Safety**: The reader thread drains named pipe / socket frames continuously, while the worker main loop pops PCM chunks and executes inference asynchronously.

## ADR-014: Process-Wide Platform Media Framework Management

**Status:** Accepted.

Windows Media Foundation (`MFStartup`/`MFShutdown`) was originally called inside per-seek source decode threads. Rapid seeks caused race conditions between `MFShutdown` in an old thread and `MFStartup` in a newly spawned thread.

Consequences:
1. **Process Lifetime**: Manage platform media frameworks process-wide in `vw_worker_run` (`vw_source_decode_platform_init` and `vw_source_decode_platform_shutdown`).
2. **Thread Safety**: `MFStartup` is called once at worker startup, and `MFShutdown` is called once at worker exit. Short-lived demux threads must not alter global COM refcounts.

## ADR-015: Model-Once Worker Lifetime Across Seek Epochs

**Status:** Accepted.

Loading the ~74 MB `ggml-tiny.en.bin` model on every `START_SESSION` message required 1–2 seconds per seek. Rapid seeks resulted in continuous model reloads, killing new session epochs before captions could start.

Consequences:
1. **Single Model Load**: Worker process loads the whisper engine once at startup (`shared->engine`) and reuses it across all seek epochs (`START_SESSION`).
2. **Resource Ownership**: The engine is owned by the worker process and released only at worker process exit. Seeks reset audio buffers and builders without reloading model parameters from disk.

## ADR-016: Native VLC SPU Subpicture Pipeline for Timed Captions

**Status:** Accepted.

In look-ahead transcription mode, the worker generates caption segments ahead of VLC's current playback position. Rather than building a custom caption queue and timing thread inside the plugin, the plugin delegates subpicture queuing and PTS-based display scheduling entirely to VLC's native SPU (Subpicture Subsystem) pipeline.

Consequences:
1. **No Plugin Caption Queue**: The plugin does not maintain an internal queue or timer loop for future captions.
2. **Native Scheduling**: Transcribed segments received from IPC are converted directly to native VLC subpictures (`vout_RegisterSubpictureChannel` + `vout_PutSubpicture`) carrying exact `i_start` and `i_stop` media PTS timestamps. VLC handles precise frame-accurate rendering automatically.
3. **Flushing on Discontinuity**: On seeking or rate changes, the plugin issues `vout_FlushSubpictureChannel` / `spu_ClearChannel` to purge all pre-rendered look-ahead captions from VLC's queue.

## ADR-017: Phrase-by-Phrase Subtitle Timing via Native Whisper Segment Offsets

**Status:** Accepted.

In earlier milestones, all sub-segments emitted by Whisper across an 8.0-second acoustic analysis window were concatenated into a single combined string stamped with the full 8-second window duration (`[window_pts, window_pts + 8.0s]`). In live media playback, this coarse aggregation caused upcoming dialogue (such as the second speaker's reply in a conversational exchange) to appear 3–5 seconds before it was spoken, spoiling dramatic and conversational pacing while overcrowding the screen.

Decision:
1. **Per-Phrase Timestamp Extraction**: The worker extracts individual sub-segments from `whisper_full` using `whisper_full_n_segments(ctx)` and their exact centisecond offsets via `whisper_full_get_segment_t0(ctx, i)` and `whisper_full_get_segment_t1(ctx, i)`.
2. **Discrete Media PTS Bounds**: Each phrase is assigned discrete media timestamps ($\text{start\_pts} = \text{window\_pts} + t_0 \times 10\,000$, $\text{end\_pts} = \text{window\_pts} + t_1 \times 10\,000$) and pushed independently to `vw_segment_builder`.
3. **SPU Frame-Accurate Scheduling**: The plugin submits each phrase as a discrete subpicture to VLC's SPU engine. VLC displays and clears each phrase in exact synchrony with the speaker's vocal cadence, blanking during conversational pauses and eliminating dialogue spoilers (see [`docs/plans/phrase_timing_segmentation_plan.md`](file:///home/razvan/vlc-whisper/.worktrees/gemini/docs/plans/phrase_timing_segmentation_plan.md)).
