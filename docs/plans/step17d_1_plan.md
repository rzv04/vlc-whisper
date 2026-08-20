# Implementation Task Template & Plan

# Task: Step 17d.1 — Phrase-by-Phrase Subtitle Timing & Segmentation & 'final' Status Handling

## Goal
Replace coarse 8.0-second window aggregation with fine-grained phrase-by-phrase subtitle timing and segmentation (`ADR-017`), extracting individual sub-segment timestamps ($t_0, t_1$ in centiseconds) directly from `whisper.cpp`, converting to microsecond media presentation timestamps (`pts_us`), and utilizing an overlap-resilient history-backed `vw_segment_builder` to emit discrete, properly finalized (`is_final = true`) caption segments over IPC. This eliminates premature dialogue spoilers, clutter, and text lingering during conversational pauses, achieving Daum PotPlayer / Netflix-style synchronized subtitle cadence with automatic screen blanking during silence.

---

## Context
- **Relevant Documentation & ADRs**:
  - `docs/plans/phrase_timing_segmentation_plan.md`: Design rationale, comparison matrix, and Whisper API mapping.
  - `docs/plans/milestone3_postmortem.md`: SPU clock domain lessons (OSD domain `b_subtitle = false`, `render_osd_date = mdate()`), subpicture construction invariants.
  - `docs/architecture.md`: Section 4 (Timeline & SPU Subpicture Subsystem), Section 5 (Look-Ahead Source Demuxing).
  - `docs/api-contracts.md`: Protocol v1.2 `VW_MSG_CAPTION_SEGMENT` specification and `is_final` semantics.
  - `docs/whisper-api.md`: `whisper_full_n_segments()`, `whisper_full_get_segment_t0/t1()`, `whisper_full_get_segment_text()`.
- **Target OS / Build Presets**:
  - Linux (`linux-x64-debug`, `linux-x64-debug-cpu`, GCC/Clang, POSIX sockets, FFmpeg demuxer).
  - Windows (`windows-x64-debug`, MinGW-w64, Win32 Named Pipes, Media Foundation demuxer).
- **Assumptions and Non-Goals**:
  - Authored code remains strictly standard C17 (`-std=c17`). Third-party `whisper.cpp` is linked exclusively via public `whisper.h`.
  - Non-goal: Word-level karaoke token animations (retained for future milestone; Step 17d.1 targets phrase-level cues).
  - Non-goal: UI configuration dialogs or subtitle styling menus (styling remains bottom-center native text).

---

## Scope

### In Scope
1. **Whisper Engine Sub-Segment Iteration (`worker/include/vw_whisper_engine.h`, `worker/src/vw_whisper_engine.c`)**:
   - Expose `vw_whisper_segment_t { int64_t t0_us; int64_t t1_us; const char* text_utf8; }`.
   - Implement `vw_whisper_engine_get_segment_count()` and `vw_whisper_engine_get_segment()`.
   - Scale Whisper centiseconds ($10\,\text{ms}$) to microsecond PTS offsets using `10000LL`.
   - Filter special tokens (`print_special = false`) and skip empty/whitespace-only segment tails.
2. **History-Backed Segment Builder & Finalization State Machine (`worker/include/vw_segment_builder.h`, `worker/src/vw_segment_builder.c`)**:
   - Decouple **Pending Output Queue** (`output_queue[20]`) from **Committed History Buffer** (`history[16]`).
   - Implement phrase deduplication across overlapping $2.0\text{s}$ hops using $500\,\text{ms}$ timestamp proximity matching ($\Delta\text{start} \le 500\,000\,\mu\text{s}$) and substring/equality checks.
   - Invariant for `is_final` status:
     - Phrases starting within the current hop slice ($t_{\text{start}} < T_k + H$) are finalized as `is_final = true` because their acoustic onset will be permanently drained before the next inference window.
     - Lookahead phrases in source mode are emitted as discrete final cues with exact start/stop horizons.
     - EOF / trailing flush finalizes all remaining in-flight phrases.
   - Implement `vw_segment_builder_clear()` to reset output queues and history buffers on seek, pause, and session restart.
