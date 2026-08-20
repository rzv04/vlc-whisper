# Implementation Plan: Step 17e.2 — Subtitle Pacing, Minimum Reading Floor & Decoding Optimization

# Task: Subtitle Pacing, Minimum Reading Floor & Decoding Optimization

## Goal
Eliminate unreadable sub-second "flash cues" by enforcing a **wall-clock minimum subtitle display floor** ($\ge 1.0\,\text{s}$) in the SPU caption presenter, with consecutive-cue clamping so short cues extend only up to the next known cue's start, and pin the whisper.cpp decoding configuration to **deterministic greedy inference with bounded temperature fallback** — stable acoustic output without latency explosions or silent caption drops.

---

## Context
- **Relevant Docs/ADRs**:
  - `docs/decisions.md` (`ADR-016` Native SPU Pipeline, `ADR-017` Phrase-by-Phrase Subtitle Timing, `ADR-018` Whole-Phrase Deduplication, `ADR-019` Multi-Tier VAD, `ADR-020` No-Hop Lookahead Chunking, and new `ADR-021`).
  - `docs/architecture.md`, `docs/api-contracts.md`, `docs/source-layout.md`, `docs/test-strategy.md`, `docs/roadmap.md`.
- **VLC/Worker/Protocol Version Affected**:
  - Protocol v1.2 (framing and wire timestamps unchanged — pacing is presentation-side only).
