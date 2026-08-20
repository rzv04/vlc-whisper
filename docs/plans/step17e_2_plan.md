# Implementation Plan: Step 17e.2 — Subtitle Pacing, Minimum Reading Floor & Decoding Optimization

# Task: Subtitle Pacing, Minimum Reading Floor & Decoding Optimization

## Goal
Eliminate unreadable sub-second "flash cues" by enforcing a minimum subtitle display duration floor ($\ge 1.0\,\text{s} = 1,000,000\,\mu\text{s}$) with consecutive cue overlap clamping, and configure whisper.cpp decoding parameters (`temperature_inc = 0.0f`, `entropy_thold = 2.40f`, `no_context = true`, `suppress_nst = true`) for strictly deterministic single-pass inference without latency explosions or hallucination cascades.

---

## Context
- **Relevant Docs/ADRs**:
  - `docs/decisions.md` (`ADR-016` Native SPU Pipeline, `ADR-017` Phrase-by-Phrase Subtitle Timing, `ADR-018` Whole-Phrase Deduplication, `ADR-019` Multi-Tier VAD, `ADR-020` No-Hop Lookahead Chunking, and new `ADR-021`).
  - `docs/architecture.md`, `docs/api-contracts.md`, `docs/source-layout.md`, `docs/test-strategy.md`, `docs/roadmap.md`.
- **VLC/Worker/Protocol Version Affected**:
  - Protocol v1.2 (framing unchanged; wire payload timestamps adhere to minimum duration and bounded pacing).
- **Assumptions & Explicit Non-Goals**:
  - *Non-goal*: Multi-pass temperature sampling search loops (which multiply compute by up to $6\times$).
  - *Non-goal*: Dynamic character-level text wrapping or font resizing (handled natively by VLC SPU renderer).
  - *Non-goal*: Modification of acoustic coverage tracking in `vw_segment_builder` (acoustic coverage remains true to audio boundaries while display duration is paced for human visual reading).

---

## Scope

### In Scope
1. **Minimum Subtitle Display Duration Floor (`VW_CAPTION_MIN_DISPLAY_DURATION_US = 1000000LL`)**:
   - In `plugin/src/vw_caption_presenter.c`: clamp any short subtitle cue ($0 < \text{duration} < 1.0\,\text{s}$) to $1.0\,\text{s}$ minimum display floor before SPU/OSD scheduling.
   - In `worker/src/vw_segment_builder.c`: for lookahead multi-phrase batches, extend short preceding cues up to the $1.0\,\text{s}$ floor while clamping at the incoming cue's `emit_start` to prevent visual cue collisions.
2. **Deterministic Whisper Decoding Configuration (`worker/src/vw_whisper_engine.c`)**:
   - `wparams.strategy = WHISPER_SAMPLING_GREEDY;`
   - `wparams.temperature = 0.0f;`
   - `wparams.temperature_inc = 0.0f;` (disables multi-pass retry loops, ensuring strictly bounded single-pass execution).
   - `wparams.entropy_thold = 2.40f;` (halts low-entropy token repetition loops).
   - `wparams.logprob_thold = -1.00f;`
   - `wparams.no_speech_thold = 0.60f;`
   - `wparams.no_context = true;` (isolates independent audio windows, preventing hallucination carryover across sliding hops).
   - `wparams.single_segment = false;` (emits discrete phrases for phrase-by-phrase timing).
   - `wparams.suppress_blank = true;`
   - `wparams.suppress_nst = true;` (suppresses non-speech tokens at logit level).
   - `wparams.print_special = false;`
   - `wparams.max_len = 0;` (preserves natural transformer phrase boundaries).
   - `wparams.token_timestamps = false;`
3. **Comprehensive Unit & Integration Test Suites**:
   - Presenter tests in `tests/unit/test_caption_presenter.c` covering $1.0\,\text{s}$ floor, rate scaling ($0.5\times$, $2.0\times$), lookahead lead pacing, and OSD fallback.
   - Segment builder tests in `tests/unit/test_segment_builder.c` covering short cue extension, consecutive cue clamping, and silence gap preservation.
   - Engine tests in `tests/unit/test_whisper_engine.c` covering decoding determinism and `no_context` isolation.
4. **Documentation & ADR-021**:
   - Document **ADR-021** (Subtitle Reading Floor & Deterministic Whisper Decoding Optimization) in `docs/decisions.md`.
   - Update `docs/architecture.md`, `docs/source-layout.md`, `docs/test-strategy.md`, and `docs/roadmap.md`.

### Out of Scope
- Font styling, color selection, or user subtitle positioning preferences (deferred to Milestone 4 settings GUI).
- Beam search decoding (too heavy for real-time streaming).

