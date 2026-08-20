# Implementation Task Template

# Task: Third-Party VAD Integration, Silence-Aware Gating & Hallucination Suppression (Step 17e.1)

## Goal
Eliminate subtitle flicker, phantom caption bursts, and background music hallucinations during non-speech intervals by integrating a deep neural Voice Activity Detector (Silero GGML VAD via native `whisper.cpp` APIs) backed by multi-tier acoustic confidence gating (`no_speech_prob`) and lexical hallucination pattern suppression.

---

## Context
- **Relevant Docs/ADRs**: ADR-002 (C17 Authored Code), ADR-004 (Offline-Only Local IPC), ADR-015 (Model-Once Worker Lifetime), ADR-017 (Phrase-by-Phrase Timing), ADR-018 (Final Immutable Subtitles), `docs/architecture.md`, `docs/api-contracts.md`, `docs/whisper-api.md`.
- **Target OS & Players**: Linux x64 (GCC/Clang, POSIX sockets) and Windows x64 (MinGW-w64, Win32 Named Pipes, VLC 3.0.23).
- **Assumptions and Explicit Non-Goals**:
  - *Non-Goal (deferred to Milestone 4 Item 21a)*: Multi-model GUI selection (`base.en`, `small.en`). Step 17e.1 operates with default `ggml-tiny.en.bin` and optional `ggml-silero-vad.bin`.
  - *Non-Goal (deferred to Step 17e.2)*: Minimum display duration floor ($\ge 1.0\,\text{s}$) and beam search / temperature fallback tuning.
  - *Zero New External Dynamic Dependencies*: Leverages `whisper.cpp`'s built-in Silero GGML VAD (`whisper_vad_context`), avoiding heavy runtime dependencies like ONNX Runtime or libtorch.

---

## Scope

### In Scope
1. **Tier 1: Pre-Inference Voice Activity Detection (`vw_vad`)**:
   - Wire up `struct whisper_vad_context*` in `worker/src/vw_vad.c` using `whisper_vad_init_from_file_with_params` and `whisper_vad_detect_speech`.
   - Add CLI parameter `--vad-model <path>` and config field `vad_model_path` in `vw_worker_config`.
   - Auto-discover `ggml-silero-vad.bin` in the model directory alongside `ggml-tiny.en.bin` or alongside `vlc-whisper-worker.exe`, requiring zero plugin launch changes (O4).
   - Replace `vw_vad_detect_speech_energy` across **all THREE call sites** in `vw_worker.c` (O1):
     1. Live PCM audio window (line 525, full 128k samples).
     2. Lookahead decoding full window (line 627, full 128k samples).
     3. Lookahead trailing/partial EOF window (line 657, `remaining < 128k` samples). Silero VAD processes arbitrary sample counts in 512-sample frames with tail padding.
   - Implement graceful zero-config fallback to RMS energy VAD (`vw_vad_detect_speech_energy`) when no VAD model file is found.
   - Reset VAD LSTM state (`whisper_vad_reset_state`) on seeking, pause/resume, and session epoch transitions.
2. **Tier 2: Post-Inference Acoustic Confidence Gating (`vw_whisper_engine` / `vw_worker`)**:
   - Set `wparams.no_speech_thold = 0.60f` in `vw_whisper_engine.c` to suppress whole-window silence (O9).
   - Expose `no_speech_prob` in `vw_whisper_segment_t` via `whisper_full_get_segment_no_speech_prob(ctx, i)`.
   - In `vw_worker.c`, discard sub-segments with $P(\text{no\_speech}) \ge 0.60$ for mixed speech-and-silence windows before passing them to the segment builder (O8, O9).
3. **Tier 3: Formatting Cleanliness & Non-Speech Tag Filtering (`vw_hallucination_filter`)**:
   - Strictly transcribe all genuine speech without censoring words, sentences, or conversational phrases (no phrase blacklists).
   - Reject segments that consist **exclusively** of isolated punctuation or symbols (e.g. `"."`, `"..."`, `"---"`, `"! ! !"`) with zero alphanumeric characters, while preserving all valid punctuation inside legitimate sentences (e.g. `"Hello, how are you?"`, `"Look out!"`, `"Wait..."`).
   - Strip/reject non-speech descriptor tags (`[Music]`, `[MUSIC]`, `(music)`, `[Applause]`, `(applause)`, `[Laughter]`, `[Silence]`, `♪`, `♫`, `*music*`) and leverage `whisper.cpp`'s `wparams.suppress_nst = true`.
   - Modular implementation in `worker/src/vw_hallucination_filter.c` with dedicated tests in `tests/unit/test_hallucination_filter.c` (O7).
