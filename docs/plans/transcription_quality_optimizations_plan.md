# Implementation Task: Real-Time Transcription Quality Optimizations

## Goal
Elevate transcription accuracy and vocabulary fidelity in VLC-Whisper while strictly preserving real-time processing guarantees ($< 0.20$ Real-Time Factor on CPU, $< 0.05$ on Vulkan GPU) through model tier upgrades (`base.en` / `base.en-q5_1`), initial prompt formatting priming, cross-chunk context carryover across VAD silence gaps, token-level Dynamic Time Warping (DTW) timestamp alignment, and front-end audio rumble filtering.

---

## Context
- **Relevant docs/ADR**: `docs/architecture.md`, `docs/whisper-api.md`, `ADR-015` (Single Model Lifetime), `ADR-020` (Silero VAD Strategy C Non-Overlapping Chunking), `ADR-021` (Subtitle Reading Floor & Deterministic Decoding).
- **VLC/worker/protocol version affected**: Worker inference pipeline (`worker/src/vw_whisper_engine.c`, `worker/src/vw_source_decoder.c`, `worker/src/vw_worker.c`), Protocol v1 (wire-compatible).
- **Assumptions and explicit non-goals**:
  - Non-goal: Introducing cloud APIs, telemetry, external network models, or Python runtimes.
  - Non-goal: Breaking the C17 standard (`-std=c17`) or zero-allocation VLC audio callback invariants.
  - Assumption: User machines support at least standard 4-thread x86_64 AVX2 / ARM64 NEON CPU inference, or Vulkan-capable discrete/integrated GPUs.

---

## Scope
- **In scope**:
  1. **Model Upgrades & Quantization Profiles**: Evaluation and configuration for `base.en` (fp16) and `base.en-q5_1` / `small.en-q5_1` (quantized).
  2. **Initial Prompt Priming (`wparams.initial_prompt`)**: Decoder formatting conditioning for standard dialogue capitalization, punctuation, and contractions.
  3. **Cross-Chunk Prompt Carryover (`prompt_tokens`)**: Contextual token history preservation across Strategy C natural silence boundaries ($\ge 300\,\text{ms}$) without within-window hallucination loops.
  4. **Token-Level DTW Timestamps (`wparams.token_timestamps = true`)**: High-precision sub-word acoustic alignment (~20–50ms accuracy).
  5. **Audio Front-End DSP (80Hz High-Pass Filter & RMS Normalization)**: Sub-80Hz environmental rumble attenuation to improve log-mel filterbank contrast.
  6. **GPU Micro-Beam Search (`strategy = WHISPER_SAMPLING_BEAM_SEARCH`, `beam_size = 2`)**: Bounded beam evaluation on GPU backends.
- **Out of scope**:
  - Training or fine-tuning custom Whisper checkpoints.
  - Multi-speaker diarization (reserved for future milestones).
  - Translating non-English speech without multilingual models.
- **Files/components expected to change**:
  - `worker/include/vw_whisper_engine.h`
  - `worker/src/vw_whisper_engine.c`
  - `worker/src/vw_worker.c`
  - `worker/src/vw_source_decoder.c`
  - `plugin/src/vw_audio_capture.c`
  - `tests/unit/test_whisper_engine.c`

---

## Design

### 1. Model Tier & Quantization Architecture

| Model Checkpoint | Parameters | Disk / RAM | WER (CommonVoice) | CPU RTF (4-thread AVX2) | Vulkan GPU RTF | Real-Time Headroom |
|---|---|---|---|---|---|---|
| `tiny.en` (baseline) | 39 M | ~75 MB | ~10–12% | 0.04–0.08 | 0.01–0.02 | 12x–25x |
| **`base.en` (fp16)** | 74 M | ~142 MB | ~6–8% | 0.12–0.20 | 0.03–0.05 | 5x–20x |
| **`base.en-q5_1`** | 74 M | ~57 MB | ~6–8% | 0.08–0.14 | 0.02–0.04 | 7x–25x |
| **`small.en-q5_1` (GPU only)** | 244 M | ~160 MB | ~3–4% | 0.35–0.55 (too slow for live) | 0.08–0.12 | 8x–12x on GPU |

- **Recommendation**: Default to `base.en` (or `base.en-q5_1` on CPU/low-memory environments). Provides a **~35–40% error reduction** over `tiny.en` while transcribing a 6-second chunk in under **300 ms on GPU** and under **1000 ms on CPU**.

---

### 2. Initial Prompt Priming

Whisper's decoder is an autoregressive transformer conditioned on a prefix sequence. By supplying a static format guide via `wparams.initial_prompt`:
```c
wparams.initial_prompt = "Subtitles for dialogue. Correct punctuation, capitalization, and names.";
```
- **Acoustic Benefit**: Primes the attention heads to emit standard English capitalization, apostrophes ("they're", "I've"), commas, question marks, and period terminations rather than continuous lower-case run-on strings.
- **Latency Overhead**: Exactly **0 ms** (processed during standard cross-attention initialization).

---

### 3. Cross-Chunk Prompt Carryover across VAD Silence Gaps

