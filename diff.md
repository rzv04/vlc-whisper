# Diff Analysis: Step 17d.1 (Phrase-by-Phrase Subtitle Timing, ADR-017 & ADR-018 Final Immutable Subtitles)

**23 files changed, 1550 insertions(+), 844 deletions(-)**  
**Base**: `038f152a8b50f3d7bb83e9373fe1c073aeb1ea6f` (Merge commit of Step 17d / PR #12)  
**Head**: `gemini/milestone-3-step-17d-1` (PR #13)

---

## 1. File-by-File Analysis

### 1.1 `worker/include/vw_whisper_engine.h`

**Why change**: Replace coarse 8.0-second whole-window string extraction with fine-grained per-phrase segment iteration from `whisper.cpp` (`ADR-017`).

**Responsibility before**: Exposed only whole-window aggregated text (`vw_whisper_engine_get_text`).  
**Responsibility after**: Exposes discrete segment accessors (`vw_whisper_engine_get_segment_count`, `vw_whisper_engine_get_segment`) returning microsecond-scaled media offsets (`vw_whisper_segment_t`).

**Callers**: `worker/src/vw_worker.c` (lines 530–539, 631–640, 661–670), `tests/unit/test_whisper_engine.c`.  
**Callees**: Pure header declarations; implemented by `worker/src/vw_whisper_engine.c`.

**Happy path**:
1. `vw_worker.c:530` calls `vw_whisper_engine_get_segment_count(engine)` returning $N > 0$.
2. Loop iterates `s_idx` from $0$ to $N-1$, calling `vw_whisper_engine_get_segment(engine, s_idx, &seg)`.
3. Populates `seg.t0_us`, `seg.t1_us`, and `seg.text_utf8`.

**Failure path**:
1. Invalid index (`index < 0` or `index >= count`) or `NULL` engine passed to `vw_whisper_engine_get_segment`.
2. Function returns `false` immediately without mutating `*out_seg`.

**Boundaries**:
- *Input validation*: Validates non-null `engine`, non-null `out_seg`, and `0 <= index < n_segments`.
- *Authorization*: N/A (in-process library API).
- *Concurrency*: Thread-confined to worker main thread.
- *I/O*: Pure memory read from `whisper_context`.
- *Persistence*: None.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|-----------|------|------|--------|
| 1 | Microsecond timestamp conversion ($t \times 10\,000LL$) | `vw_whisper_engine.h:20-24` | `test_whisper_engine.c:26-30` | ✅ done |
| 2 | Out-of-bounds index rejection | `vw_whisper_engine.h:28-33` | `test_whisper_engine.c:32-35` | ✅ done |

**Assumptions/Tradeoffs**: Borrowed string pointer `seg.text_utf8` remains valid until next `whisper_full` or engine destruction.

---

### 1.2 `worker/src/vw_whisper_engine.c`

**Why change**: Implement the segment query methods backed by `whisper.cpp` C API (`whisper_full_n_segments`, `whisper_full_get_segment_t0/t1/text`).

**Responsibility before**: Managed `whisper_context`, executed inference, and retrieved concatenated window text.  
**Responsibility after**: Owns segment count query and segment struct population with microsecond scaling (`10000LL`).

**Callers**: `worker/src/vw_worker.c`, `tests/unit/test_whisper_engine.c`.  
**Callees**: `whisper_full_n_segments`, `whisper_full_get_segment_t0`, `whisper_full_get_segment_t1`, `whisper_full_get_segment_text`.

**Happy path**:
1. Worker calls `vw_whisper_engine_get_segment(engine, 0, &seg)` (`vw_whisper_engine.c:135`).
2. Reads `whisper_full_get_segment_t0(engine->ctx, 0)` ($25\,\text{cs}$), multiplies by `10000LL` $\to 250\,000\,\mu\text{s}$.
3. Reads `whisper_full_get_segment_t1(engine->ctx, 0)` ($180\,\text{cs}$), multiplies by `10000LL` $\to 1\,800\,000\,\mu\text{s}$.
4. Reads text pointer via `whisper_full_get_segment_text()` and returns `true`.

**Failure path**:
1. `engine == NULL` or `engine->ctx == NULL` passed (`vw_whisper_engine.c:129`).
2. Returns `0` for count or `false` for segment lookup.

**Boundaries**:
- *Input validation*: Checks `engine != NULL`, `engine->ctx != NULL`, `index >= 0`, `index < n_segments`.
- *Authorization*: N/A.
- *Concurrency*: Single-threaded invocation by worker loop.
- *I/O*: None.
- *Persistence*: None.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|-----------|------|------|--------|
| 1 | Safe query on NULL or uninitialized engine | `vw_whisper_engine.c:128-132` | `test_whisper_engine.c:20-24` | ✅ done |
| 2 | Centisecond-to-microsecond conversion | `vw_whisper_engine.c:139-140` | `test_whisper_engine.c:28-30` | ✅ done |

**Assumptions/Tradeoffs**: Relies on `whisper.cpp` returning valid UTF-8 strings.

---

### 1.3 `worker/include/vw_segment_builder.h`

**Why change**: Modernize segment builder contracts for discrete phrase deduplication, dynamic output queue growth, committed history persistence across `pop()`, and `ADR-018` final immutable subtitles.

**Responsibility before**: Fixed 20-slot pending queue without committed history; popping drained all dedup state.  
**Responsibility after**: Defines dynamically growable pending queue, 16-slot committed history ring buffer, coverage frontier (`covered_end_us`), and `vw_segment_builder_clear()`.

**Callers**: `worker/src/vw_worker.c`, `tests/unit/test_segment_builder.c`.  
**Callees**: Header declarations; implemented in `vw_segment_builder.c`.

**Happy path**:
1. Worker allocates builder via `vw_segment_builder_create()` (`vw_segment_builder.h:43`).
2. Pushes phrase hypotheses via `vw_segment_builder_push_hypothesis()` (`vw_segment_builder.h:55`).
3. Drains finalized cues via `vw_segment_builder_pop()` (`vw_segment_builder.h:60`).

**Failure path**:
1. Caller passes invalid parameters (`start < 0`, `end <= start`, `text == NULL`).
2. Function returns `false` without state change.

**Boundaries**:
- *Input validation*: Strict timestamp range validation ($0 \le \text{start} < \text{end}$).
- *Authorization*: N/A.
- *Concurrency*: Confined to worker thread.
- *I/O*: None.
- *Persistence*: In-memory ring buffers.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|-----------|------|------|--------|
| 1 | Rule 11 (20-30 word docstrings) | `vw_segment_builder.h:40-62` | `clang-format` | ✅ done |
| 2 | History struct and builder fields | `vw_segment_builder.h:20-36` | `test_segment_builder.c:12-18` | ✅ done |

**Assumptions/Tradeoffs**: Deduplication tolerance is fixed at $500\,\text{ms}$ (`VW_DEDUP_TIME_TOLERANCE_US`).

---

### 1.4 `worker/src/vw_segment_builder.c`

**Why change**: Implement discrete phrase deduplication (`ADR-017`), final immutable subtitle policy (`ADR-018`), time-coverage re-transcription suppression, word-aligned tail trimming, dynamic FIFO buffer growth, and seek reset.

**Responsibility before**: Aggregated monolithic 8s window text, heuristic character overlap trimming, fixed 20-slot buffer.  
**Responsibility after**: Owns whole-phrase deduplication, coverage frontier tracking (`covered_end_us`), tail-prefix trimming, dynamic FIFO queue reallocation, and memory lifecycle.

**Callers**: `worker/src/vw_worker.c`, `tests/unit/test_segment_builder.c`.  
**Callees**: `malloc`, `calloc`, `free`, `memcpy`, `memset`, `strlen`, `strncmp`, `strstr`.

**Happy path**:
1. Candidate `"Where are you from, Victoria?"` ($500\,\text{ms} \to 2800\,\text{ms}$) arrives (`vw_segment_builder.c:227`).
2. Passes validation and coverage checks (`vw_segment_builder.c:256`).
3. Enqueued into `segment_queue` with `is_final = true` (`vw_segment_builder.c:69`).
4. Committed into `history[16]` and updates `covered_end_us = 2800000LL` (`vw_segment_builder.c:114`).
5. In next hop, re-transcription of same phrase at $530\,\text{ms}$ is dropped as time-coverage / exact duplicate (`vw_segment_builder.c:256, 308`).

**Failure path**:
1. Out-of-memory during queue expansion (`vw_segment_builder.c:84`).
2. Frees allocated string copy, preserves existing queue contents, and returns `false` without polluting committed history.

**Boundaries**:
- *Input validation*: Validates non-null text, non-empty trimmed text, length $< 1024$ bytes, valid PTS interval.
- *Authorization*: N/A.
- *Concurrency*: Single-threaded execution inside worker main loop.
- *I/O*: Dynamic heap allocation (`malloc`/`calloc`).
- *Persistence*: Ephemeral in-memory data structures.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|-----------|------|------|--------|
| 1 | Time-coverage re-transcription suppression | `vw_segment_builder.c:256-259` | `test_segment_builder.c:380-410` | ✅ done |
| 2 | Word-aligned tail-prefix trimming | `vw_segment_builder.c:142-225` | `test_segment_builder.c:290-340` | ✅ done |
| 3 | Dynamic queue growth without dropping cues | `vw_segment_builder.c:81-97` | `test_segment_builder.c:71-95` | ✅ done |
| 4 | History persistence across `pop()` | `vw_segment_builder.c:114-130` | `test_segment_builder.c:170-195` | ✅ done |
| 5 | Clear resets queue, history, and frontier | `vw_segment_builder.c:38-56` | `test_segment_builder.c:215-240` | ✅ done |

**Assumptions/Tradeoffs**: Words appearing only in later overlapping expansions of already-finalized cues are omitted rather than modifying previously displayed text (`ADR-018`).

---

### 1.5 `worker/src/vw_worker.c`

**Why change**: Integrate per-phrase Whisper segment extraction into all decoding loops, replace manual queue drain loops with `vw_segment_builder_clear()`, and fix pause-resume playhead re-sync.

**Responsibility before**: Pushed coarse single 8s window text to segment builder; drained queue with manual `while(pop)` loops; failed to re-seek source decoder on resume.  
**Responsibility after**: Iterates over all sub-segments emitted by Whisper in live PCM, lookahead, and trailing EOF loops; calls `vw_segment_builder_clear()` on state transitions; forces source seek on pause-resume.

**Callers**: `worker/src/main.c`, `tests/integration/test_worker_lifecycle.c`, `tests/integration/test_worker_ipc.c`.  
**Callees**: `vw_whisper_engine_get_segment_count`, `vw_whisper_engine_get_segment`, `vw_segment_builder_push_hypothesis`, `vw_segment_builder_clear`, `vw_source_decoder_seek`.

**Happy path**:
1. Whisper decodes audio window (`vw_worker.c:528`).
2. Worker queries `vw_whisper_engine_get_segment_count()` returning 3 phrases (`vw_worker.c:530`).
3. Worker loops over segments, computing `seg_start_pts = window_pts_us + seg.t0_us` and pushes each to `builder` (`vw_worker.c:535–537`).
4. Drains `builder` via `vw_segment_builder_pop()` and transmits `VW_MSG_CAPTION_SEGMENT` frames over IPC.

**Failure path**:
1. Whisper inference fails (`vw_worker.c:543`).
2. Worker logs warning event, skips pushing hypotheses, and continues next loop iteration without crashing.

**Boundaries**:
- *Input validation*: Saturating arithmetic (`vw_saturating_add_i64`) on media PTS timestamps.
- *Authorization*: IPC authentication token verified at startup.
- *Concurrency*: Synchronous worker event loop.
- *I/O*: IPC frame writes and source media reads.
- *Persistence*: None.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|-----------|------|------|--------|
| 1 | Multi-segment per window emission | `vw_worker.c:530-539` | `test_worker_lifecycle.c:150-180` | ✅ done |
| 2 | Pause-resume source decoder seek re-sync | `vw_worker.c:466-474, 565-573` | `test_worker_lifecycle.c:215-225` | ✅ done |
| 3 | Segment builder clearing on seek/pause/stop | `vw_worker.c:465, 498, 557, 589` | `test_worker_lifecycle.c:210-220` | ✅ done |

**Assumptions/Tradeoffs**: Lookahead decoding horizon is bounded to 30 seconds ahead of playback playhead.

---

### 1.6 `plugin/src/vw_platform_win32.c`

**Why change**: Resolve compiler warning for missing `stdio.h` header and eliminate ISO C empty translation unit warning under `-Wpedantic` on Linux.

**Responsibility before**: Win32 platform process/thread wrappers.  
**Responsibility after**: Clean compilation across Linux and Windows without pedantic warnings.

**Callers**: `plugin/src/vw_whisper_module.c`, `plugin/src/vw_worker_client.c`.  
**Callees**: Win32 API (`CreateProcessW`, `CreateThread`, `Sleep`).

**Happy path**:
1. Included during multiplatform compilation; on Linux defines `vw_platform_win32_empty_tu_t`.

**Failure path**: N/A (header inclusion and fallback typedef).

**Boundaries**:
- *Input validation*: N/A.
- *Authorization*: N/A.
- *Concurrency*: N/A.
- *I/O*: N/A.
- *Persistence*: N/A.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|-----------|------|------|--------|
| 1 | ISO C pedantic empty TU suppression | `vw_platform_win32.c:219-221` | Build suite | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.7 `tests/unit/test_whisper_engine.c`

**Why change**: Unit testing for new `vw_whisper_engine_get_segment_count` and `vw_whisper_engine_get_segment` APIs.

**Responsibility before**: Tested model loading, VAD filtering, and full text extraction.  
**Responsibility after**: Tests segment accessor NULL safety, bounds checking, centisecond scaling, and monotonic timing ($t_0 \le t_1$).

**Callers**: `ctest` harness (`linux-x64-debug`, `windows-x64-debug`).  
**Callees**: `vw_whisper_engine_create`, `vw_whisper_engine_transcribe_pcm`, `vw_whisper_engine_get_segment_count`, `vw_whisper_engine_get_segment`.

**Happy path**:
1. Transcribes synthetic audio buffer.
2. Validates `vw_whisper_engine_get_segment_count(engine) >= 0`.
3. Validates segment fields ($t_0 \le t_1$, text non-null).

**Failure path**:
1. Queries index $-1$ or $N+1$; asserts return value is `false`.

**Boundaries**:
- *Input validation*: Full accessor boundary coverage.
- *Authorization*: N/A.
- *Concurrency*: Single-threaded test.
- *I/O*: Reads tiny model file from disk.
- *Persistence*: None.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|-----------|------|------|--------|
| 1 | Accessor validation & bounds test | `test_whisper_engine.c:20-38` | `test_whisper_engine` | ✅ done |

**Assumptions/Tradeoffs**: Tiny ggml model available in test fixtures.

---

### 1.8 `tests/unit/test_segment_builder.c`

**Why change**: Comprehensive unit test suite covering discrete phrase deduplication, ADR-018 final immutable subtitles, word-aligned tail trimming, dynamic FIFO queue expansion, and clear resets.

**Responsibility before**: Basic single-segment push and circular buffer wrapping tests.  
**Responsibility after**: 17 thorough unit tests validating all deduplication invariants, acoustic bounds, and edge cases.

**Callers**: `ctest` harness.  
**Callees**: `vw_segment_builder_*` APIs.

**Happy path**:
1. Executes 17 unit tests verifying creation, invalid rejection, push/dedup, dynamic growth (40 items), pop, multi-phrase per window, hop deduplication with history persistence, silence gap preservation, clear resets, authentic timing, tail prefix trimming, superstring suppression, and time coverage frontier.

**Failure path**:
1. Assertion failure if any deduplication invariant is violated.

**Boundaries**:
- *Input validation*: Exhaustive negative test cases (null pointers, empty strings, oversized strings, negative/inverted timestamps).
- *Authorization*: N/A.
- *Concurrency*: Single-threaded.
- *I/O*: None.
- *Persistence*: None.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|-----------|------|------|--------|
| 1 | Dynamic queue growth past initial capacity | `test_segment_builder.c:71-95` | `test_segment_builder` | ✅ done |
| 2 | Hop deduplication with history persistence | `test_segment_builder.c:170-195` | `test_segment_builder` | ✅ done |
| 3 | Silence gap preservation ($0.6\text{s}$) | `test_segment_builder.c:200-215` | `test_segment_builder` | ✅ done |
| 4 | Tail prefix trimming & coverage frontier | `test_segment_builder.c:290-410` | `test_segment_builder` | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.9 `tests/unit/test_caption_presenter.c`

**Why change**: Validate that discrete SPU subpicture scheduling displays cues at authentic media timestamps and automatically blanks the screen during silence intervals.

**Responsibility before**: Tested single-subpicture SPU creation and OSD fallback.  
**Responsibility after**: Added Test 14 verifying discrete SPU subpicture scheduling with non-overlapping display ticks and exact 0.6s screen blanking duration between phrases.

**Callers**: `ctest` harness.  
**Callees**: `vw_caption_presenter_init`, `vw_caption_presenter_put_segment`, `vw_caption_presenter_destroy`.

**Happy path**:
1. Submits Phrase 1 ($0.5\text{s} \to 2.8\text{s}$) and Phrase 2 ($3.4\text{s} \to 5.2\text{s}$).
2. Verifies discrete `i_start` and `i_stop` ticks on VLC SPU mock.
3. Verifies silence gap ($2.8\text{s} \to 3.4\text{s}$) where no subpicture is scheduled.

**Failure path**:
1. SPU submission fails; returns false cleanly.

**Boundaries**:
- *Input validation*: Validates segment pointer and timestamp consistency.
- *Authorization*: N/A.
- *Concurrency*: Presenter mutex synchronization.
- *I/O*: None.
- *Persistence*: None.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|-----------|------|------|--------|
| 1 | Discrete cue scheduling & silence blanking | `test_caption_presenter.c:190-220` | `test_caption_presenter` | ✅ done |

**Assumptions/Tradeoffs**: Relies on VLC 3.0 mock structures.

---

### 1.10 `tests/integration/test_worker_lifecycle.c`

**Why change**: Validate worker lifecycle state transitions, lookahead decoding pacing, and pause-resume playhead re-synchronization.

**Responsibility before**: Verified basic session start, audio processing, and stop transitions.  
**Responsibility after**: Validates paused position handling and unpause playhead re-sync without stale subtitle blackout.

**Callers**: `ctest` harness.  
**Callees**: `vw_worker_run`, mock IPC socket pairs.

**Happy path**:
1. Starts worker session in source mode.
2. Sends `PAUSE`, verifies transcription suspension and builder clear.
3. Sends `RESUME` at new PTS, verifies source decoder seek and immediate caption resumption.

**Failure path**:
1. Simulates socket disconnect; verifies clean worker termination.

**Boundaries**:
- *Input validation*: Validates IPC frame headers and payload sizing.
- *Authorization*: 32-byte secret token exchange.
- *Concurrency*: Multi-process / threaded mock execution.
- *I/O*: Unix domain sockets / named pipes.
- *Persistence*: None.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|-----------|------|------|--------|
| 1 | Pause-resume unpause seek re-sync | `test_worker_lifecycle.c:215-225` | `test_worker_lifecycle` | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.11 `docs/decisions.md`

**Why change**: Document architectural decisions **ADR-017** (Phrase-by-Phrase Subtitle Timing via Native Whisper Segment Offsets) and **ADR-018** (Final Immutable Subtitles: No Expansion or Revision).

**Responsibility before**: Documented ADR-001 through ADR-016.  
**Responsibility after**: Added ADR-017 and ADR-018 with full context, decision points, consequences, and supersession notes.

**Callers**: Developers and AI agents reading architecture contracts.  
**Callees**: None.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|-----------|------|------|--------|
| 1 | ADR-017 documented | `docs/decisions.md:140-155` | Documentation review | ✅ done |
| 2 | ADR-018 documented | `docs/decisions.md:182-217` | Documentation review | ✅ done |

---

### 1.12 `docs/architecture.md`, `docs/api-contracts.md`, `docs/source-layout.md`, `docs/test-strategy.md`, `docs/roadmap.md`, `docs/plans/*`

**Why change**: Maintain 100% documentation synchronization with implementation changes (Rule 14).

**Summary of Updates**:
- `docs/architecture.md`: Updated phrase-by-phrase timing, discrete SPU scheduling, and pause-resume re-sync details.
- `docs/api-contracts.md`: Documented phrase-by-phrase subtitle timing and `is_final` contract.
- `docs/source-layout.md`: Updated descriptions for segment builder, whisper engine, and new tests.
- `docs/test-strategy.md`: Documented test strategy for phrase-by-phrase timing and ADR-018 final subtitle testing.
- `docs/roadmap.md`: Marked Step 17d.1 complete with comprehensive summary of shipped artifacts.
- `docs/plans/step17d_1_plan.md`: Created Step 17d.1 plan following `ai/task-template.md`.
- `docs/plans/phrase_timing_segmentation_plan.md`: Updated with design notes and final architecture details.
- Cleaned up obsolete plans (`step17a_plan.md`, `step17b_plan.md`, `step17c_plan.md`, `step17_restart_deprecation_plan.md`).

---

## 2. Happy-Path Request Trace

Tracing an audio segment through discrete phrase extraction and presentation:

1. **Audio Arrival & Windowing**:
   - `worker/src/vw_worker.c:525`: Worker extracts an 8.0-second audio window ($128\,000$ samples @ $16\,\text{kHz}$) starting at media timestamp `window_pts_us = 10000000LL` ($10.0\,\text{s}$).
2. **Whisper Transcription**:
   - `worker/src/vw_worker.c:528`: Calls `vw_whisper_engine_transcribe_pcm(engine, window_samples, read_cnt)`.
   - `whisper.cpp` processes mel-spectrogram and identifies 2 discrete spoken phrases separated by silence.
3. **Discrete Phrase Extraction**:
   - `worker/src/vw_worker.c:530`: Calls `vw_whisper_engine_get_segment_count(engine)` returning $2$.
   - **Segment 0**: `vw_whisper_engine_get_segment(engine, 0, &seg)` populates:
     - `seg.t0_us = 500000LL` ($0.5\,\text{s}$ window offset $\to \text{start\_pts} = 10.5\,\text{s}$)
     - `seg.t1_us = 2800000LL` ($2.8\,\text{s}$ window offset $\to \text{end\_pts} = 12.8\,\text{s}$)
     - `seg.text_utf8 = "Where are you from, Victoria?"`
   - **Segment 1**: `vw_whisper_engine_get_segment(engine, 1, &seg)` populates:
     - `seg.t0_us = 3400000LL` ($3.4\,\text{s}$ window offset $\to \text{start\_pts} = 13.4\,\text{s}$)
     - `seg.t1_us = 5200000LL` ($5.2\,\text{s}$ window offset $\to \text{end\_pts} = 15.2\,\text{s}$)
     - `seg.text_utf8 = "I'm from Germany,"`
4. **Segment Builder Deduplication & Enqueueing**:
   - `worker/src/vw_segment_builder.c:227`: `vw_segment_builder_push_hypothesis` checks coverage frontier and history.
   - Both segments are unique $\to$ allocated into `segment_queue` with `is_final = true`.
   - `builder->covered_end_us` is advanced to $15.2\,\text{s}$.
5. **IPC Transmission**:
   - `worker/src/vw_worker.c:700`: Drains `segment_queue` via `vw_segment_builder_pop()`.
   - Serializes into `VW_MSG_CAPTION_SEGMENT` frame and writes to IPC pipe.
6. **Plugin SPU Scheduling**:
   - `plugin/src/vw_whisper_module.c:380`: IPC receiver receives segment frames.
   - `plugin/src/vw_caption_presenter.c:120`: Creates discrete native VLC subpictures:
     - Subpicture 1: `i_start = 10.5s`, `i_stop = 12.8s`, text = *"Where are you from, Victoria?"*
     - Subpicture 2: `i_start = 13.4s`, `i_stop = 15.2s`, text = *"I'm from Germany,"*
7. **Rendering & Silence Blanking**:
   - VLC renders Subpicture 1 from $10.5\text{s} \to 12.8\text{s}$.
   - From $12.8\text{s} \to 13.4\text{s}$ ($0.6\text{s}$ pause), **screen is completely blank**.
   - VLC renders Subpicture 2 from $13.4\text{s} \to 15.2\text{s}$.
8. **Next Hop Deduplication**:
   - Next window ($12.0\text{s} \to 20.0\text{s}$) re-transcribes audio at $13.4\text{s}$.
   - `worker/src/vw_segment_builder.c:256`: Candidate starts at $13.4\text{s} < \text{covered\_end\_us} (15.2\text{s})$ $\to$ cleanly dropped as duplicate re-transcription.

---

## 3. Most Important Failure Path

**Failure Scenario**: Out-of-Memory during pending queue reallocation under extreme burst.

1. **Trigger**:
   - The worker decodes a rapid series of short phrases faster than the IPC channel drains, filling the initial 32-slot `segment_queue` (`worker/src/vw_segment_builder.c:81`).
2. **Reallocation Attempt**:
   - `vw_segment_builder_enqueue()` attempts to allocate double capacity ($64 \times \text{sizeof}(\text{vw\_caption\_segment\_t})$) via `calloc` (`worker/src/vw_segment_builder.c:83`).
3. **Allocation Failure**:
   - System memory is exhausted; `calloc` returns `NULL`.
4. **Clean Error Handling & Invariant Protection**:
   - `worker/src/vw_segment_builder.c:84-87`: Detects `new_queue == NULL`.
   - Frees the newly allocated candidate string copy `copy`.
   - Leaves existing `segment_queue`, `count`, and `head` intact without data corruption.
   - Returns `false` to `vw_segment_builder_push_hypothesis`.
   - `vw_segment_builder.c:336`: `push_hypothesis` returns `false` **without committing the candidate to history**.
5. **Recovery**:
   - The un-emitted cue is not recorded in history, allowing subsequent window hops to retry enqueueing once memory pressure subsides or the queue drains over IPC.

---

## 4. Boundary Summary

| Boundary Type | Component & Location | Check Implemented | Result / Handling |
|---|---|---|---|
| **Input Validation** | `vw_segment_builder.c:229-246` | Non-null, valid PTS ($0 \le \text{start} < \text{end}$), text length $1 \le \text{len} < 1024$ | Returns `false` immediately on invalid input |
| **Input Validation** | `vw_whisper_engine.c:136-138` | Index range $0 \le \text{index} < \text{n\_segments}$ | Returns `false` immediately on out-of-bounds |
| **Arithmetic** | `vw_worker.c:535-536` | `vw_saturating_add_i64(window_pts_us, seg.t0_us)` | Prevents 64-bit signed timestamp overflow |
| **Concurrency** | `vw_caption_presenter.c:115` | `vlc_mutex_lock(&presenter->lock)` | Protects SPU channel registration from race conditions |
| **Concurrency** | `vw_worker.c:450-480` | Thread-confined execution | Worker event loop runs single-threaded |
| **I/O & Memory** | `vw_segment_builder.c:81-97` | Dynamic queue growth on `count == capacity` | Eliminates cue loss during bursty transcription |
| **I/O** | `vw_worker.c:466-474` | Unconditional source decoder seek on unpause | Prevents 30s lookahead desync blackout |
| **Persistence** | `vw_segment_builder.c:38-56` | `vw_segment_builder_clear()` | Purges history, queue, and frontier on seek/reset |

---

## 5. Acceptance Criterion → Code Mapping

| # | Acceptance Criterion | Source Code | Test Code | Status |
|---|---|---|---|---|
| 1 | Extract discrete Whisper segments with $t_0, t_1 \times 10\,000LL$ | `vw_whisper_engine.c:127-147` | `test_whisper_engine.c:20-38` | ✅ done |
| 2 | Discrete phrase timing without whole-window aggregation | `vw_worker.c:530-539, 631-640` | `test_segment_builder.c:127-163` | ✅ done |
| 3 | Final immutable subtitles (ADR-018: no expansion/revision) | `vw_segment_builder.c:261-326` | `test_segment_builder.c:246-275` | ✅ done |
| 4 | Time-coverage re-transcription suppression | `vw_segment_builder.c:256-259` | `test_segment_builder.c:380-410` | ✅ done |
| 5 | Word-aligned tail prefix trimming | `vw_segment_builder.c:142-225` | `test_segment_builder.c:290-340` | ✅ done |
| 6 | Dynamic output queue growth (zero dropped cues) | `vw_segment_builder.c:81-97` | `test_segment_builder.c:71-95` | ✅ done |
| 7 | Committed history persistence across `pop()` | `vw_segment_builder.c:114-130` | `test_segment_builder.c:170-195` | ✅ done |
| 8 | Silence interval screen blanking ($0.6\text{s}$ gap) | `vw_caption_presenter.c:120` | `test_caption_presenter.c:190-220` | ✅ done |
| 9 | Pause-resume lookahead seek re-sync | `vw_worker.c:466-474, 565-573` | `test_worker_lifecycle.c:215-225` | ✅ done |
| 10 | Reset on seek, pause, and session stop | `vw_segment_builder.c:38-56` | `test_segment_builder.c:215-240` | ✅ done |

---

## 7. Code Review Findings (Bugs, Risks, Nitpicks)

### Bugs (Sorted by Priority)

*Zero open bugs found in the diff. All previous review findings (trimmed suffix timing, history commit ordering, symbol namespacing, superstring expansion repetition, and pause-resume blackout) have been resolved and verified.*

### Architectural & Operational Risks

| Category | Risk Description | Affected Files | Mitigation Strategy |
|---|---|---|---|
| **Acoustic Jitter** | Whisper boundary variance ($\pm 30\text{ms}$) on duplicate phrases | `vw_segment_builder.c` | Mitigated by `VW_DEDUP_TIME_TOLERANCE_US = 500000LL` proximity window. |
| **Pipeline Burst** | High-density speech generating $>32$ phrases before IPC drain | `vw_segment_builder.c` | Mitigated by dynamic queue growth doubling buffer capacity. |
| **SPU Overwrite** | SPU channel overwriting active subpicture if start timestamps overlap | `vw_segment_builder.c` | Mitigated by clamping `emit_start = max(start_pts_us, covered_end_us)`. |

### Code Style & Quality Nitpicks

| Issue Type | File & Line | Description | Recommendation |
|---|---|---|---|
| **Documentation** | `vw_segment_builder.h:1-62` | Verified all header docstrings meet Rule 11 (20-30 words) | Fully compliant |
| **Code Style** | All files | Google C Style, 2-space indentation, 120-col limit | Verified with `clang-format --dry-run --Werror` |
