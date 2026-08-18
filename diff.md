# Diff Analysis: gemini/milestone-3-step-17a (GPU Vulkan & Seek Lifecycle)

**18 files changed, +1073 / -433 lines vs current `gemini/milestone-3` (post fast-forward; previous header +809/-620 was vs pre-merge base — see note below)**  
**Base**: `gemini/milestone-3` (at `1c3ff2c`, includes step-17 seek; for the cumulative 31-file report vs pre-17 base see git log)

---

## 1. File-by-File Analysis

### 1.1 `.agents/AGENTS.md`

**Why change**: Document Rule 16 requiring mandatory inspection of local dependency graph (`graphify-out/`) before planning or modifying code.

**Responsibility before**: Core coding rules and architectural invariants up to Rule 15.  
**After**: Added Rule 16 for codebase dependency graph analysis.

**Callers**: AI agents working in the repository.  
**Callees**: None.

**Happy path**: AI agent inspects `graphify-out/` prior to planning changes.

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
| 1 | Include dependency graph reading directive | `.agents/AGENTS.md:L48` | Manual Inspection | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.2 `AGENTS.md`

**Why change**: Mirror `.agents/AGENTS.md` updates at the root level for agent consistency.

**Responsibility before**: Repository rules up to Rule 15.  
**After**: Contains Rule 16.

**Callers**: AI agents and contributors.  
**Callees**: None.

**Happy path**: Agent loads root instructions.

**Failure path**: N/A.

**Boundaries**: N/A.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Synchronize root AGENTS.md rules | `AGENTS.md:L48` | Manual Inspection | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.3 `CMakePresets.json`

**Why change**: Add dedicated CPU-only presets (`linux-x64-debug-cpu`, `windows-x64-release-cpu`, `windows-x64-debug-cpu`) that explicitly pass `VW_WITH_VULKAN=OFF`, providing a predictable low-memory build path that never invokes `glslc`.

**Responsibility before**: Defined standard Linux and Windows debug/release/coverage presets with Vulkan enabled by default.  
**After**: Defines explicit `*-cpu` configure, build, and test presets alongside GPU default presets.

**Callers**: Developer running `cmake --preset <name>`.  
**Callees**: CMake configuration parser.

**Happy path**: Developer selects `windows-x64-release-cpu` to build `vlc-whisper-worker-cpu.exe` without requiring a Vulkan SDK.

**Failure path**: Invalid preset name causes CMake to exit with error.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | JSON schema format validation |
| **Authorization** | N/A |
| **Concurrency** | N/A |
| **I/O** | N/A |
| **Persistence** | N/A |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Add `linux-x64-debug-cpu` preset | `CMakePresets.json:L70-L81` | CMake configure | ✅ done |
| 2 | Add `windows-x64-release-cpu` preset | `CMakePresets.json:L21-L33` | CMake configure | ✅ done |
| 3 | Add `windows-x64-debug-cpu` preset | `CMakePresets.json:L46-L58` | CMake configure | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.4 `README.md`

**Why change**: Document system prerequisites (Debian/Ubuntu `apt`, Fedora `dnf`), submodule cloning requirements (`--recursive`), preset matrix, Linux/Windows GPU vs CPU build steps, `$VW_VULKAN_SDK` MinGW layout, and Vulkan shader compile memory constraints.

**Responsibility before**: High-level build instructions for Windows cross-compilation and testing.  
**After**: Full reproducible developer guide for Linux and Windows targets with Vulkan GPU acceleration and CPU fallback options.

**Callers**: Developers cloning and building the repository.  
**Callees**: None.

**Happy path**: Developer follows `README.md` to install prerequisites, clone with submodules, and compile the GPU or CPU worker.

**Failure path**: N/A.

**Boundaries**: N/A.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Add OS package prerequisites | `README.md:L7-L31` | Documentation Review | ✅ done |
| 2 | Add `--recursive` clone instructions | `README.md:L37-L48` | Documentation Review | ✅ done |
| 3 | Document GPU vs CPU build workflows | `README.md:L60-L135` | Documentation Review | ✅ done |
| 4 | Document shader compile memory limits | `README.md:L137-L144` | Documentation Review | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.5 `docs/api-contracts.md`

**Why change**: Update control frame contract documentation with `VW_CTRL_REASON_SEEK_DISCONTINUITY = 2U`.

**Responsibility before**: Documented `STOP_SESSION` reasons up to Step 16 (`USER_STOP=1`, `MEDIA_END=3`).  
**After**: Documents `SEEK_DISCONTINUITY=2` reason code for seek-triggered session restarts.

**Callers**: Developers reviewing wire protocol specifications.  
**Callees**: None.

**Happy path**: Developer verifies wire constant `2U` for seek discontinuity.

**Failure path**: N/A.

**Boundaries**: N/A.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Document `SEEK_DISCONTINUITY` in control reasons | `docs/api-contracts.md:L87` | Manual Inspection | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.6 `docs/architecture.md`

**Why change**: Document Step 17 seek handling and Step 17a GPU acceleration architecture.

**Responsibility before**: Architecture specification up to Step 16 play/pause.  
**After**: Documents sender thread seek restart epoch workflow, OSD blanking, and `ggml-vulkan` backend integration with transparent CPU fallback.

**Callers**: Developers and AI agents.  
**Callees**: None.

**Happy path**: Reviewer inspects architecture document for system layout.

**Failure path**: N/A.

**Boundaries**: N/A.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Document seek handling architecture | `docs/architecture.md:L75` | Manual Inspection | ✅ done |
| 2 | Document GPU inference backend architecture | `docs/architecture.md:L85` | Manual Inspection | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.7 `docs/plans/step-14-realtime-pcm-streaming.md` (Deleted)

**Why change**: Clean up obsolete plan file.

**Responsibility before**: Step 14 implementation plan.  
**After**: Deleted file.

**Callers**: N/A.  
**Callees**: N/A.

**Happy path**: File removed from workspace.

**Failure path**: N/A.

