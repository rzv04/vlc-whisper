# Part 3 — Step 17d.1: Phrase-by-Phrase Subtitle Timing & Segmentation (vs gemini/milestone-3)

**Target**: Step 17d.1 — Phrase-by-Phrase Subtitle Timing, Overlap Deduplication & `is_final` State Handling (`ADR-017`)  
**Base**: `gemini/milestone-3` (PR #12 merged, `038f152`); branch `gemini/milestone-3-step-17d-1`

---

## 1. File-by-File Analysis

### 1.1 `worker/include/vw_whisper_engine.h` / `worker/src/vw_whisper_engine.c`
- **Why change**: Replaces monolithic 8-second window string concatenation with fine-grained sub-segment iterators directly exposing `whisper.cpp` segment table offsets ($t_0, t_1$ in centiseconds).
- **Structures & Functions Added**:
  - `vw_whisper_segment_t { int64_t t0_us; int64_t t1_us; const char* text_utf8; }`: Holds segment microsecond PTS offsets and borrowed UTF-8 string pointer.
  - `vw_whisper_engine_get_segment_count()`: Queries `whisper_full_n_segments(engine->ctx)` safely (returns 0 on NULL or silence).
  - `vw_whisper_engine_get_segment()`: Populates out segment struct with `t0 * 10000LL` and `t1 * 10000LL` microsecond conversions and bounds validation.
- **Rule 11 & C17 Compliance**: Full 20–30 word function docstrings and `-std=c17` compliance.

### 1.2 `worker/include/vw_segment_builder.h` / `worker/src/vw_segment_builder.c`
- **Why change**: Fixes historical deduplication flaw where `vw_segment_builder_pop()` erased queue history before subsequent overlapping hops ran. Emits individual phrases as immutable `is_final = true` cues.
- **Architectural Enhancements**:
  - **Decoupled Dual-Buffer**: Decouples 20-slot `segment_queue` (pending output FIFO) from a 16-slot `history` ring buffer (`vw_history_entry_t`).
  - **Cross-Hop Overlap Deduplication**: Incoming hypotheses are checked against recent history using `VW_DEDUP_TIME_TOLERANCE_US = 500000LL` (500ms) timestamp proximity matching, exact text matching, substring containment, and prefix overlap trimming.
  - **`is_final` State Machine**: Committed phrases are flagged `is_final = true`.
  - **Reset API**: `vw_segment_builder_clear()` resets both output FIFO and history ring buffer on seek, pause, and session transitions.

### 1.3 `worker/src/vw_worker.c`
- **Why change**: Integrates per-phrase extraction into the worker inference loops.
- **Mechanics**:
  - Replaces coarse single-string emission in live PCM, lookahead source decoding, and trailing EOF flush loops with iterative calls to `vw_whisper_engine_get_segment_count()` and `vw_whisper_engine_get_segment()`.
  - Replaces manual while-pop loops with `vw_segment_builder_clear(builder)` on `START_SESSION`, `POSITION(SEEK)`, `PAUSE`, and `STOP_SESSION`.

### 1.4 `tests/unit/test_whisper_engine.c`
- **Why change**: Unit tests for sub-segment accessor NULL-safety, index bounds validation, and microsecond conversion monotonicity ($t_0 \le t_1$).

### 1.5 `tests/unit/test_segment_builder.c`
- **Why change**: Comprehensive unit tests covering:
  - Multi-phrase push per window (3 distinct phrases in 8s window).
  - Cross-hop deduplication with history persistence across `pop()` calls.
  - Silence gap preservation (0.6s gap verified).
  - Circular buffer wrapping and `is_final = true` flag verification.
  - `vw_segment_builder_clear()` reset and re-push validation.

### 1.6 `tests/unit/test_caption_presenter.c`
- **Why change**: Adds Test 14 verifying discrete SPU subpicture scheduling with non-overlapping display ticks and exact 0.6s screen blanking duration between phrases.

---

## 2. Verification Summary
- **Code Style**: `clang-format --dry-run --Werror` passes cleanly on all modified files.
- **Native Test Suites**:
  - `linux-x64-debug`: 18/18 tests passed (100%).
  - `linux-x64-debug-cpu`: 18/18 tests passed (100%).
- **Valgrind Memcheck**: 0 memory errors, 0 definitely lost bytes across all suites.
- **Documentation**: All architectural docs, API contracts, source layouts, test strategies, and roadmaps updated in the same change (Rule 14).
