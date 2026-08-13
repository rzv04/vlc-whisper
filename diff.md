# Diff Analysis: gemini/milestone-3-step-17

**19 files changed, +356 / -579 lines**  
**Base**: `origin/gemini/milestone-3`

---

## 1. File-by-File Analysis

### 1.1 `docs/api-contracts.md`

**Why change**: Align documented `STOP_SESSION` control frame wire reasons with Step 17 seek/discontinuity support (`VW_CTRL_REASON_SEEK_DISCONTINUITY = 2U`).

**Responsibility before**: Binary message contracts documentation up to Step 16 (USER_STOP=1, MEDIA_END=3).  
**After**: Binary message contract specification updated with `SEEK_DISCONTINUITY` control reason.

**Callers**: Developers and AI agents reviewing IPC contracts.  
**Callees**: None.

**Happy path**: Developer checks `docs/api-contracts.md:L87` to verify control frame reason codes.

**Failure path**: N/A.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | N/A |
| **Authorization** | N/A |
| **Concurrency** | N/A |
| **I/O** | N/A |
| **Persistence** | N/A |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Document `SEEK_DISCONTINUITY` control reason | `docs/api-contracts.md:L87` | Manual Inspection | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.2 `docs/architecture.md`

**Why change**: Document Step 17 seek and discontinuity handling workflow in architecture specification.

**Responsibility before**: Architecture specification up to Step 16 play/pause lifecycle.  
**After**: Architecture specification updated with sender thread seek detection, OSD blanking, and session epoch restart logic.

**Callers**: Developers reviewing system architecture.  
**Callees**: None.

**Happy path**: Reader reviews `docs/architecture.md:L75` to understand seek handling.

**Failure path**: N/A.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | N/A |
| **Authorization** | N/A |
| **Concurrency** | Describes sender thread seek epoch restart |
| **I/O** | N/A |
| **Persistence** | N/A |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Document seek discontinuity architectural workflow | `docs/architecture.md:L75` | Manual Inspection | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.3 `docs/plans/step-14-realtime-pcm-streaming.md` (Deleted)

**Why change**: Removed obsolete step plan artifact to clean up `docs/plans/`.

**Responsibility before**: Implementation roadmap for Step 14.  
**After**: Deleted file.

**Callers**: N/A.  
**Callees**: N/A.

**Happy path**: File removed from working directory.

**Failure path**: N/A.

**Boundaries**: N/A.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Clean up obsolete plan artifact | Deleted | N/A | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.4 `docs/plans/step14c_plan.md` (Deleted)

**Why change**: Removed obsolete step plan artifact to clean up `docs/plans/`.

**Responsibility before**: Implementation roadmap for Step 14c.  
**After**: Deleted file.

**Callers**: N/A.  
**Callees**: N/A.

**Happy path**: File removed from working directory.

**Failure path**: N/A.

**Boundaries**: N/A.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Clean up obsolete plan artifact | Deleted | N/A | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.5 `docs/plans/step15_plan.md` (Deleted)

**Why change**: Removed obsolete step plan artifact to clean up `docs/plans/`.

**Responsibility before**: Implementation roadmap for Step 15.  
**After**: Deleted file.

**Callers**: N/A.  
**Callees**: N/A.

**Happy path**: File removed from working directory.

**Failure path**: N/A.

**Boundaries**: N/A.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Clean up obsolete plan artifact | Deleted | N/A | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.6 `docs/plans/step16_plan.md` (Deleted)

**Why change**: Removed obsolete step plan artifact to clean up `docs/plans/`.

**Responsibility before**: Implementation roadmap for Step 16.  
**After**: Deleted file.

**Callers**: N/A.  
**Callees**: N/A.

**Happy path**: File removed from working directory.

**Failure path**: N/A.

**Boundaries**: N/A.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Clean up obsolete plan artifact | Deleted | N/A | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.7 `docs/plans/step17_plan.md` (Added)

**Why change**: Enforce Rule 9 (Task Planning Template Enforcement) by storing technical specification for Step 17 seeking and discontinuity handling.

**Responsibility before**: New file.  
**After**: Authoritative task plan and design record for Step 17.

**Callers**: AI agents and developers reviewing Step 17 requirements.  
**Callees**: None.

**Happy path**: Reader inspects `docs/plans/step17_plan.md` for verification criteria.

