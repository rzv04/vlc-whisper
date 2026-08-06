# Diff Analysis: Step 14a — Worker Audio Ingestion Pipeline, Whisper Engine, Segment Emission

**23 files changed (22 tracked + 1 untracked), +610 / -31 tracked lines (+~84 untracked)**
**Base**: `HEAD` (`gemini/milestone-3-step-13`) → working tree on branch `gemini/milestone-3-step-14a`
**Scope**: staged + unstaged changes + untracked new tests (step 14a implementation)
**Review snapshot**: includes the follow-up memcheck fix (uncommitted) to `tests/unit/test_whisper_engine.c` + `docs/test-strategy.md` (skip-77 under Valgrind) and the docs/plan `docs/plans/step14a_plan.md`. Verified this session: `clang-format --dry-run --Werror` clean on all touched C files, build 19/19, native `ctest` 14/14 (model present, `test_whisper_engine` 0.86s), `ctest -T memcheck` 14/14 in 8.35s (model-gated test skips).

Plan: `docs/plans/step14a_plan.md`. Roadmap item 14a marked done in `docs/roadmap.md`.

---

## 1. File-by-File Analysis

### 1.1 `docs/plans/step14a_plan.md` (new)

**Why change**: The required planning artifact (AGENTS.md rule 9) for step 14a — defines scope, ADR references, acceptance criteria, and DoD.

**Responsibility before**: N/A. **After**: Task plan consumed by implementers and future agent sessions.

**Callers**: AI agents / human devs. **Callees**: none (planning doc).

**Happy path**: Not executable. **Failure path**: N/A.

**Boundaries**: N/A. **Acceptance map**: The plan's 7 acceptance criteria are the source of truth mapped in §5. Status: done.

**Assumptions/Tradeoffs**: DoD explicitly requires code to remain unstaged/uncommitted, yet at review time most files are already staged — a process mismatch with the plan (the plan itself is staged).

---

### 1.2 `docs/decisions.md`

**Why change**: Add **ADR-016 — Native VLC SPU Subpicture Pipeline for Timed Captions**: the plugin delegates caption queueing/PTS display to VLC's SPU pipeline (`vout_RegisterSubpictureChannel` + `vout_PutSubpicture`), no plugin caption queue, `vout_FlushSubpictureChannel` on discontinuity.

**Responsibility before**: ADRs 1-15. **After**: ADRs 1-16.

**Callers**: Planning/implementers. **Callees**: none.

**Happy path**: ADR anchors the step-15 presenter design (look-ahead mode). **Failure path**: N/A.

**Boundaries**: N/A. **Acceptance map**: `docs/decisions.md:129-138`. Status: done.

**Assumptions/Tradeoffs**: ADR-016 describes plugin-side behavior that is **not part of this step** (step 15). It is a forward-looking record — correctly scoped as out-of-scope here. Consistent with the milestone postmortem.

---

### 1.3 `docs/architecture.md`

**Why change**: Add the caption-queueing invariant to the resource-limits list: no plugin caption queue; timed subpictures go straight to VLC's SPU pipeline (ADR-016).

**Responsibility before**: Architecture/timing/IPC spec. **After**: Same, plus ADR-016 caption-queueing note.

**Callers**: Implementers. **Callees**: none.

**Happy path**: Informs step-15 presenter design. **Failure path**: N/A.

**Boundaries**: N/A. **Acceptance map**: `docs/architecture.md:147`. Status: done.

**Assumptions/Tradeoffs**: One-line doc only; does not yet reflect the actual single-threaded worker (see §7 Risk R-1 — ADR-013 describes a decoupled reader thread the code does not implement).

---

### 1.4 `docs/roadmap.md`

**Why change**: Mark step 14a `[x]` (completed).

**Responsibility before**: `[ ] 14a`. **After**: `[x] 14a`.

**Callers**: Planning. **Callees**: none.

**Happy path**: Roadmap reflects completed milestone. **Failure path**: N/A.

**Boundaries**: N/A. **Acceptance map**: `docs/roadmap.md:48`. Status: ⚠️ partial — 14a is checked but the worker is **not** fully ADR-013 compliant (see §7 Bug H-1), so the "decoupled reader/inference threads" portion of the roadmap text is not actually done.

**Assumptions/Tradeoffs**: Marking complete before the threaded pipeline lands is optimistic.

---

### 1.5 `docs/source-layout.md`

**Why change**: Register the two new unit tests (`test_audio_buffer.c`, `test_whisper_engine.c`) in the tests tree listing.

**Responsibility before**: Listed existing tests. **After**: Adds the two new unit-test files.

**Callers**: Onboarding/devs. **Callees**: none.

**Happy path**: File map is current. **Failure path**: N/A.

**Boundaries**: N/A. **Acceptance map**: `docs/source-layout.md:92-93`. Status: done.

---

### 1.6 `docs/test-strategy.md`

**Why change**: Register new unit-test failure-path coverage (audio buffer overflow/drain/clear; whisper engine model-path failure + model-gated skip 77).

**Responsibility before**: Documented existing coverage. **After**: Adds audio-buffer + whisper-engine coverage, plus (post-fix) the memcheck skip rationale.

**Callers**: Test maintainers. **Callees**: none.

**Happy path**: Coverage doc is current. **Failure path**: N/A.

**Boundaries**: N/A. **Acceptance map**: `docs/test-strategy.md:46-47`. Status: done (updated in same change as the memcheck fix).