4. **Automated & Manual Test Suite**:
   - Add unit tests for `vw_hallucination_filter` and `vw_vad` in `tests/unit/`.
   - Update `test_whisper_engine` and `test_segment_builder` for `no_speech_prob` and non-speech tag suppression.

### Out of Scope & Future Refinements
- **Dynamic Speech-Onset Variable-Geometry Windowing (O2)**: Step 17e.1 implements *Silence-Aware Window Gating* (skipping inference on non-speech windows) while preserving the fixed 8.0s window / 2.0s hop geometry. Triggering variable-duration hops immediately upon speech onset is deferred as a future refinement (post-MVP).
- SPU display duration expansion or reading time floors (Step 17e.2).
- Whisper beam search or temperature fallback parameter sweeps (Step 17e.2).
- Packaging GUI settings or multi-model downloads (Milestone 4).

### Files & Components Expected to Change
- `worker/include/vw_vad.h` & `worker/src/vw_vad.c`: Silero VAD context management and detection.
- `worker/include/vw_worker_config.h` & `worker/src/vw_worker_config.c`: `--vad-model` CLI parsing and auto-discovery.
- `worker/include/vw_hallucination_filter.h` & `worker/src/vw_hallucination_filter.c`: Non-speech tag & isolated punctuation filter.
- `worker/include/vw_whisper_engine.h` & `worker/src/vw_whisper_engine.c`: `no_speech_prob` segment getter.
- `worker/src/vw_worker.c`: 3-tier VAD lifecycle across 3 call sites, seek reset, and confidence gating.
- `worker/src/vw_segment_builder.c`: Lexical filter rejection before dedup.
- `tests/unit/test_vad.c`: Unit tests for Silero VAD, partial windows, and energy fallback.
- `tests/unit/test_hallucination_filter.c`: Unit tests for non-speech tags and isolated punctuation.
- `tests/unit/test_segment_builder.c`: Regression tests for phantom cue suppression.
- `docs/decisions.md`: Record ADR-019 (Multi-Tier VAD & Silence Gating).
- `docs/architecture.md`, `docs/api-contracts.md`, `docs/source-layout.md`, `docs/test-strategy.md`, `docs/roadmap.md`.

---

## Design

### 1. Multi-Tier Filtering Architecture

```
[ Audio Window (8.0s / 128k samples @ 16kHz) or Trailing Window (< 128k) ]
                                   │
                                   ▼
┌────────────────────────────────────────────────────────────────────────┐
│ TIER 1: Pre-Inference Voice Activity Detection (VAD) (All 3 Call Sites)│
│  - Silero GGML VAD (`whisper_vad_detect_speech`)                       │
│  - Graceful Fallback: RMS Energy Gate (> 0.01f)                        │
│  - Action: No speech detected ──► SKIP Whisper entirely                │
└──────────────────────────────────┬─────────────────────────────────────┘
                                   │ Speech Detected
                                   ▼
┌────────────────────────────────────────────────────────────────────────┐
│ WHISPER INFERENCE (whisper_full)                                       │
│  - suppress_nst = true, suppress_blank = true                          │
│  - no_speech_thold = 0.60f (suppresses whole-window silence)           │
└──────────────────────────────────┬─────────────────────────────────────┘
                                   │ Raw Segments
                                   ▼
┌────────────────────────────────────────────────────────────────────────┐
│ TIER 2: Post-Inference Acoustic Confidence Filtering (Mixed Windows)   │
│  - whisper_full_get_segment_no_speech_prob(ctx, i)                     │
│  - Action: if no_speech_prob >= 0.60 ──► DROP segment                  │
└──────────────────────────────────┬─────────────────────────────────────┘
                                   │ Validated Speech Segment
                                   ▼
┌────────────────────────────────────────────────────────────────────────┐
│ TIER 3: Formatting & Non-Speech Tag Cleanliness Filter                 │
│  - 3A: Non-speech descriptor tags ([Music], ♪, etc.)                   │
│  - 3B: Pure isolated punctuation (..., --- with no text)               │
│  - (All legitimate dialogue and punctuation preserved)                 │
│  - Action: Non-speech / empty ──► DROP candidate                       │
└──────────────────────────────────┬─────────────────────────────────────┘
                                   │ Clean Hypothesis Phrase
                                   ▼
┌────────────────────────────────────────────────────────────────────────┐
│ SEGMENT BUILDER (vw_segment_builder)                                   │
│  - Whole-phrase time coverage deduplication (ADR-018)                  │
│  - SPU channel start clamping & final cue emission                     │
└────────────────────────────────────────────────────────────────────────┘
```

