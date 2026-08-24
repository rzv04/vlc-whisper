# Implementation Plan: Subtitle Quality & Phrase-by-Phrase Synchronization Enhancements

## 1. Executive Summary & Goal

This document defines concrete, backward-compatible improvements to elevate transcription quality, phrase boundary precision, and inference throughput in `vlc-whisper`. 

These enhancements are derived directly from the proven audio pipelines, parameter matrices, and sliding window heuristics established in the `vlsub-opensubtitles-com` project and encapsulated in the `whisper-subtitle-sync` framework.

All current `vlc-whisper` architectural features—including the native C17 audio filter (`pf_audio_filter`), lock-free SPSC IPC ring buffers, Silero VAD boundary chunking (Strategy C), deterministic hallucination filtering, and SPU/OSD subpicture rendering—are strictly preserved with zero regressions.

---

## 2. Baseline Architecture & Current Bottlenecks in `vlc-whisper`

In `vlc-whisper` (`worker/src/vw_whisper_engine.c` and `worker/src/vw_worker.c`), audio is captured in real-time or decoded ahead-of-time, filtered via Silero VAD (`vw_vad.c`), and fed into `vw_whisper_engine_transcribe_pcm()`.

### Identified Quality Bottlenecks:
1. **Isolated Context (`wparams.no_context = true`)**:
   - Currently, `wparams.no_context = true` is hardcoded.
   - *Impact*: Disables prompt carryover across sliding VAD chunks. Whisper treats each spoken phrase in complete isolation, causing spurious mid-sentence capitalization (e.g. splitting *"we went to the... Market"*), dropped punctuation, and lost contextual awareness of names and homophones.
2. **Coarse Segment Timestamps (`wparams.token_timestamps = false`)**:
   - Dynamic Time Warping (DTW) token alignment is disabled.
   - *Impact*: Whisper only emits coarse segment-level timestamps (`whisper_full_get_segment_t0` / `t1`) spanning 3 to 6 seconds. Fast multi-phrase dialogue cannot be sliced into tight, cadence-matched sub-phrases.
3. **Flash Attention Disabled**:
   - Flash Attention (`cparams.flash_attn` / `wparams.flash_attn`) is not explicitly toggled in context initialization.
   - *Impact*: Foregoes a 15%–30% GPU throughput speedup and increases attention matrix memory pressure.
4. **Generic Model Resolution**:
   - English transcription lacks automatic resolution to `.en.bin` models (`ggml-tiny.en.bin`, `ggml-base.en.bin`).
   - *Impact*: General multilingual models divide token vocabulary across 99 languages, yielding lower accuracy and higher hallucination rates than dedicated English weights.

---

## 3. High-Level Improvements & Design Specifications

### Improvement 1: Dynamic Prompt Conditioning across VAD Chunks

Instead of total context isolation (`no_context = true`), implement bounded prompt carryover:
- **Mechanism**: Retain the last 16–32 tokens from the previously emitted, finalized phrase and supply them to `wparams.prompt_tokens` / `wparams.prompt_n_tokens` on the next chunk.
- **Safety Invariants**:
  - Immediately flush the prompt token buffer when a silence pause $> 3.0\,\text{s}$, a seek event, or a playback pause/resume occurs.
  - Set `wparams.no_context = false` while maintaining temperature fallback (`wparams.temperature_inc = 0.2f`).

```c
// Code Pattern for worker/src/vw_whisper_engine.c:
wparams.no_context = false;
if (last_emitted_token_count > 0) {
  wparams.prompt_tokens = last_emitted_tokens;
  wparams.prompt_n_tokens = last_emitted_token_count;
}
```

### Improvement 2: Token-Level DTW Timestamps for Sub-Second Phrase Boundaries

- **Mechanism**: Enable Whisper's Dynamic Time Warping (DTW) to compute cross-attention alignment matrices between decoder token embeddings and encoder audio frames.
- **Configuration**:
  ```c
  wparams.token_timestamps = true;
  wparams.thold_pt = 0.01f;
  wparams.thold_ptsum = 0.01f;
  ```
- **Integration with `vw_segment_builder.c`**:
  - Use `whisper_full_get_token_data()` to inspect individual token start/end presentation timestamps.
  - Group tokens into natural phrase clusters based on punctuation (commas, periods, question marks) or acoustic pauses $> 300\,\text{ms}$, emitting tight phrase cues with sub-100ms timing precision.

