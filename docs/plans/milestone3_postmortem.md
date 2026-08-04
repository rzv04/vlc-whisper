# Implementation Task Template & Postmortem

# Task: Postmortem & Technical Handoff Blueprint for Milestone 3 Feature Branches

## Goal
Comprehensive postmortem evaluation of three feature branches (`gemini/milestone-3-steps-14-15`, `gemini/gpu-directml`, and `gemini/transcription-lookahead`), recording all step-by-step changes, in-scope/out-of-scope additions, architectural evolutions, critical bugs encountered, and establishing a clear, phased re-implementation blueprint for future development on `gemini/milestone-3`.

## Context
- **Branches Analyzed**:
  - `gemini/milestone-3-steps-14-15`: Real-time PCM IPC streaming and initial caption segment rendering.
  - `gemini/gpu-directml`: Vulkan/GPU whisper acceleration backend and transcription quality tuning.
  - `gemini/transcription-lookahead`: Look-ahead source file decoding, SPU subpicture rendering, and seek re-sync engine.
- **Relevant Docs/ADRs**: ADR-006, ADR-013, ADR-015, `docs/architecture.md`, `docs/api-contracts.md`, `docs/whisper-api.md`.
- **Target OS/Builds**: Linux (GCC/Clang, POSIX named sockets) & Windows (MinGW x64, Win32 Named Pipes, Media Foundation).

---

## Executive Summary & Root Cause of Reset

During development following the Milestone 3 handshake baseline (`f211d84`), three major feature initiatives were implemented in rapid succession:
1. Steps 14 & 15 (Real-time PCM IPC streaming and caption presentation).
2. GPU Acceleration via Vulkan/ggml-vulkan and beam search quality passes.
3. Look-Ahead Source Decoding (FFmpeg/MediaFoundation worker demuxer) combined with SPU Subpicture channel rendering and seek re-synchronization.

While these features provided valuable functional proof-of-concept (e.g., look-ahead transcription capability and GPU acceleration), the pacing was too aggressive. Multiple complex architectural shifts were combined in single commits without sufficient step-by-step isolation. This led to subtle blocking bugs, hidden regressions (e.g., invisible subtitles due to empty subpictures, Windows weak-attribute symbol resolution failures, process lifecycle races during seeking), and an opaque codebase.

To restore total codebase transparency, maintainability, and standard-compliant stability, the local feature branches (`gemini/milestone-3-steps-14-15`, `gemini/gpu-directml`, `gemini/transcription-lookahead`) are being archived/removed, returning to the clean baseline on `gemini/milestone-3`. This postmortem serves as the authoritative handoff document and blueprint for re-implementing these features at a controlled, maintainable rhythm.

---

## Detailed Branch Analysis & Step-by-Step Breakdown

### 1. Branch: `gemini/milestone-3-steps-14-15` (Real-Time PCM Streaming & Presentation)

#### Step-by-Step Summary of Changes
- **Step 14 (IPC Audio Streaming)**: Integrated `vw_audio_capture` in the VLC filter callback to enqueue resampled 16 kHz Mono S16LE PCM chunks into a bounded SPSC queue. Created a background sender thread in the plugin to drain the queue and send `VW_MSG_AUDIO_PCM` frames across the IPC transport.
- **Step 15 (Worker Inference & Caption Presentation)**: Implemented worker inference loop using `whisper.cpp`, VAD speech detection, and 4 s window / 2 s hop windowing geometry. Created segment builder (`vw_segment_builder`) to package transcribed text into `VW_MSG_CAPTION_SEGMENT` frames. Added receiver thread in plugin to parse incoming captions and invoke `vout_OSDText`.

#### In-Scope vs. Out-of-Scope Changes
- **In-Scope**:
  - `VW_MSG_AUDIO_PCM` and `VW_MSG_CAPTION_SEGMENT` wire framing and validation.
  - Basic 4 s analysis window and 2 s hop inference.
  - VLC OSD display via `vout_OSDText`.
- **Out-of-Scope / Unplanned Refactors**:
  - **Shared SPSC Queue**: Moved `vw_spsc_queue` and `vw_audio_chunk_t` from `plugin/` to `protocol/` so worker and plugin share the implementation.
  - **Worker IPC Reader Thread (`ADR-013`)**: Decoupled IPC frame receiving from worker main loop using a dedicated reader thread and SPSC queue, preventing inference latency from stalling pipe draining.
  - **Win32 Platform Fixes**: Canonical named pipe paths (`\\\\.\\pipe\\vlc-whisper-PID`), CRT thread wrappers (`vw_thread.c`), BCryptGenRandom NTSTATUS checks.
  - **Window Geometry Macro Fix**: Corrected 32-bit arithmetic overflow in `VW_WINDOW_SAMPLES` where 8s windows overflowed to 0.2s.
  - **OSD String Lifetime Bug**: Fixed dangling pointer crash where segment UTF-8 text pointed to dead stack memory on the receiver thread.

---

### 2. Branch: `gemini/gpu-directml` (GPU Acceleration & Quality Pass)

