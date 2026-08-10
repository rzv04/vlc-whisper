# Diff Analysis: gemini/milestone-3-step-14c

**33 files changed, +1789 / -552 lines**  
**Base**: `gemini/milestone-3-step-14b`

---

## 1. File-by-File Analysis

### 1.1 `.agents/AGENTS.md`

**Why change**: Enforce directives 15 (mandatory codebase inspection before planning) and 16 (mandatory dependency graph inspection) requiring AI agents to read the codebase and consult `graphify-out/` before planning or implementing.

**Responsibility before**: Stored project coding rules (directives 1 to 14).  
**After**: Stored project coding rules including directives 15 and 16 before planning and implementing.

**Callers**: AI agents reading workspace directives.  
**Callees**: None.

**Happy path**: Agent opens repository, reads `.agents/AGENTS.md:L18-L21`, notes directives 15 and 16, inspects affected sources and `graphify-out/` prior to generating implementation plans.

**Failure path**: N/A (configuration markdown file).

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | N/A |
| **Authorization** | N/A |
| **Concurrency** | Read-only configuration file |
| **I/O** | File read by LLM harness |
| **Persistence** | Static repository file |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Include mandatory codebase inspection directive (15) | `.agents/AGENTS.md:L18` | Manual Inspection | ✅ done |
| 2 | Include mandatory dependency graph inspection directive (16) | `.agents/AGENTS.md:L20` | Manual Inspection | ✅ done |

**Assumptions/Tradeoffs**: Assumes `graphify-out/` directory exists when dependency graph has been generated.

---

### 1.2 `AGENTS.md`

**Why change**: Mirror directives 15 and 16 in root `AGENTS.md` to ensure rule consistency across all root directive entry points.

**Responsibility before**: Core coding rules (directives 1 to 14).  
**After**: Core coding rules (directives 1 to 14 plus directives 15 and 16).

**Callers**: AI agents reading workspace root instructions.  
**Callees**: None.

**Happy path**: Agent inspects `AGENTS.md:L18-L21` to verify project rules including mandatory codebase inspection and graph consultation.

**Failure path**: N/A.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | N/A |
| **Authorization** | N/A |
| **Concurrency** | Read-only configuration file |
| **I/O** | File read by LLM harness |
| **Persistence** | Static repository file |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Mirror mandatory codebase inspection directive (15) | `AGENTS.md:L18` | Manual Inspection | ✅ done |
| 2 | Mirror mandatory dependency graph inspection directive (16) | `AGENTS.md:L20` | Manual Inspection | ✅ done |

**Assumptions/Tradeoffs**: Root and `.agents/AGENTS.md` are maintained in sync manually.

---

### 1.3 `README.md`

**Why change**: Update plugin source file references (`vlc_whisper_module.c` → `vw_whisper_module.c`), document Step 14c real-time audio streaming architecture with worker reader-thread split (ADR-013), add model placement guidance (`--model` path option), and detail worker diagnostic logging (`%TEMP%\vlc-whisper-worker.log`) and console window suppression (`CREATE_NO_WINDOW`).

**Responsibility before**: Overview of repository architecture and build instructions up to Step 14b.  
**After**: Comprehensive project documentation reflecting Step 14c state, including background sender thread, `--model` path discovery, worker process lifecycle visibility in Task Manager, and console suppression details.

**Callers**: Developers reading workspace overview.  
**Callees**: None.

**Happy path**: Developer views `README.md:L166-L198` to learn how VLC audio is captured, streamed to the worker, and how worker diagnostic logging and model placement operate.

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
| 1 | Update architecture summary and module name rename | `README.md:L166` | Manual Inspection | ✅ done |
| 2 | Document model placement and `--model` path option | `README.md:L170` | Manual Inspection | ✅ done |
| 3 | Document worker Task Manager visibility and console suppression | `README.md:L196` | Manual Inspection | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.4 `docs/api-contracts.md`

**Why change**: Align module filename references (`vlc_whisper_module.c` → `vw_whisper_module.c`) per Rule 3, and update the `AUDIO` PCM frame payload description to document the ±1 byte whole-sample rounding tolerance for `pcm_bytes` validation.

**Responsibility before**: Specification of wire protocol binary layouts, payload validation rules, and error handling.  
**After**: Updated specification referencing `vw_whisper_module.c` and the documented ±1 byte `pcm_bytes` rounding tolerance constraint.

**Callers**: System architect and module developers.  
**Callees**: None.

**Happy path**: Architect checks contract specifications in `docs/api-contracts.md:L58` for payload format accuracy and whole-sample rounding tolerance rules.

**Failure path**: N/A.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | Documents ±1 byte whole-sample rounding tolerance for 16kHz S16LE PCM blocks |
| **Authorization** | N/A |
| **Concurrency** | N/A |
| **I/O** | N/A |
| **Persistence** | N/A |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Synchronize source file names in contract doc | `docs/api-contracts.md:L58` | Manual Inspection | ✅ done |
| 2 | Update AUDIO PCM payload description (±1 byte tolerance rule) | `docs/api-contracts.md:L58` | Manual Inspection | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.5 `docs/architecture.md`

**Why change**: Document ADR-013 (Decoupled Worker IPC Reader Thread & Bounded Worker Frame Queue) and the plugin background sender thread architecture (`vw_sender_thread`), including updated threading diagram and sequence description.

**Responsibility before**: High-level design document covering architecture up to Step 14b.  
**After**: Complete architectural specification of 14c dual-thread worker, `vw_worker_queue` drop-oldest-AUDIO policy, plugin 5ms/20ms streaming cadence, and the bidirectional frame flow.

**Callers**: Developers and AI agents reviewing architectural design decisions.  
**Callees**: None.

**Happy path**: Reader reviews ADR-013 implementation details in `docs/architecture.md:L185` to understand thread separation, queue handoff, and streaming cadence.

**Failure path**: N/A.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | N/A |
| **Authorization** | N/A |
| **Concurrency** | Describes thread boundaries and queue safety |
| **I/O** | N/A |
| **Persistence** | N/A |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Add ADR-013 and worker queue architecture documentation | `docs/architecture.md:L185` | Manual Inspection | ✅ done |
| 2 | Document plugin sender thread 5ms/20ms streaming cadence | `docs/architecture.md:L195` | Manual Inspection | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.6 `docs/issues.md`

**Why change**: Update filename references from `vlc_whisper_module.c` to `vw_whisper_module.c`, and fix the Linux ancestor walk bug description (start at `up = 0` instead of `up = 1`).

**Responsibility before**: Documenting open issues and technical debt.  
**After**: Updated issue log with corrected module file names and accurate Linux bug description.

**Callers**: Developers tracking project issues.  
**Callees**: None.

**Happy path**: Developer reads `docs/issues.md` to inspect reported issue contexts including the ancestor walk bug fix.

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
| 1 | Update filename references in issue log | `docs/issues.md:L42` | Manual Inspection | ✅ done |
| 2 | Correct Linux ancestor walk bug description (up=0 start) | `docs/issues.md:L42` | Manual Inspection | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.7 `docs/plans/step-14-realtime-pcm-streaming.md`

**Why change**: Mark Step 14c subtasks complete in the master Step 14 roadmap plan, updating filename references from `vlc_whisper_module.c` to `vw_whisper_module.c`.

**Responsibility before**: Implementation roadmap for Step 14 (14a and 14b checked, 14c unchecked).  
**After**: Implementation roadmap showing 14a, 14b, and 14c all checked.

**Callers**: Developers tracking Step 14 milestone progress.  
**Callees**: None.

**Happy path**: Developer checks status in `docs/plans/step-14-realtime-pcm-streaming.md:L44` to confirm all Step 14 substeps completed.

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
| 1 | Mark Step 14c subtasks complete | `docs/plans/step-14-realtime-pcm-streaming.md:L44` | Manual Inspection | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.8 `docs/plans/step13_plan.md`

**Why change**: Removed obsolete Step 13 plan artifact. Step 13 (VLC Plugin Worker IPC Client Connection) is complete; plan file is no longer needed.

**Responsibility before**: Step 13 implementation plan.  
**After**: Deleted file.

**Callers**: N/A.  
**Callees**: N/A.

**Happy path**: File removed from working directory; Step 13 plan superseded by implementation.

**Failure path**: N/A.

**Boundaries**: N/A.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Clean up obsolete plan artifact for completed Step 13 | Deleted | N/A | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.9 `docs/plans/step14a_plan.md`

**Why change**: Removed obsolete Step 14A plan artifact. Step 14A (Worker Audio Pipeline, Whisper Engine, Segment Emission) is complete; plan file superseded.

