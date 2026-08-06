# Implementation Task Plan: Step 14A (Worker Audio Pipeline)

# Task: Implement Worker Audio Ingestion Pipeline, Whisper Engine, and Segment Emission

## Goal

Implement the worker's internal audio ingestion pipeline (`vw_whisper_engine`, `vw_audio_buffer`, `vw_worker` session pipeline, `vw_segment_builder_pop`, and platform thread abstractions), allowing `vlc-whisper-worker` to consume IPC `AUDIO` frames, buffer float32 audio, execute VAD speech detection and `whisper.cpp` inference, and emit timed `CAPTION_SEGMENT` frames over IPC.

## Context

- **Relevant Docs/ADRs**:
  - `docs/architecture.md` (Worker IPC reader thread, 8s analysis window, 2s hop)
  - `docs/decisions.md` (ADR-008: Bounded loss backpressure, ADR-013: Decoupled reader thread, ADR-015: Model-once engine lifetime)
  - `docs/api-contracts.md` (IPC frames: `START`, `STARTED`, `AUDIO`, `SEGMENT`, `STATUS`, `ERROR`)
  - `docs/plans/milestone3_postmortem.md` (Phase A: Real-Time Audio Streaming)
  - `docs/plans/step-14-realtime-pcm-streaming.md` (Execution order for 14a)
- **Target OS/Builds**: Linux (GCC/Clang) & Windows (MinGW x64).

## Scope

- **In scope**:
  1. Platform thread & sleep abstractions in `plugin/include/vw_platform.h`, `plugin/src/vw_platform_linux.c`, `plugin/src/vw_platform_win32.c` (`vw_platform_thread_create`, `vw_platform_thread_join`, `vw_platform_sleep_ms`).
  2. Real float32 audio ring buffer implementation in `worker/src/vw_audio_buffer.c` (`vw_audio_buffer_append_s16le`, count/samples accessors, drop-oldest overflow policy).
  3. Segment pop accessor in `worker/src/vw_segment_builder.c` (`vw_segment_builder_pop`).
  4. Real `whisper.cpp` engine initialization and inference in `worker/src/vw_whisper_engine.c` (`vw_whisper_engine_init` with single-model load per process, `vw_whisper_engine_transcribe_pcm`, `vw_whisper_engine_get_text`).
  5. Worker session pipeline in `worker/src/vw_worker.c`:
     - Load model on startup; reply with `ERROR` (`E_MODEL_MISSING` / `E_MODEL_INVALID`, recoverable=0) if model is missing or corrupt.
     - Session state tracking (`START` -> `STARTED` reply with session_id validation).
     - Ingest `AUDIO` frames, append to ring buffer, run energy VAD check (`vw_vad_detect_speech_energy`), run inference on 8s windows (2s hop), push hypotheses to segment builder, and emit outbound `CAPTION_SEGMENT` frames over IPC.
  6. Unit and integration test suite updates (`test_audio_buffer`, `test_segment_builder`, `test_whisper_engine`, `test_worker_lifecycle`).
- **Out of scope**:
  - Worker decoupled IPC reader thread + inference worker thread split (`ADR-013`) — deferred to Step 14B; 14a runs a single-threaded session loop.
  - Plugin background sender thread (Step 14B).
  - Presenter OSD / SPU display wiring (Step 15).
  - Play/Pause IPC lifecycle (Step 16).

## Design

- **Inputs and Outputs**:
  - Input: `VW_MSG_START` and `VW_MSG_AUDIO_PCM` frames received over IPC transport.
  - Output: `VW_MSG_STARTED`, `VW_MSG_CAPTION_SEGMENT`, `VW_MSG_STATUS`, or `VW_MSG_ERROR` frames sent back to plugin client.
- **Ownership/Threading Model**:
  - Step 14A runs a single-threaded session loop (read + infer + emit on one thread).
  - The ADR-013 decoupled IPC reader thread + inference worker thread split is deferred to Step 14B.
  - Engine instance owned by worker process (`ADR-015`), initialized once at startup.
- **Bounds and Failure Behavior**:
  - Buffer bounded to 10 seconds of audio (160,000 samples). Drop-oldest policy on overflow.
  - Missing model file returns `E_MODEL_MISSING` error frame without crashing process.
- **Privacy/Security**:
  - Offline processing only; zero transcript or PCM disk persistence.

## Acceptance Criteria

- [ ] Platform thread helpers (`vw_platform_thread_create`, `vw_platform_thread_join`, `vw_platform_sleep_ms`) implemented and unit tested.
- [ ] `vw_audio_buffer` converts S16LE PCM to normalized float32, tracks PTS, and handles ring buffer overflow correctly.
- [ ] `vw_segment_builder_pop` pops oldest caption segment and transfers text ownership cleanly.
- [ ] `vw_whisper_engine` initializes model once per process (`ADR-015`), runs inference on 16kHz PCM arrays, and extracts transcribed UTF-8 text.
- [ ] `vw_worker` handles `START` -> `STARTED`, validates `session_id`, ingests `AUDIO` frames, executes 8s windowing with 2s hop, and emits `CAPTION_SEGMENT` frames (single-threaded loop; ADR-013 thread split lands in Step 14B).
- [ ] Unit and integration test suite passing 100% (with model-dependent tests returning skip code 77 when `models/ggml-tiny.en.bin` is absent).
- [ ] Formatting (`clang-format`), native build & test suite, and Valgrind memcheck passing 100% clean.

## Definition of Done

- [ ] C17 code compliant with project rules and `vw_` symbol namespacing.
- [ ] All verification checks pass.
- [ ] Code modifications remain **unstaged and uncommitted** per user instruction.

> Note: "No blocking calls on the IPC transport read thread" is intentionally NOT a 14a DoD item — it requires the ADR-013 thread split, which is deferred to Step 14B.