#### Step-by-Step Summary of Changes
- **GPU Backend Integration**: Integrated `ggml-vulkan` backend into `whisper.cpp` CMake build (`VW_WITH_VULKAN`). Added CLI flags `--backend auto|gpu|cpu` and `--gpu-device <id>` to `vlc-whisper-worker`.
- **Automatic Fallback**: Implemented automatic CPU fallback when GPU initialization or Vulkan device enumeration fails.
- **Quality Tuning Pass**: Configured beam search decoding parameters, cross-window context preservation, and anti-aliasing audio resampler filtering.

#### In-Scope vs. Out-of-Scope Changes
- **In-Scope**:
  - Vulkan GPU acceleration for `whisper.cpp`.
  - CPU backend fallback.
- **Out-of-Scope**:
  - **Build Resource Memory Spikes**: Discovered that compiling Vulkan shaders (`glslc`) creates significant host RAM/swap pressure during parallel builds (`ninja -j`), requiring documentation of build OOM flags.
  - **Quality Pass Bundling**: Combined inference quality parameters (beam search, cross-window context) directly into the GPU branch instead of keeping backend acceleration isolated.

---

### 3. Branch: `gemini/transcription-lookahead` (Look-Ahead Source Decoding & SPU Subsystem)

#### Step-by-Step Summary of Changes
- **Phase 0 (Spikes)**: Implemented SPU Subpicture rendering spike (S1) and `vw_timeline` state machine spike (S2).
- **Phase 1 (Protocol 1.1 & Source Decoder)**: Extended protocol to v1.1 (`VW_CAPABILITY_SOURCE_MODE`, `source_url`, `timeline_origin_pts_us`, `POSITION` messages). Implemented `vw_source_decode.c` using FFmpeg (Linux) and Media Foundation (Windows) to decode media files ahead-of-time in the worker process.
- **Phase 2 (Seek Re-Sync Engine)**: Implemented input clock jump detection (`VW_INPUT_JUMP_DISCONTINUITY_US = 5s`), epoch restarts, SPU flushing, and session ID updates on seeking.

#### Critical Bugs & Technical Failures Encountered
1. **The Empty SPU Subpicture Bug (`subpicture_New(NULL)`)**:
   - *Symptom*: Subtitles stopped displaying entirely; worker GPU usage was 50%, captions were sent over IPC, but screen remained blank.
   - *Cause*: `subpicture_New(NULL)` created a subpicture with `p_region = NULL` and no updater callback. VLC's vout had no graphic or text regions to draw.
   - *Fix*: Replaced with `subpicture_region_New(VLC_CODEC_TEXT)` and `text_segment_New(text_utf8)`.
2. **Windows MinGW Weak Symbol Resolution Failure**:
   - *Symptom*: SPU channel registration (`vout_RegisterSubpictureChannel`) returned NULL pointer on Windows.
   - *Cause*: `__attribute__((weak))` on MinGW DLLs resolved external VLC symbols to NULL at runtime.
   - *Fix*: Introduced `VW_WEAK` macro (weak on Linux, standard `extern` on Windows) and added explicit exports in `plugin/libvlccore.def`.
3. **Session ID Zero-Stamping Bug**:
   - *Symptom*: Worker generated segments but plugin rejected all of them as `PLUGIN_SEGMENT_REJECTED`.
   - *Cause*: Worker failed to copy `session_id` to outbound `vw_caption_segment_t`, causing session ID mismatch checks in plugin to fail.
4. **Model Reload Latency (`ADR-015`)**:
   - *Symptom*: Rapid seeks killed worker sessions before captions could start.
   - *Cause*: Worker reloaded the 74 MB model on every `START_SESSION` message (taking 1-2 seconds per seek).
   - *Fix*: Adopted "Model-Once" worker architecture where the model is loaded once at worker start and reused across seek epochs.
5. **Media Foundation Seek & Lifecycle Races**:
   - *Symptom*: Worker crashed or stopped decoding immediately after a seek event.
   - *Cause*: `is_seeking` flag was not set on epoch restart; `worker_session_free` was not called between STOP and START; per-thread `MFStartup`/`MFShutdown` calls raced with thread joins.

---

## Architectural Changes & State Transitions Overview

```text
Baseline (Milestone 3 Baseline)
   │
   ├─► Step 14/15: Real-time Audio Streaming (Plugin Capture -> Pipe -> Worker CPU -> OSD)
   │
   ├─► GPU Branch: Vulkan Acceleration Backend (whisper.cpp Vulkan shaders + CPU fallback)
   │
   └─► Look-Ahead Branch: Ahead-of-Time Source Decoding (Worker FFmpeg/MF Demux -> SPU Text Regions)
```

### Key Technical Patterns Discovered
1. **SPU Text Region Construction**:
   - Native VLC subtitle decoders construct subpictures by allocating a text region:
     ```c
     video_format_t fmt;
     memset(&fmt, 0, sizeof(fmt));
     fmt.i_chroma = VLC_CODEC_TEXT;
     subpicture_region_t* region = subpicture_region_New(&fmt);
     region->p_text = text_segment_New(text_utf8);
     region->i_align = SUBPICTURE_ALIGN_BOTTOM;
     subpic->p_region = region;
     ```