**Responsibility before**: Step 14A implementation plan.  
**After**: Deleted file.

**Callers**: N/A.  
**Callees**: N/A.

**Happy path**: File removed from working directory; Step 14A plan superseded by implementation.

**Failure path**: N/A.

**Boundaries**: N/A.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Clean up obsolete plan artifact for completed Step 14A | Deleted | N/A | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.10 `docs/plans/step14c_plan.md`

**Why change**: Enforce Rule 9 (Task Planning Template Enforcement) by storing the complete technical specification and step plan for 14c real-time audio streaming and worker reader-thread split (ADR-013).

**Responsibility before**: New file.  
**After**: Authoritative task plan and design record for Step 14c implementation.

**Callers**: AI agents and developers inspecting Step 14c requirements and verification criteria.  
**Callees**: None.

**Happy path**: Agent references `docs/plans/step14c_plan.md` to map design criteria to unit/integration test assertions and implementation subtasks.

**Failure path**: N/A.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | N/A |
| **Authorization** | N/A |
| **Concurrency** | N/A |
| **I/O** | N/A |
| **Persistence** | File created in repo |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Enforce Rule 9 task template for Step 14c | `docs/plans/step14c_plan.md:L1` | Manual Inspection | ✅ done |

**Assumptions/Tradeoffs**: Plan describes target state; implementation verified via test suite.

---

### 1.11 `docs/roadmap.md`

**Why change**: Mark Step 14b and 14c as completed (`[x]`) per Rule 14 (mandatory documentation updates for completed milestones). Update file references from `vlc_whisper_module.c` to `vw_whisper_module.c`.

**Responsibility before**: Project milestone tracking document with 14b and 14c still unchecked.  
**After**: Roadmap showing 14b and 14c both completed with shipped-summary annotations.

**Callers**: Project maintainers and AI agents verifying feature status.  
**Callees**: None.

**Happy path**: Reviewer checks `docs/roadmap.md:L47-L49` to confirm 14b and 14c now show `[x]` status.

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
| 1 | Mark Step 14b as completed with shipped summary | `docs/roadmap.md:L47` | Manual Inspection | ✅ done |
| 2 | Mark Step 14c as completed with shipped summary | `docs/roadmap.md:L49` | Manual Inspection | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.12 `docs/source-layout.md`

**Why change**: Update repository source map for `vw_whisper_module.c` (was `vlc_whisper_module.c`), add `vw_worker_queue.h` and `vw_worker_queue.c` entries to source layout.

**Responsibility before**: Map of codebase directories and file responsibilities up to Step 14b.  
**After**: Comprehensive source layout reflecting the renamed plugin entry point, new worker queue component, and sender thread.

**Callers**: Developers navigating repository source tree.  
**Callees**: None.

**Happy path**: Developer looks up `vw_worker_queue.c` and `vw_whisper_module.c` in `docs/source-layout.md:L38, L59`.

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
| 1 | Document new worker queue files and renamed plugin module | `docs/source-layout.md:L38` | Manual Inspection | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.13 `docs/test-strategy.md`

**Why change**: Document Step 14c unit test additions (`test_worker_queue` for bounded queue and eviction policy, `vw_test_worker_client` for frame receiving tests covering caption segments, status, errors, timeouts, and unknown frame drain, and `test_worker_lifecycle` for `E_MODEL_MISSING` and full PCM streaming assertions).

**Responsibility before**: Strategy and inventory of project test suites up to Step 14b.  
**After**: Complete test inventory updated with 14c worker queue and end-to-end PCM streaming tests.

**Callers**: Developers adding or running tests.  
**Callees**: None.

**Happy path**: Developer reads `docs/test-strategy.md:L52` to verify test coverage expectations for worker queues and client frame receiving.

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
| 1 | Add 14c unit test inventory entries | `docs/test-strategy.md:L52` | Manual Inspection | ✅ done |
| 2 | Document E_MODEL_MISSING and full PCM streaming test additions | `docs/test-strategy.md:L55` | Manual Inspection | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.14 `docs/vlc-api-essentials.md`

**Why change**: Update filename references from `vlc_whisper_module.c` to `vw_whisper_module.c` to align with Rule 3 symbol namespacing.

**Responsibility before**: Reference guide for VLC C API usage up to Step 14b.  
**After**: Reference guide with updated module file references.

**Callers**: Developers working on VLC plugin integration.  
**Callees**: None.

**Happy path**: Developer checks VLC filter callback conventions in `docs/vlc-api-essentials.md:L134`.

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
| 1 | Update module file reference | `docs/vlc-api-essentials.md:L134` | Manual Inspection | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.15 `plugin/CMakeLists.txt`

**Why change**: Update CMake build target `vlc_whisper_plugin` to build `src/vw_whisper_module.c` instead of `src/vlc_whisper_module.c`.

**Responsibility before**: Built `vlc_whisper_plugin` dynamic library from `src/vlc_whisper_module.c`.  
**After**: Builds `vlc_whisper_plugin` dynamic library from `src/vw_whisper_module.c`.

**Callers**: CMake build system (`cmake --build`).  
**Callees**: Compiler compiling `vw_whisper_module.c`.

**Happy path**: `cmake --build --preset linux-x64-debug` links `vw_whisper_module.c` into `vlc_whisper_plugin.so`.

**Failure path**: Build error if `vw_whisper_module.c` is missing or fails to compile.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | CMake file syntax |
| **Authorization** | N/A |
| **Concurrency** | N/A |
| **I/O** | CMake file reading |
| **Persistence** | N/A |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Compile `vw_whisper_module.c` into plugin binary | `plugin/CMakeLists.txt:L18` | `test_plugin_load` | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.16 `plugin/include/vw_worker_client.h`

**Why change**: Declare `model_path` parameter in `vw_worker_client_launch_and_connect`, `vw_worker_recv_t` container struct, and `vw_worker_client_receive_frame` function signature.

**Responsibility before**: Header for plugin IPC client state machine up to Step 14b session control.  
**After**: Header for plugin IPC client with background frame receiver API and model path forwarding.

**Callers**: `vw_whisper_module.c`, `vw_worker_client.c`, `vw_test_worker_client.c`, `test_worker_lifecycle.c`.  
**Callees**: None (header declaration file).

**Happy path**: `vw_whisper_module.c` includes header and invokes `vw_worker_client_receive_frame` to decode worker frames.

**Failure path**: Compiler error if caller signature mismatch occurs.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | Header nullability macros and type constraints |
| **Authorization** | N/A |
| **Concurrency** | Client struct accessed exclusively by sender thread |
| **I/O** | N/A |
| **Persistence** | N/A |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Declare `vw_worker_client_receive_frame` & model_path parameter | `plugin/include/vw_worker_client.h:L61-L107` | `vw_test_worker_client` | ✅ done |

**Assumptions/Tradeoffs**: All header functions documented with 20-30 word standard comments per Rule 11.

---

### 1.17 `plugin/src/vlc_whisper_module.c` (Deleted)

**Why change**: Renamed to `vw_whisper_module.c` to satisfy Rule 3 (`vw_` symbol/file prefixing convention).

**Responsibility before**: VLC plugin module entry point.  
**After**: Deleted / moved to `vw_whisper_module.c`.

**Callers**: N/A.  
**Callees**: N/A.

**Happy path**: File removed from source tree; all references updated to `vw_whisper_module.c`.

**Failure path**: N/A.

**Boundaries**: N/A.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Rename file to follow `vw_` symbol prefix rule | Deleted | `test_plugin_load` | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.18 `plugin/src/vw_audio_capture.c`

**Why change**: Adjust logging level/formatting for SPSC queue push operations to keep logs clean during active playback.

**Responsibility before**: Converts raw VLC PCM blocks to normalized 16kHz mono S16LE chunks and pushes to SPSC queue.  
**After**: Normalizes raw VLC PCM blocks and pushes to SPSC queue with updated logging.

**Callers**: `vw_whisper_module.c:vw_plugin_filter`.  
**Callees**: `vw_spsc_queue_push`.

**Happy path**: Audio callback passes `vw_audio_input_t` to `vw_audio_capture_process_block:L72`; audio chunk is enqueued to `vw_spsc_queue_t`.

**Failure path**: SPSC queue full; chunk dropped, drop count incremented.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | Null checks on capture handle and PCM buffer |
| **Authorization** | N/A |
| **Concurrency** | Called on VLC main audio thread; 100% lock-free (Rule 4) |
| **I/O** | None |
| **Persistence** | None |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Lock-free audio capture in VLC callback | `plugin/src/vw_audio_capture.c:L72` | `test_audio_capture` | ✅ done |