**Boundaries**: N/A.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Remove obsolete plan | Deleted | N/A | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.8 `docs/plans/step14c_plan.md` (Deleted)

**Why change**: Clean up obsolete plan file.

**Responsibility before**: Step 14c implementation plan.  
**After**: Deleted file.

**Callers**: N/A.  
**Callees**: N/A.

**Happy path**: File removed from workspace.

**Failure path**: N/A.

**Boundaries**: N/A.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Remove obsolete plan | Deleted | N/A | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.9 `docs/plans/step15_plan.md` (Deleted)

**Why change**: Clean up obsolete plan file.

**Responsibility before**: Step 15 implementation plan.  
**After**: Deleted file.

**Callers**: N/A.  
**Callees**: N/A.

**Happy path**: File removed from workspace.

**Failure path**: N/A.

**Boundaries**: N/A.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Remove obsolete plan | Deleted | N/A | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.10 `docs/plans/step16_plan.md` (Deleted)

**Why change**: Clean up obsolete plan file.

**Responsibility before**: Step 16 implementation plan.  
**After**: Deleted file.

**Callers**: N/A.  
**Callees**: N/A.

**Happy path**: File removed from workspace.

**Failure path**: N/A.

**Boundaries**: N/A.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Remove obsolete plan | Deleted | N/A | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.11 `docs/plans/step17_restart_deprecation_plan.md`

**Why change**: Provide architectural plan documenting seek session restart and deprecation analysis.

**Responsibility before**: New file.  
**After**: Technical design record for seek handling.

**Callers**: AI agents and developers.  
**Callees**: None.

**Happy path**: Reader consults plan file.

**Failure path**: N/A.

**Boundaries**: N/A.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Store Step 17 seek plan | `docs/plans/step17_restart_deprecation_plan.md:L1` | Manual Inspection | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.12 `docs/plans/step17a_plan.md`

**Why change**: Provide technical plan for Step 17a (GPU Vulkan Acceleration default ON) per Rule 9.

**Responsibility before**: New file.  
**After**: Authoritative task plan and design record for Step 17a.

**Callers**: AI agents and developers.  
**Callees**: None.

**Happy path**: Reader inspects `docs/plans/step17a_plan.md` for GPU acceleration requirements and verification gates.

**Failure path**: N/A.

**Boundaries**: N/A.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Enforce Rule 9 task template for Step 17a | `docs/plans/step17a_plan.md:L1` | Manual Inspection | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.13 `docs/roadmap.md`

**Why change**: Mark Step 17 (Seeking & Discontinuity support) and Step 17a (GPU Vulkan Acceleration) as completed per Rule 14.

**Responsibility before**: Roadmap with Step 17 and 17a unchecked.  
**After**: Roadmap with Step 17 and 17a marked complete.

**Callers**: Project maintainers.  
**Callees**: None.

**Happy path**: Reviewer checks roadmap progress.

**Failure path**: N/A.

**Boundaries**: N/A.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Update roadmap completion status | `docs/roadmap.md:L51-L53` | Manual Inspection | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.14 `docs/test-strategy.md`

**Why change**: Document Step 17 seek tests and Step 17a GPU worker CLI/engine tests per Rule 14.

**Responsibility before**: Test inventory up to Step 16.  
**After**: Test inventory updated with Step 17 seek tests and Step 17a backend config tests.

**Callers**: Developers running test suites.  
**Callees**: None.

**Happy path**: Developer verifies test inventory.

**Failure path**: N/A.

**Boundaries**: N/A.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Add Step 17 & 17a test documentation | `docs/test-strategy.md:L59-L62` | Manual Inspection | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.15 `docs/vlc-api-essentials.md`

**Why change**: Update VLC API essentials document for discontinuity flags and position jump polling.

**Responsibility before**: VLC API reference guide.  
**After**: Updated guide explaining `BLOCK_FLAG_DISCONTINUITY` and `INPUT_GET_TIME` behavior.

**Callers**: Plugin developers.  
**Callees**: None.

**Happy path**: Reader consults VLC API essentials guide.

**Failure path**: N/A.

**Boundaries**: N/A.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Update VLC API guide | `docs/vlc-api-essentials.md:L140` | Manual Inspection | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.16 `docs/whisper-api.md`

**Why change**: Document `vw_whisper_engine` C17 engine wrapper types (`vw_worker_backend_t`), initialization parameters (`model_path`, `backend`, `gpu_device`), default behaviors, and updated Vulkan GPU usage pattern per Rule 14.

**Responsibility before**: Documented third-party `whisper.cpp` C API and CPU-only usage pattern.  
**After**: Added documentation for `vw_whisper_engine` wrapper API, `vw_worker_backend_t` enum values (`AUTO`, `GPU`, `CPU`), device ordinal selection, and Vulkan GPU execution flow with automatic CPU fallback.

**Callers**: Worker developers and API consumers.  
**Callees**: None.

**Happy path**: Developer reviews `docs/whisper-api.md` for engine wrapper signatures and usage patterns.

**Failure path**: N/A.

**Boundaries**: N/A.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Document `vw_whisper_engine` API & backend options | `docs/whisper-api.md:L774-L840` | Manual Inspection | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.17 `plugin/include/vw_caption_presenter.h`

**Why change**: Declare `vw_caption_presenter_blank()` for mid-session OSD blanking and document `vw_caption_presenter_clear()` as teardown-only.

**Responsibility before**: Declared presenter display, show_segment, and clear functions.  
**After**: Declares `vw_caption_presenter_blank()` and clarifies clear caller lifecycle (`vw_plugin_close`).

**Callers**: `vw_whisper_module.c`, `vw_caption_presenter.c`, `test_caption_presenter.c`.  
**Callees**: None.

**Happy path**: `vw_whisper_module.c` calls `vw_caption_presenter_blank(&sys->presenter)` on seek restart.

**Failure path**: N/A.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | Handles NULL presenter pointers |
| **Authorization** | N/A |
| **Concurrency** | Sender thread only |
| **I/O** | N/A |
| **Persistence** | N/A |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Declare `vw_caption_presenter_blank` | `plugin/include/vw_caption_presenter.h:L26` | `test_caption_presenter` | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.17 `plugin/libvlccore.def`