2. **System vs. Media Timeline Mapping**:
   - SPU subpictures are rendered against VLC's `SYSTEM` date domain (`mdate()`). Media timeline PTS (`int64_t pts_us`) must be mapped using live system-to-media offsets (`offset_us = mdate() - input_time`) to prevent captions from being rejected as "late".
3. **Process-Wide Media Framework Initialization**:
   - Windows Media Foundation (`MFStartup`/`MFShutdown`) must be initialized once per worker process lifetime in `vw_worker_run`, never inside short-lived worker threads.

---

## Phased Re-Implementation Roadmap & Blueprint

To implement these feature sets cleanly and verifiably on `gemini/milestone-3`, future work MUST follow this 4-phase sequence. Each phase MUST be implemented on its own dedicated branch, verified against the definition of done, and merged before starting the next.

```text
[Milestone 3 Baseline]
          │
          ▼
┌────────────────────────────────────────────────────────┐
│ Phase A: Real-Time Audio Streaming & OSD (Steps 14-15) │
│ - Pure real-time PCM streaming via IPC                 │
│ - 4s window / 2s hop whisper.cpp inference             │
│ - vout_OSDText timed caption presentation              │
└─────────────────────────┬──────────────────────────────┘
                          │
                          ▼
┌────────────────────────────────────────────────────────┐
│ Phase B: GPU Acceleration Backend                      │
│ - ggml-vulkan CMake integration                        │
│ - CLI --backend auto|gpu|cpu & --gpu-device            │
│ - Isolated CPU fallback & build documentation          │
└─────────────────────────┬──────────────────────────────┘
                          │
                          ▼
┌────────────────────────────────────────────────────────┐
│ Phase C: SPU Subpicture Subsystem                      │
│ - SPU channel registration & vout_PutSubpicture        │
│ - Proper subpicture_region_New(VLC_CODEC_TEXT) setup   │
│ - System-to-media date domain conversion               │
└─────────────────────────┬──────────────────────────────┘
                          │
                          ▼
┌────────────────────────────────────────────────────────┐
│ Phase D: Look-Ahead Source Decoding & Seek Engine     │
│ - Protocol 1.1 source_url & POSITION messages          │
│ - Worker FFmpeg / MF source demuxer                    │
│ - Model-once process lifetime (ADR-015)                │
│ - Input-clock jump seek re-sync & SPU flushing         │
└─────────────────────────┬──────────────────────────────┘
```

### Phase A Details: Real-Time PCM IPC & OSD Presentation
- **Goal**: Implement baseline real-time audio streaming from VLC audio filter callback to worker, performing CPU inference and displaying captions via OSD (`vout_OSDText`).
- **Key Constraints**:
  - Keep `vw_spsc_queue` thread-safe and non-blocking in VLC audio callback.
  - Implement worker reader thread (`ADR-013`) for IPC pipe draining.
  - Use `vout_OSDText` only; do not introduce SPU channels yet.

### Phase B Details: GPU Acceleration Backend
- **Goal**: Add Vulkan GPU acceleration to `whisper.cpp` worker build without changing plugin IPC or rendering contracts.
- **Key Constraints**:
  - Validate clean CPU fallback when Vulkan is unavailable.
  - Document parallel build memory limits (`glslc`).

### Phase C Details: SPU Subpicture Subsystem
- **Goal**: Replace OSD rendering with native SPU subpicture channel rendering for exact subtitle timing and positioning.
- **Key Constraints**:
  - Always allocate `subpicture_region_New(VLC_CODEC_TEXT)` and `text_segment_New()`.
  - Use `VW_WEAK` macro for Windows compatibility.
  - Perform system-to-media timestamp translation (`mdate() - input_time`).

### Phase D Details: Look-Ahead Source Decoding & Seek Engine
- **Goal**: Enable worker ahead-of-time source file demuxing (FFmpeg/MF) and seek re-synchronization.
- **Key Constraints**:
  - Enforce Model-Once worker engine lifetime (`ADR-015`).
  - Set `is_seeking = true` explicitly when `timeline_origin_pts_us > 0`.
  - Manage Media Foundation process-wide in `vw_worker_run`.
  - Re-anchor seek detector and media-system offset upon input clock jump.

---

## Acceptance Criteria & Definition of Done Checklist

- [x] Comprehensive postmortem report generated in `docs/plans/milestone3_postmortem.md`.
- [x] Architectural changes, root causes of failures, and technical handoff details fully documented.
- [x] Phased 4-step re-implementation blueprint defined.
- [x] Code formatting verification (`clang-format --dry-run --Werror`) clean on target files.
- [x] Native build and unit test suite (`cmake --preset linux-x64-debug && cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug`) 100% passing.
- [x] Valgrind memory leak verification (`ctest --test-dir build/linux-x64-debug -T memcheck`) 100% clean.

---

## Evidence & Verification Results
- **Postmortem Report File**: `docs/plans/milestone3_postmortem.md`
- **Native Test Suite**: 13/13 tests passing on `gemini/milestone-3` baseline.
- **Memcheck Output**: 0 memory leaks, 100% tests passed under Valgrind.