**Assumptions/Tradeoffs**: SPSC queue capacity handles normal VLC audio burst variations.

---

### 1.19 `plugin/src/vw_platform_win32.c`

**Why change**: Correct Win32 process spawning argument string formatting (add `--model` flag support via proper argv array construction) and log output path formatting (fix path slashes in file logging).

**Responsibility before**: Win32 platform implementation for thread creation, timing, process spawning, and named pipes.  
**After**: Win32 platform implementation with fixed `--model` argument array formatting and clean backslash path separators in log output.

**Callers**: `vw_worker_client.c`, `vw_whisper_module.c`.  
**Callees**: Win32 API (`CreateProcessA`, `GetFileAttributesA`).

**Happy path**: `vw_platform_spawn_process:L124` formats command line string with arguments enclosed in quotes and spawns `vlc-whisper-worker.exe` with `--pipe`, `--token`, and `--model` arguments.

**Failure path**: `CreateProcessA` returns zero; `vw_platform_spawn_process` returns false.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | Null checks on executable path and argv array |
| **Authorization** | Windows process creation permissions |
| **Concurrency** | Thread handle creation and process handle management |
| **I/O** | Win32 pipe handles and process creation |
| **Persistence** | None |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Support `--model` argv formatting in Win32 spawn | `plugin/src/vw_platform_win32.c:L124` | `test_platform` | ✅ done |
| 2 | Fix path slash formatting in log output | `plugin/src/vw_platform_win32.c:L45` | Manual Inspection | ✅ done |

**Assumptions/Tradeoffs**: Command line length strictly bounds within Win32 `MAX_PATH` limits.

---

### 1.20 `plugin/src/vw_whisper_module.c`

**Why change**: Created as the main VLC audio filter module (renamed from `vlc_whisper_module.c` for Rule 3 compliance). Added background sender thread (`vw_sender_thread`), model path discovery (`vw_plugin_resolve_model_path`), worker binary discovery (`vw_plugin_resolve_worker_path`), passthrough degradation on worker failure, and `--model` option registration.

**Responsibility before**: N/A (new file, replacing `vlc_whisper_module.c`).  
**After**: Plugin entry point implementing VLC module registration, audio filter callback (`vw_plugin_filter`), worker process supervisor, sender thread loop (`vw_plugin_sender_main`), model path discovery, and passthrough fallback.

**Callers**: VLC module loader (`vlc_entry__3_0_0f`), VLC audio pipeline (`pf_audio_filter`).  
**Callees**: `vw_spsc_queue_create`, `vw_worker_client_launch_and_connect`, `vw_worker_client_start_session`, `vw_worker_client_send_audio`, `vw_worker_client_receive_frame`, `vw_worker_client_stop_session`, `vw_worker_client_shutdown`.

**Happy path**:
1. VLC opens filter: `vw_plugin_open:L322` allocates `sys`, creates 8-second SPSC queue, resolves worker executable & GGML model file paths, launches `vlc-whisper-worker`, and spawns `vw_sender_thread:L398`.
2. Audio flows: `vw_plugin_filter:L289` captures PCM to SPSC queue and returns `p_block` untouched.
3. Sender thread: `vw_plugin_sender_main:L217` starts session, pops audio chunks from SPSC queue, sends `AUDIO` frames over IPC, calls `vw_worker_client_receive_frame:L248` to drain `CAPTION_SEGMENT` / `STATUS` / `ERROR` frames.
4. VLC closes filter: `vw_plugin_close:L411` signals sender thread to exit, joins thread, stops session, shuts down IPC client, destroys SPSC queue, and frees `sys`.

**Failure path**:
1. Model file missing or worker binary not found: `vw_worker_client_start_session` fails with `E_MODEL_MISSING`; sender thread sets `worker_dead = true`, logs warning, and exits (`vw_whisper_module.c:L223`). `vw_plugin_filter` continues returning `p_block` untouched (passthrough mode).
2. Worker process crashes or pipe dies during streaming: `vw_worker_client_send_audio` or `vw_worker_client_receive_frame` returns false / -1; sender thread sets `worker_dead = true` (`vw_whisper_module.c:L237, L250`) and halts IPC traffic. Passthrough mode maintained seamlessly.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | Validates VLC `fmt_in` audio codecs (`FL32`, `S16N`, `S32N`), null checks on `p_block` and `p_filter` |
| **Authorization** | Generates 32-byte secret token via CSPRNG (`vw_platform_get_random_bytes`) |
| **Concurrency** | VLC callback `vw_plugin_filter` is 100% lock-free (Rule 4); sender thread is the single consumer of SPSC queue and single user of IPC client handle |
| **I/O** | Non-blocking SPSC queue reads; 5ms/20ms timed IPC receives |
| **Persistence** | Named pipe / Unix domain socket file cleaned up on close |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Implement background sender thread with 5ms/20ms cadence | `plugin/src/vw_whisper_module.c:L217` | `test_worker_lifecycle` | ✅ done |
| 2 | Model file path discovery and `--model` option support | `plugin/src/vw_whisper_module.c:L156, L373` | `test_worker_lifecycle` | ✅ done |
| 3 | Lock-free audio callback (Rule 4) | `plugin/src/vw_whisper_module.c:L289` | `test_plugin_load` | ✅ done |
| 4 | Degrade gracefully to passthrough on worker error | `plugin/src/vw_whisper_module.c:L223, L237` | `test_worker_lifecycle` | ✅ done |

**Assumptions/Tradeoffs**: `CAPTION_SEGMENT` frames are counted and logged in 14c; presentation via SPU presenter is wired in Step 15.

---

### 1.21 `plugin/src/vw_worker_client.c`

**Why change**: Support optional `--model` flag in `vw_worker_client_launch_and_connect`, log worker error payloads (e.g. `E_MODEL_MISSING`) on `START_SESSION` rejection, and implement `vw_worker_client_receive_frame` for non-blocking/timed frame reception.

**Responsibility before**: Managed worker process launch, HELLO handshake, and session control frame encoding/sending (`START`, `AUDIO`, `STOP`, `SHUTDOWN`).  
**After**: Full-duplex client API: process supervisor with optional `--model` flag, session control frame sender, and timed frame receiver (`vw_worker_client_receive_frame`) decoding `CAPTION_SEGMENT`, `STATUS`, and `ERROR` payloads into owned caller buffers.

**Callers**: `vw_whisper_module.c`, `vw_test_worker_client.c`, `test_worker_lifecycle.c`.  
**Callees**: `vw_ipc_send`, `vw_ipc_receive_timeout`, `vw_protocol_decode_header`, `vw_protocol_decode_payload`, `vw_protocol_validate_header`, `vw_protocol_validate_payload`.

**Happy path**:
1. Launch: `vw_worker_client_launch_and_connect:L65` builds argv array (spawning worker with `--pipe <endpoint> --token <hex> --model <path>`).
2. Start: `vw_worker_client_start_session:L266` sends `START_SESSION` frame and awaits `STARTED` reply within deadline.
3. Receive frame: `vw_worker_client_receive_frame:L398` calculates absolute deadline, reads 20-byte header via `receive_all`, validates header, reads payload, decodes frame payload (`CAPTION_SEGMENT`, `STATUS`, or `ERROR`), copies segment UTF-8 text into `out->text_buf`, null-terminates string, and returns 1.

**Failure path**:
1. Worker returns error frame on `START_SESSION` (e.g. `E_MODEL_MISSING`): `vw_worker_client_start_session:L284` decodes `VW_MSG_ERROR`, logs warning, frees payload, drops transport, and returns false.
2. Receive timeout: `receive_all` returns false because clock reached `deadline_us`; function checks clock, confirms timeout (not transport failure), and returns 0 (client handle stays valid).
3. Transport failure / EOF: `receive_all` returns false before deadline; function calls `vw_worker_client_drop_transport:L60` and returns -1 (caller marks worker dead).

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | Null checks on `client`, `out`, `auth_token`; payload length bounds (`hdr.payload_length`); text buffer truncation (`VW_MAX_TEXT_BYTES - 1`) |
| **Authorization** | Token verification in launch layer |
| **Concurrency** | Thread safety: client handle caller-confined to sender thread |
| **I/O** | Uses timed IPC reads (`vw_ipc_receive_timeout`); caller controls timeout duration |
| **Persistence** | None |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Pass optional `--model` flag to worker argv | `plugin/src/vw_worker_client.c:L87` | `test_worker_lifecycle` | ✅ done |
| 2 | Log error payload on `START_SESSION` failure | `plugin/src/vw_worker_client.c:L284` | `test_worker_lifecycle` | ✅ done |
| 3 | Implement `vw_worker_client_receive_frame` with timeout | `plugin/src/vw_worker_client.c:L398` | `vw_test_worker_client` | ✅ done |
| 4 | Copy segment text into owned storage | `plugin/src/vw_worker_client.c:L458` | `vw_test_worker_client` | ✅ done |

