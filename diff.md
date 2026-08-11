# Diff Analysis: gemini/milestone-3

**13 files changed, +369 / -34 lines**  
**Base**: `gemini/milestone-3`

---

## 1. File-by-File Analysis

### 1.1 `docs/api-contracts.md`

**Why change**: Document `VW_CTRL_REASON_USER_PAUSE` (1) and `VW_CTRL_REASON_USER_RESUME` (1) control frame reason codes for `PAUSE` and `RESUME` frames per Step 16 specification.

**Responsibility before**: Defined IPC message types (`VW_MSG_PAUSE`=0x0006, `VW_MSG_RESUME`=0x0007), payload structures, and session lifecycle states with TBD reason codes.  
**After**: Fully specified numeric control reason codes (`USER_PAUSE` = 1, `USER_RESUME` = 1) and defined worker in-flight window clearing behavior upon pause.

**Callers**: Developers and AI agents reviewing protocol contracts.  
**Callees**: None.

**Happy path**: Client invokes `vw_worker_client_pause_session()`, populating `vw_msg_control_t.reason = VW_CTRL_REASON_USER_PAUSE` (1) in compliance with `docs/api-contracts.md:L139`.

**Failure path**: N/A (specification document).

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | N/A |
| **Authorization** | N/A |
| **Concurrency** | Read-only documentation file |
| **I/O** | Read by LLM harness / developers |
| **Persistence** | Static repository file |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Document VW_CTRL_REASON_USER_PAUSE & USER_RESUME | `docs/api-contracts.md:L139` | Manual Inspection | ✅ done |

**Assumptions/Tradeoffs**: Reason code 1U is used for both user-initiated pause and resume control frames as specified in API contracts.

---

### 1.2 `docs/architecture.md`

**Why change**: Update architecture documentation and data flow diagrams to include sender thread `input_GetState` polling, IPC `PAUSE`/`RESUME` control frames, and worker audio window clearing.

**Responsibility before**: Documented Step 14c / Step 15 streaming architecture with SPSC queue and presenter display.  
**After**: Detailed Step 16 play/pause lifecycle including background thread input tree walk, PCM discarding during pause, and worker analysis window reset.

**Callers**: Developers and maintainers reviewing system layout.  
**Callees**: None.

**Happy path**: Developer inspects `docs/architecture.md:L42` to verify sender thread pause polling architecture.

**Failure path**: N/A.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | N/A |
| **Authorization** | N/A |
| **Concurrency** | Read-only documentation file |
| **I/O** | Read by LLM harness / developers |
| **Persistence** | Static repository file |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Document sender thread pause polling architecture | `docs/architecture.md:L42` | Manual Inspection | ✅ done |

**Assumptions/Tradeoffs**: Pause state polling is executed on background sender thread during idle/send cadence (5ms/20ms).

---

### 1.3 `docs/plans/step15_plan.md`

**Why change**: Record implementation plan, architectural rationale, and postmortem latency notes for Step 15 (caption presenter integration on sender thread).

**Responsibility before**: New plan document added for Step 15.  
**After**: Comprehensive 81-line specification and postmortem analysis for Step 15 presenter display wiring and 8s window batch inference latency.

**Callers**: Developers tracking milestone deliverables.  
**Callees**: None.

**Happy path**: N/A (documentation).

**Failure path**: N/A (documentation).

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | N/A |
| **Authorization** | N/A |
| **Concurrency** | Read-only documentation file |
| **I/O** | Read by LLM harness / developers |
| **Persistence** | Static repository file |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Add Step 15 plan and latency postmortem | `docs/plans/step15_plan.md:L1-L81` | Manual Inspection | ✅ done |

**Assumptions/Tradeoffs**: Captions display ~8s behind live audio due to batch window geometry; targeted by look-ahead in Step 17.

---

### 1.4 `docs/plans/step16_plan.md`

**Why change**: Document design, object walk logic, protocol rationale, and Definition of Done for Step 16 (Play/Pause lifecycle).

**Responsibility before**: New plan document added for Step 16.  
**After**: Detailed 96-line technical specification covering `input_GetState` object walk, IPC control frames, sender thread PCM drop while paused, and unit/integration test cases.

**Callers**: Developers and AI agents verifying Step 16 design requirements.  
**Callees**: None.

**Happy path**: N/A (documentation).