3. **Worker Inference Loop Integration (`worker/src/vw_worker.c`)**:
   - Replace single-string concatenation with per-segment hypothesis loop across both live PCM and lookahead decoding branches.
   - Clear segment builder and purge stale hypotheses on seek (`POSITION(SEEK)`), pause (`PAUSE`), and media swap (`START_SESSION`).
4. **Presenter Verification & Discrete Cue Scheduling (`plugin/src/vw_caption_presenter.c`)**:
   - Verify discrete SPU cue scheduling (`i_start = mdate() + lead_us`, `i_stop = i_start + dur_us / rate`).
   - Verify screen blanking during silence intervals between non-contiguous phrases.
5. **Comprehensive Unit & Integration Test Suite**:
   - `tests/unit/test_whisper_engine.c`: segment count, bounds checking, centisecond-to-microsecond scaling.
   - `tests/unit/test_segment_builder.c`: multi-phrase per window, overlapping hop deduplication, history persistence across `pop()`, silence gap preservation, `is_final` flag, seek reset.
   - `tests/unit/test_caption_presenter.c`: discrete SPU scheduling, silence interval blanking, playback rate scaling.
   - `tests/integration/test_worker_lifecycle.c`: integration test verifying discrete `CAPTION_SEGMENT` emissions.
6. **Documentation Updates (Rule 14)**:
   - `docs/architecture.md`, `docs/api-contracts.md`, `docs/source-layout.md`, `docs/test-strategy.md`, `docs/roadmap.md`, and `diff.md`.

### Out of Scope
- Word-level token highlighting or word-by-word streaming rendering.
- Re-encoding audio or modifying VLC playback timeline.

---

## Detailed Design & State Transitions

### 1. Whisper Segment Extraction (`vw_whisper_engine`)

```c
typedef struct vw_whisper_segment {
  int64_t t0_us;          // Offset in microseconds from window start (whisper_full_get_segment_t0 * 10000LL)
  int64_t t1_us;          // Offset in microseconds from window start (whisper_full_get_segment_t1 * 10000LL)
  const char* text_utf8;  // Borrowed pointer to UTF-8 text (valid until next transcribe or engine_free)
} vw_whisper_segment_t;

int vw_whisper_engine_get_segment_count(const vw_whisper_engine_t* engine);
bool vw_whisper_engine_get_segment(const vw_whisper_engine_t* engine, int index, vw_whisper_segment_t* out_seg);
```

### 2. Segment Builder Dual-Buffer Architecture (`vw_segment_builder`)

```
                          Whisper Sub-Segments (t0, t1, text)
                                         │
                                         ▼
                     ┌───────────────────────────────────────┐
                     │   vw_segment_builder_push_hypothesis  │
                     └───────────────────┬───────────────────┘
                                         │
                   ┌─────────────────────┴─────────────────────┐
                   │ Deduplication Check vs Committed History  │
                   │ (Δstart ≤ 500ms OR substring inclusion)   │
                   └─────────────────────┬─────────────────────┘
                                         │
                     ┌───────────────────┴───────────────────┐
       Duplicate? ───► [ DROP / SUPPRESS ]                   │ Unique?
                                                             ▼
                                             ┌───────────────────────────────┐
                                             │ Commit to History Buffer (16) │
                                             └───────────────┬───────────────┘
                                                             │
                                                             ▼
                                             ┌───────────────────────────────┐
                                             │ Enqueue to Output Queue (20)  │
                                             │ (is_final = true)             │
                                             └───────────────┬───────────────┘
                                                             │
                                                             ▼
                                             ┌───────────────────────────────┐
                                             │  vw_segment_builder_pop()     │
                                             │  (Transfers text ownership    │
                                             │   for IPC frame transmission) │
                                             └───────────────────────────────┘
```