**Assumptions/Tradeoffs**: Single per-call deadline governs header and payload reads; a timeout mid-payload returns 0 without breaking wire synchronization.

---

### 1.22 `protocol/src/vw_protocol_validate.c`

**Why change**: Validate audio sample rate requirements (`16000 Hz`) and session parameters during payload validation. Support ±1 byte whole-sample rounding tolerance in `VW_MSG_AUDIO_PCM` payload duration validation.

**Responsibility before**: Wire protocol payload structure validation functions (`vw_protocol_validate_payload`).  
**After**: Payload validation functions with sample rate, bounds checking, and ±1 byte whole-sample rounding tolerance for audio PCM frames.

**Callers**: `vw_worker.c`, `vw_worker_client.c`, `test_protocol_validate.c`.  
**Callees**: `is_valid_utf8`, `is_empty_or_whitespace`.

**Happy path**: `vw_protocol_validate_payload:L92` validates `VW_MSG_AUDIO_PCM` payload; calculates `expected_bytes = (p->duration_us * 32) / 1000`, allows `expected_bytes ± 1` byte tolerance, and returns true.

**Failure path**: Invalid sample rate (e.g. 44100 Hz) or `pcm_bytes` outside `expected_bytes ± 1`; function returns false.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | Checks sample rate (16000), channel count (1), ±1 byte rounding tolerance for PCM duration |
| **Authorization** | N/A |
| **Concurrency** | Pure functions (thread-safe, side-effect free) |
| **I/O** | None |
| **Persistence** | None |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Enforce 16kHz Mono sample format constraint | `protocol/src/vw_protocol_validate.c:L54` | `test_protocol_validate` | ✅ done |
| 2 | Accept ±1 byte whole-sample rounding tolerance for audio PCM | `protocol/src/vw_protocol_validate.c:L100` | `test_protocol_validate` | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.23 `tests/CMakeLists.txt`

**Why change**: Register `test_worker_queue` target in CMake test suite and update source references for `vw_whisper_module.c`.

**Responsibility before**: Built unit and integration test binaries for protocol, capture, buffer, engine, and client.  
**After**: Builds test binaries including `test_worker_queue` and updated module source references.

**Callers**: CMake / CTest harness.  
**Callees**: Compiler building `test_worker_queue.c`.

**Happy path**: `ctest --preset linux-x64-debug` executes `test_worker_queue` alongside other tests.

**Failure path**: Test target fails build or exits non-zero.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | CMake target syntax |
| **Authorization** | N/A |
| **Concurrency** | N/A |
| **I/O** | N/A |
| **Persistence** | N/A |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Register `test_worker_queue` in CTest | `tests/CMakeLists.txt:L32` | CTest | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.24 `tests/integration/test_worker_ipc.c`

**Why change**: Verify IPC message exchange against the refactored dual-thread worker architecture (ADR-013).

**Responsibility before**: Integration test asserting `HELLO`/`HELLO_ACK`, unsupported sample rate rejection (`E_AUDIO_FORMAT`), and clean shutdown.  
**After**: Integration test validating the same wire protocol assertions against the reader-thread worker split.

**Callers**: CTest harness (`ctest`).  
**Callees**: `vw_ipc_connect`, `vw_protocol_encode_header`, `vw_protocol_decode_header`.

**Happy path**: Test connects to worker pipe, sends `HELLO`, receives `HELLO_ACK`, sends `START` with 44100Hz sample rate, receives `E_AUDIO_FORMAT` error payload, sends `SHUTDOWN`, and asserts clean exit code 0.

**Failure path**: Assertion failure (`EXPECT`) if worker fails to respond or crashes.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | Validates message wire formats |
| **Authorization** | Uses valid 32-byte secret auth token |
| **Concurrency** | Exercises worker reader thread + main loop concurrency |
| **I/O** | IPC socket / named pipe transport |
| **Persistence** | Temporary pipe socket cleanup |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Verify worker reader-thread IPC compliance | `tests/integration/test_worker_ipc.c:L45` | `test_worker_ipc` | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.25 `tests/integration/test_worker_lifecycle.c`

**Why change**: Add integration tests for `E_MODEL_MISSING` session rejection when `model_path` is invalid/zeroed, and end-to-end PCM audio chunk streaming when model file is present.

**Responsibility before**: Tested basic worker process launch and shutdown lifecycle.  
**After**: Comprehensive integration test validating `E_MODEL_MISSING` error reply handling, model discovery, and multi-chunk PCM streaming session lifecycle (`START` -> `AUDIO` x4 -> `STOP` -> `SHUTDOWN`).

**Callers**: CTest harness (`ctest`).  
**Callees**: `vw_worker_client_launch_and_connect`, `vw_worker_client_start_session`, `vw_worker_client_send_audio`, `vw_worker_client_stop_session`, `vw_worker_client_shutdown`.

**Happy path**:
1. E_MODEL_MISSING test: Launches worker with dummy token and NULL model path; `vw_worker_client_start_session:L45` receives `E_MODEL_MISSING` error frame, logs failure, and asserts function returns false.
2. Model-gated streaming test: When `models/ggml-tiny.en.bin` exists, launches worker with model path, calls `vw_worker_client_start_session` (returns true), streams four 512ms audio chunks, calls `vw_worker_client_stop_session`, shuts down client, and asserts worker process exits 0 cleanly.

**Failure path**: Test assertion fails if worker crashes, deadlocks, or fails to return `E_MODEL_MISSING`.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | Checks session response codes and error codes |
| **Authorization** | Secret token verification |
| **Concurrency** | Tests full client process + worker process IPC concurrency |
| **I/O** | IPC pipe communication and process management |
| **Persistence** | Cleans up worker process and pipe endpoints |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Test `E_MODEL_MISSING` rejection path | `tests/integration/test_worker_lifecycle.c:L45` | `test_worker_lifecycle` | ✅ done |
| 2 | Test end-to-end PCM streaming session | `tests/integration/test_worker_lifecycle.c:L82` | `test_worker_lifecycle` | ✅ done |

**Assumptions/Tradeoffs**: Model-gated section skips heavy inference when running under Valgrind to keep test suite fast.

---

### 1.26 `tests/unit/test_platform.c`

**Why change**: Update unit tests for platform utility functions.

**Responsibility before**: Tested timing, process spawning, and RNG functions.  
**After**: Updated platform tests asserting timing precision and process handle cleanup.

**Callers**: CTest harness (`ctest`).  
**Callees**: `vw_platform_get_monotonic_time_us`, `vw_platform_spawn_process`.

**Happy path**: Test asserts monotonic clock increases monotonically and process handles reap correctly.

**Failure path**: Test fails if clock regresses or process spawn fails.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | Handles parameter nullability |
| **Authorization** | N/A |
| **Concurrency** | Timing safety across threads |
| **I/O** | Subprocess creation |
| **Persistence** | None |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Validate platform timing and process utilities | `tests/unit/test_platform.c:L22` | `test_platform` | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.27 `tests/unit/test_protocol_validate.c`

**Why change**: Add unit tests for payload validation logic including 16kHz sample rate validation and ±1 byte whole-sample rounding tolerance.

**Responsibility before**: Tested header and payload validation logic up to Step 14b.  
**After**: Updated test suite validating sample rate restrictions, ±1 byte rounding tolerance, and UTF-8 string boundary checks.

**Callers**: CTest harness (`ctest`).  
**Callees**: `vw_protocol_validate_payload`, `vw_protocol_validate_header`.

**Happy path**: Test passes valid `vw_msg_start_t` (sample_rate=16000) -> returns true; passes invalid sample_rate (44100) -> returns false.

**Failure path**: Test assertion fails if validation logic misclassifies valid or invalid payloads.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | Boundary values for payload lengths, sample rates, channels, ±1 byte tolerance |
| **Authorization** | N/A |
| **Concurrency** | Single-threaded test assertions |
| **I/O** | None |
| **Persistence** | None |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Verify sample rate payload validation | `tests/unit/test_protocol_validate.c:L35` | `test_protocol_validate` | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.28 `tests/unit/test_worker_queue.c`

**Why change**: New unit test file asserting `vw_worker_queue` behavior: FIFO ordering, mixed message types, payload memory ownership transfer, drop-oldest-AUDIO overflow eviction policy, `dropped_audio_us` calculation, control frame survival, zero-payload handling, and destroy cleanup.