In Strategy C non-overlapping VAD chunking (`ADR-020`), audio chunks are sliced exclusively at natural conversational pauses ($\ge 300\,\text{ms}$ silence gap).
- **Problem with `no_context = true`**: The transformer has zero memory of the preceding sentence, leading to pronoun ambiguity ("he" vs "she"), repeated word stuttering, or misheard domain terms.
- **Problem with naive context**: Unbounded within-window conditioning triggers hallucination loops on music/noise.
- **Solution**: Carry forward the last 16–32 tokens of the *finalized, emitted* predecessor cue and supply them as `prompt_tokens` / `initial_prompt` to the next chunk:
  ```c
  if (last_emitted_tokens_count > 0) {
    wparams.prompt_tokens = last_emitted_tokens;
    wparams.prompt_n_tokens = last_emitted_tokens_count;
  }
  ```
- **Safety Invariant**: When a silence gap $> 3.0\,\text{s}$, a seek event, or a session discontinuity occurs, the prompt carryover buffer is cleared immediately.

---

### 4. Token-Level Timestamps via Dynamic Time Warping (DTW)

- **Configuration**:
  ```c
  wparams.token_timestamps = true;
  wparams.thold_pt = 0.01f;
  wparams.thold_ptsum = 0.01f;
  ```
- **Mechanism**: Computes cross-attention alignment matrices between decoder token embeddings and encoder frame states.
- **Benefit**: Replaces Whisper's coarse 10ms segment timestamp heuristics with sub-word acoustic alignment (~20–50ms accuracy). Cues snap precisely to vocal onset and offset.
- **Latency Overhead**: ~15–30 ms per 6-second audio chunk.

---

### 5. Front-End Audio Conditioning (80Hz High-Pass Biquad Filter)

Dialogue audio in movies and streams frequently contains sub-80Hz low-frequency noise (room HVAC, traffic rumble, explosive bass transients, sub-bass synth lines) that corrupts the lower bins of Whisper's 80-channel log-mel spectrogram.
- **Implementation**: 2nd-order Direct Form II Transposed Butterworth high-pass filter ($f_c = 80\,\text{Hz}$, $f_s = 16000\,\text{Hz}$):
  $$H(z) = \frac{b_0 + b_1 z^{-1} + b_2 z^{-2}}{1 + a_1 z^{-1} + a_2 z^{-2}}$$
- **Compute Overhead**: Vectorizable in-place SIMD loop on float32 PCM; takes $< 0.5\,\text{ms}$ for 96,000 samples (6 seconds).

---

### 6. GPU Micro-Beam Search (`beam_size = 2`)

- **Configuration**:
  ```c
  if (engine->backend == VW_WORKER_BACKEND_GPU) {
    wparams.strategy = WHISPER_SAMPLING_BEAM_SEARCH;
    wparams.beam_search.beam_size = 2;
    wparams.beam_search.patience = -1.0f;
  } else {
    wparams.strategy = WHISPER_SAMPLING_GREEDY;
  }
  ```
- **Benefit**: Evaluates top-2 token paths simultaneously on GPU tensor cores, resolving local phonetic traps (e.g. "recognize" vs "wreck a nice") without the latency penalty of large beams ($N=5$).

---

## Acceptance Criteria
- [ ] `base.en` and `base.en-q5_1` models load cleanly and transcribe with verified $> 30\%$ WER improvement over `tiny.en`.
- [ ] Initial prompt formatting conditioning enforces sentence-initial capitalization and proper punctuation.
- [ ] Cross-chunk prompt token carryover preserves conversational context across $\ge 300\,\text{ms}$ VAD silence gaps and clears on seek/pause.
- [ ] Token-level DTW timestamp alignment produces sub-50ms sync precision without crashing or leaking memory.
- [ ] 80Hz high-pass filter attenuates sub-bass acoustic rumble on float32 PCM input before log-mel spectrogram generation.
- [ ] Real-time factor remains $\le 0.20$ on CPU (4-thread AVX2) and $\le 0.05$ on Vulkan GPU across all tested media.
- [ ] Automated unit and lifecycle test suite passes 100% with zero memory leaks in Valgrind.

---

## Test Plan
1. **Model Loading & Benchmarking**:
   - `ctest --test-dir build/linux-x64-debug -R test_whisper_engine` with `tiny.en`, `base.en`, and `base.en-q5_1`.
   - Measure RTF on 30-second speech sample (`jfk.wav`).
2. **Determinism & Prompt Tests**:
   - Verify repeated transcription of identical audio produces identical token sequences with initial prompt active.
3. **Acoustic Noise Stress Test**:
   - Transcribe speech mixed with 40Hz synthetic sub-bass tone; verify 80Hz high-pass filter restores speech recognition accuracy.
4. **VLC End-to-End Playback**:
   - Play 1080p/4K movie clips with fast conversational dialogue; verify zero caption stutter, crisp capitalization, and zero overlap.

---

## Definition of Done
- [ ] C17 code standard enforced (`-std=c17`).
- [ ] Zero blocking locks, heap allocations, or IPC in VLC audio callback.
- [ ] Offline privacy invariant preserved (zero network calls).
- [ ] Valgrind memcheck clean (0 leaks).
- [ ] Native Linux and Windows MinGW cross-builds pass with zero warnings.
- [ ] ADR and documentation updated in `docs/decisions.md` and `docs/architecture.md`.