**Why change**: Export `vout_FlushSubpictureChannel` symbol for MinGW Windows linking.

**Responsibility before**: Exported standard VLC symbols for `libvlccore.dll`.  
**After**: Includes `vout_FlushSubpictureChannel`.

**Callers**: MinGW linker.  
**Callees**: `libvlccore.dll`.

**Happy path**: Linker resolves `vout_FlushSubpictureChannel` on Windows.

**Failure path**: Linker error if symbol is missing.

**Boundaries**: N/A.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Export `vout_FlushSubpictureChannel` | `plugin/libvlccore.def:L10` | Build verification | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.18 `plugin/src/vw_caption_presenter.c`

**Why change**: Implement `vw_caption_presenter_blank()` to erase active OSD subpictures mid-session without clearing filter context.

**Responsibility before**: Handled presenter display, vout lookup, and teardown clear.  
**After**: Implements `vw_caption_presenter_blank()` using `vout_FlushSubpictureChannel(vout, VOUT_SPU_CHANNEL_OSD)` and 1ms blank OSD text.

**Callers**: `vw_whisper_module.c:L320`, `test_caption_presenter.c`.  
**Callees**: `vw_caption_presenter_find_vout`, `vout_FlushSubpictureChannel`, `vout_OSDText`, `vlc_object_release`.

**Happy path**: `vw_caption_presenter_blank` finds vout, flushes channel 1, emits 1ms blank OSD text, releases vout, preserving `p_filter_ctx`.

**Failure path**: Returns false if presenter is NULL or vout cannot be found.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | Null checks on `presenter` and `p_filter_ctx` |
| **Authorization** | N/A |
| **Concurrency** | Sender thread only; releases vout reference immediately |
| **I/O** | OSD subpicture channel flush |
| **Persistence** | None |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Implement mid-session OSD blanking | `plugin/src/vw_caption_presenter.c:L125` | `test_caption_presenter` | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.19 `plugin/src/vw_whisper_module.c`

**Why change**: Implement seek/discontinuity epoch restart (`STOP` -> drain SPSC -> `START` with new `session_id`), position-jump detection (`VW_SEEK_JUMP_THRESHOLD_US = 1s`), pause baseline backfill, PTS fallback check (`VW_PTS_JUMP_THRESHOLD_US = 500ms`), and fallback candidate probing for `vlc-whisper-worker` / `vlc-whisper-worker-cpu` in `vw_plugin_resolve_worker_path`.

**Responsibility before**: Audio callback and sender thread with play/pause lifecycle.  
**After**: Audio callback with PTS jump fallback, sender thread with position-jump seek detection, OSD blanking, session epoch restart, and multi-candidate worker executable resolution (`vlc-whisper-worker` then `vlc-whisper-worker-cpu`).

**Callers**: VLC module loader, VLC audio pipeline (`pf_audio_filter`), sender thread.  
**Callees**: `vw_caption_presenter_blank`, `vw_worker_client_stop_session`, `vw_spsc_queue_pop`, `vw_worker_client_start_session`, `vw_plugin_find_input`, `input_GetState`, `vw_plugin_input_position_us`, `vw_plugin_probe_ancestors`.

**Happy path**: Callback or sender poll detects seek, sets `discontinuity_pending = true`. Sender thread blanks OSD, sends `STOP_SESSION(SEEK_DISCONTINUITY)`, drains pre-seek PCM from SPSC queue, and sends `START_SESSION` with new session ID and PTS anchor. On startup, `vw_plugin_resolve_worker_path` checks for GPU worker `vlc-whisper-worker` and falls back to CPU worker `vlc-whisper-worker-cpu`.

**Failure path**: Worker rejects restart; sender sets `worker_dead = true`, logs warning, breaks loop, and plugin degrades to audio passthrough.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | `p_block->i_pts >= VLC_TS_0` + `last_pts_us > 0` guards; `position_us >= 0` guards; threshold macros (1s / 500ms); candidate name array bounds |
| **Authorization** | CSPRNG auth token verification |
| **Concurrency** | Lock-free audio callback (Rule 4), writes only atomic bool/int64; sender thread executes restart sequence exclusively |
| **I/O** | Non-blocking SPSC queue drain; IPC control frames |
| **Persistence** | None |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Seek discontinuity session restart | `plugin/src/vw_whisper_module.c:L318-L335` | `test_worker_lifecycle` | ✅ done |
| 2 | Paused seek baseline backfill | `plugin/src/vw_whisper_module.c:L274, L286` | Code Review | ✅ done |
| 3 | Threshold macros | `plugin/src/vw_whisper_module.c:L57-L58` | Code Review | ✅ done |
| 4 | Worker binary fallback probing (GPU then CPU) | `plugin/src/vw_whisper_module.c:L128, L148` | `test_plugin_load` | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.20 `protocol/include/vw_protocol_types.h`

**Why change**: Add `VW_CTRL_REASON_SEEK_DISCONTINUITY = 2U` control reason constant.

**Responsibility before**: Protocol types up to Step 16.  
**After**: Protocol types including `VW_CTRL_REASON_SEEK_DISCONTINUITY`.

**Callers**: `vw_whisper_module.c`, `vw_worker_client.c`, `vw_worker.c`, unit tests.  
**Callees**: None.

**Happy path**: Code passes `VW_CTRL_REASON_SEEK_DISCONTINUITY` in control frame payload.

**Failure path**: N/A.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | Fits in `uint16_t` |
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

### 1.21 `tests/integration/test_worker_lifecycle.c`

**Why change**: Assert multi-session restart (`START` -> `AUDIO` -> `STOP(SEEK_DISCONTINUITY)` -> `START` -> `AUDIO` -> `STOP` -> `SHUTDOWN`) with session ID epoch gating verification.

**Responsibility before**: Single session streaming integration test.  
**After**: Lifecycle test verifying multi-session epoch restarts and pre-seek audio rejection.