**Failure path**: N/A (documentation).

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | N/A |
| **Authorization** | N/A |
| **Concurrency** | Read-only documentation file |
| **I/O** | Read by LLM harness / developers |
| **Persistence** | Static repository file |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Add Step 16 design blueprint and test plan | `docs/plans/step16_plan.md:L1-L96` | Manual Inspection | ✅ done |

**Assumptions/Tradeoffs**: Object walk avoids `vlc_object_find_name` to prevent MinGW weak symbol linkage crashes.

---

### 1.5 `docs/roadmap.md`

**Why change**: Mark Step 15 and Step 16 completed (`[x]`) in milestone 3 roadmap and document sub-item 17e (transcription quality pass).

**Responsibility before**: Steps 15 and 16 marked pending (`[ ]`).  
**After**: Steps 15 and 16 marked completed with implementation summaries; roadmap updated to include 17e.

**Callers**: Project maintainers tracking milestone progress.  
**Callees**: None.

**Happy path**: Maintainer checks `docs/roadmap.md:L49-L50` to verify completed deliverables.

**Failure path**: N/A.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | N/A |
| **Authorization** | N/A |
| **Concurrency** | Read-only documentation file |
| **I/O** | Read by LLM harness / developers |
| **Persistence** | Static repository file |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Mark Step 15 & Step 16 complete in roadmap | `docs/roadmap.md:L49-L50` | Manual Inspection | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.6 `docs/test-strategy.md`

**Why change**: Document test strategy updates for Step 15 (caption presenter stubs) and Step 16 (unit fake server PAUSE/RESUME sequence and integration lifecycle tests).

**Responsibility before**: Documented test coverage up to Step 14c.  
**After**: Updated test strategy covering `vw_test_worker_client` pause/resume assertions and `test_worker_lifecycle` mid-stream pause/resume verification.

**Callers**: Developers executing test suite.  
**Callees**: None.

**Happy path**: Developer reviews `docs/test-strategy.md:L56-L58` for pause/resume test coverage requirements.

**Failure path**: N/A.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | N/A |
| **Authorization** | N/A |
| **Concurrency** | Read-only documentation file |
| **I/O** | Read by LLM harness / developers |
| **Persistence** | Static repository file |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Document Step 15 & Step 16 test strategy | `docs/test-strategy.md:L56-L58` | Manual Inspection | ✅ done |

**Assumptions/Tradeoffs**: Live VLC presenter testing remains manual; automated suite tests IPC framing and worker lifecycle.

---

### 1.7 `plugin/include/vw_worker_client.h`

**Why change**: Export `vw_worker_client_pause_session` and `vw_worker_client_resume_session` API functions for plugin sender thread and tests.

**Responsibility before**: Declared client API methods for `start_session`, `send_audio`, `stop_session`, `shutdown`, `disconnect`, `receive_frame`.  
**After**: Extended interface with `vw_worker_client_pause_session` and `vw_worker_client_resume_session`.

**Callers**: `plugin/src/vw_whisper_module.c`, `tests/unit/vw_test_worker_client.c`, `tests/integration/test_worker_lifecycle.c`.  
**Callees**: Implemented in `plugin/src/vw_worker_client.c`.

**Happy path**: Sender thread includes `vw_worker_client.h` and invokes `vw_worker_client_pause_session(sys->client)` upon detecting `PAUSE_S`.

**Failure path**: N/A (header file).

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | Functions check `!client` before proceeding |
| **Authorization** | Session ID populated from active client instance |
| **Concurrency** | Invoked from single background sender thread |
| **I/O** | N/A (header declaration) |
| **Persistence** | N/A |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Declare vw_worker_client_pause_session | `plugin/include/vw_worker_client.h:L90` | `vw_test_worker_client.c` | ✅ done |
| 2 | Declare vw_worker_client_resume_session | `plugin/include/vw_worker_client.h:L98` | `vw_test_worker_client.c` | ✅ done |

**Assumptions/Tradeoffs**: Thread-safe invocation when called from plugin background sender thread.

---

### 1.8 `plugin/src/vw_whisper_module.c`

**Why change**: Implement pause detection via `input_GetState` object walk on sender thread, send `PAUSE`/`RESUME` IPC control frames, discard queued PCM while paused, wire presenter display on `VW_MSG_CAPTION_SEGMENT`, and clear presenter on plugin close.