**Failure path**: N/A.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | N/A |
| **Authorization** | N/A |
| **Concurrency** | N/A |
| **I/O** | N/A |
| **Persistence** | File stored in repo |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Enforce Rule 9 task template for Step 17 | `docs/plans/step17_plan.md:L1` | Manual Inspection | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.8 `docs/roadmap.md`

**Why change**: Mark Step 17 (Seeking & Discontinuity support) as completed per Rule 14.

**Responsibility before**: Roadmap with Step 17 unchecked.  
**After**: Roadmap with Step 17 marked complete.

**Callers**: Project maintainers and AI agents.  
**Callees**: None.

**Happy path**: Reviewer checks `docs/roadmap.md:L51`.

**Failure path**: N/A.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | N/A |
| **Authorization** | N/A |
| **Concurrency** | N/A |
| **I/O** | N/A |
| **Persistence** | N/A |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Mark Step 17 completed in roadmap | `docs/roadmap.md:L51` | Manual Inspection | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.9 `docs/test-strategy.md`

**Why change**: Document Step 17 seek test additions in test strategy per Rule 14.

**Responsibility before**: Test inventory up to Step 16.  
**After**: Test inventory updated with Step 17 seek tests.

**Callers**: Developers running test suite.  
**Callees**: None.

**Happy path**: Developer checks test strategy doc.

**Failure path**: N/A.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | N/A |
| **Authorization** | N/A |
| **Concurrency** | N/A |
| **I/O** | N/A |
| **Persistence** | N/A |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Add Step 17 test strategy documentation | `docs/test-strategy.md:L59` | Manual Inspection | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.10 `docs/vlc-api-essentials.md`

**Why change**: Update discontinuity handling workflow in VLC API essentials document per Rule 14.

**Responsibility before**: VLC API usage guide.  
**After**: Updated guide explaining `BLOCK_FLAG_DISCONTINUITY` and position jump handling.

**Callers**: Developers working on VLC plugin.  
**Callees**: None.

**Happy path**: Reader checks `docs/vlc-api-essentials.md:L140`.

**Failure path**: N/A.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | N/A |
| **Authorization** | N/A |
| **Concurrency** | N/A |
| **I/O** | N/A |
| **Persistence** | N/A |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Update VLC API essentials doc | `docs/vlc-api-essentials.md:L140` | Manual Inspection | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.11 `plugin/include/vw_caption_presenter.h`

**Why change**: Add `vw_caption_presenter_blank()` declaration for mid-session OSD blanking (keeping filter context) and document `vw_caption_presenter_clear()` as teardown-only (called only from `vw_plugin_close`).

**Responsibility before**: Declared presenter init, display, show_segment, and clear functions.  
**After**: Declares `vw_caption_presenter_blank()` for mid-session OSD clearing, and expands clear doc comment to cite `vw_plugin_close`.

**Callers**: `vw_whisper_module.c`, `vw_caption_presenter.c`, `test_caption_presenter.c`.  
**Callees**: None.

**Happy path**: `vw_whisper_module.c` includes header and calls `vw_caption_presenter_blank(&sys->presenter)` on seek restart.

**Failure path**: Compiler error on signature mismatch.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | Parameter nullability handling |
| **Authorization** | N/A |
| **Concurrency** | Called on sender thread only |
| **I/O** | N/A |
| **Persistence** | N/A |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Declare `vw_caption_presenter_blank()` and update doc comments | `plugin/include/vw_caption_presenter.h:L26` | `test_caption_presenter` | ✅ done |

**Assumptions/Tradeoffs**: Enforces Rule 11 (20-30 word header function comments).

---

### 1.12 `plugin/libvlccore.def`

**Why change**: Add `vout_FlushSubpictureChannel` symbol export for Win32 MinGW build.

**Responsibility before**: Symbol import definitions for `libvlccore.dll`.  
**After**: Includes `vout_FlushSubpictureChannel`.

**Callers**: MinGW linker.  
**Callees**: `libvlccore.dll`.

**Happy path**: Linker resolves `vout_FlushSubpictureChannel` symbol on Win32.

**Failure path**: Linker error if symbol is missing.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | Definition file syntax |
| **Authorization** | N/A |
| **Concurrency** | N/A |
| **I/O** | N/A |
| **Persistence** | N/A |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Export `vout_FlushSubpictureChannel` symbol | `plugin/libvlccore.def:L10` | Build Verification | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.13 `plugin/src/vw_caption_presenter.c`