---

### 1.7 `plugin/include/vw_audio_capture.h`

**Why change**: Document the `pcm_data` buffer sizing intent: ~512 ms of 16 kHz mono S16LE (VLC delivers ~20-40 ms blocks, first/last may be longer).

**Responsibility before**: Inline PCM buffer with no sizing rationale. **After**: Comment explains the capacity contract.

**Callers**: Plugin capture path. **Callees**: none.

**Happy path**: Comment-only. **Failure path**: N/A.

**Boundaries**: Comment only — no behavior change. **Acceptance map**: `vw_audio_capture.h:36-38`. Status: done.

---

### 1.8 `plugin/include/vw_platform.h`

**Why change**: Add platform thread + sleep abstractions (`vw_thread_t`, `vw_platform_thread_create`, `vw_platform_thread_join`, `vw_platform_sleep_ms`) needed for the ADR-013 reader/inference worker threads.

**Responsibility before**: Time + process-spawn helpers only. **After**: Adds thread/sleep helpers.

**Callers**: Intended: `vw_worker.c` (reader/inference threads) — **not actually used anywhere yet**. **Callees**: pthread (POSIX) / Win32 API.

**Happy path**: Would let the worker spawn a decoupled inference thread. **Failure path**: N/A — no callers today.

**Boundaries**: `vw_thread_t` is `void*` on Win32 vs `pthread_t` on POSIX; `#include <pthread.h>` leaks into any includer on non-Windows. **Acceptance map**: `vw_platform.h:22-35`. Status: ⚠️ implemented, **unused** and **not unit tested** (plan AC #1 requires "implemented and unit tested").

**Assumptions/Tradeoffs**: Linux `pthread_create` requires linking pthread on pre-glibc-2.34 systems; CMake does not yet add `Threads::Threads` to the worker target (latent portability risk once threads are wired in). Win32 thread proc heap-allocates an arg wrapper and `free`s it inside the thread.

---

### 1.9 `plugin/src/vw_platform_linux.c`

**Why change**: Implement the three thread/sleep helpers for Linux.

**Responsibility before**: Time + spawn. **After**: Adds `pthread_create`/`pthread_join`/`nanosleep` wrappers.

**Callers**: `vw_platform.h` API (currently no callers). **Callees**: `pthread_create`, `pthread_join`, `nanosleep`.

**Happy path**: `vw_platform_thread_create` returns true and the thread runs `func(arg)`. **Failure path**: `pthread_create` failure → returns false; `thread_join` on a thread that never ran → `pthread_join` error ignored.

**Boundaries**: NULL `thread`/`func` rejected; `sleep_ms` truncates to `timespec` (ms ≤ 1000 handled; large ms → `tv_sec` division). **Acceptance map**: `vw_platform_linux.c:54-66`. Status: ⚠️ implemented, unused.

**Assumptions/Tradeoffs**: No trailing newline at EOF (style nit). Join return value dropped (worker exit code lost).

---

### 1.10 `plugin/src/vw_platform_win32.c`

**Why change**: Implement the same three helpers for Windows (MinGW).

**Responsibility before**: Time + spawn. **After**: Adds Win32 `CreateThread`/`WaitForSingleObject`/`Sleep` wrappers with an adapter struct to bridge `DWORD WINAPI` vs `void*(void*)` signatures.

**Callers**: `vw_platform.h` API (currently no callers). **Callees**: `CreateThread`, `WaitForSingleObject`, `CloseHandle`, `Sleep`, `malloc/free`.

**Happy path**: `CreateThread` succeeds; wrapper frees its arg inside the proc. **Failure path**: malloc or `CreateThread` failure → returns false (wrapper freed).

**Boundaries**: NULL checks on `thread`/`func`; `join` no-ops on NULL handle; thread return value (`func`'s `void*`) is discarded. **Acceptance map**: `vw_platform_win32.c:106-144`. Status: ⚠️ implemented, unused.

**Assumptions/Tradeoffs**: `vw_thread_t` cast to/from `HANDLE`; no trailing newline at EOF.

---

### 1.11 `protocol/include/vw_protocol_types.h`

**Why change**: Add `vw_error_code_t` enum (E_PROTOCOL_VERSION..E_INTERNAL) so `VW_MSG_ERROR` frames can carry structured error codes.

**Responsibility before**: No error-code enum. **After**: 9-code enum; `E_MODEL_MISSING=3`, `E_MODEL_INVALID=4` specifically for the model-gated error path.

**Callers**: `vw_worker.c:158` (emits `E_MODEL_MISSING`). **Callees**: none.

**Happy path**: Worker sends a typed error frame. **Failure path**: N/A.

**Boundaries**: Wire contract change — new enum values are stable ordinals; must be mirrored on the plugin client. **Acceptance map**: `vw_protocol_types.h:44-54`. Status: done (partial use — see Bug M-3).

**Assumptions/Tradeoffs**: `E_MODEL_INVALID` (4) is **never emitted** — the worker collapses missing/corrupt models into `E_MODEL_MISSING`. `E_AUTH` (2) also unused (auth failure still just closes without an error frame).

---

### 1.12 `worker/include/vw_audio_buffer.h`

**Why change**: Promote the audio buffer from a stub struct to a real float32 ring buffer with full state (samples, head, count, start_pts_us, dropped_samples) and add accessors (`get_count`, `get_samples`, `drain`, `clear`).

**Responsibility before**: Stub `max_samples`-only struct. **After**: Full ring-buffer contract with doc comments.

**Callers**: `vw_worker.c`, `test_audio_buffer.c`. **Callees**: none.

**Happy path**: Caller appends S16LE, reads float32 windows, drains hops. **Failure path**: NULL buffer/args rejected by accessors (documented).

**Boundaries**: `start_pts_us` convention: `-1` = empty; caller takes ownership of nothing (all value semantics). **Acceptance map**: `vw_audio_buffer.h:68-73`, `86-101`. Status: done.

---

### 1.13 `worker/src/vw_audio_buffer.c`

**Why change**: Implement the ring buffer: S16LE→float32 normalization, PTS indexing, drop-oldest overflow, drain, clear.

**Responsibility before**: Stub (create/free/append no-ops). **After**: Real 10 s (160k sample) float32 ring buffer.

**Callers**: `vw_worker.c:204,224,232`; `test_audio_buffer.c`. **Callees**: none (stdlib only).

**Happy path**: `create(160000)`; append sets `start_pts_us` on first sample; `get_samples` copies oldest `count` samples and returns `start_pts_us`; `drain(HOP)` advances PTS.

**Failure path**: NULL/zero-size rejection; overflow drops oldest and bumps `dropped_samples`.

**Boundaries**:

- **PTS precision mismatch (Bug M-1)**: append overflow advances `start_pts_us += 62` (integer µs/sample, `vw_audio_buffer.c:44`); drain advances `start_pts_us += (int64_t)(drained * 62.5)` (`:74`). 16 kHz sample period is 62.5 µs — the append path truncates to 62 µs and the drain path truncates odd counts by 0.5 µs. Mixed overflow+drain drift is small (≤0.5 µs/sample) but the two paths are inconsistent.
- **No discontinuity/PTS-gap detection**: `start_pts_us` is set from the incoming chunk's PTS only when the buffer is empty; non-contiguous appends (seek/rate change) are assumed contiguous and overflow advances by fixed 62 µs rather than the chunk's actual PTS. ADR-016 flush-on-discontinuity is plugin-side (step 15); worker-side has no gap handling.
- `get_samples` returns `start_pts_us` regardless of `to_copy` — correct since it always starts at the oldest sample.
- `drain` clamps to `count`; `clear` resets head/count/PTS. **Acceptance map**: `vw_audio_buffer.c:6-85`. Status: done (unit-tested).

**Assumptions/Tradeoffs**: `dropped_samples` is tracked but **never surfaced** (no STATUS frame emitted — see Bug M-4).

---

### 1.14 `worker/include/vw_segment_builder.h`

**Why change**: Add window/hop sample constants used by the worker pipeline and declare `vw_segment_builder_pop`.

**Responsibility before**: Segment constants (durations only) + push API. **After**: Adds `VW_AUDIO_SAMPLE_RATE`, `VW_WINDOW_SAMPLES` (128000), `VW_HOP_SAMPLES` (32000), and the pop accessor.

**Callers**: `vw_worker.c` (via `vw_worker.h`). **Callees**: none.

**Happy path**: Worker uses constants for windowing. **Failure path**: N/A.

**Boundaries**: `VW_SEGMENT_BUILDER_MAX_TEXT_BYTES = 1024` interacts badly with the emit buffer (Bug M-2). **Acceptance map**: `vw_segment_builder.h:10,13-14`. Status: done.

---

### 1.15 `worker/src/vw_segment_builder.c`

**Why change**: Implement `vw_segment_builder_pop` — pop the oldest segment and transfer `text_utf8` ownership to the caller.

**Responsibility before**: Push/dedup only. **After**: Adds FIFO pop with slot clearing.

**Callers**: `vw_worker.c:247`; `test_segment_builder.c:88`. **Callees**: none.

**Happy path**: `pop` returns oldest segment, `memset`s the ring slot to zero (so the builder no longer owns `text_utf8`), decrements count. Caller frees text after encoding.

**Failure path**: NULL builder/out, empty queue → false; caller must check.

**Boundaries**: Ownership transfer is correct — builder `free` (`vw_segment_builder_free`) iterates slots and would double-free if a slot weren't zeroed; the `memset` at `:128` prevents that. **Acceptance map**: `vw_segment_builder.c:120-130`. Status: done.

**Assumptions/Tradeoffs**: No trailing newline at EOF (nit). `is_final` remains hardcoded `true` in `write_slot` (pre-existing, not this diff).

---

### 1.16 `worker/include/vw_whisper_engine.h`

**Why change**: Give the engine real fields (`struct whisper_context* ctx`, `last_text` buffer) and document the ADR-015 model-once + warmup contract.

**Responsibility before**: Stub opaque engine. **After**: Real engine API contract.

**Callers**: `vw_worker.c`, `test_whisper_engine.c`. **Callees**: `whisper.h` (public C API).

**Happy path**: init→transcribe→get_text. **Failure path**: NULL on missing/invalid model.

**Boundaries**: `last_text` is a growable owned buffer; `get_text` returns `""` when NULL. **Acceptance map**: `vw_whisper_engine.h:8-23`. Status: done.

---

### 1.17 `worker/src/vw_whisper_engine.c`

**Why change**: Implement real whisper.cpp init (model load + silent warmup), transcribe, text extraction.

**Responsibility before**: Stub no-ops. **After**: Real engine using `whisper.h` C API.

**Callers**: `vw_worker.c:35,214,219`; `test_whisper_engine.c`. **Callees**: `whisper_init_from_file_with_params`, `whisper_full`, `whisper_full_n_segments`, `whisper_full_get_segment_text`, `whisper_free`, `realloc`.

**Happy path**: `init(model)` loads model, runs a 100 ms silent warmup (`whisper_full` on `silent[1600]`, `n_threads=2`), `transcribe_pcm` runs `whisper_full` (`n_threads=4`), concatenates per-segment UTF-8 text into `last_text` (growing via `realloc`), `get_text` returns it.

**Failure path**: missing/empty model path → NULL; `whisper_init_from_file` NULL → NULL; `whisper_full` non-zero → false; OOM in `realloc` → text truncation (returns true anyway — see Bug L-1).

**Boundaries**:

- `whisper_full(..., (int)sample_count)` cast (`:71`) — fine for 128k-sample windows, would overflow for >2^31 samples (not reachable).
- Warmup return value ignored (`:42`) — a warmup failure is silently tolerated.
- Warmup logs `input is too short - 90 ms < 100 ms` (cosmetic; 1600 samples ≈ 100 ms).
- **Not thread-safe** (single shared `ctx` + `last_text`) — safe today only because the worker is single-threaded (Bug H-1 makes this a latent hazard). **Acceptance map**: `vw_whisper_engine.c:9-99`. Status: done (model-gated unit test).

**Assumptions/Tradeoffs**: `language = "en"` hardcoded; no GPU config exposed (`use_gpu` default true; on this GPU-less machine whisper falls back to CPU and emits `close(-1)` warnings — see Bug H-2/memcheck finding).

---

### 1.18 `worker/include/vw_worker.h`

**Why change**: Document `vw_worker_run` (thread-safety + pipeline contract).

**Responsibility before**: Undocumented decl. **After**: Doc comment added. (Note: the header already transitively includes `vw_audio_buffer.h`, `vw_segment_builder.h`, `vw_vad.h` — the include list is unchanged in this diff.)

**Callers**: `main.c`, integration tests. **Callees**: `vw_worker_run`. **Acceptance map**: `vw_worker.h:11`. Status: done.

---

### 1.19 `worker/src/vw_worker.c` (core of the step)

**Why change**: Wire the full pipeline: engine load at startup, START→STARTED + session_id validation, AUDIO ingest → ring buffer → 8 s/2 s windowing + energy-VAD → whisper inference → segment builder → `CAPTION_SEGMENT` frames; STOP clears the session; ERROR frame on missing model.

**Responsibility before**: HELLO handshake + stub message handling. **After**: Real session pipeline (single-threaded).

**Callers**: `main.c`, integration tests. **Callees**: `vw_whisper_engine_*`, `vw_audio_buffer_*`, `vw_segment_builder_*`, `vw_vad_detect_speech_energy`, `vw_protocol_encode_*`, `vw_ipc_*`.

**Happy path**: HELLO → START → STARTED → repeated AUDIO frames; once ≥ 8 s buffered, copy oldest 8 s window, VAD-gate, transcribe, push hypothesis (start=`window_pts_us`, end=`start + read_cnt/16k*1e6`), drain 2 s; segments drained and emitted as `CAPTION_SEGMENT`; STOP clears; SHUTDOWN exits.

**Failure path**: missing model → `E_MODEL_MISSING` ERROR frame (recoverable=0), session stays inactive; invalid session_id on AUDIO → frame silently dropped; VAD/transcribe failure → window dropped silently.

**Boundaries**:

- **ADR-013 not implemented (Bug H-1)**: single-threaded loop; `whisper_full` blocks the IPC read thread; `vw_platform_thread_*` unused.
- **512 KB stack window (Bug H-3)**: `float window_samples[VW_WINDOW_SAMPLES]` = 128000 floats at `:208` in the message loop.
- **Segment emit buffer too small (Bug M-2)**: `seg_payload[1024]` at `:249`; max encoded `CAPTION_SEGMENT` = 43 + text (16 session + 8 id + 8 start + 8 end + 1 is_final + 2 text_bytes); `push_hypothesis` allows text up to 1024 bytes → payload up to 1067 > 1024 → `encode_payload` returns false → segments with 982–1024-byte text silently dropped.
- **Session hardening (Bug M-5)**: no first-message-after-HELLO enforcement, no duplicate-START rejection, no request/response validation of `START`'s `sample_rate`/`sample_format` fields (accepts anything).
- **Silent failures (Bug M-4)**: transcribe failures and overflow (`dropped_samples`) produce no `STATUS`/`ERROR` frame; `vw_msg_status_t` is never emitted although the plan lists STATUS as an output.
- **Cleanup ordering**: audio_buf/builder/engine freed after loop (`:274-277`) — no leak (verified memcheck 14/14). **Acceptance map**: `vw_worker.c:35-37` (init), `152-192` (START/ERROR/STARTED), `193-229` (AUDIO pipeline), `230-235` (STOP), `247-260` (segment emit), `274-277` (cleanup). Status: ⚠️ partial (ADR-013 thread split missing).

**Assumptions/Tradeoffs**: `sequence` counter starts at 1 and is `++`-ed per reply, but the HELLO_ACK uses a literal `.sequence = 1` — consistent enough (first post-HELLO frame = 2). Rate/channel fields of START are ignored.

---

### 1.20 `tests/CMakeLists.txt`

**Why change**: Register `test_audio_buffer`, `test_whisper_engine` (with `SKIP_RETURN_CODE 77`), and expand the two integration tests to link the new worker sources (`vw_audio_buffer.c`, `vw_segment_builder.c`, `vw_whisper_engine.c`, `vw_vad.c`) + `whisper`.

**Responsibility before**: Registered old test set. **After**: 14 tests incl. new model-gated and pipeline tests.

**Callers**: CTest. **Callees**: build system.

**Happy path**: All 14 tests build/link/run. **Failure path**: `test_whisper_engine` returns 77 (skip) when model absent or under Valgrind.

**Boundaries**: Integration tests now compile `vw_worker.c` with the real engine; they pass `model_path=NULL` (memset config) so no model is loaded in CI. `test_whisper_engine` links `whisper` directly. **Acceptance map**: `tests/CMakeLists.txt:29,31-33,35-39`. Status: done. (Pre-existing `test_plugin_load` lines at 56-57 are not clang-format clean, unrelated to this diff.)

---

### 1.21 `tests/unit/test_audio_buffer.c` (new, untracked)

**Why change**: Unit test the float32 ring buffer: NULL rejection, create/free, S16LE append + normalization, PTS indexing, overflow drop-oldest accounting, drain, clear.

**Responsibility before**: N/A. **After**: Full unit coverage of `vw_audio_buffer`.

**Callers**: CTest. **Callees**: `vw_audio_buffer_*`.

**Happy path**: append 100 → count 100, `get_samples` returns exact values + PTS; 15 samples into a 10-sample buffer → count 10, `dropped_samples == 5` (`:54`); drain/clear verified.

**Failure path**: NULL args rejected safely (no-ops).

**Boundaries**: Uses a small (10-sample) buffer to exercise overflow without 10 s of data. Test reaches into `small_buf->dropped_samples` — couples to struct internals (nit). **Acceptance map**: `test_audio_buffer.c:16-83`. Status: done (passing).

---

### 1.22 `tests/unit/test_segment_builder.c`

**Why change**: Add `test_pop` — pop oldest segments, verify FIFO order + PTS + text ownership, and empty-queue false.

**Responsibility before**: create/free, invalid-hypothesis, dedup, wrap tests. **After**: Adds pop coverage.

**Callers**: CTest. **Callees**: `vw_segment_builder_*`.

**Happy path**: push "First"/"Second" → pop returns "First" (0 µs) then "Second" (2 s), then false on empty. **Failure path**: pop on empty → false.

**Boundaries**: Frees popped text; wrap-around path implicitly covered by existing `test_circular_buffer_wrap`. **Acceptance map**: `test_segment_builder.c:88-113`, invoked `:118`. Status: done (passing).

---

### 1.23 `tests/unit/test_whisper_engine.c` (new)

**Why change**: Model-gated engine test: invalid-path NULL, model discovery (4 candidate paths), then — when the model is present and **not** under Valgrind — real init + silent transcribe + get_text.

**Responsibility before**: N/A. **After**: Smoke test for `vw_whisper_engine`.

**Callers**: CTest (`SKIP_RETURN_CODE 77`). **Callees**: `vw_whisper_engine_*`.

**Happy path**: model found → init succeeds → transcribe 16000 zero samples → `get_text` non-NULL → PASSED (0.86 s native with real model).

**Failure path**: model absent → skip 77 (`:59`); **running under Valgrind → skip 77 (`:64-66`)** via `running_under_valgrind()` (`:21-31`, `/proc/self/maps` contains `vgpreload`).

**Boundaries**: Linux-only Valgrind detection (no `valgrind.h` dependency); Windows builds return 0 (never skip — memcheck is a Linux-only gate). Model search relies on CWD-relative paths — works from `build/linux-x64-debug` (finds `../../models/...`). **Acceptance map**: `test_whisper_engine.c:41` (NULL init), `45-52` (search), `59/64-66` (skip 77), `70-75` (real path). Status: done; this is the memcheck-fix follow-up folded into the review.

**Assumptions/Tradeoffs**: Transcribing 1 s of zeros validates plumbing, not accuracy (acceptable smoke scope).

---

## 2. Happy-Path Request Trace

Full end-to-end trace: worker startup → authenticated session → one 8 s caption segment emitted.

1. `worker/src/main.c` parses args → `vw_worker_config_t` (pipe, token, model path) → `vw_worker_run(config)` (`vw_worker.c:32`).
2. `vw_worker.c:35-37` — `engine = vw_whisper_engine_init(model)` (loads model + warmup), `audio_buf = create(160000)` (10 s), `builder = create()`.
3. `vw_worker.c:43-67` — listen; blocking header read loop.
4. Plugin sends `VW_MSG_HELLO` → `verify_token_constant_time` (`:62`) → HELLO_ACK reply (`:100-128`), `authenticated=true`.
5. Plugin sends `VW_MSG_START_SESSION` → `:152` → engine present → `session_id` copied (`:183`), `session_active=true`, STARTED reply (`:189-191`).
6. Plugin sends `VW_MSG_AUDIO_PCM` (16-bit PCM, `start_pts_us`) → `:193` → session_id matches (`:195`) → `vw_audio_buffer_append_s16le` (`:204`) normalizes to float32 and tracks PTS.
7. Once `count >= 128000` (`:207`), loop: `vw_audio_buffer_get_samples` copies the oldest 8 s window into a 512 KB stack array (`:208`) with `window_pts_us` (`:211`); `vw_vad_detect_speech_energy` (`:213`) gates; `vw_whisper_engine_transcribe_pcm` (`:214`) runs `whisper_full`; non-empty text → `vw_segment_builder_push_hypothesis(builder, text, window_pts_us, window_pts_us + read_cnt/16000*1e6)` (`:219`); `vw_audio_buffer_drain(audio_buf, 32000)` (`:224`) advances 2 s.
8. End of message iteration: `:247` — `vw_segment_builder_pop(builder, &seg)` drains each completed segment; `seg.session_id` stamped (`:248`); encoded into `seg_payload[1024]` (`:249-256`) and sent as `VW_MSG_CAPTION_SEGMENT`; `free(seg.text_utf8)` (`:258`).
9. Plugin receives the caption segment and (step 15) displays via VLC SPU.

Net: 1 AUDIO frame → 0..N caption segments (every 2 s hop once ≥ 8 s of audio is buffered), PTS-accurate from the ring buffer.

---

## 3. Most Important Failure Path

**Missing/corrupt model at startup** (recoverable=0, process stays alive):

1. `vw_worker.c:35` — `vw_whisper_engine_init(config->model_path)`.
2. `vw_whisper_engine.c:13` — `whisper_init_from_file_with_params` fails (missing file / bad magic) → NULL → `init` returns NULL.
3. `vw_worker.c:36-37` — buffer + builder still created; engine NULL.
4. Plugin sends `START` → `:152` → `if (!engine)` (`:154`) → build `vw_msg_error_t` with `error_code = E_MODEL_MISSING` (`:158`), `recoverable = 0`, message "Whisper model file missing or invalid" (`:160`).
5. Encoded into `err_payload[512]` (`:162`; max ERROR payload = 16+4+1+256 = 277 bytes — fits) and sent (`:163-169`).
6. Session stays inactive; subsequent AUDIO frames are dropped (`:194-195`); SHUTDOWN still honored.

Result: plugin receives a typed, non-recoverable error and can surface "install the model" to the user. No crash, no leak (memcheck clean).

**Secondary critical path — memcheck (post-fix)**: previously `test_whisper_engine` stalled ~3–5 min and failed with 17 `close(-1)` false positives from whisper's GPU-less Vulkan fallback; now returns 77 under Valgrind so the memcheck gate completes in ~8 s.

---

## 4. Boundary Summary

| Boundary type        | Findings                                                                                                                        | Location                                                                        | Status          |
| -------------------- | ------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------- | --------------- |
| **Input validation** | NULL/zero-size rejects in audio buffer, engine, platform threads; pop/append arg checks                                         | `vw_audio_buffer.c:7,28`, `vw_whisper_engine.c:10,60`, `vw_platform_linux.c:55` | ✅              |
| **Input validation** | START `sample_rate`/`sample_format`/`channels` never validated; session_id accepted blindly                                     | `vw_worker.c:183`                                                               | ⚠️ gap          |
| **Input validation** | Segment text length cap (1024) vs emit buffer (1024) mismatch → 982–1024 B segments dropped                                     | `vw_worker.c:249`, `vw_segment_builder.h:9`                                     | ❌ Bug M-2      |
| **Authorization**    | 32-byte constant-time token HELLO gate (pre-existing)                                                                           | `vw_worker.c:62`                                                                | ✅              |
| **Authorization**    | No first-message-after-HELLO / duplicate-START enforcement                                                                      | `vw_worker.c:152`                                                               | ⚠️ Bug M-5      |
| **Concurrency**      | ADR-013 reader/inference thread split **not implemented**; single-threaded; engine not thread-safe (latent)                     | `vw_worker.c` whole loop; `vw_platform_*.c` unused                              | ❌ Bug H-1      |
| **Concurrency**      | 512 KB stack window in message loop                                                                                             | `vw_worker.c:208`                                                               | ⚠️ Bug H-3      |
| **I/O**              | IPC read loop blocks during `whisper_full` → pipe backpressure (ADR-008 drop-oldest, "acceptable" but architecture-violating)   | `vw_worker.c:213-219`                                                           | ⚠️              |
| **Persistence**      | None — offline, zero disk persistence (ADR-005 invariant)                                                                       | —                                                                               | ✅              |
| **PTS/timing**       | ~~62 vs 62.5 µs/sample inconsistency between append-overflow and drain; no seek/gap detection~~ (solved by rounding both to 63) | `vw_audio_buffer.c:44,74`                                                       | ⚠️ Bug M-1      |
| **Error reporting**  | `E_MODEL_INVALID` never used; inference/overflow failures silent (no STATUS)                                                    | `vw_worker.c:158`, `vw_protocol_types.h:48`                                     | ⚠️ Bugs M-3/M-4 |
| **Test skip**        | model-absent + Valgrind both → skip 77                                                                                          | `test_whisper_engine.c:59,64-66`                                                | ✅              |

---

## 5. Acceptance Criterion → Code Mapping

| #   | Criterion (from `docs/plans/step14a_plan.md`)                                                                                                  | Code                                                                              | Test                                                                  | Status                                          |
| --- | ---------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------- | --------------------------------------------------------------------- | ----------------------------------------------- |
| 1   | Platform thread helpers implemented **and unit tested**                                                                                        | `vw_platform.h:22-35`, `vw_platform_linux.c:54-66`, `vw_platform_win32.c:106-144` | **no test**                                                           | ⚠️ partial (implemented, **unused + untested**) |
| 2   | audio_buffer S16LE→float32, PTS tracking, overflow                                                                                             | `vw_audio_buffer.c:27-45`                                                         | `test_audio_buffer.c` (passing)                                       | ✅                                              |
| 3   | `vw_segment_builder_pop` oldest + ownership transfer                                                                                           | `vw_segment_builder.c:120-130`                                                    | `test_segment_builder.c:88` (passing)                                 | ✅                                              |
| 4   | whisper engine model-once init, 16 kHz inference, UTF-8 text                                                                                   | `vw_whisper_engine.c:9-99`                                                        | `test_whisper_engine.c` (passing, skip 77)                            | ✅                                              |
| 5   | worker START→STARTED, session_id validation, **AUDIO ingest via decoupled reader thread (ADR-013)**, 8 s/2 s windowing, `CAPTION_SEGMENT` emit | `vw_worker.c:152-260` (single-threaded)                                           | `test_worker_ipc`, `test_worker_lifecycle` (passing)                  | ⚠️ partial — **ADR-013 thread split missing**   |
| 6   | Tests 100% + model-gated skip 77                                                                                                               | `tests/CMakeLists.txt:33`                                                         | native 14/14, memcheck 14/14                                          | ✅                                              |
| 7   | Formatting + native build/test + memcheck                                                                                                      | —                                                                                 | clang-format clean; build 19/19; ctest 14/14; memcheck 14/14 (8.35 s) | ✅                                              |

---

## 7. Code Review Findings (Bugs, Risks, Nitpicks)

### Bugs (Sorted by Priority)

| Priority   | Component / Location                                                    | Description                                                                                                                                                                                                                                                                                                                                                                      | Impact                                                                                        | Proposed Fix                                                                                                                                                                                                                                   |
| ---------- | ----------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **High**   | `worker/src/vw_worker.c:32-277`                                         | ADR-013 decoupled reader/inference threads **not implemented**. `vw_platform_thread_create/join/sleep_ms` are dead code; `whisper_full` (n_threads=4, seconds) runs on the IPC read thread. Violates ADR-013, plan AC #5, and DoD "no blocking calls on IPC transport read thread". During inference no frames are read → pipe backpressure → plugin SPSC drop-oldest (ADR-008). | Architecture violation; throughput collapse; hard to extend to real-time streaming (step 14b) | Split into reader thread (IPC→SPSC queue) + inference thread (pop→buffer→VAD→transcribe→emit) using `vw_platform_thread_*`; add `Threads::Threads` to worker CMake; document engine thread-affinity (engine must stay on the inference thread) |
| **High**   | `tests/unit/test_whisper_engine.c` (pre-fix)                            | Under Valgrind: ~3–5 min runtime + 17 false-positive `invalid file descriptor -1 in syscall close()` from whisper's GPU-less Vulkan fallback → `ctest -T memcheck` stalls then fails. **Fixed this session** via skip-77 under Valgrind (`running_under_valgrind`, `/proc/self/maps` `vgpreload`).                                                                               | Memcheck gate unusable                                                                        | ✅ Fixed — skip 77 under Valgrind; native `ctest` still exercises the model                                                                                                                                                                    |
| **Medium** | `worker/src/vw_worker.c:249` vs `worker/include/vw_segment_builder.h:9` | ~~Emit buffer `seg_payload[1024]` < max encoded `CAPTION_SEGMENT` (43 + up to 1024 B text = 1067). `encode_payload` returns false → segments with 982–1024 B text silently dropped.~~ (solved with specified fix, however the fixed fields sizes are hardcoded)                                                                                                                  | Lost captions for long segments                                                               | Size buffer to `43 + VW_SEGMENT_BUILDER_MAX_TEXT_BYTES` (e.g. 1088) or cap text at push time to fit                                                                                                                                            |
| **Medium** | `worker/src/vw_worker.c:208`                                            | `float window_samples[VW_WINDOW_SAMPLES]` = 512 KB stack allocation inside the message loop.                                                                                                                                                                                                                                                                                     | Fragile stack frame; worse once moved to a worker thread (smaller default stacks)             | Heap-allocate once at startup (reuse buffer) or use a `static`/reused window                                                                                                                                                                   |
| **Medium** | `worker/src/vw_audio_buffer.c:44,74`                                    | ~~PTS precision inconsistency: overflow path advances `start_pts_us += 62` (int) vs drain path `+= (int64_t)(drained * 62.5)`. No PTS-gap/seek detection (non-contiguous appends assumed contiguous).~~ (fixed by rounding both to 63)                                                                                                                                           | Small PTS drift + wrong captions after seek/rate change                                       | Use integer 62.5 µs (µs per 2 samples) consistently; reject/handle PTS discontinuities (clear per ADR-007/016)                                                                                                                                 |
| **Medium** | `worker/src/vw_worker.c:158`, `protocol/include/vw_protocol_types.h:48` | `E_MODEL_INVALID` (4) never emitted — missing vs corrupt model both report `E_MODEL_MISSING`.                                                                                                                                                                                                                                                                                    | Plugin can't distinguish corrupt models                                                       | Probe the file (magic/load) and emit `E_MODEL_INVALID` on parse failure                                                                                                                                                                        |
| **Medium** | `worker/src/vw_worker.c:236-260`                                        | No `STATUS` frames ever emitted (`vw_msg_status_t` unused); inference failures and `dropped_audio_us` overflow accounting are silent. Plan lists STATUS as an output.                                                                                                                                                                                                            | No observability of backlog/dropped audio                                                     | Emit periodic STATUS with `queued_audio_us`/`inference_us`/`dropped_audio_us` from `audio_buf->dropped_samples`                                                                                                                                |
| **Medium** | `worker/src/vw_worker.c:183`                                            | No first-message-after-HELLO check, no duplicate-START rejection, START `sample_rate`/`sample_format` fields ignored.                                                                                                                                                                                                                                                            | Session state confusion / format mismatch                                                     | Reject START if already active; validate `sample_rate == 16000`, format S16LE before STARTED                                                                                                                                                   |
| **Low**    | `worker/src/vw_whisper_engine.c:82-88`                                  | On `realloc` OOM, transcription continues with truncated text and returns true.                                                                                                                                                                                                                                                                                                  | Truncated captions on OOM                                                                     | Return false and skip text on realloc failure                                                                                                                                                                                                  |

### Architectural & Operational Risks

| Category         | Risk Description                                                                                                                          | Affected Files                                            | Mitigation Strategy                                                                                         |
| ---------------- | ----------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------- |
| **Architecture** | Single-threaded worker contradicts ADR-013/DoD; real-time streaming (14b) will need the thread split before it can work without loss      | `vw_worker.c`, `vw_platform_*.c`                          | Implement reader+inference threads before 14b; gate 14a completion on it (currently marked done in roadmap) |
| **Portability**  | `pthread_create` link dependency not declared (`Threads::Threads` absent); pre-glibc-2.34 systems will fail to link once threads are used | `worker/CMakeLists.txt`                                   | `find_package(Threads)` + link `Threads::Threads` when wiring threads                                       |
| **Portability**  | `#include <pthread.h>` leaked via `vw_platform.h` on non-Windows                                                                          | `plugin/include/vw_platform.h:24`                         | Forward-declare / include inside `.c`; keep header self-contained                                           |
| **Runtime env**  | Vulkan enabled (`VW_ENABLE_VULKAN=ON`) on a GPU-less machine → whisper CPU fallback + `close(-1)` noise; pathological under Valgrind      | `vw_whisper_engine.c`, `tests/unit/test_whisper_engine.c` | Keep the memcheck skip; optionally disable Vulkan for CPU-only builds; document GPU requirement             |
| **Data loss**    | Segment drop at 982–1024 B text and silent VAD/transcribe failures can lose captions with no feedback                                     | `vw_worker.c:249,213-219`                                 | Fix buffer size; emit ERROR/STATUS on failures                                                              |

### Code Style & Quality Nitpicks

| Issue Type              | File & Line                                                                     | Description                                                            | Recommendation                                               |
| ----------------------- | ------------------------------------------------------------------------------- | ---------------------------------------------------------------------- | ------------------------------------------------------------ |
| **Missing EOF newline** | `vw_platform_linux.c:66`, `vw_platform_win32.c:144`, `vw_segment_builder.c:130` | Files end without trailing newline                                     | Add final newline                                            |
| **Style**               | `vw_worker.c:160`                                                               | `snprintf(msg, sizeof(msg), "%s", "literal")` — unnecessary format     | Use `strncpy` or direct literal                              |
| **Style**               | `vw_audio_capture.h:36-38`                                                      | Comment-only change is fine but unrelated to 14a scope                 | Acceptable                                                   |
| **Test coupling**       | `test_audio_buffer.c:54`                                                        | Asserts `small_buf->dropped_samples` directly (struct internals)       | Expose a `get_dropped()` accessor or keep as-is (documented) |
| **Cosmetic**            | `vw_whisper_engine.c:42`                                                        | Warmup logs "input is too short - 90 ms < 100 ms"                      | Pad warmup to ≥100 ms or suppress                            |
| **Quality**             | `vw_platform.h:22-25`                                                           | `vw_thread_t` typedef differs by platform with no size/type guarantees | Document platform guarantee (pthread_t / HANDLE)             |
| **Test scope**          | `test_whisper_engine.c:74`                                                      | Transcribes 1 s of zeros — plumbing smoke only                         | Acceptable; accuracy tests deferred to integration           |

---

## 8. Verification Summary

| Check                                                   | Result                                                                                                    |
| ------------------------------------------------------- | --------------------------------------------------------------------------------------------------------- |
| `clang-format --dry-run --Werror` (all touched C)       | ✅ clean (only pre-existing `tests/CMakeLists.txt:56-57` `test_plugin_load` lines flagged, not this diff) |
| Native build (`cmake --build --preset linux-x64-debug`) | ✅ 19/19 targets                                                                                          |
| Native `ctest --preset linux-x64-debug`                 | ✅ 14/14 (model present; `test_whisper_engine` 0.86 s)                                                    |
| `ctest --test-dir build/linux-x64-debug -T memcheck`    | ✅ 14/14 in 8.35 s (`test_whisper_engine` skips under Valgrind)                                           |
| Git state                                               | Staged + unstaged + 1 untracked test; uncommitted per plan DoD                                            |

## Key Takeaway

The step-14a pipeline (ring buffer, segment pop, whisper engine, worker session handling, tests) is solid and fully green. The single material gap is **ADR-013**: the worker is still a single-threaded blocking loop — the platform thread helpers are dead code and the "decoupled reader thread" acceptance criterion is unmet. Before step 14b (plugin real-time streaming) lands, the worker must be split into reader + inference threads or the 14a roadmap check is premature.