**Callers**: CTest harness (`ctest`).  
**Callees**: `vw_worker_client_start_session`, `vw_worker_client_send_audio`, `vw_worker_client_stop_session`.

**Happy path**: Test starts session 1, sends audio, sends `STOP(SEEK_DISCONTINUITY)`, starts session 2 with new session ID, sends audio, stops session 2, shuts down client, and asserts worker process exits 0 cleanly.

**Failure path**: Test assertion fails if worker crashes or fails restart.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | Checks response codes |
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

### 1.22 `tests/unit/test_caption_presenter.c`

**Why change**: Assert `vw_caption_presenter_blank()` mid-session behavior, `vw_caption_presenter_clear()` teardown behavior, and flush invocation counts on `fake_filter.obj.object_type = "vout"`.

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

### 1.23 `tests/unit/test_whisper_engine.c`

**Why change**: Update `test_whisper_engine` call site to match the new `vw_whisper_engine_init(model_path, backend, gpu_device)` signature.

**Responsibility before**: Initialized engine with `model_path` only.  
**After**: Passes `VW_WORKER_BACKEND_AUTO` and `gpu_device = 0` to engine init.

**Callers**: CTest harness (`ctest`).  
**Callees**: `vw_whisper_engine_init`, `vw_whisper_engine_transcribe`, `vw_whisper_engine_free`.

**Happy path**: Test initializes engine with `VW_WORKER_BACKEND_AUTO`, transcribes test audio vector, verifies output, and frees engine.

**Failure path**: Test fails if engine init returns NULL or transcription fails.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | Validates engine init and transcription results |
| **Authorization** | N/A |
| **Concurrency** | Single-threaded test |
| **I/O** | File model load |
| **Persistence** | None |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Update engine test for backend params | `tests/unit/test_whisper_engine.c:L22` | `test_whisper_engine` | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.24 `tests/unit/test_worker_config.c`

**Why change**: Add unit tests for `--backend auto|gpu|cpu` and `--gpu-device <id>` CLI arguments, default values, and error cases (invalid backend string, negative device ID, non-numeric device ID, dangling flags).

**Responsibility before**: Tested `--pipe`, `--token`, `--model`, `--log-file` arguments and error cases.  
**After**: Tests `--backend` and `--gpu-device` flag parsing, default initialization (`AUTO` and `0`), and invalid flag rejection.

**Callers**: CTest harness (`ctest`).  
**Callees**: `vw_worker_config_init_defaults`, `vw_worker_config_parse_args`.

**Happy path**: Test parses `--backend gpu --gpu-device 1`, asserts `backend == VW_WORKER_BACKEND_GPU` and `gpu_device == 1`. Tests `--backend cpu`, asserts `backend == VW_WORKER_BACKEND_CPU`. Tests `--backend auto`, asserts `backend == VW_WORKER_BACKEND_AUTO`.

**Failure path**: Test passes `--backend cuda`, `--gpu-device -1`, `--gpu-device abc`, `--backend` (dangling), asserts return code `2`.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | Checks enum bounds and integer ranges (0..65535) |
| **Authorization** | N/A |
| **Concurrency** | Single-threaded test |
| **I/O** | None |
| **Persistence** | None |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Test backend and GPU device success paths | `tests/unit/test_worker_config.c:L62-L80` | `test_worker_config` | ✅ done |
| 2 | Test backend and GPU device failure paths | `tests/unit/test_worker_config.c:L83-L100` | `test_worker_config` | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.25 `tests/unit/vw_test_worker_client.c`

**Why change**: Assert `VW_CTRL_REASON_SEEK_DISCONTINUITY` framing against mock server.

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

### 1.26 `worker/CMakeLists.txt`

**Why change**: Enable Vulkan GPU acceleration by default (`VW_WITH_VULKAN=ON`), detect Vulkan SDK and `glslc`, handle MinGW cross-compilation find paths via `$ENV{VW_VULKAN_SDK}` (with explicit include dir override), set distinct output binary names (`vlc-whisper-worker` vs `vlc-whisper-worker-cpu`), and fallback to CPU-only with a warning if Vulkan SDK is absent.

**Responsibility before**: Pinned `GGML_VULKAN OFF` and built CPU-only worker.  
**After**: `VW_WITH_VULKAN` option (default ON), `find_package(Vulkan COMPONENTS glslc)`, cross find path support, and output artifact naming.

**Callers**: CMake build system.  
**Callees**: `third_party/whisper.cpp`, `find_package(Vulkan)`.

**Happy path**:
1. Host native Linux: CMake finds system `libvulkan-dev` and `glslc`, sets `GGML_VULKAN=ON`, builds `vlc-whisper-worker` with Vulkan GPU backend.
2. Windows MinGW cross: With `VW_VULKAN_SDK` set, CMake finds Windows `libvulkan-1.a` and headers, sets `Vulkan_INCLUDE_DIR` to avoid host include pollution, sets `GGML_VULKAN=ON`, builds `vlc-whisper-worker.exe`.

**Failure path**:
1. Vulkan SDK / glslc absent: Emits `WARNING: VW: VW_WITH_VULKAN=ON but no Vulkan SDK/glslc found — building CPU-only worker`, sets `GGML_VULKAN=OFF`, builds `vlc-whisper-worker-cpu`.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | Checks `Vulkan_FOUND AND Vulkan_GLSLC_EXECUTABLE` |
| **Authorization** | N/A |
| **Concurrency** | Multi-threaded build target configuration |
| **I/O** | Toolchain discovery |
| **Persistence** | Output binary generation |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Add `VW_WITH_VULKAN` option (default ON) | `worker/CMakeLists.txt:L23` | CMake configure | ✅ done |
| 2 | Vulkan SDK & glslc detection | `worker/CMakeLists.txt:L37-L43` | CMake configure | ✅ done |
| 3 | MinGW cross find path & include isolation | `worker/CMakeLists.txt:L31-L35` | Windows build | ✅ done |
| 4 | Output binary naming (`vlc-whisper-worker` vs `-cpu`) | `worker/CMakeLists.txt:L83-L87` | Build verification | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.27 `worker/include/vw_whisper_engine.h`