**Responsibility before**: Sender thread forwarded SPSC audio chunks over IPC and discarded `CAPTION_SEGMENT` worker frames without rendering.  
**After**: Sender thread polls `input_GetState` via `vw_plugin_input_is_paused()`, sends `PAUSE`/`RESUME` frames on transition, drops SPSC chunks while paused, calls `vw_caption_presenter_show_segment()` when `VW_MSG_CAPTION_SEGMENT` arrives, and clears presenter on close (`vw_caption_presenter_clear()`).

**Callers**: VLC module lifecycle (`vw_plugin_open`, `vw_plugin_close`, filter callback, and sender thread).  
**Callees**: `vw_plugin_input_is_paused()`, `vw_worker_client_pause_session()`, `vw_worker_client_resume_session()`, `vw_caption_presenter_show_segment()`, `vw_caption_presenter_clear()`, `vlc_list_children()`, `vlc_list_release()`, `input_GetState()`.

**Happy path**: User pauses playback → `vw_plugin_input_is_paused()` detects `PAUSE_S` → `vw_worker_client_pause_session()` sends `VW_MSG_PAUSE` → SPSC queue pops and drops audio (`if (paused) continue;`) → user resumes → `vw_worker_client_resume_session()` sends `VW_MSG_RESUME` → audio forwarding resumes.

**Failure path**: If VLC object tree has no "input" node, `vw_plugin_input_is_paused()` safely returns false without NULL pointer dereference or crash.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | Checks `!p_filter`, validates `cur->obj.object_type`, checks `children != NULL`, calls `vlc_list_release(children)` |
| **Authorization** | N/A |
| **Concurrency** | Read-only object tree walk on background sender thread; `paused` state thread-local to sender loop |
| **I/O** | Discards SPSC audio chunks locally during pause to prevent queue backlog |
| **Persistence** | N/A |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Poll input pause state via object walk | `plugin/src/vw_whisper_module.c:L316-L343` | Manual VLC acceptance | ✅ done |
| 2 | Send PAUSE/RESUME on state transition | `plugin/src/vw_whisper_module.c:L227-L236` | `vw_test_worker_client.c` | ✅ done |
| 3 | Discard SPSC queue chunks while paused | `plugin/src/vw_whisper_module.c:L244-L246` | Manual VLC acceptance | ✅ done |
| 4 | Presenter display segment rendering | `plugin/src/vw_whisper_module.c:L286` | `test_caption_presenter.c` | ✅ done |
| 5 | Presenter OSD clear on plugin close | `plugin/src/vw_whisper_module.c:L526` | `test_caption_presenter.c` | ✅ done |

**Assumptions/Tradeoffs**: Object walk searches parent chain and children list of each parent for `object_type == "input"`, avoiding deprecated `vlc_object_find_name`.

---

### 1.9 `plugin/src/vw_worker_client.c`

**Why change**: Refactor control frame transmission (`STOP_SESSION`, `PAUSE`, `RESUME`) into a unified `send_control_frame` helper function with fail-closed transport drop on write failure, and implement `vw_worker_client_pause_session` and `vw_worker_client_resume_session`.

**Responsibility before**: `vw_worker_client_stop_session` manually encoded `VW_MSG_STOP_SESSION` header/payload.  
**After**: `send_control_frame` helper handles header/payload encoding and drops transport on failure; `stop_session`, `pause_session`, and `resume_session` utilize `send_control_frame`.

**Callers**: `vw_plugin_sender_main` (`plugin/src/vw_whisper_module.c`), unit and integration tests.  
**Callees**: `vw_protocol_encode_payload()`, `vw_protocol_encode_header()`, `vw_ipc_send()`, `vw_worker_client_drop_transport()`.

**Happy path**: `vw_worker_client_pause_session(client)` calls `send_control_frame(client, VW_MSG_PAUSE, VW_CTRL_REASON_USER_PAUSE)` → encodes header & payload → sends via `vw_ipc_send()` → returns true.