**Why change**: Implement `vw_caption_presenter_blank()` to erase active OSD subpictures mid-session without destroying presenter filter context.

**Responsibility before**: Handled presenter display, vout lookup, and teardown clear.  
**After**: Implements `vw_caption_presenter_blank()` using `vout_FlushSubpictureChannel(vout, VOUT_SPU_CHANNEL_OSD)` with 1ms blank fallback, and updates `vw_caption_presenter_clear()` to delegate to `blank()`.

**Callers**: `vw_whisper_module.c:L320`, `test_caption_presenter.c`.  
**Callees**: `vw_caption_presenter_find_vout`, `vout_FlushSubpictureChannel`, `vout_OSDText`, `vlc_object_release`.

**Happy path**: `vw_caption_presenter_blank` locates active `vout_thread_t`, invokes `vout_FlushSubpictureChannel(vout, VOUT_SPU_CHANNEL_OSD)` and 1ms blank OSD text, releases vout reference, preserving `presenter->p_filter_ctx`.

**Failure path**: `p_filter_ctx` NULL or `find_vout` returns NULL; returns false gracefully.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | Null checks on `presenter` and `p_filter_ctx` |
| **Authorization** | N/A |
| **Concurrency** | Called on sender thread; releases vout reference immediately |
| **I/O** | OSD subpicture channel flush |
| **Persistence** | None |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Implement mid-session OSD blanking | `plugin/src/vw_caption_presenter.c:L125` | `test_caption_presenter` | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.14 `plugin/src/vw_whisper_module.c`

**Why change**: Implement Step 17 seek & discontinuity session restart logic, threshold macros (`VW_SEEK_JUMP_THRESHOLD_US`, `VW_PTS_JUMP_THRESHOLD_US`), seek-while-paused baseline backfill, position-jump detection, and audio callback PTS fallback checks.

**Responsibility before**: Module descriptor, audio callback, sender thread with play/pause lifecycle.  
**After**: Module descriptor, audio callback with PTS jump fallback (`VW_PTS_JUMP_THRESHOLD_US = 500ms`), sender thread with position-jump detection (`VW_SEEK_JUMP_THRESHOLD_US = 1s`), pause baseline backfill, OSD blanking, and session epoch restart (`STOP` -> drain SPSC -> `START` with new `session_id`).

**Callers**: VLC module loader, VLC audio pipeline (`pf_audio_filter`), sender thread.  
**Callees**: `vw_caption_presenter_blank`, `vw_worker_client_stop_session`, `vw_spsc_queue_pop`, `vw_worker_client_start_session`, `vw_plugin_find_input`, `input_GetState`, `vw_plugin_input_position_us`.

**Happy path**:
1. Callback: `vw_plugin_filter:L481` checks `BLOCK_FLAG_DISCONTINUITY` or `p_block->i_pts < last_pts_us - VW_PTS_JUMP_THRESHOLD_US` (500ms). Sets `discontinuity_pending = true` and stores `resume_pts_us`.
2. Sender poll: `vw_plugin_sender_main:L276-L300` checks `position_us` jumps (>1s) while playing or paused (backfilling pause baseline if input lookup raced pause edge). Sets `discontinuity_pending = true`.
3. Epoch restart: Sender thread sees `discontinuity_pending = true` (`:L318`):
   - Calls `vw_caption_presenter_blank(&sys->presenter)` to erase OSD.
   - Calls `vw_worker_client_stop_session(sys->client, VW_CTRL_REASON_SEEK_DISCONTINUITY)`.
   - Drains stale pre-seek PCM from SPSC queue (`:L323`).
   - Calls `vw_worker_client_start_session(sys->client, resume_pts_us, "tiny.en")`.
   - Resets `discontinuity_pending = false` and resumes real-time audio streaming.