- **Review grounding**: this plan incorporates the Step 17e.2 review objections O1–O9 (see "Review Objections & Resolutions"); the engine section was verified against the vendored `whisper.cpp` (`worker/third_party/whisper.cpp/src/whisper.cpp`) defaults and the current `vw_whisper_engine.c`.
- **Assumptions & Explicit Non-Goals**:
  - *Non-goal*: Multi-pass temperature sampling as the default path (retained only as a *bounded* fallback for degenerate sequences, per roadmap 17e.2 "temperature fallback").
  - *Non-goal*: Dynamic character-level text wrapping or font resizing (handled natively by VLC SPU renderer).
  - *Non-goal*: Modification of acoustic coverage tracking or segment emission in `vw_segment_builder` (acoustic coverage remains true to audio boundaries; pacing is a presentation concern with a single owner — the presenter).
  - *Non-goal*: Any change to `temperature_inc = 0.0` single-pass mode (it would convert whisper's entropy retry into a silent segment drop — see O2/O3).

---

## Scope

### In Scope
1. **Minimum Subtitle Display Duration Floor (presenter-owned)** in `plugin/src/vw_caption_presenter.c` + `plugin/include/vw_caption_presenter.h`:
   - Define `VW_CAPTION_MIN_DISPLAY_DURATION_US = 1000000LL` (plugin tree only — single owner).
   - Wall-clock floor, rate-scaled: `duration_us = max(duration_us, (int64_t)(VW_CAPTION_MIN_DISPLAY_DURATION_US * rate))` before the existing `/rate` wall-clock conversion, so a cue displays **at least 1.0 s of wall time at any playback rate** (0.5× → 0.5 s media floor, 2.0× → 2.0 s media floor).
   - Consecutive-cue clamping at schedule time: when the next cue is already queued, clamp `i_stop = min(i_start + floor_duration, next_scheduled_i_start)`. A cue arriving *after* the current one was scheduled replaces it (VLC SPU newest-wins) — the floor is best-effort for cues whose successor is not yet known; documented behavior, test-pinned.
   - OSD fallback path: apply the same floor (`max(dur, 1.0 s)`; rate scaling is N/A on the OSD path).
2. **Deterministic Whisper Decoding Configuration** (`worker/src/vw_whisper_engine.c`) — the *actual delta* plus explicit verify/keep, not a 14-param reconfiguration (O1):
   - **Changed/explicit** (only real functional line):
     - `wparams.temperature_inc = 0.2f;` — explicit bounded fallback (O2/O3): whisper builds the temperature ladder `[0.0, 0.2, …, <1.0]` (≤ 5 passes, hardcoded cap at `1.0f + 1e-6f`), each pass runs a single greedy decoder (`greedy.best_of = -1` → `n_decoders = 1`). The retry fires only when `entropy < 2.4` on a >32-token degenerate sequence — the anti-repetition mechanism keeps its *retry* semantics and cannot silently drop captions (O6).
   - **Verify already set (17e.1 code)**: `strategy = WHISPER_SAMPLING_GREEDY`, `temperature = 0.0f`, `no_speech_thold = 0.60f`, `suppress_nst = true`, `suppress_blank = true`, `logprob_thold = -1.0f`.
   - **Explicit vendored defaults (set with a comment for self-documentation; no behavior change)**: `no_context = true` (correct rationale — see O7), `entropy_thold = 2.4f`, `max_len = 0`, `single_segment = false`, `print_special = false`, `token_timestamps = false`.
   - **Determinism claim (corrected)**: greedy token selection is argmax (no RNG in token choice; `std::mt19937` only seeds decoders and is unused for greedy) → the same audio window yields identical segments *with or without* the retry ladder. Determinism does not require `temperature_inc = 0.0`.
3. **Comprehensive Unit & Integration Test Suites**:
   - Presenter tests in `tests/unit/test_caption_presenter.c`: 1.0 s wall floor at rates $0.5\times$, $1.0\times$, $2.0\times$; long-cue full acoustic duration; consecutive-cue clamp (cue B queued while A scheduled → A's `i_stop` clamped to B's `i_start`); later-arrival replacement (B replaces A; no visual overlap); OSD fallback floor; existing lead-pacing tests.
   - Engine tests in `tests/unit/test_whisper_engine.c`: determinism (same PCM → identical segments across runs); bounded retry on a degenerate repetitive input (assert pass count ≤ 5 and no silent drop — pins the entropy gate semantics, O3/O9); no-context within-window isolation.
4. **Documentation & ADR-021**:
   - Document **ADR-021** (Subtitle Reading Floor & Deterministic Whisper Decoding) in `docs/decisions.md`.
   - Update `docs/architecture.md`, `docs/source-layout.md`, `docs/test-strategy.md`, and `docs/roadmap.md`.

### Out of Scope
- Font styling, color selection, or user subtitle positioning preferences (deferred to Milestone 4 settings GUI).
- Beam search decoding (too heavy for real-time streaming).
- Builder-side pacing (`vw_segment_builder` unchanged — see O5: single owner in the presenter).

### Files Expected to Change
- `plugin/include/vw_caption_presenter.h`
- `plugin/src/vw_caption_presenter.c`
- `worker/include/vw_whisper_engine.h` (docstrings only, Rule 11)
- `worker/src/vw_whisper_engine.c`
- `tests/unit/test_caption_presenter.c`
- `tests/unit/test_whisper_engine.c`
- `docs/decisions.md`
- `docs/architecture.md`
- `docs/source-layout.md`
- `docs/test-strategy.md`
- `docs/roadmap.md`

---

## Design

### 1. Single-Owner Duration Pacing Model (presenter)

```text
[Whisper Engine]  (deterministic greedy + bounded retry)
       │
       ▼ (discrete phrase: t0, t1 — acoustic, untouched)
[vw_segment_builder]  (unchanged: coverage/dedup/emit, emit_start clamped at covered_end)
       │
       ▼ (IPC: VW_MSG_CAPTION_SEGMENT, v1.2 framing)
[vw_caption_presenter]  ──> SOLE pacing owner:
       │                   1. wall floor: dur = max(dur, 1.0 s × rate)
       │                   2. next-cue clamp: i_stop = min(i_stop, next_i_start)
       ▼ (Rate Scaling & SPU Scheduling)
[VLC SPU Pipeline (vout_PutSubpicture)]
  i_start = now_tick + (start_pts - input_time) / rate
  i_stop  = i_start + dur_wall
```

### 2. Pacing Formulas (presenter)

1. **Wall-clock floor (rate-scaled)**:
   $$\text{duration\_us} = \max\big(\text{raw\_dur},\ \lfloor 1\,000\,000 \times \text{rate} \rfloor\big),\qquad \text{dur\_wall} = \frac{\text{duration\_us}}{\text{rate}} \ge 1\,000\,000\,\mu\text{s}$$
   (existing `raw_dur ≤ 0 → 2\,000\,000` default retained). At $0.5\times$: floor $= 0.5\,\text{s}$ media → $1.0\,\text{s}$ wall. At $2.0\times$: floor $= 2.0\,\text{s}$ media → $1.0\,\text{s}$ wall.
2. **Consecutive-cue clamp** (only cues already queued at scheduling time):
   $$\text{i\_stop}_A = \min\Big(\text{i\_start}_A + \text{dur\_wall}_A,\ \text{i\_start}_{B}\Big)$$
   where $B$ is the earliest queued successor. If $B$ arrives after $A$ is scheduled, $B$ replaces $A$ (SPU newest-wins) — no visual overlap ever; the floor is best-effort across window boundaries (successor unknown), documented in ADR-021.

---

## Review Objections & Resolutions

| # | Objection | Resolution |
| --- | --- | --- |
| O1 | Engine table is ~90% no-op (only `temperature_inc` differed) | §Scope.2 rewritten as the true delta + verify/keep + explicit-default lists |
| O2 | `temperature_inc = 0.0` contradicts roadmap 17e.2 "temperature fallback" | Fallback retained, explicit `0.2f`, bounded ≤ 5 passes |
| O3 | `entropy_thold = 2.4` with single pass silently *drops* degenerate segments instead of retrying | Retry ladder kept → entropy gate keeps retry semantics; test pins pass count and no-drop (O9) |
| O4 | 1.0 s floor was media-time: 0.5 s wall at 2× — flash cue survives | Wall-clock floor `1.0 s × rate` (§Design.2 formula 1); rate tests at 0.5×/1.0×/2.0× |
| O5 | Two conflicting pacing layers; no owner of the collision constraint; window-boundary seams | Single owner: presenter. Builder untouched; presenter clamps at the next known cue (§Design.2 formula 2); later arrivals replace |
| O6 | Single-pass + `best_of=-1` removed the only in-window recovery | n_decoders = 1 retained, but the temperature ladder is the recovery path; never a silent drop |
| O7 | `no_context` rationale false (no cross-call carryover exists) | Rationale corrected: within-window segment-to-segment conditioning disabled; per-call isolation already inherent to `whisper_full` |
| O8 | `logprob_thold = -1.0` described as disabled; it's a weak real gate | Described correctly: drops segments with `avg_logprobs < -1.0` *and* `no_speech_prob < 0.60` (catastrophic-confidence) |
| O9 | Tests missed the entropy gate and collision seams; constant shared across processes | Added degenerate-input retry test + presenter collision test; constant lives in the plugin tree only (single owner) |

---

## Acceptance Criteria

- [ ] Sub-second cues ($< 1.0\,\text{s}$ wall, e.g. "Yeah", "Right") display for at least $1.0\,\text{s}$ **wall time** at $0.5\times$, $1.0\times$, and $2.0\times$ playback.
- [ ] Consecutive dialogue cues never visually collide or overlap: a short cue extends only up to the start of the next **already-known** cue; a later-arriving cue replaces the current one.
- [ ] Long speech utterances ($> 1.0\,\text{s}$) display for their full authentic acoustic duration (scaled by rate).
- [ ] Whisper decoding is deterministic for a given audio window (greedy argmax), and the temperature fallback is bounded (≤ 5 passes, only on low-entropy degenerate sequences).
- [ ] `no_context = true` isolates phrases within a window; `suppress_nst`/`suppress_blank` and the entropy gate suppress non-speech tokens and repetition loops **without silently dropping captions**.
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
   clang-format --dry-run --Werror plugin/src/vw_caption_presenter.c worker/src/vw_whisper_engine.c tests/unit/test_caption_presenter.c tests/unit/test_whisper_engine.c
   ```

---

## Definition of Done
- [ ] C17 standard compliant; zero project-authored C++.
- [ ] All functions, types, and macros namespaced with `vw_`.
- [ ] Realtime VLC callback safety preserved (zero heap allocations or blocking locks).
- [ ] 20–30 words docstrings in all updated `.h` header files (Rule 11).
- [ ] Documentation updated in `docs/architecture.md`, `docs/decisions.md` (ADR-021), `docs/source-layout.md`, `docs/test-strategy.md`, and `docs/roadmap.md` (Rule 14).
- [ ] Clean Conventional Commit messages (Rule 12).