**Responsibility before**: N/A (new file).  
**After**: Authoritative unit test suite for `vw_worker_queue`.

**Callers**: CTest harness (`ctest`).  
**Callees**: `vw_worker_queue_create`, `vw_worker_queue_push`, `vw_worker_queue_pop`, `vw_worker_queue_get_dropped_audio_us`, `vw_worker_queue_destroy`.

**Happy path**:
1. FIFO test (`test_worker_queue.c:L26`): Pushes audio 1, audio 2, and stop session; pops in exact FIFO order; verifies payload pointers match original allocations and payload lengths match.
2. Eviction test (`test_worker_queue.c:L57`): Fills queue (capacity 4) with 3 audio frames + 1 PAUSE control frame. Pushes 4th audio frame; asserts oldest audio frame (500000us) is evicted, PAUSE control frame survives, and `vw_worker_queue_get_dropped_audio_us` returns 500000us.
3. Cleanup test (`test_worker_queue.c:L132`): Pushes 2 audio payloads to queue and calls `vw_worker_queue_destroy`; Valgrind verifies zero memory leaks.

**Failure path**: Test fails if queue drops a control frame, corrupts FIFO order, fails to calculate dropped audio duration, or leaks payload memory.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | Capacity bounds (capacity = 0 returns NULL), NULL queue handling |
| **Authorization** | N/A |
| **Concurrency** | Queue mutex locking and atomic `dropped_audio_us` reads |
| **I/O** | None |
| **Persistence** | Memory allocation and deallocation |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Verify FIFO ordering with mixed frame types | `tests/unit/test_worker_queue.c:L26` | `test_worker_queue` | ✅ done |
| 2 | Verify drop-oldest-AUDIO overflow policy | `tests/unit/test_worker_queue.c:L57` | `test_worker_queue` | ✅ done |
| 3 | Verify control frame survival on overflow | `tests/unit/test_worker_queue.c:L65, L85` | `test_worker_queue` | ✅ done |
| 4 | Verify `dropped_audio_us` atomic tracking | `tests/unit/test_worker_queue.c:L72` | `test_worker_queue` | ✅ done |
| 5 | Verify destroy frees all queued payloads | `tests/unit/test_worker_queue.c:L132` | `test_worker_queue` | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.29 `tests/unit/vw_test_worker_client.c`

**Why change**: Add unit tests for `vw_worker_client_receive_frame`: decoding `CAPTION_SEGMENT`, `STATUS`, and `ERROR` frames in order, skipping unknown message types (e.g. `PAUSE`), timing out with 0 against a silent mock server without breaking transport, and returning -1 on EOF.

**Responsibility before**: Tested client session state machine and framing against an in-process mock server.  
**After**: Complete unit test suite verifying both outbound session management and inbound frame reception.

**Callers**: CTest harness (`ctest`).  
**Callees**: `vw_worker_client_receive_frame`, `vw_worker_client_start_session`, mock IPC server helpers.

**Happy path**:
1. Receiving test (`vw_test_worker_client.c:L280`): Mock server sends `CAPTION_SEGMENT` payload ("hello world"); test calls `vw_worker_client_receive_frame`, asserts return code 1, verifies `recv.type == VW_MSG_CAPTION_SEGMENT`, and checks `recv.segment.text_utf8` equals "hello world".
2. Timeout test (`vw_test_worker_client.c:L330`): Mock server sends nothing; test calls `vw_worker_client_receive_frame(client, 10000, &recv)`, asserts return code 0 (timeout), and verifies client connection remains active and usable for subsequent reads.

**Failure path**: Test fails if frame decoding corrupts text pointers, fails to handle timeouts, or misinterprets transport EOF.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | Payload length validation, string buffer bounds (`VW_MAX_TEXT_BYTES`) |
| **Authorization** | N/A |
| **Concurrency** | Client mock server thread synchronization |
| **I/O** | Mock socket pair / IPC pipe reads and writes |
| **Persistence** | None |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Test `CAPTION_SEGMENT` frame decoding | `tests/unit/vw_test_worker_client.c:L280` | `test_worker_client` | ✅ done |
| 2 | Test `STATUS` and `ERROR` frame decoding | `tests/unit/vw_test_worker_client.c:L310` | `test_worker_client` | ✅ done |
| 3 | Test frame receive timeout (returns 0) | `tests/unit/vw_test_worker_client.c:L330` | `test_worker_client` | ✅ done |
| 4 | Test transport dead EOF (returns -1) | `tests/unit/vw_test_worker_client.c:L350` | `test_worker_client` | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.30 `worker/CMakeLists.txt`

**Why change**: Add `src/vw_worker_queue.c` and `include/vw_worker_queue.h` to the `vlc-whisper-worker` build target, link platform sources (`vw_platform_linux.c` / `vw_platform_win32.c`), add `Threads::Threads` link dependency, disable `GGML_OPENMP` on Win32 to prevent runtime `libgomp-1.dll` dependency (ADR-010), pin `GGML_VULKAN OFF` to enforce CPU-only build reproducibility, and link `bcrypt` on Windows for auth token generation.

**Responsibility before**: Built `vlc-whisper-worker` binary from `main.c`, `vw_worker.c`, `vw_whisper_engine.c`, `vw_vad.c`, `vw_segment_builder.c`.  
**After**: Builds `vlc-whisper-worker` binary including `vw_worker_queue.c`, platform sources, `Threads::Threads`, `bcrypt` on Win32, with `GGML_OPENMP OFF` on Win32 and `GGML_VULKAN OFF` pinned.

**Callers**: CMake build system (`cmake --build`).  
**Callees**: Compiler compiling `vw_worker_queue.c`, `vw_worker.c`, and linked `Threads::Threads`.

**Happy path**: `cmake --build --preset linux-x64-debug` compiles `vw_worker_queue.c` and links it into `vlc-whisper-worker`.

**Failure path**: Build error if `vw_worker_queue.c` has compilation errors or Threads dependency missing.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | CMake file syntax |
| **Authorization** | Links Win32 `bcrypt` library for CSPRNG |
| **Concurrency** | Links POSIX / Win32 native threading libraries |
| **I/O** | Build system configuration |
| **Persistence** | Static build target definitions |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Link `vw_worker_queue.c` into worker binary | `worker/CMakeLists.txt:L39` | `test_worker_ipc` | ✅ done |
| 2 | Disable `GGML_OPENMP` on Win32 (avoid `libgomp-1.dll` dependency) | `worker/CMakeLists.txt:L11` | Build Verification | ✅ done |
| 3 | Pin `GGML_VULKAN OFF` for CPU-only build reproducibility | `worker/CMakeLists.txt:L20` | Build Verification | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.31 `worker/include/vw_worker_queue.h`

**Why change**: Header file declaring `vw_worker_queue_t` API (create, destroy, push, pop, `dropped_audio_us` tracking) and the `vw_worker_frame_t` struct container. Enforces Rule 11 with 20-30 word function comments.

**Responsibility before**: N/A (new file).  
**After**: Public C interface for bounded worker frame queue used between the IPC reader thread and main inference loop.

**Callers**: `worker/src/vw_worker.c`, `worker/src/vw_worker_queue.c`, `tests/unit/test_worker_queue.c`.  
**Callees**: None (header declaration file).

**Happy path**: `vw_worker.c` includes header and calls `vw_worker_queue_push` and `vw_worker_queue_pop`.

**Failure path**: Compiler error on invalid include or type mismatch.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | Documentation specifies parameter invariants and NULL behavior |
| **Authorization** | N/A |
| **Concurrency** | Mutex locking for push/pop; relaxed atomic load for `get_dropped_audio_us` |
| **I/O** | None |
| **Persistence** | None |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Declare `vw_worker_queue_t` API & 20-30 word standard comments | `worker/include/vw_worker_queue.h:L20-L36` | `test_worker_queue` | ✅ done |

**Assumptions/Tradeoffs**: Enforces Rule 11 (20-30 word header function documentation comments).

---

### 1.32 `worker/src/vw_worker.c`

**Why change**: Implement ADR-013 (Decoupled Worker IPC Reader Thread & Bounded Worker Frame Queue). Move pipe reading out of `vw_worker_run`'s main loop into a dedicated reader thread (`vw_worker_reader_main`), enqueueing incoming frames into `vw_worker_queue_t`. Main loop pops frames from queue, processes control messages, accumulates audio PCM, runs VAD speech detection, executes Whisper inference, and emits `CAPTION_SEGMENT` frames over IPC. Handle missing model path gracefully with `E_MODEL_MISSING` error reply.