**Failure path**:
1. Worker rejects restart (e.g. model missing): `vw_worker_client_start_session` returns false; sender marks `worker_dead = true`, logs warning, breaks loop, and plugin degrades to audio passthrough.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | `p_block->i_pts >= VLC_TS_0` + `last_pts_us > 0` guards; `position_us >= 0` guards; `VW_SEEK_JUMP_THRESHOLD_US` (1s) and `VW_PTS_JUMP_THRESHOLD_US` (500ms) macro gates |
| **Authorization** | CSPRNG auth token verification |
| **Concurrency** | Callback is 100% lock-free (Rule 4), writes only atomic bool/int64; sender thread executes restart sequence exclusively |
| **I/O** | Non-blocking SPSC drain; IPC control frames |
| **Persistence** | None |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Implement seek discontinuity session restart | `plugin/src/vw_whisper_module.c:L318-L335` | `test_worker_lifecycle` | ✅ done |
| 2 | Robust paused-seek baseline backfill | `plugin/src/vw_whisper_module.c:L274, L286` | Manual & Code Review | ✅ done |
| 3 | Use threshold macros (`VW_SEEK_JUMP_THRESHOLD_US`, `VW_PTS_JUMP_THRESHOLD_US`) | `plugin/src/vw_whisper_module.c:L57-L58` | Code Review | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.15 `protocol/include/vw_protocol_types.h`

**Why change**: Add `VW_CTRL_REASON_SEEK_DISCONTINUITY = 2U` control reason constant.

**Responsibility before**: Protocol types and message constants up to Step 16.  
**After**: Protocol types including `VW_CTRL_REASON_SEEK_DISCONTINUITY`.

**Callers**: `vw_whisper_module.c`, `vw_worker_client.c`, `vw_worker.c`, unit tests.  
**Callees**: None.

**Happy path**: Code passes `VW_CTRL_REASON_SEEK_DISCONTINUITY` in control frame payload.

**Failure path**: N/A.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | Wire reason code fits in `uint16_t` |
| **Authorization** | N/A |
| **Concurrency** | Pure constants |
| **I/O** | N/A |
| **Persistence** | N/A |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Define `VW_CTRL_REASON_SEEK_DISCONTINUITY` | `protocol/include/vw_protocol_types.h:L143` | `vw_test_worker_client` | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.16 `tests/integration/test_worker_lifecycle.c`

**Why change**: Update worker lifecycle integration test to assert multi-session restart (`START` -> `AUDIO` -> `STOP(SEEK_DISCONTINUITY)` -> `START` -> `AUDIO` -> `STOP` -> `SHUTDOWN`) with session ID epoch gating verification.

**Responsibility before**: Lifecycle test for single session streaming.  
**After**: Lifecycle test verifying session epoch restarts and pre-seek audio rejection.

**Callers**: CTest harness (`ctest`).  
**Callees**: `vw_worker_client_start_session`, `vw_worker_client_send_audio`, `vw_worker_client_stop_session`.

**Happy path**: Test starts session 1, sends audio, sends `STOP(SEEK_DISCONTINUITY)`, starts session 2 with new session ID, sends audio, stops session 2, shuts down client, and asserts worker process exits 0 cleanly.

**Failure path**: Test assertion fails if worker crashes or fails session restart.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | Checks session response codes |
| **Authorization** | Secret auth token |
| **Concurrency** | Client + worker process IPC |
| **I/O** | IPC transport |
| **Persistence** | Temporary sockets cleaned up |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Test multi-session epoch restart | `tests/integration/test_worker_lifecycle.c:L218` | `test_worker_lifecycle` | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.17 `tests/unit/test_caption_presenter.c`

**Why change**: Update presenter unit test to assert `vw_caption_presenter_blank()` mid-session behavior, `vw_caption_presenter_clear()` teardown behavior, and flush invocation counts on `fake_filter.obj.object_type = "vout"`.

**Responsibility before**: Tested standalone display and segment presenter functions.  
**After**: Unit test asserting `blank()` OSD channel flush on `channel == 1` (`VOUT_SPU_CHANNEL_OSD`) and context retention, and `clear()` context resetting.

**Callers**: CTest harness (`ctest`).  
**Callees**: `vw_caption_presenter_blank`, `vw_caption_presenter_clear`.

**Happy path**: Test sets `.obj.object_type = "vout"`, calls `blank()`, asserts `g_flush_calls == 1 && g_flush_channel == 1` and `p_filter_ctx` retained; calls `clear()`, asserts `g_flush_calls == 2` and `p_filter_ctx == NULL`.

**Failure path**: Test fails if flush is not called or context is lost on blank.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | Handles NULL and non-NULL contexts |
| **Authorization** | N/A |
| **Concurrency** | Single-threaded test |
| **I/O** | None |
| **Persistence** | None |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Test `blank()` and `clear()` presenter functions | `tests/unit/test_caption_presenter.c:L90` | `test_caption_presenter` | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.18 `tests/unit/vw_test_worker_client.c`