**Why change**: Update `vw_whisper_engine_init` signature to accept `vw_worker_backend_t backend` and `int gpu_device`.

**Responsibility before**: `vw_whisper_engine_init(const char* model_path)`.  
**After**: `vw_whisper_engine_init(const char* model_path, vw_worker_backend_t backend, int gpu_device)` with doc comment explaining backend selection and transparent CPU fallback.

**Callers**: `worker/src/vw_worker.c`, unit tests.  
**Callees**: None.

**Happy path**: Caller passes backend enum and GPU device ordinal to engine initialization.

**Failure path**: N/A.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | Parameter typing |
| **Authorization** | N/A |
| **Concurrency** | Engine instance initialization |
| **I/O** | N/A |
| **Persistence** | N/A |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Update engine header signature and doc comment | `worker/include/vw_whisper_engine.h:L14-L18` | `test_whisper_engine` | ✅ done |

**Assumptions/Tradeoffs**: Enforces Rule 11 (20-30 word header comments).

---

### 1.28 `worker/include/vw_worker_config.h`

**Why change**: Add `vw_worker_backend_t` enum (`VW_WORKER_BACKEND_AUTO = 0`, `VW_WORKER_BACKEND_GPU`, `VW_WORKER_BACKEND_CPU`) and `gpu_device` field to `vw_worker_config_t`.

**Responsibility before**: Defined worker configuration struct for model path, pipe name, log file, token.  
**After**: Includes `backend` and `gpu_device` fields.

**Callers**: `worker/src/vw_worker_config.c`, `worker/src/vw_worker.c`, `tests/unit/test_worker_config.c`.  
**Callees**: None.

**Happy path**: Config struct holds parsed backend choice and GPU device index.

**Failure path**: N/A.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | Enum type safety |
| **Authorization** | N/A |
| **Concurrency** | Config struct passed by pointer |
| **I/O** | N/A |
| **Persistence** | N/A |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Add `vw_worker_backend_t` enum | `worker/include/vw_worker_config.h:L12-L16` | `test_worker_config` | ✅ done |
| 2 | Add `backend` and `gpu_device` config fields | `worker/include/vw_worker_config.h:L25-L26` | `test_worker_config` | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.29 `worker/src/vw_whisper_engine.c`

**Why change**: Configure whisper.cpp context parameters `cparams.use_gpu` and `cparams.gpu_device` according to requested backend, and log startup backend information.

**Responsibility before**: Created default context with CPU inference.  
**After**: Sets `cparams.use_gpu = (backend != VW_WORKER_BACKEND_CPU)` and `cparams.gpu_device = (gpu_device >= 0) ? gpu_device : 0`, and logs `WORKER_ENGINE` with effective backend.

**Callers**: `worker/src/vw_worker.c:L183`.  
**Callees**: `whisper_context_default_params`, `whisper_init_from_file_with_params`, `vw_log_event`.

**Happy path**:
1. `backend == VW_WORKER_BACKEND_AUTO` or `GPU`: sets `cparams.use_gpu = true`, `whisper.cpp` selects first GPU/IGPU device, logs `"inference backend: gpu (auto CPU fallback if no device)"`, returns valid engine context.
2. `backend == VW_WORKER_BACKEND_CPU`: sets `cparams.use_gpu = false`, logs `"inference backend: cpu"`, returns valid engine context.

**Failure path**:
1. Model file missing/invalid: `whisper_init_from_file_with_params` returns NULL; returns NULL gracefully.
2. GPU unavailable at runtime: `whisper.cpp` internal `whisper_backend_init_gpu` logs `"no GPU found"` and falls back to CPU backend, returning a valid context.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | Null check on `model_path`; clamps negative `gpu_device` to 0 |
| **Authorization** | N/A |
| **Concurrency** | Single engine instance created on worker main thread |
| **I/O** | Model file read |
| **Persistence** | None |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Set `cparams.use_gpu` and `cparams.gpu_device` | `worker/src/vw_whisper_engine.c:L14-L15` | `test_whisper_engine` | ✅ done |
| 2 | Log effective backend startup message | `worker/src/vw_whisper_engine.c:L22-L23` | Log inspection | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.30 `worker/src/vw_worker.c`

**Why change**: Pass `config->backend` and `config->gpu_device` to `vw_whisper_engine_init`, drain segment builder on `START_SESSION` to discard pre-seek hypotheses, clear audio buffer on `STOP`, and implement thread-safe `vw_worker_stop_reason_name` with `_Thread_local static char buf[16]`.

**Responsibility before**: Single session CPU worker loop.  
**After**: Multi-session worker loop supporting GPU backend initialization, seek epoch restarts, and thread-safe reason logging.

**Callers**: `worker/src/main.c` (`main()`), integration tests.  
**Callees**: `vw_whisper_engine_init`, `vw_worker_stop_reason_name`, `vw_audio_buffer_clear`, `vw_segment_builder_pop`, `vw_ipc_send`.

**Happy path**:
1. Startup: Initializes engine with `config->backend` and `config->gpu_device` (`:L183`).
2. Multi-session: On `START_SESSION`, drains pre-seek hypotheses from builder (`:L323-L327`), adopts new `session_id`, replies `STARTED`. On `STOP_SESSION`, clears `audio_buf` (`:L404`), logs reason via `vw_worker_stop_reason_name`.

**Failure path**:
1. Invalid model on start: Rejects start with `E_MODEL_MISSING`.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | Checks `session_id` match on incoming audio |
| **Authorization** | Constant-time auth token comparison |
| **Concurrency** | Single-writer main loop; `vw_worker_stop_reason_name` uses `_Thread_local static char buf[16]` |
| **I/O** | Timed IPC socket operations |
| **Persistence** | None |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Pass backend config to engine init | `worker/src/vw_worker.c:L183` | `test_worker_lifecycle` | ✅ done |
| 2 | Drain segment builder on `START_SESSION` | `worker/src/vw_worker.c:L323-L327` | `test_worker_lifecycle` | ✅ done |
| 3 | Thread-safe `vw_worker_stop_reason_name` | `worker/src/vw_worker.c:L41-L56` | `test_worker_ipc` | ✅ done |