**Responsibility before**: Single-threaded worker event loop interleave-reading pipe messages, performing Whisper inference, and writing replies. Inference blocked pipe reads, causing pipe backpressure stalls on long transcriptions.  
**After**: Dual-thread architecture: dedicated IPC reader thread (`vw_worker_reader_main:L62`) drains transport continuously into `vw_worker_queue_t` using 3-second receive timeouts (`VW_IPC_RECV_TIMEOUT`). Main loop (`vw_worker_run:L128`) pops frames from queue, executes inference, and sends replies single-writer over IPC. Graceful handling of missing model via `E_MODEL_MISSING` error frame (`vw_worker.c:L274`). Bounded teardown sequence: set `running = false`, join reader thread (bounded by 3s receive timeout), destroy queue, free resources, close IPC handle.

**Callers**: `worker/src/main.c` (`main()`), `test_worker_ipc.c`, `test_worker_lifecycle.c`.  
**Callees**: `vw_ipc_listen`, `vw_platform_thread_create`, `vw_platform_thread_join`, `vw_worker_queue_create`, `vw_worker_queue_push`, `vw_worker_queue_pop`, `vw_worker_queue_destroy`, `vw_whisper_engine_init`, `vw_whisper_engine_transcribe_pcm`, `vw_vad_detect_speech_energy`, `vw_segment_builder_push_hypothesis`, `vw_segment_builder_pop`, `send_error`.

**Happy path**:
1. Worker startup: `vw_worker_run:L128` opens IPC pipe, initializes Whisper engine, creates `vw_worker_queue_t` (capacity 32), and spawns `vw_worker_reader_main:L174`.
2. Reader thread: `vw_worker_reader_main:L62` loops receiving 20-byte headers and payloads via `vw_ipc_receive`. Pushes decoded frames into `queue`. Retries cleanly on 3s timeout.
3. Main loop: `vw_worker_run:L186` pops frames from `queue`.
   - `VW_MSG_HELLO`: verifies 32-byte secret auth token using constant-time comparison `verify_token_constant_time:L23`, replies `HELLO_ACK`.
   - `VW_MSG_START_SESSION`: checks sample rate (16kHz) and engine validity; if engine is missing, calls `send_error:L274` with `E_MODEL_MISSING` (recoverable=0); if valid, sets `session_active = true` and replies `STARTED`.
   - `VW_MSG_AUDIO_PCM`: appends S16LE PCM samples to `audio_buf`. When buffer accumulates 8 seconds (128,000 samples), runs VAD energy detection (`vw_vad_detect_speech_energy:L314`). If speech detected, transcribes via `vw_whisper_engine_transcribe_pcm`, pushes result to `builder`. Drains 2-second hop (32,000 samples).
   - Segments: pops completed segments from `builder`, encodes `CAPTION_SEGMENT` frame, and sends over IPC to plugin.
4. Teardown: `VW_MSG_SHUTDOWN` sets `running = false`. Main loop exits processing loop, sets `running = false` for reader, joins `reader_thread` (`vw_worker.c:L381`), destroys queue, frees audio buffers & engine, closes IPC handle, and returns 0.

**Failure path**:
1. Missing model file: Plugin sends `START_SESSION`; main loop checks `if (!engine)` at `vw_worker.c:L271`, calls `send_error(handle, ..., E_MODEL_MISSING, 0, ...)`, logs warning, and does NOT activate session. Worker stays alive for clean SHUTDOWN.
2. Pipe EOF / Peer closed: Reader thread gets `res < 0` (not timeout); jumps to `fatal:L119`, sets `running = false`, pushes synthetic `VW_MSG_SHUTDOWN` frame into `queue` to wake main loop immediately, and exits thread (`vw_worker.c:L124`). Main loop pops `VW_MSG_SHUTDOWN`, exits cleanly, joins reader thread, and frees resources.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | Header magic validation (`VW_PROTOCOL_MAGIC`), version major check, payload size bounds, sample rate check (16000Hz) |
| **Authorization** | Auth token constant-time comparison `verify_token_constant_time` prevents timing side-channels |
| **Concurrency** | Reader thread is sole IPC reader; main loop is sole IPC writer and sole queue consumer; thread exit synchronized via `atomic_bool running` and `vw_platform_thread_join` |
| **I/O** | Non-blocking/timed IPC reads (`VW_IPC_RECV_TIMEOUT = 3s`); bounded shutdown join |
| **Persistence** | None |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Decouple IPC reader thread from main inference main loop (ADR-013) | `worker/src/vw_worker.c:L62, L128` | `test_worker_ipc` | ✅ done |
| 2 | Constant-time auth token comparison | `worker/src/vw_worker.c:L23` | `test_worker_ipc` | ✅ done |
| 3 | Handle missing model with `E_MODEL_MISSING` error reply | `worker/src/vw_worker.c:L271` | `test_worker_lifecycle` | ✅ done |
| 4 | Clean 3s bounded teardown order (stop -> join -> destroy -> close) | `worker/src/vw_worker.c:L377-L391` | `test_worker_lifecycle` | ✅ done |

**Assumptions/Tradeoffs**: 3-second receive timeout in `vw_ipc_receive` bounds maximum thread join latency on shutdown without closing file descriptors while blocked in `recv`.

---

### 1.33 `worker/src/vw_worker_queue.c`

**Why change**: Authoritative implementation of bounded worker frame queue (`vw_worker_queue_t`) with mutex protection, FIFO ring-buffer storage, drop-oldest-AUDIO overflow eviction policy, and atomic `dropped_audio_us` metric tracking.

**Responsibility before**: N/A (new file).  
**After**: Thread-safe bounded ring buffer providing frame handoff between `vw_worker_reader_main` (producer) and `vw_worker_run` main loop (consumer).

**Callers**: `worker/src/vw_worker.c`, `tests/unit/test_worker_queue.c`.  
**Callees**: `pthread_mutex_init`, `pthread_mutex_lock`, `pthread_mutex_unlock`, `pthread_mutex_destroy`, `vw_protocol_decode_payload`, `malloc`, `free`.

**Happy path**:
1. Creation: `vw_worker_queue_create:L24` allocates `vw_worker_queue_t` and `slots` array of `capacity` (e.g. 32), initializes `pthread_mutex_t`.
2. Push: `vw_worker_queue_push:L67` locks mutex. If `head - tail < capacity`, assigns slot at `head % capacity`, increments `head`, unlocks mutex, returns true.
3. Pop: `vw_worker_queue_pop:L123` locks mutex. If `head == tail` (empty), unlocks and returns false. Otherwise copies slot at `tail % capacity` into `out`, increments `tail`, unlocks mutex, returns true.
4. Destruction: `vw_worker_queue_destroy:L46` loops from `tail` to `head`, frees any unpopped `payload` buffers, destroys mutex, and frees `slots` and `q`.

**Failure path**:
1. Queue Full (Overflow): `vw_worker_queue_push:L84` detects `head - tail == capacity`. Searches ring buffer from `tail` to `head` for the first frame with `type == VW_MSG_AUDIO_PCM` (`vw_worker_queue.c:L88`).
   - Evicts victim: decodes `duration_us` via `vw_worker_queue_audio_duration_us:L58`, adds to `dropped_audio_us` using relaxed atomic fetch-and-add (`vw_worker_queue.c:L95`), frees victim payload buffer.
   - Shifts remaining items left to preserve strict FIFO order of surviving frames (`vw_worker_queue.c:L100`).
   - Inserts new frame into freed slot, unlocks mutex, returns true. Control frames (`PAUSE`, `STOP`, `SHUTDOWN`, `START`) are NEVER evicted.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | `capacity == 0` check returns NULL; handles NULL queue, NULL payload, undecodable payload duration gracefully |
| **Authorization** | N/A |
| **Concurrency** | Protected by `pthread_mutex_t`; atomic `dropped_audio_us` read allows lock-free metric polling |
| **I/O** | None |
| **Persistence** | Heap allocations for slots and payload blocks |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Thread-safe mutex-protected ring buffer | `worker/src/vw_worker_queue.c:L24, L67, L123` | `test_worker_queue` | ✅ done |
| 2 | Drop-oldest-AUDIO overflow eviction policy | `worker/src/vw_worker_queue.c:L84-L111` | `test_worker_queue` | ✅ done |
| 3 | Control frame immunity from eviction | `worker/src/vw_worker_queue.c:L88` | `test_worker_queue` | ✅ done |
| 4 | Atomic `dropped_audio_us` tracking | `worker/src/vw_worker_queue.c:L95, L138` | `test_worker_queue` | ✅ done |
| 5 | Clean destroy freeing queued payloads | `worker/src/vw_worker_queue.c:L46-L53` | `test_worker_queue` | ✅ done |