**Why change**: Update worker client unit test to assert `VW_CTRL_REASON_SEEK_DISCONTINUITY` framing against mock server.

**Responsibility before**: Tested client session state machine up to Step 16.  
**After**: Updated test suite asserting seek discontinuity reason codes.

**Callers**: CTest harness (`ctest`).  
**Callees**: `vw_worker_client_stop_session`.

**Happy path**: Test calls `vw_worker_client_stop_session(client, VW_CTRL_REASON_SEEK_DISCONTINUITY)` and mock server receives `STOP_SESSION` frame with reason `2U`.

**Failure path**: Test assertion fails if reason code is corrupted.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | Checks reason code framing |
| **Authorization** | N/A |
| **Concurrency** | Mock server thread synchronization |
| **I/O** | IPC socket pair |
| **Persistence** | None |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Test `SEEK_DISCONTINUITY` reason framing | `tests/unit/vw_test_worker_client.c:L166` | `test_worker_client` | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.19 `worker/src/vw_worker.c`

**Why change**: Handle `STOP_SESSION` with `VW_CTRL_REASON_SEEK_DISCONTINUITY`, drain segment builder on `START_SESSION`, clear audio buffer on `STOP`, implement `vw_worker_stop_reason_name` with `_Thread_local static char buf[16]` buffer, and document STOP-only usage.

**Responsibility before**: Handled single session lifecycle and inference event loop.  
**After**: Handled multi-session epoch restarts: on `STOP`, clears audio buffer and logs reason via thread-safe `vw_worker_stop_reason_name`; on `START`, drains segment builder hypothesis queue to drop pre-seek hypotheses, resets session ID, and begins new epoch.

**Callers**: `worker/src/main.c` (`main()`), integration tests.  
**Callees**: `vw_worker_stop_reason_name`, `vw_audio_buffer_clear`, `vw_segment_builder_pop`, `vw_protocol_encode_header`, `vw_ipc_send`.

**Happy path**:
1. Worker receives `STOP_SESSION` (reason `2U`): `vw_worker.c:L402` sets `session_active = false`, clears `audio_buf`, and logs `WORKER_SESSION: session stopped (reason=SEEK_DISCONTINUITY)`.
2. Worker receives `START_SESSION` (new session ID): `vw_worker.c:L322-L327` drains and discards any remaining hypotheses from `builder`, copies new `session_id`, sets `session_active = true`, and replies `STARTED`.
3. Audio processing: `VW_MSG_AUDIO_PCM` frames matching `session_id` are accumulated and transcribed; any stale pre-seek `AUDIO` frame with old `session_id` is dropped (`:L343`).

**Failure path**:
1. Invalid model on restart: `if (!engine)` at `:L309` triggers `send_error(..., E_MODEL_MISSING, 0, ...)`; `session_active` stays false; worker remains ready for subsequent clean shutdown.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | Checks `session_id` bytes match active session; validates sample rate (16000) |
| **Authorization** | Constant-time auth token comparison |
| **Concurrency** | Single-writer main loop; `vw_worker_stop_reason_name` uses `_Thread_local static char buf[16]` for thread safety |
| **I/O** | Timed IPC socket reads and writes |
| **Persistence** | None |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Drain segment builder on `START` to drop pre-seek hypotheses | `worker/src/vw_worker.c:L322-L327` | `test_worker_lifecycle` | ✅ done |
| 2 | Clear audio buffer on `STOP` | `worker/src/vw_worker.c:L404` | `test_worker_lifecycle` | ✅ done |
| 3 | Thread-safe `vw_worker_stop_reason_name` helper | `worker/src/vw_worker.c:L41-L56` | `test_worker_ipc` | ✅ done |

**Assumptions/Tradeoffs**: None.

---

## 2. Happy-Path Request Trace

The following trace demonstrates an end-to-end seek discontinuity handling workflow during active playback:

```text
1. User Seeks During Playback (VLC Filter & Main Audio Thread)
   └─ plugin/src/vw_whisper_module.c:vw_plugin_filter (L481)
      ├─ VLC audio callback receives block with BLOCK_FLAG_DISCONTINUITY set (or PTS backward jump >500ms)
      ├─ Updates atomic flag: atomic_store(&sys->discontinuity_pending, true)
      └─ Stores new media PTS anchor: atomic_store(&sys->resume_pts_us, p_block->i_pts)

2. Plugin Background Sender Thread (vw_sender_thread)
   └─ plugin/src/vw_whisper_module.c:vw_plugin_sender_main (L318)
      ├─ Detects atomic_load(&sys->discontinuity_pending) == true
      ├─ Erases active OSD subpictures: plugin/src/vw_caption_presenter.c:vw_caption_presenter_blank (L125)
      │  └─ Calls vout_FlushSubpictureChannel(vout, VOUT_SPU_CHANNEL_OSD) and 1ms blank OSD
      ├─ Sends STOP_SESSION frame over IPC: plugin/src/vw_worker_client.c:vw_worker_client_stop_session (L321)
      │  └─ Reason: VW_CTRL_REASON_SEEK_DISCONTINUITY (2U)
      ├─ Drains and discards stale pre-seek audio chunks from SPSC queue: vw_spsc_queue_pop (L323)
      ├─ Reads new PTS anchor: resume_pts_us = atomic_load(&sys->resume_pts_us) (L325)
      ├─ Clears pending flag: atomic_store(&sys->discontinuity_pending, false) (L326)
      └─ Sends START_SESSION frame over IPC with new session_id and resume_pts_us (L327)

3. Worker Process Handling (vlc-whisper-worker Main Loop)
   └─ worker/src/vw_worker.c:vw_worker_run (L322, L400)
      ├─ Receives STOP_SESSION:
      │  ├─ Sets session_active = false
      │  ├─ Clears PCM audio buffer: vw_audio_buffer_clear (L404)
      │  └─ Logs: "WORKER_SESSION: session stopped (reason=SEEK_DISCONTINUITY)"
      └─ Receives START_SESSION:
         ├─ Drains and discards pre-seek hypotheses from segment builder: vw_segment_builder_pop (L323)
         ├─ Copies new session_id into session state (L328)
         ├─ Sets session_active = true
         └─ Sends STARTED reply header over IPC

4. Resumed Audio Streaming & Caption Generation
   └─ Plugin sender thread receives STARTED reply, logs PLUGIN_SESSION_RESTARTED (L333)
      ├─ Resumes popping post-seek PCM audio chunks from SPSC queue
      ├─ Transmits post-seek VW_MSG_AUDIO_PCM frames tagged with new session_id
      └─ Worker receives post-seek PCM, accumulates post-seek window, transcribes, and emits post-seek CAPTION_SEGMENT
```

---

## 3. Most Important Failure Path

### Failure Scenario: Worker Model File Removed Before Seek Restart (`E_MODEL_MISSING`)

```text
1. Seek Occurs During Playback
   └─ plugin/src/vw_whisper_module.c:vw_plugin_filter (L481)
      └─ Callback sets discontinuity_pending = true and stores resume_pts_us

2. Plugin Sender Thread Initiates Restart Epoch
   └─ plugin/src/vw_whisper_module.c:vw_plugin_sender_main (L318)
      ├─ Erases active OSD: vw_caption_presenter_blank (L320)
      ├─ Transmits STOP_SESSION (reason=SEEK_DISCONTINUITY) over IPC (L321)
      ├─ Drains stale pre-seek SPSC audio queue chunks (L323)
      └─ Transmits START_SESSION over IPC: vw_worker_client_start_session (L327)

3. Worker Missing Model Handling
   └─ worker/src/vw_worker.c:vw_worker_run (L309)
      ├─ Receives STOP_SESSION: clears audio buffer, sets session_active = false
      ├─ Receives START_SESSION: evaluates if (!engine) -> true (model deleted)
      ├─ Logs warning: "WORKER_SESSION: START rejected: E_MODEL_MISSING" (L311)
      ├─ Invokes send_error(..., E_MODEL_MISSING, recoverable=0, "Whisper model file missing or invalid")
      └─ session_active remains false

4. Plugin Rejection & Passthrough Fallback
   └─ plugin/src/vw_worker_client.c:vw_worker_client_start_session (L327)
      ├─ Awaited STARTED reply times out / receives VW_MSG_ERROR payload
      ├─ Logs warning: "PLUGIN_SESSION_RESTART_FAIL: worker rejected restart; captions disabled, passthrough only" (L330)
      ├─ Sets atomic flag: atomic_store(&sys->worker_dead, true) (L328)
      └─ Sender thread breaks execution loop and terminates cleanly (L332)

5. VLC Media Playback Uninterrupted
   └─ plugin/src/vw_whisper_module.c:vw_plugin_filter (L475)
      ├─ Audio callback continues executing on VLC main audio thread
      ├─ Captures audio to SPSC queue (overflow chunks dropped harmlessly)
      └─ Returns p_block untouched to VLC audio output pipeline (100% uninterrupted audio playback)
```