### 2. Threading & Ownership Model
- **Single-Threaded Worker Event Loop**: VAD classification, Whisper inference, confidence gating, and segment builder execution all occur synchronously within `vw_worker_run` on the worker main thread.
- **VAD Context Lifecycle**: `struct whisper_vad_context*` is initialized once at worker startup and freed at shutdown (`ADR-015`).
- **State Reset on Discontinuity**: When a seek, pause, or media swap occurs, `whisper_vad_reset_state()` clears LSTM hidden states so previous audio does not bleed into the new position.

### 3. Bounds, Time Units, and Performance
- **Acoustic Bounds**: Whisper segment centiseconds ($t_0, t_1$) are converted to microsecond media presentation timestamps (`pts_us = window_pts_us + t * 10000LL`).
- **No-Speech Probability**: Float bounded in $[0.0, 1.0]$. Segments with $\ge 0.60$ are dropped immediately.
- **VAD Processing Latency (O3)**: Silero GGML processes 512-sample (32ms) chunks. An 8.0s window is 250 frames ($\sim 15\text{--}40\,\text{ms}$ on CPU, $< 2\,\text{ms}$ on GPU). In lookahead decoding, this is $< 2\%$ of the 2.0s hop interval (to be verified in benchmarks).
- **Failure Behavior**: If the VAD model fails to load at startup, the worker logs an informative warning and automatically falls back to RMS Energy VAD. VLC playback is never stalled or interrupted.

### 4. Conversational Pauses, Speech-Silence-Speech & Mid-Sentence Cadence