### 3. Invariant for `is_final` Status
- In wire protocol: `is_final == true` signals an immutable subtitle cue that VLC can schedule on the SPU channel.
- Because the audio window hops forward by $H = 2.0\text{s}$ every iteration, audio in $[T_k, T_k + H]$ is permanently drained.
- Any detected phrase starting in the drained region ($t_{\text{start}} < T_k + H$) has complete onset context and will not be re-evaluated with full onset; it is emitted with `is_final = true`.
- In look-ahead source mode, every emitted phrase ahead of playback is finalized (`is_final = true`), allowing SPU subpictures to be scheduled cleanly into future presentation ticks.
- In `vw_segment_builder`, `is_final` is set to `true` for all committed phrases.

---

## Files and Components Expected to Change

| File | Type | Description |
|---|---|---|
| `worker/include/vw_whisper_engine.h` | Header | Add `vw_whisper_segment_t`, `vw_whisper_engine_get_segment_count()`, `vw_whisper_engine_get_segment()` |
| `worker/src/vw_whisper_engine.c` | Source | Implement segment count and getter with `10000LL` conversion |
| `worker/include/vw_segment_builder.h` | Header | Add history buffer structures, `VW_DEDUP_TIME_TOLERANCE_US`, `vw_segment_builder_clear()` |
| `worker/src/vw_segment_builder.c` | Source | Implement dual-buffer history deduplication, multi-phrase support, `is_final = true` |
| `worker/src/vw_worker.c` | Source | Update inference loop to push discrete phrases, clear builder on seek/pause/start |
| `tests/unit/test_whisper_engine.c` | Test | Unit tests for segment accessor bounds, timestamps, and scaling |
| `tests/unit/test_segment_builder.c` | Test | Unit tests for multi-phrase push, hop deduplication, history persistence, silence gap |
| `tests/unit/test_caption_presenter.c` | Test | Unit tests for discrete SPU scheduling, silence interval blanking, rate scaling |
| `tests/integration/test_worker_lifecycle.c` | Test | Integration test for discrete `CAPTION_SEGMENT` emission |
| `docs/architecture.md` | Docs | Document phrase-level segmentation and SPU timeline scheduling |
| `docs/api-contracts.md` | Docs | Update caption segment lifecycle and `is_final` invariants |
| `docs/source-layout.md` | Docs | Note updated engine and builder APIs |
| `docs/test-strategy.md` | Docs | Add Step 17d.1 automated test coverage breakdown |
| `docs/roadmap.md` | Docs | Mark Step 17d.1 complete |
| `diff.md` | Review | Document 8-point diff review for Step 17d.1 |

---

## Step-by-Step Implementation Tasks

### Task 1: Extend `vw_whisper_engine` API with Sub-Segment Iterators
1. In `worker/include/vw_whisper_engine.h`:
   - Define `vw_whisper_segment_t` struct with `t0_us`, `t1_us`, and `const char* text_utf8`.
   - Declare `vw_whisper_engine_get_segment_count()` and `vw_whisper_engine_get_segment()`.
   - Add 20–30 word Rule 11 doc comments for all new functions.
2. In `worker/src/vw_whisper_engine.c`:
   - Implement `vw_whisper_engine_get_segment_count()` using `whisper_full_n_segments(engine->ctx)`.
   - Implement `vw_whisper_engine_get_segment()` using `whisper_full_get_segment_t0/t1/text` with `10000LL` conversion and bounds validation.

### Task 2: Implement History-Backed Overlap Deduplication & `is_final` in `vw_segment_builder`
1. In `worker/include/vw_segment_builder.h`:
   - Define `VW_SEGMENT_HISTORY_CAPACITY 16` and `VW_DEDUP_TIME_TOLERANCE_US 500000LL`.
   - Define `vw_history_entry_t` struct and update `vw_segment_builder_t` to hold `history` array.
   - Declare `void vw_segment_builder_clear(vw_segment_builder_t* builder)`.
2. In `worker/src/vw_segment_builder.c`:
   - Rewrite `vw_segment_builder_push_hypothesis` to match incoming phrases against `history` ring buffer entries using $\Delta\text{start} \le 500\,000\,\mu\text{s}$ or span overlap and substring containment.
   - Ensure committed phrases set `is_final = true` and copy into both `history` and `output_queue`.
   - Implement `vw_segment_builder_clear()` to purge both queues on seek/reset.