**Failure path**: If either IPC send call fails, `send_control_frame` calls `vw_worker_client_drop_transport(client)` to drop pipe and prevent desynchronization, returning false.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | Checks `!client || !client->pipe_handle || !client->session_active` before sending |
| **Authorization** | Stamps 16-byte session ID on control payload |
| **Concurrency** | Atomic sequence increment / single-threaded sender invocation |
| **I/O** | Sends header and payload atomically; drops transport if write fails |
| **Persistence** | N/A |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Unified send_control_frame helper | `plugin/src/vw_worker_client.c:L365-L386` | `vw_test_worker_client.c` | ✅ done |
| 2 | Implement vw_worker_client_pause_session | `plugin/src/vw_worker_client.c:L395-L398` | `vw_test_worker_client.c` | ✅ done |
| 3 | Implement vw_worker_client_resume_session | `plugin/src/vw_worker_client.c:L400-L403` | `vw_test_worker_client.c` | ✅ done |
| 4 | Transport drop on control write failure | `plugin/src/vw_worker_client.c:L383` | `vw_test_worker_client.c` | ✅ done |

**Assumptions/Tradeoffs**: Control frames require an active session and valid IPC pipe handle.

---

### 1.10 `protocol/include/vw_protocol_types.h`

**Why change**: Define `VW_CTRL_REASON_USER_PAUSE` (1U) and `VW_CTRL_REASON_USER_RESUME` (1U) protocol reason macros.

**Responsibility before**: Defined IPC header/payload structures without reason code macro definitions.  
**After**: Added `#define VW_CTRL_REASON_USER_PAUSE 1U` and `#define VW_CTRL_REASON_USER_RESUME 1U`.

**Callers**: `vw_worker_client.c`, `vw_worker.c`, and test suites.  
**Callees**: None.

**Happy path**: Code referencing `VW_CTRL_REASON_USER_PAUSE` compiles with constant value 1U.

**Failure path**: N/A.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | N/A |
| **Authorization** | N/A |
| **Concurrency** | Macro constants |
| **I/O** | N/A |
| **Persistence** | N/A |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Add control reason code macros | `protocol/include/vw_protocol_types.h:L140-L141` | `vw_test_worker_client.c` | ✅ done |

**Assumptions/Tradeoffs**: Numeric values match specification in `docs/api-contracts.md`.

---

### 1.11 `tests/integration/test_worker_lifecycle.c`

**Why change**: Extend worker lifecycle integration test to invoke `pause_session` and `resume_session` mid-stream during audio transmission.

**Responsibility before**: Lifecycle test sent `START`, `AUDIO` chunks, `STOP_SESSION`, and `SHUTDOWN` against a spawned worker process.  
**After**: Included `vw_worker_client_pause_session(c)` and `vw_worker_client_resume_session(c)` mid-stream, verifying worker process remains healthy and exits 0.

**Callers**: CTest harness (`test_worker_lifecycle`).  
**Callees**: `vw_worker_client_pause_session()`, `vw_worker_client_resume_session()`, worker binary.

**Happy path**: Test spawns worker → streams audio → invokes pause & resume → sends stop & shutdown → worker process exits cleanly with status 0 (`EXPECT(exit_code == 0)`).

**Failure path**: If worker process crashes on `PAUSE` or `RESUME`, exit status is non-zero, triggering test failure.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | N/A |
| **Authorization** | Session ID validation across pause/resume |
| **Concurrency** | Process IPC communication |
| **I/O** | Pipe write/read assertion |
| **Persistence** | Process lifetime assertion |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Assert worker pause/resume mid-stream survival | `tests/integration/test_worker_lifecycle.c:L219-L220` | `test_worker_lifecycle` | ✅ done |

**Assumptions/Tradeoffs**: Worker process handles `PAUSE`/`RESUME` without resetting session active flag.

---

### 1.12 `tests/unit/vw_test_worker_client.c`

**Why change**: Update fake server thread and client unit test cases to verify `VW_MSG_PAUSE` and `VW_MSG_RESUME` IPC frame sequence and reason payload values.

**Responsibility before**: Fake server expected `START_SESSION` → `AUDIO_PCM` → `STOP_SESSION` → `SHUTDOWN`.  
**After**: Fake server sequence updated to: `START_SESSION` → `AUDIO_PCM` → `PAUSE` (reason=1) → `RESUME` (reason=1) → `STOP_SESSION` → `SHUTDOWN`. Updated client test cases.

**Callers**: CTest harness (`vw_test_worker_client`).  
**Callees**: `vw_ipc_receive()`, `vw_protocol_decode_header()`, `vw_protocol_decode_payload()`, `vw_worker_client_pause_session()`, `vw_worker_client_resume_session()`.