### Files Expected to Change
- `plugin/include/vw_caption_presenter.h`
- `plugin/src/vw_caption_presenter.c`
- `worker/include/vw_segment_builder.h`
- `worker/src/vw_segment_builder.c`
- `worker/include/vw_whisper_engine.h`
- `worker/src/vw_whisper_engine.c`
- `tests/unit/test_caption_presenter.c`
- `tests/unit/test_segment_builder.c`
- `tests/unit/test_whisper_engine.c`
- `docs/decisions.md`
- `docs/architecture.md`
- `docs/source-layout.md`
- `docs/test-strategy.md`
- `docs/roadmap.md`

---

## Design

### 1. Dual-Layer Duration Pacing Model

```text
[Whisper Engine]
       │
       ▼ (discrete phrase: t0, t1)
[vw_segment_builder] ──> Batch Pacing: extend short cue (dur < 1.0s) up to min(1.0s, next_start - cur_start)
       │
       ▼ (IPC: VW_MSG_CAPTION_SEGMENT)
[vw_caption_presenter] ──> Universal Display Floor: dur = max(dur, 1.0s)
       │
       ▼ (Rate Scaling & SPU Scheduling)
[VLC SPU Pipeline (vout_PutSubpicture)]
  i_start = now_tick + (start_pts - input_time) / rate
  i_stop  = i_start + dur / rate
```

### 2. Pacing & Boundary Formulas

1. **Presenter Duration Clamping**:
   $$\text{duration\_us} = \begin{cases} 2\,000\,000\,\mu\text{s} & \text{if } \text{raw\_dur} \le 0 \\ 1\,000\,000\,\mu\text{s} & \text{if } 0 < \text{raw\_dur} < 1\,000\,000\,\mu\text{s} \\ \text{raw\_dur} & \text{if } \text{raw\_dur} \ge 1\,000\,000\,\mu\text{s} \end{cases}$$

2. **Builder Consecutive Cue Clamping**:
   For consecutive queued cues $A$ and $B$:
   $$\text{end}_A = \min\Big(\max\big(\text{end}_A, \text{start}_A + 1\,000\,000\,\mu\text{s}\big), \text{start}_B\Big)$$

---

## Acceptance Criteria

- [ ] Sub-second cues ($< 1.0\,\text{s}$, e.g. "Yeah", "Right") display for at least $1.0\,\text{s}$ on screen in standard playback.
- [ ] Consecutive dialogue cues never visually collide or overlap; short cues extend only up to the start of the next cue.
- [ ] Long speech utterances ($> 1.0\,\text{s}$) display for their full authentic acoustic duration.
- [ ] Non-1.0 playback rates ($0.5\times$, $2.0\times$) scale subtitle display duration and lead time accurately.
- [ ] Whisper decoding executes strictly in a single pass (`temperature_inc = 0.0f`), eliminating multi-pass latency spikes.
- [ ] `no_context = true` prevents previous window hallucinations from contaminating subsequent audio chunks.
- [ ] `suppress_nst = true`, `suppress_blank = true`, and `entropy_thold = 2.40f` suppress non-speech tokens and repetition loops.
- [ ] 100% of automated unit tests pass across Linux and Windows MinGW targets.
- [ ] Valgrind memcheck confirms zero memory leaks.

---

## Test Plan

1. **Native Build & Test Run**:
   ```bash
   cmake --preset linux-x64-debug
   cmake --build --preset linux-x64-debug
   ctest --preset linux-x64-debug --output-on-failure
   ```
2. **Valgrind Memory Leak Verification**:
   ```bash
   ctest --test-dir build/linux-x64-debug -T memcheck
   ```
3. **Windows MinGW Cross-Compilation**:
   ```bash
   cmake --preset windows-x64-debug
   cmake --build --preset windows-x64-debug
   ```
4. **Code Style Verification**:
   ```bash
   clang-format --dry-run --Werror plugin/src/vw_caption_presenter.c worker/src/vw_whisper_engine.c worker/src/vw_segment_builder.c tests/unit/test_caption_presenter.c tests/unit/test_whisper_engine.c tests/unit/test_segment_builder.c
   ```

---

## Definition of Done
- [ ] C17 standard compliant; zero project-authored C++.
- [ ] All functions, types, and macros namespaced with `vw_`.
- [ ] Realtime VLC callback safety preserved (zero heap allocations or blocking locks).
- [ ] 20–30 words docstrings in all updated `.h` header files (Rule 11).
- [ ] Documentation updated in `docs/architecture.md`, `docs/decisions.md` (ADR-021), `docs/source-layout.md`, `docs/test-strategy.md`, and `docs/roadmap.md` (Rule 14).
- [ ] Clean Conventional Commit messages (Rule 12).