```
Audio Window: [ 0.0s === Phrase A (0.5s-2.5s) === [ 2.5s SILENCE PAUSE ] === Phrase B (5.0s-7.2s) === 8.0s ]
                                  │
                                  ▼
                     1. VAD Classification (Tier 1)
                     Is speech present in window? YES (detected at 0.5s & 5.0s)
                                  │
                                  ▼
                     2. Whisper Inference & Segment Extraction (ADR-017)
                     Whisper emits 2 discrete sub-segments:
                     - Segment 0: [ 0.5s - 2.5s ] "Where are you from?" (no_speech_prob = 0.02)
                     - Segment 1: [ 5.0s - 7.2s ] "I am from Romania." (no_speech_prob = 0.03)
                                  │
                                  ▼
                     3. Acoustic Confidence & Cleanliness (Tiers 2 & 3)
                     Both pass (confidence high, valid text, no non-speech tags)
                                  │
                                  ▼
                     4. VLC SPU Subpicture Scheduling (Presenter)
                     - Cue 1: i_start = 10.5s, i_stop = 12.5s
                     - Cue 2: i_start = 15.0s, i_stop = 17.2s
                                  │
                                  ▼
┌─────────────────────────────────────────────────────────────────────────────────────────────────────┐
│ VLC Screen Timeline Rendering:                                                                     │
│                                                                                                     │
│ 10.5s ──────► 12.5s               12.5s ──────────────► 15.0s             15.0s ──────► 17.2s       │
│ ["Where are you from?"]           [   SCREEN COMPLETELY BLANK   ]         ["I am from Romania."]    │
│ (Subpicture 1 active)             (Silence pause: zero subtitles)         (Subpicture 2 active)     │
└─────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

#### Handling Specific Pausing Patterns
1. **Short Mid-Sentence Pauses ($< 0.5\,\text{s}$)**:
   - *Example*: *"I think that... [0.3s pause] ...we should go."*
   - **Whisper Behavior**: Emits a single continuous acoustic phrase (`[ 1.0s - 3.2s ] "I think that we should go."`). Subtitle stays steadily on screen across the short pause.
2. **Long Conversational Pauses ($\ge 1.0\,\text{s}$)**:
   - *Example*: Conversational turn-taking or thinking pause.
   - **Whisper Behavior**: Acoustic segmentation splits the utterance along silence boundaries into discrete segments with exact acoustic bounds ($t_0, t_1$).
   - **On Screen**: Phrase 1 clears when speech stops, **screen remains completely blank during the pause** (zero phantom subtitles, zero lingering text), and Phrase 2 appears instantly when speech resumes.
3. **Cross-Hop Deduplication (ADR-018)**:
   - When the next sliding window hops (e.g. $12.0\,\text{s} \to 20.0\,\text{s}$), re-recognitions of Phrase B starting inside covered audio ($\le \text{covered\_end\_us}$) are cleanly suppressed, preventing subtitle flicker or word duplication.

---

## Acceptance Criteria
- [ ] VAD accurately detects speech across all three call sites (live PCM, lookahead full, lookahead trailing).
- [ ] Worker falls back cleanly to RMS energy VAD when `--vad-model` is not supplied or model file is absent.
- [ ] High `no_speech_prob` segments ($\ge 0.60$) are discarded before segment builder insertion.
- [ ] Standalone non-speech tags (`[Music]`, `[Applause]`, `♪`) and isolated punctuation (`...`, `---`) with zero alphanumeric characters are rejected while 100% of spoken dialogue and valid sentence punctuation are preserved.
- [ ] Seeking and pause-resume reset VAD state without crashes or memory leaks.
- [ ] `whisper_vad_*` symbols in vendored `whisper.cpp` link cleanly into `vlc-whisper-worker` across all presets.
- [ ] 100% automated tests pass (`ctest --preset linux-x64-debug`).
- [ ] Valgrind memcheck reports 0 errors and 0 leaks.
- [ ] Documentation and ADRs updated in the same change.

---

## Manual Verification & Testing Protocol (Windows x64 / VLC 3.0.23)

### 1. Environment & Pre-Conditions
- **OS**: Windows 10 or Windows 11 (64-bit).
- **VLC**: Official VLC media player 3.0.23 (x86_64).
- **Binaries**:
  - `libvlc_whisper_plugin.dll` copied to `C:\Program Files\VideoLAN\VLC\plugins\misc\`
  - `vlc-whisper-worker.exe` copied to `C:\Program Files\VideoLAN\VLC\`
  - `ggml-tiny.en.bin` located in `C:\Program Files\VideoLAN\VLC\models\`
  - `ggml-silero-vad.bin` (optional) located in `C:\Program Files\VideoLAN\VLC\models\`

### 2. Verification Commands
```cmd
:: 1. Reset plugin cache and verify registration
"C:\Program Files\VideoLAN\VLC\vlc.exe" --reset-plugins-cache --list | findstr /i whisper