**Happy path**: Fake server receives `VW_MSG_PAUSE` → validates `reason == 1` → receives `VW_MSG_RESUME` → validates `reason == 1` → receives `STOP_SESSION` → exits thread 0.

**Failure path**: If client sends invalid payload or incorrect frame sequence, fake server returns non-zero code (11..22), failing test assertion.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | Frame header type and payload length checks |
| **Authorization** | Reason code assertions (`VW_CTRL_REASON_USER_PAUSE`/`RESUME`) |
| **Concurrency** | Multi-threaded client/server IPC mock |
| **I/O** | Socket read timeouts |
| **Persistence** | N/A |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Server expectation for PAUSE frame & reason | `tests/unit/vw_test_worker_client.c:L108-L127` | `vw_test_worker_client` | ✅ done |
| 2 | Server expectation for RESUME frame & reason | `tests/unit/vw_test_worker_client.c:L129-L148` | `vw_test_worker_client` | ✅ done |
| 3 | Client pause/resume API unit test | `tests/unit/vw_test_worker_client.c:L411-L412` | `vw_test_worker_client` | ✅ done |

**Assumptions/Tradeoffs**: Fake server runs in dedicated thread with synchronous IPC reads.

---

### 1.13 `worker/src/vw_worker.c`

**Why change**: Implement `VW_MSG_PAUSE` and `VW_MSG_RESUME` handling in worker main loop, clear active audio buffer window on pause, gate incoming `AUDIO_PCM` frames while paused, and resume window accumulation on resume.

**Responsibility before**: Worker processed `START_SESSION`, `AUDIO_PCM`, `STOP_SESSION`, `SHUTDOWN`, ignoring unknown frames.  
**After**: Worker loop tracks `bool paused = false;`. On `VW_MSG_PAUSE`, sets `paused = true`, executes `vw_audio_buffer_clear(audio_buf)` to flush partial window, and logs event. On `VW_MSG_AUDIO_PCM`, drops frame if `paused` is true. On `VW_MSG_RESUME`, sets `paused = false`.

**Callers**: Worker main process loop (`vw_worker_run`).  
**Callees**: `vw_audio_buffer_clear()`, `vw_log_event()`, `memcmp()`.

**Happy path**: Worker receives `VW_MSG_PAUSE` → sets `paused = true` → calls `vw_audio_buffer_clear(audio_buf)` → drops subsequent `VW_MSG_AUDIO_PCM` frames → receives `VW_MSG_RESUME` → sets `paused = false` → accumulates fresh 8s window.

**Failure path**: If `VW_MSG_PAUSE` or `RESUME` arrives when `session_active` is false, worker ignores frame (`if (!session_active) break;`). `audio_buf` null-check prevents invalid dereference.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | Checks `!session_active` before handling control; checks `audio_buf != NULL` before clear |
| **Authorization** | Session ID comparison on incoming audio frames |
| **Concurrency** | Processed on main worker event loop |
| **I/O** | Frame queue consumption |
| **Persistence** | Preserves session timeline (PTS epoch); clears only in-flight partial audio window |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Gate AUDIO frames while paused | `worker/src/vw_worker.c:L316` | `test_worker_lifecycle` | ✅ done |
| 2 | Clear audio buffer on VW_MSG_PAUSE | `worker/src/vw_worker.c:L356-L361` | `test_worker_lifecycle` | ✅ done |
| 3 | Reset paused state on VW_MSG_RESUME | `worker/src/vw_worker.c:L366-L370` | `test_worker_lifecycle` | ✅ done |
| 4 | Preserve session timeline across pause/resume | `worker/src/vw_worker.c:L358` | Manual PTS log audit | ✅ done |

**Assumptions/Tradeoffs**: Clearing partial audio buffer on pause guarantees no window spans across a user playback pause gap.

---

## 2. Happy-Path Request Trace

End-to-end trace when user pauses and resumes playback in VLC during active captioning:

1. User clicks Pause in VLC player UI.
2. Background sender thread running `vw_plugin_sender_main` (`plugin/src/vw_whisper_module.c:L226`) invokes `vw_plugin_input_is_paused(p_filter)` (`plugin/src/vw_whisper_module.c:L316`).
3. `vw_plugin_input_is_paused` traverses VLC parent object chain, finds child with `object_type == "input"`, and evaluates `input_GetState((input_thread_t*)child) == PAUSE_S` (`plugin/src/vw_whisper_module.c:L330`).
4. Pause transition detected (`now_paused != paused`). Sender thread calls `vw_worker_client_pause_session(sys->client)` (`plugin/src/vw_whisper_module.c:L228`).
5. `vw_worker_client_pause_session` (`plugin/src/vw_worker_client.c:L395`) calls `send_control_frame(client, VW_MSG_PAUSE, VW_CTRL_REASON_USER_PAUSE)` (`plugin/src/vw_worker_client.c:L365`).
6. `send_control_frame` encodes `vw_msg_control_t` payload with `reason = 1` and `vw_frame_header_t` with `type = VW_MSG_PAUSE`, sending both over IPC pipe via `vw_ipc_send` (`plugin/src/vw_worker_client.c:L380`).
7. Sender thread sets local `paused = true` and logs `PLUGIN_PAUSE` event (`plugin/src/vw_whisper_module.c:L229`).
8. Sender thread continues draining SPSC audio queue (`plugin/src/vw_whisper_module.c:L243`); since `paused` is true, it discards chunks (`if (paused) continue;` `plugin/src/vw_whisper_module.c:L245`), keeping queue empty without sending `AUDIO` IPC messages.
9. Worker reader/main thread running `vw_worker_run` (`worker/src/vw_worker.c:L168`) receives IPC header, decodes payload, and enters `case VW_MSG_PAUSE:` (`worker/src/vw_worker.c:L356`).
10. Worker sets `paused = true`, executes `vw_audio_buffer_clear(audio_buf)` (`worker/src/vw_worker.c:L360`) to flush in-flight partial audio window, and logs `WORKER_SESSION` paused event.
11. User clicks Play in VLC player UI.
12. Sender thread invokes `vw_plugin_input_is_paused`, receives `false`. Transition detected (`now_paused != paused`).
13. Sender thread calls `vw_worker_client_resume_session(sys->client)` (`plugin/src/vw_whisper_module.c:L231`).
14. `vw_worker_client_resume_session` (`plugin/src/vw_worker_client.c:L400`) sends `VW_MSG_RESUME` frame with `reason = VW_CTRL_REASON_USER_RESUME` (1) over IPC.
15. Sender thread sets `paused = false` and resumes sending audio chunks from SPSC queue across IPC.
16. Worker main loop receives `VW_MSG_RESUME` (`worker/src/vw_worker.c:L365`), sets `paused = false`, and begins accumulating fresh audio window into `audio_buf`.

---

## 3. Most Important Failure Path

Trace failure scenario: Transport pipe write failure during `PAUSE` control frame transmission (e.g. worker process killed or pipe broken right as user pauses):

1. User pauses VLC playback.
2. Sender thread detects pause state transition and calls `vw_worker_client_pause_session(sys->client)` (`plugin/src/vw_whisper_module.c:L228`).
3. `vw_worker_client_pause_session` invokes `send_control_frame(client, VW_MSG_PAUSE, VW_CTRL_REASON_USER_PAUSE)` (`plugin/src/vw_worker_client.c:L396`).
4. `send_control_frame` encodes header and payload buffers and calls `vw_ipc_send(client->pipe_handle, hdr_buf, sizeof(hdr_buf))` (`plugin/src/vw_worker_client.c:L379`).
5. `vw_ipc_send` returns `false` due to broken pipe / write failure.
6. `send_control_frame` evaluates `if (ok1 && ok2)` as false, enters error path, and calls `vw_worker_client_drop_transport(client)` (`plugin/src/vw_worker_client.c:L383`).
7. `vw_worker_client_drop_transport` closes `client->pipe_handle`, resets `client->session_active = false`, and sets pipe handle to NULL.
8. `send_control_frame` returns `false`.
9. Next iteration of sender thread attempts audio/control sends; `vw_worker_client_send_audio` detects `client->pipe_handle == NULL` and returns `false`.
10. Sender thread evaluates `if (!vw_worker_client_send_audio(...))` as true and sets `atomic_store(&sys->worker_dead, true)` (`plugin/src/vw_whisper_module.c:L248`).
11. Sender thread exits main loop cleanly, and plugin degrades gracefully to audio passthrough without crashing VLC.

---

## 4. Boundary Summary

