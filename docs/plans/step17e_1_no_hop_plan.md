# Implementation Task: VAD-Guided Non-Overlapping Audio Chunking (Strategy C)

# Task: Implement VAD-Guided Non-Overlapping Audio Chunking for Lookahead Transcription

## Goal
Eliminate redundant overlapping window inference (2s hop) in Lookahead (Source) Mode by implementing Silero VAD-guided non-overlapping audio chunking (Strategy C), partitioning audio at natural silence pauses (between 6s and 24s) to eliminate duplicate/stuttering subtitles, avoid mid-sentence word clipping, and reduce worker CPU/GPU compute by up to 75%.

---

## Context
- **Relevant Docs & ADRs**:
  - `docs/architecture.md` (Section 3: Audio Ingestion & Pipeline, Section 4: Worker Engine)
  - `docs/decisions.md` (ADR-013: SPU Subpicture Channel Presentation, ADR-017: Discrete Phrase Timestamp Extraction, ADR-018: Whole-Phrase Deduplication, ADR-019: Silero VAD Integration, and new **ADR-020: VAD-Guided Non-Overlapping Audio Chunking**)
  - `docs/api-contracts.md` (Lookahead Timeline Synchronization & SPU Cues)
- **VLC/Worker/Protocol Version**: v1.0 / Worker v1.0 (internal worker lookahead scheduling optimization; fully compatible with existing wire protocol).
- **Assumptions & Explicit Non-Goals**:
  - Live PCM mode (`source_mode == false`) retains low-latency sliding windows for real-time responsiveness when future audio is unavailable.
  - Lookahead Source Mode (`source_mode == true`) processes local media files ahead of playback and uses Strategy C exclusively.
  - Model selection / language switching remains in Milestone 4 Item 21a/22.

---

## Scope

### In Scope
1. **`worker/include/vw_vad.h` & `worker/src/vw_vad.c`**:
   - Implement `vw_vad_find_chunk_boundary` to evaluate speech intervals in a 16kHz audio buffer and determine either:
     - Leading / all-silence drain length (`out_silence_drain`), bypassing Whisper inference entirely.
     - Natural conversational pause cut point (`out_cut_samples`), bounded between $T_{min} = 6.0\text{s}$ ($96,000$ samples) and $T_{max} = 24.0\text{s}$ ($384,000$ samples) with $150\text{ms}$ acoustic padding ($2,400$ samples).
     - Forced split at $T_{max}$ for continuous speech / monologues.
     - EOF trailing audio processing.
     - Zero-config RMS Energy fallback mode when `vad_ctx == NULL`.