**Assumptions/Tradeoffs**: None.

---

### 1.31 `worker/src/vw_worker_config.c`

**Why change**: Parse `--backend <auto|gpu|cpu>` and `--gpu-device <id>` CLI arguments and set defaults.

**Responsibility before**: Parsed `--pipe`, `--token`, `--model`, `--log-file`.  
**After**: Parses `--backend` and `--gpu-device`, initializing defaults to `VW_WORKER_BACKEND_AUTO` and `gpu_device = 0`.

**Callers**: `worker/src/main.c`, `tests/unit/test_worker_config.c`.  
**Callees**: `strcmp`, `strtol`, `fprintf`.

**Happy path**:
1. No flags: `config->backend = VW_WORKER_BACKEND_AUTO`, `config->gpu_device = 0`.
2. `--backend gpu --gpu-device 1`: sets `backend = VW_WORKER_BACKEND_GPU`, `gpu_device = 1`.

**Failure path**:
1. Unknown backend (e.g. `--backend cuda`): prints error, returns `2`.
2. Invalid device ID (e.g. `--gpu-device -1` or `abc`): prints error, returns `2`.
3. Dangling flag with missing argument: prints error, returns `2`.

**Boundaries**:

| Boundary type | What to check |
| --- | --- |
| **Input validation** | Strict string match on `auto|gpu|cpu`; `strtol` numeric check and range check `[0..65535]` |
| **Authorization** | N/A |
| **Concurrency** | Single-threaded argument parsing |
| **I/O** | Error output to stderr |
| **Persistence** | None |

**Acceptance map**:

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Set backend and device defaults | `worker/src/vw_worker_config.c:L42-L43` | `test_worker_config` | ✅ done |
| 2 | Parse `--backend auto|gpu|cpu` | `worker/src/vw_worker_config.c:L63-L74` | `test_worker_config` | ✅ done |
| 3 | Parse `--gpu-device <id>` with bounds check | `worker/src/vw_worker_config.c:L75-L82` | `test_worker_config` | ✅ done |

**Assumptions/Tradeoffs**: None.

---

## 2. Happy-Path Request Trace

The following trace outlines worker startup, Vulkan GPU initialization, audio streaming, seek restart, and clean shutdown:

```text
1. Worker Process Launch & Argument Parsing
   └─ worker/src/main.c:main
      ├─ Initializes defaults: worker/src/vw_worker_config.c:vw_worker_config_init_defaults
      │  └─ backend = VW_WORKER_BACKEND_AUTO, gpu_device = 0
      ├─ Parses CLI flags: worker/src/vw_worker_config.c:vw_worker_config_parse_args
      │  └─ Extracts --pipe, --token, --model, and optional --backend/--gpu-device
      └─ Launches worker event loop: worker/src/vw_worker.c:vw_worker_run (L183)

2. Whisper Engine & Vulkan GPU Initialization
   └─ worker/src/vw_whisper_engine.c:vw_whisper_engine_init (L14)
      ├─ Sets cparams.use_gpu = true (since backend != CPU)
      ├─ Sets cparams.gpu_device = 0
      ├─ Invokes whisper.cpp: whisper_init_from_file_with_params(model_path, cparams)
      │  ├─ whisper.cpp queries Vulkan runtime (ggml-vulkan backend)
      │  ├─ Selects first enumerated GPU/IGPU device and compiles compute pipelines
      │  └─ Loads GGML model weights into GPU VRAM
      ├─ Logs startup info: WORKER_ENGINE: "inference backend: gpu (auto CPU fallback if no device)"
      └─ Spawns background IPC reader thread (vw_worker_reader_main)

3. Handshake & Session Start
   └─ Plugin connects over authenticated IPC pipe and exchanges HELLO / HELLO_ACK
   └─ Plugin sends START_SESSION with session_id and timeline_origin_pts_us
   └─ Worker receives START_SESSION:
      ├─ Drains stale hypotheses from segment builder: vw_segment_builder_pop (L323)
      ├─ Copies session_id and sets session_active = true
      └─ Transmits STARTED reply header over IPC

4. Real-Time Audio Streaming & GPU Inference
   └─ VLC filter callback captures 16kHz Mono S16LE PCM chunks into SPSC queue
   └─ Plugin sender thread transmits VW_MSG_AUDIO_PCM frames over IPC
   └─ Worker reader thread pushes audio frames into worker queue (vw_worker_queue_push)
   └─ Worker main loop accumulates audio into audio_buf:
      ├─ Evaluates VAD energy window
      ├─ Executes GPU whisper inference: vw_whisper_engine_transcribe (L247)
      ├─ Builds caption segment: vw_segment_builder_add_hypothesis
      └─ Emits VW_MSG_CAPTION_SEGMENT frame over IPC to VLC plugin for OSD rendering

5. Seek Discontinuity Epoch Restart
   └─ User seeks in VLC -> callback/poll sets discontinuity_pending = true
   └─ Plugin sender thread:
      ├─ Erases active OSD: vw_caption_presenter_blank (L320)
      ├─ Sends STOP_SESSION (reason=SEEK_DISCONTINUITY) over IPC (L321)
      ├─ Drains stale pre-seek PCM from SPSC queue (L323)
      └─ Sends START_SESSION with new session_id and resume_pts_us
   └─ Worker receives STOP_SESSION:
      ├─ Clears audio buffer: vw_audio_buffer_clear (L404)
      └─ Logs: "WORKER_SESSION: session stopped (reason=SEEK_DISCONTINUITY)"
   └─ Worker receives START_SESSION:
      ├─ Drains builder pre-seek hypotheses (L323-L327)
      ├─ Adopts new session_id and sets session_active = true
      └─ Resumes GPU transcription for new media epoch

6. Clean Teardown & Exit
   └─ VLC stops -> plugin sends SHUTDOWN frame over IPC
   └─ Worker reader receives SHUTDOWN, pushes to queue
   └─ Worker main loop pops SHUTDOWN, breaks execution loop
   └─ Frees engine (VRAM deallocated), unlinks socket, exits 0
```