:: 2. Launch VLC with audio filter and file logging enabled
"C:\Program Files\VideoLAN\VLC\vlc.exe" --reset-plugins-cache --audio-filter=vlc_whisper --file-logging --logfile=vlc-debug.log -vvv "C:\path\to\test_media.mp4"
```

### 3. Verification Checklist & Test Matrix

| ID | Test Scenario | Execution Steps | Pass Criteria | Status |
|---|---|---|---|---|
| **TC-01** | **Dialogue with Background Music / SFX** | Play dialogue scene containing background soundtrack and ambient noise. | Dialogue transcribed accurately; music/SFX ignored; subtitles appear at vocal onset without hallucinated words. | [ ] PASS |
| **TC-02** | **Instrumental Music & Silence Gating (O5)** | Play pure music track (`upbeat_music.mp3`) and clips with >15s silence. | Subtitle area remains completely blank; music with vocal-like harmonics may trigger VAD, but Tiers 2–3 suppress captions; zero `[Music]` or phantom loops. | [ ] PASS |
| **TC-03** | **Conversational Rapid Speech & Pauses** | Play rapid conversation/podcast with 0.5s–2.0s pauses between speaker turns. | Subtitles appear phrase-by-phrase; screen blanks during pauses ($\ge 0.5\text{s}$); second speaker's lines are not spoiled early (`ADR-017`). | [ ] PASS |
| **TC-04** | **Pause & Resume Lifecycle** | Pause mid-sentence for 5s, resume; pause during instrumental music, resume. | On pause, capture suspends and worker clears window; on resume, playback continues with synchronized captions without duplicate words or PTS drift. | [ ] PASS |
| **TC-05** | **Seeking & Rapid Scrubbing** | Seek forward into dialogue, backward into music, and scrub slider rapidly 3–5 times. | Active subtitles clear immediately (`vout_FlushSubpictureChannel`); no residual ghost text; timeline re-anchors cleanly; zero audio stutter or crash. | [ ] PASS |
| **TC-06** | **Worker Failure & Missing Model Resiliency** | Delete model file or terminate `vlc-whisper-worker.exe` from Task Manager during playback. | VLC audio/video playback continues 100% smoothly without pause or crash; plugin logs error diagnostic and degrades to passthrough. | [ ] PASS |
| **TC-07** | **30-Minute Long-Play Stability** | Play continuous 30-minute local media file. | Worker RAM remains bounded (< 300MB); CPU usage stable; zero queue overflows or PTS desync over time. | [ ] PASS |

### 4. Behavioral Expectations

| Category | WHAT TO EXPECT (Correct Behavior) | WHAT NOT TO EXPECT (Out of Scope / Prohibited) |
|---|---|---|
| **Subtitle Timing & Cadence** | Instant, frame-accurate subtitle appearance on vocal onset ($t_0$) with natural phrase boundaries (`ADR-017`). | Karaoke-style word-by-word rolling animations; coarse 8-second block aggregation with early spoilers. |
| **Visual Quality & Silence** | Crystal-clear bottom-center SPU text rendering; automatic screen blanking during silence, music, and pauses. | Lingering subtitles during silence; `[Music]`, `[Applause]`, `[Laughter]`, `♪`, or repetitive phantom subtitle loops. |
| **Audio Processing** | Accurate transcription of clear human vocal speech in English. | Transcription or description of non-vocal audio (ambient noise, sirens, dog barks, instrumental solos). |
| **Language Support** | Full English transcription using `ggml-tiny.en.bin`. | Translation of foreign languages into English (multilingual translation is Milestone 4 Step 22). |
| **Subtitle Immutability** | Subtitles are **FINAL and IMMUTABLE** once emitted (`ADR-018`). | Modification, editing, re-expanding, or flickering of already displayed, finalized subtitles. |
| **Playback & Stability** | Zero audio stutter, zero clicks, bounded memory usage, graceful degradation on failure. | VLC crashes, audio callback blocking, unhandled exceptions, memory growth over time. |

---

## Definition of Done
- [ ] C17 code; no project-authored C++ introduced
- [ ] No blocking work in VLC audio callback
- [ ] No network access, telemetry, transcript/PCM persistence, or sensitive logs introduced
- [ ] Memory, audio queue, frame, text, and retry limits are bounded
- [ ] Error path is safe: captions may stop, playback does not
- [ ] `whisper_vad_*` symbols in vendored `whisper.cpp` build and link successfully (O6)
- [ ] Unit/contract/integration tests pass as applicable
- [ ] Formatting, warnings-as-errors, and static checks pass
- [ ] Protocol contract and compatibility version updated if needed
- [ ] `docs/decisions.md`, roadmap, and AI context updated when assumptions change
- [ ] Reviewer can reproduce the result from a clean checkout

---

## Evidence
- Build/test outputs or CI links: `ctest --preset linux-x64-debug` (100% pass)
- Measured performance: VAD evaluation benchmark on CPU vs GPU across 250 frames ($\sim 15\text{--}40\,\text{ms}$ on CPU, $< 2\,\text{ms}$ on GPU)
- Known limitations/follow-ups: Minimum display duration floor and Whisper decoding parameter optimization implemented in Step 17e.2; variable-geometry speech-onset windowing deferred to post-MVP.

---

## Slice Rule
This task cuts vertically through:
1. VAD engine integration (`vw_vad.c`, `whisper_vad_context`).
2. Acoustic probability gating across 3 call sites (`vw_whisper_engine.c`, `vw_worker.c`).
3. Lexical non-speech tag & isolated punctuation filter (`vw_hallucination_filter.c`).
4. Unit tests (`test_vad.c`, `test_hallucination_filter.c`, `test_segment_builder.c`).
5. Windows manual verification protocol and documentation.