2. **`worker/src/vw_worker.c`**:
   - Enlarge `audio_buf` capacity from 10s ($160,000$ samples) to 60s ($960,000$ samples, ~3.84 MB) to allow looking ahead and detecting natural sentence boundaries.
   - Enlarge `window_samples` allocation from 8s ($128,000$ floats) to 30s ($480,000$ floats, ~1.92 MB, Whisper's native attention window).
   - In `vw_worker_run` lookahead loop, replace the fixed 2s sliding-hop with `vw_vad_find_chunk_boundary`:
     - Zero-inference silence drain for non-speech blocks.
     - Single-pass Whisper transcription for VAD-bounded speech chunks.
     - Non-overlapping drain (`vw_audio_buffer_drain(audio_buf, cut_samples)`).
3. **`tests/unit/test_vad.c` & Integration Tests**:
   - Unit tests verifying `vw_vad_find_chunk_boundary` for silence, multi-sentence speech (`jfk.wav`), continuous speech clamping, and EOF handling.
   - Integration tests in `test_worker_lifecycle.c` and `test_source_decoder.c` verifying lookahead pacing and subtitle delivery.
4. **Documentation**:
   - Author **ADR-020** in `docs/decisions.md`.
   - Update `docs/architecture.md`, `docs/source-layout.md`, `docs/api-contracts.md`, `docs/test-strategy.md`, `docs/roadmap.md`, `README.md`.

### Out of Scope
- Modifying VLC SPU subpicture channel rendering (`plugin/src/vw_caption_presenter.c`), which already handles discrete non-overlapping cues.
- Live microphone streaming protocol changes.

---

## Design

### 1. Chunk Bounds and Silence Detection Parameters
```c
#define VW_CHUNK_MIN_SAMPLES      96000   // 6.0s at 16kHz
#define VW_CHUNK_TARGET_SAMPLES   192000  // 12.0s at 16kHz
#define VW_CHUNK_MAX_SAMPLES      384000  // 24.0s at 16kHz
#define VW_CHUNK_PAD_SAMPLES      2400    // 150ms speech boundary padding
#define VW_CHUNK_MIN_SILENCE_GAP  4800    // 300ms silence interval for natural cut
#define VW_WHISPER_MAX_CHUNK_SAMPLES 480000 // 30.0s maximum Whisper window
#define VW_LOOKAHEAD_BUFFER_SAMPLES  960000 // 60.0s lookahead ring buffer
```

### 2. Algorithmic State Flow
```
Lookahead Loop:
  1. Ingest audio from source_decoder into audio_buf (up to lead_target_us or buffer capacity).
  2. If audio_buf.count >= VW_CHUNK_MIN_SAMPLES (6.0s) OR (source_eof && audio_buf.count > 0):
     Evaluate vw_vad_find_chunk_boundary(samples, count, vad_ctx, is_eof, &cut_samples, &silence_drain).
     
     If silence_drain > 0:
       vw_audio_buffer_drain(audio_buf, silence_drain); // ZERO Whisper calls!
     
     Else If cut_samples > 0:
       vw_whisper_engine_transcribe_pcm(engine, samples, cut_samples);
       For each Whisper segment:
         If no_speech_prob < 0.60f:
           vw_segment_builder_push_hypothesis(builder, text, start_pts, end_pts);
       vw_audio_buffer_drain(audio_buf, cut_samples); // Non-overlapping 100% drain!
```

---

## Acceptance Criteria
- [ ] `vw_vad_find_chunk_boundary` identifies leading silence, inter-sentence silence gaps, and speech bounds accurately.
- [ ] Lookahead Mode drains speech chunks completely without 2s overlapping re-transcriptions.
- [ ] During non-speech / instrumental music / silence, lookahead drains audio without invoking `whisper_full`.
- [ ] No mid-sentence word clipping occurs; speech padding preserves word-initial and word-final phonemes.
- [ ] Monologues / continuous speech without silence are clamped cleanly at `VW_CHUNK_MAX_SAMPLES` ($24.0\text{s}$).
- [ ] All unit and integration test suites pass 100% (`ctest --preset linux-x64-debug`).
- [ ] Valgrind memory leak verification reports 0 leaks (`ctest -T memcheck`).
- [ ] Windows MinGW cross-compilation builds cleanly (`cmake --preset windows-x64-debug`).
- [ ] Code formatted with `clang-format`.
- [ ] Documentation, ADR-020, and architecture docs updated in the same commit.

---

## Test Plan
1. **Unit Tests**:
   - `tests/unit/test_vad.c`:
     - Test pure silence $\to$ `out_silence_drain == count`, `out_cut_samples == 0`.
     - Test speech audio with pauses (`jfk.wav`) $\to$ cut point located at silence gap with $150\text{ms}$ padding.
     - Test continuous speech $\to$ clamped at $24.0\text{s}$.
     - Test EOF trailing audio ($< 6.0\text{s}$) $\to$ processed when `is_eof == true`.
     - Test Energy VAD fallback when `vad_ctx == NULL`.
2. **Integration Tests**:
   - `test_worker_lifecycle.c` & `test_worker_ipc.c`: Verify lookahead playback, pause/seek transitions, and clean subtitle cue emission.
3. **Memory & Portability**:
   - `ctest -T memcheck`
   - `cmake --preset windows-x64-debug && cmake --build --preset windows-x64-debug`