---

## 4. Boundary Summary

| Boundary type | Checks performed | Code Location | Finding / Guard Implementation |
|---|---|---|---|
| **Input validation** | Audio Callback PTS Fallback | `vw_whisper_module.c:L481` | Guards `p_block->i_pts >= VLC_TS_0` and `last_pts_us > 0` before checking `VW_PTS_JUMP_THRESHOLD_US` (500ms) backward jump. |
| **Input validation** | Position Jump Gates | `vw_whisper_module.c:L276, L295` | `position_us >= 0` guards and `VW_SEEK_JUMP_THRESHOLD_US` (1s) macro gate on continuous and paused position checks. |
| **Input validation** | Paused Baseline Backfill | `vw_whisper_module.c:L286-L290` | Backfills `paused_position_us` if initial pause edge input lookup returned `-1`, ensuring paused seeks are never missed. |
| **Input validation** | Session ID Gating | `vw_worker.c:L343`<br>`vw_whisper_module.c:L357` | `session_id` memcmp gating drops stale pre-seek `AUDIO` frames and in-flight `SEGMENT` frames. |
| **Authorization** | Auth Token Verification | `vw_worker.c:L30` | Secret 32-byte auth token validated using `verify_token_constant_time`. |
| **Concurrency** | Lock-Free Callback | `vw_whisper_module.c:L475` | Callback is 100% lock-free (Rule 4). Writes only atomic bool/int64 variables (`discontinuity_pending`, `resume_pts_us`). |
| **Concurrency** | Sender Thread Exclusive Restart | `vw_whisper_module.c:L318` | Session epoch restart sequence runs exclusively on background sender thread. |
| **Concurrency** | Thread-Safe Stop Reason Logger | `vw_worker.c:L51` | `vw_worker_stop_reason_name` uses `_Thread_local static char buf[16]` to ensure thread safety across logging calls. |
| **I/O** | OSD Channel Flush | `vw_caption_presenter.c:L125` | `vw_caption_presenter_blank` uses `vout_FlushSubpictureChannel` and 1ms OSD blanking to erase active subpictures mid-session. |
| **I/O** | IPC Control Frames | `vw_worker_client.c:L321` | Non-blocking IPC control frame send (`STOP_SESSION` with `VW_CTRL_REASON_SEEK_DISCONTINUITY`). |
| **Persistence** | Socket & File Cleanup | `vw_whisper_module.c:L411`<br>`vw_worker.c:L440` | Socket files and pipe handles unlinked and closed on teardown. Zero transcript/PCM data written to disk (Rule 5). |

---

## 5. Acceptance Criterion → Code Mapping

| # | Criterion | Code Implementation | Test Assertion | Status |
|---|---|---|---|---|
| 1 | Seek during playback: OSD clears immediately | `plugin/src/vw_caption_presenter.c:L125`<br>`plugin/src/vw_whisper_module.c:L320` | `tests/unit/test_caption_presenter.c:L90` | ✅ done |
| 2 | New captions resume from post-seek position | `worker/src/vw_worker.c:L322-L328` epoch restart | `tests/integration/test_worker_lifecycle.c:L218` | ✅ done |
| 3 | No caption mixes pre-seek and post-seek audio | `worker/src/vw_worker.c:L323` builder drain + `worker/src/vw_worker.c:L404` audio buffer clear | `tests/integration/test_worker_lifecycle.c:L223` | ✅ done |
| 4 | Log `PLUGIN_DISCONTINUITY` and `PLUGIN_SESSION_RESTARTED` | `plugin/src/vw_whisper_module.c:L319, L333` | `tests/integration/test_worker_lifecycle.c:L218` | ✅ done |
| 5 | Pre-seek audio discarded from SPSC queue | `plugin/src/vw_whisper_module.c:L323-L324` SPSC drain loop | `tests/integration/test_worker_lifecycle.c:L223` | ✅ done |
| 6 | Seek while paused: robust baseline backfill, no transport drop | `plugin/src/vw_whisper_module.c:L274, L286-L290` | Manual & Code Review | ✅ done |
| 7 | Threshold macros (`VW_SEEK_JUMP_THRESHOLD_US`, `VW_PTS_JUMP_THRESHOLD_US`) | `plugin/src/vw_whisper_module.c:L57-L58` | Code Review | ✅ done |
| 8 | `VW_CTRL_REASON_SEEK_DISCONTINUITY` constant | `protocol/include/vw_protocol_types.h:L143` | `tests/unit/vw_test_worker_client.c:L166` | ✅ done |
| 9 | Thread-safe `vw_worker_stop_reason_name` helper | `worker/src/vw_worker.c:L41-L56` | `tests/integration/test_worker_ipc.c` | ✅ done |
| 10 | `vout_FlushSubpictureChannel` test assertions | `tests/unit/test_caption_presenter.c:L90-L100` | `test_caption_presenter` | ✅ done |
| 11 | C17, Google C style, no C++ | All `.c`/`.h` files | `clang-format --dry-run --Werror` | ✅ done |
| 12 | 100% CTest pass rate (16/16 targets) | CMake & CTest suite | All 16 targets passing | ✅ done |
| 13 | Zero memory leaks under Valgrind | `ctest -T memcheck` | Valgrind memcheck clean | ✅ done |