### Improvement 3: Flash Attention Hardware Acceleration

- **Mechanism**: Enable Flash Attention in GGML context parameters to optimize tensor matrix operations and reduce VRAM bandwidth.
- **Configuration in `vw_whisper_engine.c`**:
  ```c
  struct whisper_context_params cparams = whisper_context_default_params();
  cparams.use_gpu = (backend != VW_WORKER_BACKEND_CPU);
  cparams.gpu_device = (gpu_device >= 0) ? gpu_device : 0;
  cparams.flash_attn = true;
  ```
- **Outcome**: 15%–30% lower inference latency on CUDA and Vulkan backends.

### Improvement 4: Automatic English-Only Model Prioritization

- **Mechanism**: In `vw_worker_config.c`, when the user configures `language = "en"`, automatically inspect the model directory for `.en.bin` model variants (e.g. `ggml-tiny.en.bin`, `ggml-base.en.bin`, `ggml-base.en-q5_1.bin`).
- **Benefit**: 100% of embedding parameters are dedicated to English vocabulary, eliminating multilingual token dispersion and reducing word error rate (WER) by 35%–40% without increasing model parameter size.

### Improvement 5: Audio Front-End Signal Padding & Rumble Attenuation

- **Pre-Speech Padding**:
  - In `vw_vad.c`, ensure that whenever a speech cut occurs, a minimum of 150ms (`VW_CHUNK_PAD_SAMPLES`) of pre-speech audio buffer is preserved before the first detected vocal energy frame.
  - *Benefit*: Guarantees that initial plosives and soft attack consonants (e.g., /p/, /t/, /k/, /s/) are never clipped.
- **80Hz High-Pass Filtering**:
  - Apply a 2nd-order Direct Form II Transposed Butterworth filter ($f_c = 80\,\text{Hz}$, $f_s = 16000\,\text{Hz}$) to eliminate sub-audible room rumble and HVAC noise from lower Mel filterbank bins.

---

## 4. Architectural Invariants Preserved

1. **C17 Standard Compliance**: All additions adhere strictly to `-std=c17` without third-party C++ runtime dependencies.
2. **Audio Callback Safety**: The VLC audio filter (`pf_audio_filter`) remains completely lock-free, zero-allocation, and non-blocking.
3. **Deterministic Memory Management**: Fixed-capacity ring buffers and pre-allocated float arrays are used for all audio ingestion and inference chunks.
4. **SPU Subpicture Presentation**: Cues remain strictly bound to authentic presentation timestamps (`pts_us`), rendering smoothly across timeline scrub events.
5. **Zero Emojis Policy**: Maintained across all code, logs, and documentation.

---

## 5. Step-by-Step Implementation Tasks

### Task 1: Engine Parameter Tuning (`worker/src/vw_whisper_engine.c`)
- Add `cparams.flash_attn = true;` during context initialization.
- Set `wparams.token_timestamps = true;`, `wparams.thold_pt = 0.01f;`, `wparams.thold_ptsum = 0.01f;`.
- Set `wparams.no_context = false;` and add `vw_whisper_engine_set_prompt_tokens()`.

### Task 2: Prompt History Buffer (`worker/src/vw_worker.c`)
- Maintain a ring buffer of the last 32 emitted tokens.
- Inject tokens as prompt conditioning before transcribing lookahead chunks.
- Flush buffer on seek events, session stop, or silence gaps $> 3.0\,\text{s}$.

### Task 3: Token-Aware Phrase Builder (`worker/src/vw_segment_builder.c`)
- Enhance `vw_segment_builder_push_hypothesis()` to evaluate token-level DTW timestamps when available.
- Split multi-sentence single segments into discrete, cadence-matched phrase cues.

### Task 4: Configuration & Model Resolution (`worker/src/vw_worker_config.c`)
- Add `.en` model resolution helpers when target language is `en`.

---

## 6. Definition of Done (DoD)

1. `ninja -C build` compiles cleanly with zero warnings (`-Wall -Wextra -Werror`).
2. Unit tests in `tests/unit/test_whisper_engine.c` and `tests/unit/test_segment_builder.c` pass 100%.
3. Continuous dialogue preserves correct sentence capitalization and punctuation across VAD chunk transitions.
4. Spoken phrases synchronize to audio within $\le 100\,\text{ms}$ accuracy.
5. Zero regressions in memory footprint or real-time factor (RTF).