---

## 3. Most Important Failure Path

### Failure Scenario: Worker Launches on Machine Lacking Physical Vulkan GPU or Driver

```text
1. Worker Process Launch
   └─ Worker starts with default configuration (--backend auto)
   └─ worker/src/vw_whisper_engine.c:vw_whisper_engine_init (L14)
      ├─ cparams.use_gpu is set to true
      └─ Calls whisper_init_from_file_with_params(model_path, cparams)

2. Whisper.cpp GPU Probing & Transparent CPU Fallback
   └─ whisper.cpp:whisper_backend_init_gpu
      ├─ Calls ggml_backend_vk_init(gpu_device)
      ├─ Vulkan physical device enumeration returns 0 devices / driver missing
      ├─ whisper.cpp logs: "no GPU found" (INFO)
      ├─ whisper_backend_init_gpu returns NULL
      └─ whisper.cpp falls back to whisper_backend_init(CPU)
         └─ Appends standard CPU backend to context backend registry
         └─ Loads GGML model weights into system RAM

3. Worker Engine Initialization Success
   └─ whisper_init_from_file_with_params returns a VALID whisper_context pointer
   └─ vw_whisper_engine_init logs:
      "WORKER_ENGINE: inference backend: gpu (auto CPU fallback if no device)"
   └─ Engine successfully loaded and ready for transcription

4. Normal Session Execution via CPU
   └─ Worker receives START_SESSION and AUDIO_PCM frames
   └─ Transcribes audio using CPU thread pool
   └─ Emits CAPTION_SEGMENT frames to VLC plugin
   └─ End-user receives real-time subtitles with zero crashes or connection failures
```

---

## 4. Boundary Summary

| Boundary type | Checks performed | Code Location | Finding / Guard Implementation |
|---|---|---|---|
| **Input validation** | CLI `--backend` validation | `vw_worker_config.c:L63-L74` | Rejects unknown backend strings with clear error and exit code 2. |
| **Input validation** | CLI `--gpu-device` validation | `vw_worker_config.c:L75-L82` | Uses `strtol` to ensure non-negative integer and bounds check `[0..65535]`. |
| **Input validation** | Engine Init Device Clamping | `vw_whisper_engine.c:L15` | Clamps negative `gpu_device` parameter values to 0. |
| **Input validation** | Audio Callback PTS Fallback | `vw_whisper_module.c:L481` | Guards `p_block->i_pts >= VLC_TS_0` and `last_pts_us > 0` before checking `VW_PTS_JUMP_THRESHOLD_US` (500ms). |
| **Input validation** | Position Jump Gates | `vw_whisper_module.c:L276, L295` | `position_us >= 0` guards and `VW_SEEK_JUMP_THRESHOLD_US` (1s) macro gate on continuous and paused checks. |
| **Input validation** | Session ID Gating | `vw_worker.c:L343`<br>`vw_whisper_module.c:L357` | `session_id` memcmp gating drops stale pre-seek `AUDIO` frames and in-flight `SEGMENT` frames. |
| **Authorization** | Auth Token Verification | `vw_worker.c:L30` | Secret 32-byte auth token validated using `verify_token_constant_time`. |
| **Concurrency** | Lock-Free Callback | `vw_whisper_module.c:L475` | Callback is 100% lock-free (Rule 4), writing only atomic bool/int64 variables. |
| **Concurrency** | Thread-Safe Stop Reason Logger | `worker/src/vw_worker.c:L51` | `vw_worker_stop_reason_name` uses `_Thread_local static char buf[16]` to guarantee thread safety. |
| **I/O** | CMake Cross Find Path Isolation | `worker/CMakeLists.txt:L31-L35` | Sets `Vulkan_INCLUDE_DIR` to MinGW target path when `$ENV{VW_VULKAN_SDK}` is defined, preventing host `/usr/include` header pollution. |
| **I/O** | OSD Channel Flush | `vw_caption_presenter.c:L125` | `vw_caption_presenter_blank` uses `vout_FlushSubpictureChannel` and 1ms OSD blanking to erase active subpictures mid-session. |
| **Persistence** | Socket & File Cleanup | `vw_whisper_module.c:L411`<br>`vw_worker.c:L440` | Socket files and pipe handles unlinked and closed on teardown. Zero transcript/PCM data written to disk (Rule 5). |

---

## 5. Acceptance Criterion → Code Mapping

| # | Criterion | Code Implementation | Test Assertion | Status |
|---|---|---|---|---|
| 1 | Default build on Vulkan host produces GPU worker | `worker/CMakeLists.txt:L23` (`VW_WITH_VULKAN=ON`) | `test_whisper_engine` + PE header inspection (`vulkan-1.dll`) | ✅ done |
| 2 | `--backend cpu` produces CPU-only worker | `worker/src/vw_worker_config.c:L70`<br>`worker/src/vw_whisper_engine.c:L14` | `tests/unit/test_worker_config.c:L71` | ✅ done |
| 3 | `--backend gpu` / `auto` transparent fallback to CPU | `worker/src/vw_whisper_engine.c:L14` | `whisper.cpp` built-in fallback + `test_whisper_engine` | ✅ done |
| 4 | `--gpu-device <id>` selects GPU device | `worker/src/vw_worker_config.c:L75`<br>`worker/src/vw_whisper_engine.c:L15` | `tests/unit/test_worker_config.c:L65` | ✅ done |
| 5 | Host without Vulkan SDK builds CPU-only with warning | `worker/CMakeLists.txt:L42` | CMake configure without SDK | ✅ done |
| 6 | Explicit CPU presets provided | `CMakePresets.json:L21, L46, L70` | CMake configure `*-cpu` presets | ✅ done |
| 7 | Full reproducible README instructions | `README.md:L7-L144` | Manual documentation review | ✅ done |
| 8 | Seek during playback: OSD clears immediately | `plugin/src/vw_caption_presenter.c:L125`<br>`plugin/src/vw_whisper_module.c:L320` | `tests/unit/test_caption_presenter.c:L90` | ✅ done |
| 9 | Pre-seek hypotheses discarded on `START_SESSION` | `worker/src/vw_worker.c:L323-L327` | `tests/integration/test_worker_lifecycle.c:L218` | ✅ done |
| 10 | Pre-seek audio discarded from SPSC queue | `plugin/src/vw_whisper_module.c:L323-L324` | `tests/integration/test_worker_lifecycle.c:L223` | ✅ done |
| 11 | C17, Google C style, no project C++ | All `.c`/`.h` files | `clang-format --dry-run --Werror` | ✅ done |
| 12 | 100% CTest pass rate (16/16 targets) | CMake & CTest suite | All 16 targets passing | ✅ done |
| 13 | Zero memory leaks under Valgrind | `ctest -T memcheck` | Valgrind memcheck clean | ✅ done |