### Task 3: Integrate Phrase Extraction into Worker Inference Loop
1. In `worker/src/vw_worker.c`:
   - In both live PCM processing and lookahead decoding branches:
     - Query `vw_whisper_engine_get_segment_count()`.
     - Iterate through segments, trim leading whitespace, compute `seg_start_pts = window_pts_us + s.t0_us` and `seg_end_pts = window_pts_us + s.t1_us`, and push to builder.
   - In `POSITION`, `PAUSE`, and `START_SESSION` handlers, invoke `vw_segment_builder_clear(builder)`.

### Task 4: Enhance Unit & Integration Test Suites
1. In `tests/unit/test_whisper_engine.c`:
   - Add unit tests verifying `vw_whisper_engine_get_segment_count` and `vw_whisper_engine_get_segment` bounds, NULL-safety, and timestamp scaling.
2. In `tests/unit/test_segment_builder.c`:
   - Add comprehensive tests: multi-phrase push, hop deduplication across overlapping windows, history persistence across `pop()`, silence gap preservation, `is_final` validation, and clear/reset.
3. In `tests/unit/test_caption_presenter.c`:
   - Add unit tests verifying discrete SPU subpicture scheduling with silence gap blanking and playback rate scaling ($0.5\times$, $1.0\times$, $2.0\times$).
4. In `tests/integration/test_worker_lifecycle.c`:
   - Verify multi-phrase `CAPTION_SEGMENT` emission over IPC.

### Task 5: Documentation Updates & Final Verification
1. Update `docs/architecture.md`, `docs/api-contracts.md`, `docs/source-layout.md`, `docs/test-strategy.md`, `docs/roadmap.md`, and `diff.md`.
2. Verify `clang-format --dry-run --Werror` on all authored files.
3. Verify 100% pass rate on `linux-x64-debug` and `linux-x64-debug-cpu` presets (`-j1`).
4. Verify 0 memory leaks under Valgrind memcheck (`ctest -T memcheck`).

---

## Acceptance Criteria Checklist

- [ ] `vw_whisper_engine` exports `get_segment_count()` and `get_segment()` with microsecond conversion (`10000LL`).
- [ ] `vw_segment_builder` maintains a history ring buffer (capacity 16) that persists across `pop()` calls.
- [ ] Overlapping 2.0s hops deduplicate identical or substring phrases within $500\,\text{ms}$ tolerance.
- [ ] All emitted caption segments have `is_final = true` and discrete `[start_pts_us, end_pts_us]` spans.
- [ ] Silence gaps (e.g. 0.6s pause) create non-overlapping subpictures that automatically blank the screen.
- [ ] Variable playback rates ($0.5\times$ to $16.0\times$) scale subpicture lead and duration proportionally.
- [ ] Seek, pause, and media swap cleanly flush active subpictures and clear the builder history.
- [ ] All unit and integration tests pass (18/18+ tests) across all build presets with 0 Valgrind memory leaks.
- [ ] All project documentation updated in the same change (Rule 14).

---

## Definition of Done (Rule 10 & Rule 14)
- [ ] **Standard C17**: No project-authored C++ code.
- [ ] **Realtime Safety**: Zero heap allocations or blocking calls in VLC audio callbacks.
- [ ] **Rule 11 Compliance**: 20–30 word documentation comments on all header functions.
- [ ] **Rule 12 Compliance**: Conventional Commits standard.
- [ ] **Rule 14 Compliance**: Documentation updated in `docs/architecture.md`, `docs/api-contracts.md`, `docs/source-layout.md`, `docs/test-strategy.md`, `docs/roadmap.md`, and `diff.md`.
- [ ] **Clang-Format**: Clean verification (`clang-format --dry-run --Werror`).
- [ ] **Native Builds & Tests**: Clean build and pass on `linux-x64-debug` and `linux-x64-debug-cpu`.
- [ ] **Memcheck**: 0 memory errors under Valgrind.