| Boundary type | What to check | Code Location | Status / Mitigation |
| --- | --- | --- | --- |
| **Input validation** | NULL client/filter checks, valid uint16_t reason codes | `vw_worker_client.c:L366`, `vw_whisper_module.c:L317`, `vw_worker.c:L357` | Verified NULL checks on client, pipe_handle, and p_filter; bounds check on child object list |
| **Authorization** | Session ID matching on control & audio frames | `vw_worker_client.c:L368`, `vw_worker.c:L317` | Session ID copied onto control frame; worker rejects mismatched session IDs |
| **Concurrency** | Sender thread local pause state vs worker main loop state; lock-free SPSC queue | `vw_whisper_module.c:L226-L246`, `vw_worker.c:L171` | Pause state managed on background sender thread; audio queue popped safely; worker loop single-threaded event loop |
| **I/O** | Atomic control frame writes (header + payload), fail-closed transport drop | `vw_worker_client.c:L379-L384` | `send_control_frame` drops pipe on partial/failed write to prevent protocol desynchronization |
| **Persistence** | Session timeline vs in-flight analysis window state | `vw_worker.c:L360` | `vw_audio_buffer_clear()` flushes partial window on pause while maintaining overall session timeline |

---

## 5. Acceptance Criterion → Code Mapping

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Input pause detection via VLC object tree walk | `plugin/src/vw_whisper_module.c:L316-L343` | Manual live VLC acceptance | ✅ done |
| 2 | PAUSE and RESUME IPC frame generation with reason codes | `plugin/src/vw_worker_client.c:L395-L403`, `protocol/include/vw_protocol_types.h:L140-L141` | `tests/unit/vw_test_worker_client.c:L411-L412` | ✅ done |
| 3 | SPSC audio queue discarding during pause state | `plugin/src/vw_whisper_module.c:L244-L246` | Manual live VLC acceptance | ✅ done |
| 4 | Worker audio window clear on VW_MSG_PAUSE | `worker/src/vw_worker.c:L356-L361` | `tests/integration/test_worker_lifecycle.c:L219-L220` | ✅ done |
| 5 | Worker AUDIO frame gating while paused | `worker/src/vw_worker.c:L316` | `tests/integration/test_worker_lifecycle.c:L219-L220` | ✅ done |
| 6 | Refactored send_control_frame with fail-closed transport drop | `plugin/src/vw_worker_client.c:L365-L386` | `tests/unit/vw_test_worker_client.c:L108-L148` | ✅ done |
| 7 | Caption presenter display rendering on SEGMENT frame | `plugin/src/vw_whisper_module.c:L286` | `tests/unit/test_caption_presenter.c` | ✅ done |
| 8 | Presenter OSD clear on plugin close | `plugin/src/vw_whisper_module.c:L526` | `tests/unit/test_caption_presenter.c` | ✅ done |

---

## 7. Code Review Findings (Bugs, Risks, Nitpicks)

### Bugs (Sorted by Priority)

| Priority | Component / Location | Description | Impact | Proposed Fix |
| --- | --- | --- | --- | --- |
| **Low** | `plugin/src/vw_whisper_module.c:328` | Multi-level child object traversal allocates `vlc_list_children` at parent level without recursive release on deeper hierarchy nodes | Minor temporary memory allocation during pause check | Ensure nested child lists are explicitly released or restrict child walk to depth 1 |

### Architectural & Operational Risks

| Category | Risk Description | Affected Files | Mitigation Strategy |
| --- | --- | --- | --- |
| **Performance** | Sender thread calls `vlc_list_children()` every send iteration (~5-20ms) when checking pause state | `plugin/src/vw_whisper_module.c:323` | Cache parent input object pointer or limit object tree walk frequency to every N iterations (~100ms) |
| **Latency** | Batch 8-second window inference results in ~8s presentation delay for live captions | `docs/plans/step15_plan.md:75` | Implement look-ahead ahead-of-time source file decoding (Steps 17c/17d) |

### Code Style & Quality Nitpicks

| Issue Type | File & Line | Description | Recommendation |
| --- | --- | --- | --- |
| **Duplicate Constant** | `protocol/include/vw_protocol_types.h:140` | `VW_CTRL_REASON_USER_PAUSE` and `VW_CTRL_REASON_USER_RESUME` are both defined as 1U | Consider distinct reason codes if telemetry requires distinguishing pause vs resume causes |
| **Code Formatting** | `plugin/src/vw_worker_client.c:365` | `send_control_frame` helper function static linkage scope | Function correctly declared static; consistent with internal client helpers |