**Assumptions/Tradeoffs**: Ponytail design decision: uses `pthread_mutex_t` rather than lock-free atomic CAS because mid-queue eviction of audio slots requires shifting items, and worker thread has no hard realtime constraints (Rule 4 applies strictly to VLC callback thread, not worker).

---

## 2. Happy-Path Request Trace

The following end-to-end trace demonstrates the complete audio processing lifecycle when the plugin is active and the worker is healthy:

```text
1. VLC opens the audio filter
   └─ plugin/src/vw_whisper_module.c:vw_plugin_open (L322)
      ├─ Allocates vw_plugin_sys_t, creates 8-second SPSC queue
      ├─ Resolves worker executable path via vw_plugin_resolve_worker_path (L117)
      ├─ Resolves model file path via vw_plugin_resolve_model_path (L156)
      ├─ Registers "model-path" config option in vlc_module_begin() (L454)
      ├─ Launches worker process with --pipe, --token, and optionally --model (L385)
      ├─ Calls vw_worker_client_launch_and_connect to set up IPC client (L385)
      ├─ Starts sender thread: vw_platform_thread_create(&sys->sender_thread, vw_plugin_sender_main, sys) (L398)
      └─ On success, enters VLC audio pipeline

2. VLC audio callback fires (main audio thread, lock-free)
   └─ plugin/src/vw_whisper_module.c:vw_plugin_filter (L289)
      ├─ Validates audio format (FL32/S16N/S32N)
      ├─ Normalizes and converts PCM via vw_audio_capture_process_block (L315)
      ├─ Enqueues to SPSC queue (drop-newest on overflow)
      └─ Returns p_block untouched (passthrough mode)

3. Plugin sender thread drains queue and sends AUDIO frames (background thread)
   └─ plugin/src/vw_whisper_module.c:vw_plugin_sender_main (L217)
      ├─ First iteration: calls vw_worker_client_start_session (returns false → worker_dead, passthrough)
      ├─ Loop: pops all available SPSC chunks and sends each via vw_worker_client_send_audio
      │   └─ On send failure → worker_dead = true, break
      ├─ After sends (or idle): calls vw_worker_client_receive_frame with 5ms/20ms deadline
      │   ├─ CAPTION_SEGMENT → segments_received++ (Step 15: presenter display)
      │   ├─ STATUS → status_received++, debug log (queued/inference/dropped)
      │   ├─ ERROR → errors_received++; non-recoverable → worker_dead = true
      │   └─ 0 (timeout) → loop continues
      ├─ Every 1024 chunks: info-level rate-limited log of sent/received counts
      └─ On worker_dead || !sender_running → loop exits, thread terminates

4. Worker reader thread drains pipe (dedicated reader, ADR-013)
   └─ worker/src/vw_worker.c:vw_worker_reader_main (L62)
      ├─ Receive header (20 bytes) via vw_ipc_receive, retry on timeout
      ├─ Decode + validate header
      ├─ Read payload via receive_all loop
      ├─ Push {type, payload_len, payload} to vw_worker_queue_t (L114)
      ├─ On timeout: clean retry (keeps pipe open)
      ├─ On fatal EOF: push synthetic VW_MSG_SHUTDOWN frame, set running = false, exit
      └─ Reader owns all reads; main loop is sole consumer and sole writer

5. Worker main loop processes frames (inference thread)
   └─ worker/src/vw_worker.c:vw_worker_run (L186)
      ├─ Pop frame from queue
      ├─ VW_MSG_HELLO → verify 32-byte auth token → HELLO_ACK
      ├─ VW_MSG_START_SESSION → validate sample_rate=16kHz, start session, reply STARTED
      │   (or missing engine → E_MODEL_MISSING, recoverable=false)
      ├─ VW_MSG_AUDIO_PCM → append S16LE to audio_buf, run VAD on 8s window, whisper inference on speech, emit CAPTION_SEGMENT
      ├─ VW_MSG_STOP → end session
      ├─ VW_MSG_SHUTDOWN → set running=false, exit loop
      └─ Teardown bounded by 3s reader receive timeout + thread join

6. Worker reply flows back through sender thread to plugin
   └─ vw_worker_client_receive_frame (poll with deadline) receives decoded frame  
      └─ vw_plugin_sender_main counts/logs by type (captions held for Step 15 SPU presenter)

7. VLC closes filter, clean shutdown
   └─ plugin/src/vw_whisper_module.c:vw_plugin_close (L411)
      ├─ Signals sender_running = false
      ├─ Joins sender thread (bounded)
      ├─ Stops IPC session, shuts down client
      ├─ Frees SPSC queue, worker client, sys struct
      └─ Unlinks IPC socket endpoint
```

---

## 3. Most Important Failure Path

### Failure Scenario: Missing Model File at Session Initialization (`E_MODEL_MISSING`)

```text
1. Plugin Open & Worker Process Launch
   ├─ plugin/src/vw_whisper_module.c:vw_plugin_open (L373)
   ├─ Resolves model path: vw_plugin_resolve_model_path returns false (model file not found)
   ├─ sys->model_path[] remains empty (pass NULL to worker spawn → no --model argv)
   ├─ Spawns worker executable with --pipe and --token only
   └─ Spawns sender thread: vw_plugin_sender_main (L398)

2. Session Start Request
   └─ plugin/src/vw_whisper_module.c:vw_plugin_sender_main (L222)
      └─ vw_worker_client_start_session(client, 0, "tiny.en")
         ├─ Sends VW_MSG_START_SESSION frame over IPC (no model_path → worker engine = NULL)
         └─ Awaits response via receive_all

3. Worker Receives START_SESSION, Detects Missing Engine
   └─ worker/src/vw_worker.c:vw_worker_run (L262)
      ├─ Pops VW_MSG_START_SESSION frame from queue
      ├─ Evaluates if (!engine) → true (no model loaded)
      ├─ Calls send_error(handle, session_id, E_MODEL_MISSING, recoverable=0, "Whisper model file missing or invalid", &sequence) (L274)
      │   ├─ Encodes VW_MSG_ERROR payload (error_code=1001, recoverable=false)
      │   └─ Transmits VW_MSG_ERROR header + payload frame over IPC
      └─ session_active remains false

4. Plugin Receives ERROR Reply and Degrades
   └─ plugin/src/vw_worker_client.c:vw_worker_client_start_session (L284)
      ├─ receive_all returns VW_MSG_ERROR
      ├─ Decodes error payload: code=1001 (E_MODEL_MISSING), recoverable=false
      ├─ Logs warning via vw_log_event("WORKER_START_ERROR", "code=1001 recoverable=0 msg=Whisper model file missing or invalid")
      ├─ Frees payload buffer, drops transport
      └─ Returns false to vw_plugin_sender_main

5. Sender Thread Enters Passthrough Mode
   └─ plugin/src/vw_whisper_module.c:vw_plugin_sender_main (L223)
      ├─ Receives false from vw_worker_client_start_session
      ├─ Sets atomic flag: atomic_store(&sys->worker_dead, true)
      ├─ Logs warning: "PLUGIN_SESSION_START_FAIL: worker rejected session; captions disabled, passthrough only" (L224)
      └─ Sender thread exits loop, returns NULL (thread terminates)

6. VLC Media Playback Continues Uninterrupted
   └─ plugin/src/vw_whisper_module.c:vw_plugin_filter (L289)
      ├─ Audio callback continues executing on VLC main audio thread
      ├─ sys->capture enqueues audio to SPSC queue (overflow chunks dropped harmlessly)
      ├─ No more IPC activity (sender thread is dead)
      └─ Returns p_block untouched to VLC audio output pipeline (100% uninterrupted audio playback, captions disabled)
```

---

## 4. Boundary Summary