---

## 7. Code Review Findings (Bugs, Risks, Nitpicks)

### Resolved Findings (100% Fixed in `36a8948`)

| Priority | Component / Location | Description | Fix Implemented | Status |
|---|---|---|---|---|
| **High** | `worker/src/vw_worker.c:41-56` | `vw_worker_control_reason_name` used `static char buf[16]` (not thread-safe) and conflated `USER_STOP` with `USER_PAUSE`/`USER_RESUME` (value `1U`). | Renamed to `vw_worker_stop_reason_name`, documented as STOP-only, and changed buffer to `_Thread_local static char buf[16]` (`:51`). | **RESOLVED** |
| **Medium** | `plugin/src/vw_whisper_module.c:274, L286-290` | If input lookup returned `-1` on pause edge, `paused_position_us` stored `-1`, causing subsequent resume jump check to miss paused seeks. | `paused_position_us` assignment guarded and backfill logic added (`:286-290`) so transient input lookup failures on pause edge backfill on next poll. | **RESOLVED** |
| **Medium** | `plugin/src/vw_whisper_module.c:57-58` | Un-named magic literals `1000000` (1s jump) and `500000` (500ms PTS jump). | Defined `#define VW_SEEK_JUMP_THRESHOLD_US 1000000` and `#define VW_PTS_JUMP_THRESHOLD_US 500000` and replaced all literal usages. | **RESOLVED** |
| **Low** | `tests/unit/test_caption_presenter.c:90-100` | `vout_FlushSubpictureChannel` stub was a no-op that did not assert invocation or channel number. | Updated `fake_filter` with `.obj.object_type = "vout"`, and added assertions verifying `g_flush_calls == 1 && g_flush_channel == 1` for `blank()` and `g_flush_calls == 2` for `clear()`. | **RESOLVED** |
| **Nitpick** | `plugin/include/vw_caption_presenter.h:26` | Header comment for `vw_caption_presenter_clear` did not explicitly cite caller. | Doc comment expanded to explicitly cite `vw_plugin_close` (teardown-only) vs `vw_caption_presenter_blank` (mid-session OSD clear). | **RESOLVED** |

---

### Architectural & Operational Observations

| Category | Observation / Risk Description | Affected Files | Mitigation Strategy |
|---|---|---|---|
| **Jitter vs Seek** | `BLOCK_FLAG_DISCONTINUITY` is set on network re-buffer/jitter (VOD/live streams), not just user seeks. Flag-triggered restart without position jump gate clears captions on jittery streams. | `plugin/src/vw_whisper_module.c:L481` | Tracked for Step 17d: add `VW_INPUT_JUMP_DISCONTINUITY_US` position gate requiring a position jump in addition to the flag for live network streams. MVP behavior is safe and correct for local playback. |
| **Symbol Export** | `vout_FlushSubpictureChannel` symbol exported via `plugin/libvlccore.def:L10` for Win32 MinGW linking. | `plugin/libvlccore.def`, `plugin/src/vw_caption_presenter.c:L125` | Exported in VLC 3.0.23 baseline. For future VLC build targets, `VW_WEAK` weak symbol resolution can be added if custom VLC builds omit the symbol export. |