---

## 7. Code Review Findings (Bugs, Risks, Nitpicks)

### Bugs (Sorted by Priority)

| Priority | Component / Location | Description | Impact | Proposed Fix / Status |
|---|---|---|---|---|
| **Medium** | `worker/CMakeLists.txt:34` | When cross-compiling with `VW_VULKAN_SDK`, `find_package(Vulkan)` can pick up host `/usr/include` from host `glslc`, causing header conflicts with MinGW CRT. | MinGW cross-build failure on `ggml-vulkan.cpp` | Explicitly set `Vulkan_INCLUDE_DIR` to `$ENV{VW_VULKAN_SDK}/mingw/include` in CMake when `VW_VULKAN_SDK` is defined. (**RESOLVED**). |
| **Low** | `worker/src/vw_whisper_engine.c:22` | `vw_log_event(cparams.use_gpu ? VW_LOG_LEVEL_INFO : VW_LOG_LEVEL_INFO, ...)` — redundant ternary over identical values. | Code hygiene / potential dead code warning | Collapsed to single `VW_LOG_LEVEL_INFO`. (**RESOLVED**). |
| **Low** | `CMakePresets.json` testPresets | Three `*-cpu` configurePresets existed but were absent from `testPresets`. | `ctest --preset linux-x64-debug-cpu` failed with unknown preset | Added `linux-x64-debug-cpu`, `windows-x64-release-cpu`, and `windows-x64-debug-cpu` to `testPresets`. (**RESOLVED**). |
| **Low** | `worker/include/vw_whisper_engine.h:8` | Header included `vw_worker_config.h` solely for `vw_worker_backend_t`. | Unnecessary coupling between engine abstraction and worker config | Moved `vw_worker_backend_t` to `vw_whisper_engine.h`; `vw_worker_config.h` includes `vw_whisper_engine.h`. (**RESOLVED**). |
| **Low** | `worker/src/vw_worker_config.c:51-86` | Options with missing values fell through to `unknown option` error message. | Misleading CLI error reporting | Added explicit `missing value for <option>` checks for all flags; added unit tests in `test_worker_config.c`. (**RESOLVED**). |
| **Low** | `worker/src/vw_whisper_engine.c:15` | Negative `gpu_device` values clamped to 0 without documentation. | Hides caller expectations | Added clarifying documentation comment for direct API callers. (**RESOLVED**). |
| **Low** | `worker/src/vw_worker_config.c:78` | `strtol` return value check allowed values up to `LONG_MAX` before casting to `int`. | Possible integer overflow on out-of-range device numbers | Added explicit range check `id >= 0 && id <= 65535` before casting to `int`. (**RESOLVED**). |

### Architectural & Operational Risks

| Category | Risk Description | Affected Files | Mitigation Strategy |
|---|---|---|---|
| **Resource Consumption** | Compiling Vulkan compute shaders (`glslc` + `vulkan-shaders-gen`) creates significant memory spikes during parallel builds (`ninja -j`), which can trigger OOM kills on hosts with <= 8 GB RAM. | `worker/CMakeLists.txt`, `README.md` | Documented build memory limits in `README.md` and `step17a_plan.md`, recommending `-j1` or `-j2` for GPU builds. Added explicit `*-cpu` presets for low-memory environments. |
| **Runtime Dependency** | The Windows GPU worker (`vlc-whisper-worker.exe`) imports `vulkan-1.dll` at load time. On legacy Windows installations lacking Vulkan drivers, process loader fails before main. | `worker/CMakeLists.txt`, `README.md` | Documented runtime dependency; provided separate standalone `vlc-whisper-worker-cpu.exe` binary via `windows-x64-release-cpu` preset for loader-less machines. |

### Code Style & Quality Nitpicks

| Issue Type | File & Line | Description | Recommendation |
|---|---|---|---|
| **Tautological Ternary** | `worker/src/vw_whisper_engine.c:22` | Redundant `? VW_LOG_LEVEL_INFO : VW_LOG_LEVEL_INFO`. | Fixed by removing ternary branch. |
| **Header Coupling** | `worker/include/vw_whisper_engine.h:8` | Whisper header coupled to `vw_worker_config.h`. | Fixed by moving enum definition to engine header. |
| **CLI Dangling-Value UX** | `worker/src/vw_worker_config.c:51-86` | Option with no value misreported as `unknown option`. | Fixed with dedicated `missing value for <flag>` guards. |
| **Silent gpu_device Clamp** | `worker/src/vw_whisper_engine.c:15` | Negatives silently remapped to 0. | Documented inline behavior. |
| **Missing *-cpu testPresets** | `CMakePresets.json:133-149` | `*-cpu` builds had no `ctest --preset` entry. | Added test presets for all CPU configurations. |
| **Naming Consistency** | `worker/include/vw_whisper_engine.h:11` | `vw_worker_backend_t` enum uses prefix `VW_WORKER_BACKEND_` matching project naming conventions. | Retained `vw_` symbol namespacing. |