| Boundary type | What to check | Code Location | Finding / Guard Implementation |
| --- | --- | --- | --- |
| **Input validation** | Audio Sample Rate | `vw_protocol_validate.c:L54`<br>`vw_worker.c:L266` | Enforces `sample_rate == 16000`. Unsupported rates rejected with `E_AUDIO_FORMAT` error frame reply. |
| **Input validation** | Payload Length Bounds | `vw_worker_client.c:L412`<br>`vw_worker.c:L91` | Frame payload lengths verified against declared `hdr.payload_length`. Memory allocations checked against NULL. |
| **Input validation** | Text String Bounds | `vw_worker_client.c:L458` | Caption segment UTF-8 text length bounded to `VW_MAX_TEXT_BYTES - 1` and explicit null terminator applied. |
| **Input validation** | PCM Payload Rounding Tolerance | `vw_protocol_validate.c:L100` | Accepts `expected_bytes ± 1` byte tolerance to prevent off-by-one audio frame rejection on odd-length partial PCM blocks. |
| **Authorization** | Auth Token Verification | `vw_worker.c:L23` | Secret 32-byte auth token validated using `verify_token_constant_time` to eliminate timing side-channel attacks. |
| **Authorization** | IPC Named Pipe Endpoint | `vw_whisper_module.c:L349` | Named pipe / Unix socket bound to process PID with restricted OS ACL permissions (`0600`). |
| **Concurrency** | VLC Callback Lock-Free Invariant | `vw_whisper_module.c:L289`<br>`vw_audio_capture.c:L72` | Audio callback thread is 100% lock-free (Rule 4). Enqueues PCM to lock-free SPSC queue (`vw_spsc_queue_t`). |
| **Concurrency** | Worker Thread Split (ADR-013) | `vw_worker.c:L62, L128` | Reader thread (`vw_worker_reader_main`) is sole IPC socket reader. Main loop (`vw_worker_run`) is sole queue consumer and sole IPC socket writer. Thread exit synchronized via `atomic_bool running` and `vw_platform_thread_join`. |
| **Concurrency** | Worker Frame Queue Mutex | `vw_worker_queue.c:L67, L123` | Frame queue push and pop protected by `pthread_mutex_t`. Metrics (`dropped_audio_us`) updated/read via relaxed atomic operations. |
| **I/O** | Worker Pipe Read Timeout | `vw_worker.c:L73` | IPC reads use 3-second timeout (`VW_IPC_RECV_TIMEOUT`). Retries loop on timeout while running, enabling prompt thread exit during teardown without closing file descriptor while blocked in `recv`. |
| **I/O** | Plugin Frame Receive Deadline | `vw_worker_client.c:L398` | Receiver uses absolute deadline timing. Timed-out receives return 0 without dropping the transport handle. |
| **Persistence** | IPC Socket File Cleanup | `vw_whisper_module.c:L411`<br>`vw_worker.c:L389` | Pipe endpoints and socket files unlinked and closed cleanly on shutdown. Zero transcript or PCM data persisted to disk (Rule 5). |

---

## 5. Acceptance Criterion -> Code Mapping

| # | Criterion | Code Implementation | Test Assertion | Status |
| --- | --- | --- | --- | --- |
| 1 | Decouple IPC reader thread from main inference loop (ADR-013) | `worker/src/vw_worker.c:L62, L128` | `tests/integration/test_worker_ipc.c:L45` | ✅ done |
| 2 | Bounded worker frame queue (`vw_worker_queue`) with drop-oldest-AUDIO policy | `worker/src/vw_worker_queue.c:L24-L144` | `tests/unit/test_worker_queue.c:L25-L143` | ✅ done |
| 3 | Control frames (`START`, `STOP`, `PAUSE`, `RESUME`, `SHUTDOWN`) immune from eviction | `worker/src/vw_worker_queue.c:L88` | `tests/unit/test_worker_queue.c:L65, L85, L108` | ✅ done |
| 4 | Atomic `dropped_audio_us` metrics tracking on queue overflow | `worker/src/vw_worker_queue.c:L95, L138` | `tests/unit/test_worker_queue.c:L72, L109` | ✅ done |
| 5 | Plugin background sender thread (`vw_sender_thread`) with 5ms/20ms cadence | `plugin/src/vw_whisper_module.c:L217-L286` | `tests/integration/test_worker_lifecycle.c:L82` | ✅ done |
| 6 | Full-duplex `vw_worker_client_receive_frame` frame decoding | `plugin/src/vw_worker_client.c:L398-L476` | `tests/unit/vw_test_worker_client.c:L280-L370` | ✅ done |
| 7 | Pass optional `--model` flag in worker spawn command line | `plugin/src/vw_worker_client.c:L87-L93` | `tests/integration/test_worker_lifecycle.c:L82` | ✅ done |
| 8 | Auto-discover GGML model file next to plugin/executable | `plugin/src/vw_whisper_module.c:L156-L190` | `tests/integration/test_worker_lifecycle.c:L82` | ✅ done |
| 9 | Surface worker model missing error (`E_MODEL_MISSING`) | `worker/src/vw_worker.c:L271-L277`<br>`plugin/src/vw_worker_client.c:L284` | `tests/integration/test_worker_lifecycle.c:L45` | ✅ done |
| 10 | Degrade gracefully to audio passthrough on worker error | `plugin/src/vw_whisper_module.c:L223, L237, L289` | `tests/integration/test_worker_lifecycle.c:L45` | ✅ done |
| 11 | Accept ±1 byte whole-sample rounding tolerance for audio PCM | `protocol/src/vw_protocol_validate.c:L100` | `tests/unit/test_protocol_validate.c:L35` | ✅ done |
| 12 | Disable `GGML_OPENMP` on Win32 to prevent runtime `libgomp-1.dll` dependency | `worker/CMakeLists.txt:L11` | Build Verification | ✅ done |
| 13 | Complete test suite execution clean under CTest | CMake & CTest Build Files | All 16 CTest targets passing 100% | ✅ done |
| 14 | Zero memory leaks under Valgrind | `test_worker_queue.c`, destroy cleanups | Valgrind memcheck clean | ✅ done |

---

## 7. Code Review Findings (Bugs, Risks, Nitpicks)

### Bugs (Sorted by Priority)

| Priority | Component / Location | Description | Impact | Proposed Fix |
| --- | --- | --- | --- | --- |
| **Medium** | `worker/src/vw_worker_queue.c:L100` | In `vw_worker_queue_push`, when evicting an audio frame from a full queue, the left-shift loop `for (size_t i = evict; i + 1 < q->head; i++)` copies slot contents using raw struct assignment. For large queue capacities under sustained burst, shifting up to N items under the mutex holds the lock longer than necessary. | Minor CPU lock contention during sustained queue saturation. | Replace item-by-item array shift with two `memmove` calls handling the ring wrap-around boundaries. |
| **Low** | `plugin/src/vw_whisper_module.c:L280` | In `vw_plugin_sender_main`, logging cadence triggers every 1024 chunks (`sys->chunks_sent % 1024 == 0`), but if playback stops mid-burst, the final partial chunk count is not logged. | Minor diagnostic omission in verbose debug logs on stop. | Add an explicit debug log in `vw_plugin_close` displaying final `chunks_sent` count. |
| **Low** | `plugin/src/vw_whisper_module.c:L247` | Local variable `r` in `vw_plugin_sender_main` is brief (`int r = vw_worker_client_receive_frame(...)`). | Minor readability issue. | Rename `r` to `recv_status` for enhanced readability. |

### Architectural & Operational Risks

| Category | Risk Description | Affected Files | Mitigation Strategy |
| --- | --- | --- | --- |
| **Thread Synchronization** | In `vw_worker.c`, worker reader thread exit relies on `atomic_bool running` and a 3-second receive timeout (`VW_IPC_RECV_TIMEOUT`). If an OS platform IPC read call ignores timeouts during network/pipe fault states, joining the thread could delay worker exit up to 3s. | `worker/src/vw_worker.c`, `protocol/src/vw_ipc_socket_linux.c` | Ensure socket SO_RCVTIMEO or non-blocking select/poll is strictly honored across all target OS platforms. |
| **Resource Allocation** | In `vw_worker_queue.c`, incoming frame payloads are dynamic `malloc` blocks passed into the queue. Under extreme audio burst conditions, memory footprint scales with queued frames up to capacity (32 frames * ~16KB = ~512KB). | `worker/src/vw_worker_queue.c` | Bounded capacity (32 slots) places a strict upper bound of ~512KB on queue memory allocation. |
| **Model Path Validation** | `vw_plugin_resolve_model_path` returns false silently when model is missing; worker discovers this at session start (`E_MODEL_MISSING`). No pre-flight check exists at plugin open time. | `plugin/src/vw_whisper_module.c` | Intentional: enables graceful passthrough mode without blocking user playback. |

### Code Style & Quality Nitpicks

| Issue Type | File & Line | Description | Recommendation |
| --- | --- | --- | --- |
| **Header Documentation** | `plugin/include/vw_worker_client.h:L61` | Comment explaining `vw_worker_recv_t` struct layout is ~18 words instead of target 20-30 word range. | Expand comment slightly to elaborate on payload buffer ownership details. |
| **Variable Naming** | `plugin/src/vw_whisper_module.c:L247` | Local variable `r` in `vw_plugin_sender_main` is very brief. | Rename `r` to `recv_status` for enhanced readability. |
