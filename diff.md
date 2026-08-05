# Diff Analysis: Milestone 3 → Step 13 (Worker Client Handshake & Protocol Codec Extensions)

**35 files changed, +1215 / -554 lines**
**Base**: `gemini/milestone-3` @ `5fbce99` → `HEAD` (`gemini/milestone-3-step-13`, 9 commits)

Scope: VLC plugin worker-client connection during module `Open` (roadmap step 13), `HELLO`/`HELLO_ACK` authentication handshake, cross-platform process spawning (`vw_platform_linux.c` new), protocol message-builder groundwork, HELLO_ACK reply in the worker, new `test_platform` suite, expanded lifecycle failure-path tests, and the Milestone 3 postmortem/blueprint docs. Plan: `docs/plans/step13_plan.md`; postmortem: `docs/plans/milestone3_postmortem.md`.

---

## 1. File-by-File Analysis

### 1.1 `.agents/AGENTS.md` and `AGENTS.md`

**Why change**: Add Rule 14 — mandatory documentation updates in the same change, mapping each change type to its doc (`architecture.md`, `decisions.md`, `source-layout.md`, `api-contracts.md`, `whisper-api.md`, `test-strategy.md`, `roadmap.md`).

**Responsibility before**: 13 rules. **After**: 14 rules (doc-sync enforcement for agents).

**Callers**: AI agents. **Callees**: none.

**Happy path**: Rule 14 drives the doc updates present elsewhere in this diff (source-layout, architecture, test-strategy, roadmap). **Failure path**: N/A (policy).

**Boundaries**: N/A. **Acceptance map**: Rule 14 present in both files. Status: done.

**Assumptions/Tradeoffs**: Duplication between the two AGENTS.md files remains a sync risk (they already diverge in whitespace). Notably, Rule 14 is itself violated by this branch (see §7, Risk R-2).

---

### 1.2 `.gitignore`

**Why change**: Ignore `.commandcode/` (agent scratch dir).

**Responsibility before**: Ignored build artifacts, secrets, editor configs. **After**: Same, plus `.commandcode/`.

**Callers**: git. **Callees**: none. **Happy path**: Agent scratch stays untracked. **Failure path**: N/A.

**Boundaries**: Pattern specificity. **Acceptance map**: `.gitignore:196`. Status: done.

---

### 1.3 `CMakeLists.txt`

**Why change**: Set `MEMORYCHECK_COMMAND_OPTIONS "--leak-check=full --show-leak-kinds=all"` before `include(CTest)` so memcheck runs with full leak checking.

**Responsibility before**: Build orchestration + test registration. **After**: Same, plus stricter memcheck defaults.

**Callers**: CTest. **Callees**: none. **Happy path**: `ctest -T memcheck` reports all leak kinds. **Failure path**: N/A.

**Boundaries**: Option string only. **Acceptance map**: `CMakeLists.txt:40`. Status: done — full memcheck is 11/11 clean (verified this session).

---

### 1.4 `ai/project-context.md`

**Why change**: "capability tokens" → "authentication tokens" in the privacy invariant list.

**Responsibility before**: Project context for agents. **After**: Wording aligned with the actual 32-byte auth-token model. Status: done.

---

### 1.5 `cmake/valgrind.supp`

**Why change**: New **empty** Valgrind suppression file (0 bytes, 0 entries).

**Responsibility before**: N/A. **After**: Placeholder for future suppressions.

**Boundaries**: Empty file is inert — no suppressions registered anywhere; not wired into any CTest/MemCheck config. **Acceptance map**: none. Status: ⚠️ partial (dead placeholder; the memcheck gate in `docs/issues.md` #1 is already unblocked, so a suppression is unnecessary for `test_platform`).

---

### 1.6 `diff.md` (this file)

**Why change**: Superseded by this review. The previous `diff.md` (in-tree before this rewrite) was a "Milestone 3 Step 13" review against base `gemini/milestone-2`; this review re-bases the same feature work against `gemini/milestone-3` per the requested base.

**Responsibility before**: Milestone-2/Step-13 partial review. **After**: Full branch review vs `gemini/milestone-3`.

**Callers**: Humans. **Callees**: none. **Acceptance map**: replaced. Status: done.

---

### 1.7 `docs/architecture.md`

**Why change**: Add worker IPC reader thread (ADR-013) and Model-Once engine (ADR-015) to the component diagram and ownership table; drop "session ID" from the `HELLO`/`HELLO_ACK` required payload; wording "capability token" → "authentication token".

**Responsibility before**: System architecture, timing, IPC specs. **After**: Architecture reflecting the Step 13 handshake and the postmortem ADRs.

**Callers**: All implementers. **Callees**: none.

**Happy path**: Reader derives the worker-side threading split (reader thread vs engine) and the HELLO token contract. **Failure path**: N/A.

**Boundaries**: N/A. **Acceptance map**: ADR-013/015 in diagram (`:19-24`), ownership table (`:27-35`), HELLO payload (`:97`). Status: done.

**Assumptions/Tradeoffs**: ADR-013/015 describe _future_ worker architecture (Step 14+); the current `vw_worker.c` still runs a single-threaded loop and `main.c` is a stub — the architecture doc is ahead of the code (documented as blueprint in `milestone3_postmortem.md`).

---

### 1.8 `docs/decisions.md`

**Why change**: Add ADR-013 (decoupled worker IPC reader thread), ADR-014 (process-wide Media Foundation management), ADR-015 (Model-Once worker lifetime).

**Responsibility before**: ADRs 1-12. **After**: ADRs 1-15.

**Callers**: Planning. **Callees**: none. **Happy path**: ADRs anchor the postmortem's Phase A/D re-implementation. **Failure path**: N/A.

**Acceptance map**: ADR-013 `:98-113`, ADR-014 `:114-127`, ADR-015 `:128-140`. Status: done.

**Assumptions/Tradeoffs**: All three ADRs describe behavior not yet present in the Step 13 baseline (forward-looking records for the archived feature branches). Not wrong, but they document code that does not exist in this tree.

---

### 1.9 `docs/issues.md` (new)

**Why change**: Track known issues — Issue #1: `test_platform` failing under Valgrind memcheck; Issue #2: cosmetic warnings in `test_worker_lifecycle.c`.

**Responsibility before**: N/A. **After**: Known-issues register.

**Callers**: Developers/CI. **Callees**: none.

**Happy path**: Issues link symptom → root cause → fix path. **Failure path**: Issue #1's content is **stale and factually wrong for this tree** (see §7, Bug M-2): `test_platform` passes under memcheck 11/11 with the current build (verified this session with both direct `valgrind` and `ctest -T memcheck`). The suggested fix it describes — "validate executable existence before spawning" — is **already implemented** (`vw_platform_linux.c:33`, the `access()` pre-check), which is exactly why the failure no longer reproduces. It also calls the failure "pre-existing" although `test_platform` is a new file in this branch.

**Boundaries**: N/A. **Acceptance map**: Issue #1 `:3-18`, Issue #2 `:20-22`. Status: ⚠️ stale.

---

### 1.10 `docs/plans/milestone3_postmortem.md` (new, 313 lines)

**Why change**: Postmortem and technical handoff for the three archived feature branches (`milestone-3-steps-14-15`, `gpu-directml`, `transcription-lookahead`), including root causes of the reset and a 4-phase re-implementation blueprint (A: real-time streaming/OSD; B: GPU Vulkan; C: SPU subpicture; D: look-ahead decode + seek engine).

**Responsibility before**: N/A. **After**: Authoritative handoff/blueprint doc referenced from `docs/roadmap.md`.

**Callers**: Future implementers. **Callees**: none.

**Happy path**: Reader extracts the five critical bug fixes (empty subpicture `subpicture_New(NULL)`, MinGW weak-symbol resolution, session-ID zero-stamping, model-reload latency/ADR-015, MF lifecycle races) and the phased roadmap. **Failure path**: N/A.

**Boundaries**: N/A. **Acceptance map**: postmortem checklist `:299-306` (all `[x]`); 4-phase diagram `:245-266`. Status: done, with one accuracy nit — it claims "13/13 tests passing on `gemini/milestone-3` baseline", but the milestone-3 baseline + this branch has 11 tests (10 baseline + `test_platform`). This means that new tests have been added on the way.

---

### 1.11 `docs/plans/milestone_2_review_fixes_plan.md` (deleted)

**Why change**: Superseded — its 5 fix items (Rule-4 callback decoupling, deprecated `vlc_object_find_name`, OSD clear, resampler drift, Rule 11 comments) belong to Milestone 2 and are closed.

**Responsibility before**: Milestone 2 review-fix plan. **After**: Deleted.

**Callers**: N/A. **Acceptance map**: content no longer tracked. Status: done (verify the five fixes were actually merged earlier; not part of this diff).

---

### 1.12 `docs/plans/step13_plan.md` (new)

**Why change**: Task plan for Step 13: connect IPC client during `vw_plugin_open`, generate 32-byte token, spawn worker, handshake, graceful passthrough fallback.

**Responsibility before**: N/A. **After**: Step 13 plan with acceptance criteria and DoD.

**Callers**: Agents implementing Step 13. **Callees**: none.

**Happy path**: Plan drives implementation. **Failure path**: **All acceptance criteria and DoD checkboxes remain `[ ]` unchecked in the committed file while the code is committed** (`step13_plan.md:36-43`), and the goal ("establishing the authenticated HELLO/HELLO_ACK handshake upon module load") is **not actually achievable end-to-end** (see §7, Bug H-1).

**Boundaries**: N/A. **Acceptance map**: criteria `:36-42`, DoD `:44-46`. Status: ⚠️ unchecked + partial fulfillment.

---

### 1.13 `docs/roadmap.md`

**Why change**: Milestone 3 → "In Progress"; add postmortem note; add roadmap items 17a (GPU Vulkan), 17b (SPU subpicture), 17c (look-ahead decode), 17d (seek re-sync); renumber 23/24 (release docs → 23, benchmark suite → 24).

**Responsibility before**: Milestone 3 "Planned". **After**: "In Progress" with the postmortem blueprint wired in.

**Callers**: Planning. **Callees**: none.

**Happy path**: Roadmap reflects the new phased plan. **Failure path**: **Step 13 checkbox is still `[ ]`** (`roadmap.md:41`) even though this branch implements it — roadmap not marked complete for the very step it is named after. Minor consistency gap with the plan.

**Acceptance map**: `roadmap.md:38-55`. Status: partial (17a-d added; step 13 unchecked).

---

### 1.14 `docs/source-layout.md`

**Why change**: Update tree/table for `vw_platform_linux.c` (new), `vw_worker_client.c` (renamed role: launcher + handshake), `test_platform.c`, `test_audio_capture.c`/`test_caption_presenter.c` (added to tree), and re-format tables (Rule 14 compliance).

**Responsibility before**: Source layout spec. **After**: Updated layout reflecting this branch.

**Callers**: Developers/agents. **Callees**: none. **Happy path**: Tree matches disk. **Failure path**: N/A.

**Acceptance map**: `source-layout.md:36-45` (plugin), `:92-99` (tests), `:129-137` (plugin files table). Status: done.

---

### 1.15 `docs/test-strategy.md`

**Why change**: Add "Automated failure-path coverage" section listing `test_platform` and `test_worker_lifecycle` failure cases; table reformat.

**Responsibility before**: Test gates. **After**: Same, plus failure-path coverage documentation.

**Callers**: Test planning. **Callees**: none. **Happy path**: Strategy matches new tests. **Failure path**: N/A.

**Acceptance map**: `test-strategy.md:43-48`. Status: done — the listed "wrong-token → worker exits 1", "first-frame-not-HELLO → worker exits 1", "client NULL-arg validation", and "connect failure with no listener" cases are all actually present in `test_worker_lifecycle.c`.

---

### 1.16 `plugin/CMakeLists.txt`

**Why change**: Add `src/vw_platform_linux.c` to the plugin sources; link `bcrypt` on Windows (for `BCryptGenRandom`).

**Responsibility before**: Plugin shared lib. **After**: Same, plus Linux platform impl + Win32 bcrypt dep.

**Callers**: CMake. **Callees**: none. **Happy path**: `vw_platform_*` symbols resolve on both OSes. **Failure path**: Windows build requires bcrypt (added correctly). **Acceptance map**: `plugin/CMakeLists.txt:6-9`, `:39`. Status: done.

---

### 1.17 `plugin/include/vw_platform.h`

**Why change**: Declare `vw_platform_spawn_process` (cross-platform process spawning).

**Responsibility before**: CSPRNG + time declarations. **After**: Adds spawn contract (argv NULL-terminated; argv[0] = program name, not passed as arg).

**Callers**: `vw_worker_client.c`, `test_platform.c`. **Callees**: none (declaration).

**Happy path**: Caller spawns worker with `{path, NULL}`. **Failure path**: false on NULL/`access()` failure (Linux). **Acceptance map**: `vw_platform.h:16-19`. Status: done — but the Linux implementation of the sibling `vw_platform_get_random_bytes` violates the header's "cryptographically secure" intent (see §7, Bug H-2).

---

### 1.18 `plugin/include/vw_worker_client.h`

**Why change**: Rename `token` → `auth_token`, size `VW_AUTH_TOKEN_BYTES`; add `#include "vw_protocol_types.h"`; pointer-spacing style fix.

**Responsibility before**: Minimal client stub. **After**: Typed handshake contract.

**Callers**: `vlc_whisper_module.c`, tests. **Callees**: none. **Acceptance map**: `vw_worker_client.h:8-17`. Status: done.

---

### 1.19 `plugin/src/vlc_whisper_module.c`

**Why change**: Step 13 wiring — generate auth token, build OS-specific pipe name/worker path, launch & connect the worker client in `vw_plugin_open`, disconnect in `vw_plugin_close`, log `PLUGIN_WORKER_UNAVAILABLE` on failure (passthrough preserved).

**Responsibility before**: Module open/close + audio filter passthrough. **After**: Also owns worker-client lifecycle.

**Callers**: VLC pipeline. **Callees**: `vw_platform_get_random_bytes`, `vw_worker_client_launch_and_connect`, `vw_worker_client_disconnect`, `vw_log_event`.

**Happy path**: `vw_plugin_open` (`:91`) → pipe name `/tmp/vlc-whisper-<pid>.sock` (Linux) / `\\.\pipe\vlc-whisper-<pid>` (Win32) + `worker_path` (`:123-131`) → RNG (`:128`) → launch+connect (`:130`) → `sys->client` set → `PLUGIN_OPEN` (`:140`). `vw_plugin_close` (`:146`) → disconnect (`:153-157`).

**Failure path**: RNG fail → `PLUGIN_RNG_FAIL` warn, no client; launch/connect fail → `PLUGIN_WORKER_UNAVAILABLE` warn (`:133-135`), `sys->client == NULL`, filter still passthrough, `VLC_SUCCESS` returned. Callback (`vw_plugin_filter`, `:47-87`) unchanged — zero lock/alloc/IPC (Rule 4 intact).

**Boundaries**:

- Input validation: none needed beyond sys/block null guards (unchanged).
- Authorization: token generated per-instance; **never transmitted to the worker** (see §7, Bug H-1).
- Concurrency: spawn+handshake run in `vw_plugin_open` (module-activation thread, not the audio callback) — blocks module open up to ~2 s when no worker responds.
- I/O: up to 2 s connect retry, then 3 s read timeouts.
- Persistence: `/tmp/vlc-whisper-<pid>.sock` — no cleanup path in this diff; stale socket file if the worker dies.

**Acceptance map**:

| #   | Criterion                                            | Code                           | Test                      | Status                       |
| --- | ---------------------------------------------------- | ------------------------------ | ------------------------- | ---------------------------- |
| 1   | Generate 32-byte token + OS endpoint                 | `vlc_whisper_module.c:123-131` | `test_platform.c:18-27`   | ✅ (token strength: see H-2) |
| 2   | Invoke `vw_worker_client_launch_and_connect` in open | `vlc_whisper_module.c:130`     | `test_worker_ipc.c:35`    | ✅                           |
| 3   | Clean up in close                                    | `vlc_whisper_module.c:153-157` | `test_worker_lifecycle.c` | ✅                           |
| 4   | Missing worker → warn + passthrough                  | `vlc_whisper_module.c:133-135` | manual                    | ✅                           |
| 5   | Callback stays lock-free                             | `vlc_whisper_module.c:47-87`   | invariant                 | ✅                           |

**Assumptions/Tradeoffs**: `worker_path` is a bare relative name (`"vlc-whisper-worker[.exe]"`) resolved against VLC's CWD — no plugin-dir discovery, no config override. `#include <stdio.h>/<stdlib.h>/<string.h>` + platform headers inserted mid-file (`:36-47`) instead of at the top (style).

---

### 1.20 `plugin/src/vw_platform_linux.c` (new)

**Why change**: POSIX platform implementation — `vw_platform_get_random_bytes`, `vw_platform_get_time_us`, `vw_platform_spawn_process`.

**Responsibility before**: N/A (milestone-3 had only `vw_platform_win32.c` with a `#else` rand() fallback). **After**: Real Linux implementation.

**Callers**: `vw_worker_client.c`, `test_platform.c`. **Callees**: `srand/rand`, `time`, `getpid`, `access`, `posix_spawn`.

**Happy path**: `vw_platform_spawn_process("/bin/true", argv)` → `access` OK → `posix_spawn` → true. **Failure path**: NULL args or `access() != 0` → false (deterministic, Valgrind-safe).

**Boundaries**:

- Input validation: NULL/zero-size rejected for RNG; NULL path/argv and missing executable rejected for spawn.
- Authorization: `vw_platform_get_random_bytes` uses **`rand()` seeded once with `time ^ pid` — explicitly NOT a CSPRNG** (comment at `:17` admits "rand() is not a CSPRNG (MVP shortcut)"). The 32-byte auth token is therefore guessable by an attacker who can approximate the spawn time/PID. This is a **direct violation of the security model** ("cryptographically secure random byte generation" per header/docs, and the "secret token" privacy invariant).
- Concurrency: `srand` once, then `rand` — thread-safe enough for single-caller usage but not documented as such.
- I/O: `access(F_OK)` only checks existence, not executability (`X_OK`); a non-executable file passes `F_OK` then `posix_spawn` fails.
- Persistence: none.

**Acceptance map**:

| #   | Criterion                                    | Code                        | Test                                          | Status      |
| --- | -------------------------------------------- | --------------------------- | --------------------------------------------- | ----------- |
| 1   | POSIX spawn via `posix_spawn` with pre-check | `vw_platform_linux.c:31-39` | `test_platform.c:48-53`                       | ✅          |
| 2   | CSPRNG-grade token                           | `vw_platform_linux.c:8-25`  | `test_platform.c:19-26` (only "draws differ") | ❌ (rand()) |

**Assumptions/Tradeoffs**: `get_time_us` is second-resolution wall clock (`time(NULL)*1e6`) — coarse, but only used in tests. Missing trailing newline.

---

### 1.21 `plugin/src/vw_platform_win32.c`

**Why change**: Fix `<windows.h>` include order (before `<bcrypt.h>` — required by MinGW), implement `vw_platform_spawn_process` via `CreateProcessW`, UTF-8→UTF-16 command line, remove the `#else` rand() fallback.

**Responsibility before**: Win32 RNG + time. **After**: Full Win32 platform (RNG via `BCryptGenRandom`, time via `GetSystemTimeAsFileTime`, spawn via `CreateProcessW`).

**Callers**: `vw_worker_client.c`, `test_platform.c`. **Callees**: `BCryptGenRandom`, `GetSystemTimeAsFileTime`, `MultiByteToWideChar`, `CreateProcessW`, `CloseHandle`.

**Happy path**: build quoted cmdline → `MultiByteToWideChar` → `CreateProcessW(NULL, wcmd, ...)` → success → close thread+process handles → true. **Failure path**: NULL args, malloc fail, `MultiByteToWideChar <= 0`, `CreateProcessW` fail → false.

**Boundaries**:

- Input validation: NULL checks; command-line quoting is naive — embedded quotes in paths/args are not escaped (paths with `"` are broken). No arg-length cap beyond heap growth.
- Authorization: BCrypt RNG (CSPRNG) — correct on Windows.
- Concurrency: single-threaded use.
- I/O: process handles closed immediately after creation — **no retained process handle for supervision, waiting, or cleanup**; orphaned worker if the plugin crashes before disconnect.
- Persistence: none.

**Acceptance map**:

| #   | Criterion                         | Code                         | Test                             | Status |
| --- | --------------------------------- | ---------------------------- | -------------------------------- | ------ |
| 1   | Win32 process creation            | `vw_platform_win32.c:41-107` | `test_platform.c:47` (`cmd.exe`) | ✅     |
| 2   | `<windows.h>` before `<bcrypt.h>` | `vw_platform_win32.c:3-6`    | compiles                         | ✅     |

**Assumptions/Tradeoffs**: `CreateProcessW` is called with `dwCreationFlags = 0`, not `CREATE_NO_WINDOW` (`:88`) — no console suppression is applied despite the intent. `status != CMC_STATUS_SUCCESS` compares an NTSTATUS against a `wincrypt.h` constant (numerically 0, works, but semantically odd — should be `STATUS_SUCCESS`). Missing trailing newline.

---

### 1.22 `plugin/src/vw_worker_client.c`

**Why change**: Full Step 13 implementation — spawn worker, retry-connect, encode+send `HELLO`, receive/decode `HELLO_ACK`, return authenticated client; `receive_all` helper for partial reads.

**Responsibility before**: Stub that ignored args and returned a bare connected handle. **After**: Worker supervisor + authentication manager.

**Callers**: `vlc_whisper_module.c`, `test_worker_ipc.c`, `test_worker_lifecycle.c`. **Callees**: `vw_platform_spawn_process`, `vw_ipc_connect`, `vw_ipc_send`, `vw_ipc_receive`, `vw_ipc_close`, `vw_protocol_encode/decode_header/payload`.

**Happy path** (test path, `executable_path == NULL`): connect → `vw_msg_hello_t` init (`:59-66`) → encode payload into 256-byte buf (`:68-71`) → header `sequence=1` (`:73-79`) → send (`:80-82`) → `receive_all` header (`:84`) → decode+type-check (`:86-89`) → bounded ACK payload malloc (`:92-104`) → decode → return client.

**Failure path**: NULL endpoint/token → NULL (`:30-32`); spawn fail → NULL (`:36-39`); connect timeout 40×50 ms → NULL (`:43-52`); encode/send/ACK failures → `goto fail` → `vw_worker_client_disconnect` (`:123-125`) → NULL.

**Boundaries**:

- Input validation: NULL checks; `ack_hdr.payload_length > 0 && <= 1024` bounds the malloc (good); 256-byte HELLO buffer is sufficient.
- Authorization: client presents token; **client never verifies the ACK** (no version negotiation check, no authenticity check on `HELLO_ACK` — the worker→client direction is unauthenticated).
- Concurrency: runs on module-open thread; `receive_all` blocks up to 3 s per read.
- I/O: partial-read loop handles frame fragmentation; `res <= 0` treats both timeout and EOF as failure (worker treats `res == 0` as "keep waiting" — inconsistent semantics).
- Persistence: none.

**Acceptance map**:

| #   | Criterion                      | Code                         | Test                            | Status                    |
| --- | ------------------------------ | ---------------------------- | ------------------------------- | ------------------------- |
| 1   | Spawn before connect           | `vw_worker_client.c:36-39`   | `test_worker_lifecycle.c:40`    | ✅ (only when path given) |
| 2   | HELLO/HELLO_ACK handshake      | `vw_worker_client.c:59-104`  | `test_worker_ipc.c:35`          | ✅                        |
| 3   | Handshake failure → clean NULL | `vw_worker_client.c:123-125` | `test_worker_lifecycle.c:38,63` | ✅                        |

**Assumptions/Tradeoffs**: When `executable_path` is non-NULL (production), the spawned worker is passed **no arguments** — no pipe name, no token (see §7, Bug H-1). `malloc(ack_hdr.payload_length)` is properly capped at 1024. `disconnect` closes the pipe but never sends `SHUTDOWN` and never waits for the worker process.

---

### 1.23 `protocol/include/vw_protocol_codec.h`

**Why change**: Move the validate comment from `vw_protocol_validate.c` to the header declaration.

**Responsibility before**: Codec API. **After**: Same, comment relocated. Status: done (pure churn).

---

### 1.24 `protocol/include/vw_protocol_types.h`

**Why change**: Add `VW_CLIENT_VERSION`/`VW_CLIENT_VERSION_LENGTH` and `VW_WORKER_VERSION`/`VW_WORKER_VERSION_LENGTH` ("1.0.0"/5); add `VW_SESSION_ID_BYTES` (16); rename `vw_msg_hello.token` → `auth_token`.

**Responsibility before**: Wire constants/types. **After**: Version constants + renamed auth field + named session-id size.

**Callers**: codec, client, worker, tests. **Callees**: none.

**Happy path**: Version strings shared between client HELLO and worker HELLO_ACK. **Failure path**: N/A.

**Boundaries**: `client_version_length = 5` matches `"1.0.0"` (5 chars + NUL; encode writes `client_version_length` bytes via `ENC_BYTES` — the NUL is not sent, decode `DEC_PTR` allocates `len+1`). Status: done.

**Assumptions/Tradeoffs**: This is a wire-contract change (new constants, renamed field) — per Rule 14, `docs/api-contracts.md` should have been updated and was not (see §7, Risk R-2).

---

### 1.25 `protocol/src/vw_ipc_pipe_win32.c`

**Why change**: Remove the non-Windows `#else` fallback stubs (previously provided no-op `vw_ipc_*` for platforms other than Linux/Mac/Unix/Win32).

**Responsibility before**: Win32 pipe + fallback stubs. **After**: Win32 pipe only.

**Callers**: protocol transport. **Callees**: none. **Happy path**: clean `#endif`. **Failure path**: any hypothetical unsupported platform now gets unresolved symbols instead of silent no-ops — acceptable given Linux/Windows-only targets. Status: done.

---

### 1.26 `protocol/src/vw_protocol_codec.c`

**Why change**: Rename `p->token` → `p->auth_token` and literal `16` → `VW_SESSION_ID_BYTES` across all encode/decode paths.

**Responsibility before**: Codec. **After**: Same, aligned with renamed types. Status: done — mechanical, verified by `test_protocol_codec.c`.

---

### 1.27 `protocol/src/vw_protocol_validate.c`

**Why change**: Comment moved to header. Status: done (churn).

---

### 1.28 `tests/CMakeLists.txt`

**Why change**: Add `vw_platform_linux.c`/`vw_platform_win32.c` to `test_worker_ipc`/`test_worker_lifecycle` sources; add `test_platform` (win32 or linux impl); link `bcrypt` on Windows for the three tests.

**Responsibility before**: Test targets. **After**: Platform coverage wired into unit + integration.

**Callers**: CTest. **Callees**: test binaries. **Happy path**: 11 targets registered, 11/11 pass. **Failure path**: N/A.

**Acceptance map**: `tests/CMakeLists.txt:28-42`. Status: done — note both `vw_platform_linux.c` and `vw_platform_win32.c` are compiled into the integration tests simultaneously; each is `#if`-guarded so only one contributes symbols per platform. Clean.

---

### 1.29 `tests/integration/test_worker_ipc.c`

**Why change**: Delete hand-rolled `send_hello`; the handshake now happens inside `vw_worker_client_launch_and_connect`; config field renamed to `auth_token`.

**Responsibility before**: Manual HELLO after connect. **After**: Handshake exercised by the client itself.

**Callers**: CTest. **Callees**: `vw_worker_client_launch_and_connect`, `vw_worker_client_disconnect`.

**Happy path**: pthread worker (`vw_worker_run` with config `pipe_name="test_ipc_socket"`, token `0..31`) → client connects + handshakes → SHUTDOWN frame → worker exits. **Failure path**: N/A here (covered in lifecycle).

**Boundaries**: config token `(uint8_t)i` for i in 0..31. **Acceptance map**: `test_worker_ipc.c:32-41`. Status: done — passes 11/11 and under memcheck.

---

### 1.30 `tests/integration/test_worker_lifecycle.c`

**Why change**: Rework tests around the in-client handshake and add failure paths: (1) wrong token → `bad_client == NULL`, worker exits 1; (2) good token + START_SESSION + SHUTDOWN; (3) client NULL-arg validation + no-listener connect; (4) first-frame-not-HELLO → worker exits 1.

**Responsibility before**: Manual HELLO + wrong-token test. **After**: Comprehensive handshake/lifecycle coverage including auth rejection and protocol-violation paths.

**Callers**: CTest. **Callees**: client API, `vw_ipc_connect`, codec.

**Happy path**: good-token connect → START_SESSION → SHUTDOWN → clean exit. **Failure path**: wrong token → client NULL (worker `vw_worker.c:108-111` exits 1); non-HELLO first frame → worker exits 1 (`vw_worker.c:101-104`); NULL endpoint/token → NULL; no listener → NULL after ~2 s retry.

**Boundaries**: `for (int i = 0; i < VW_AUTH_TOKEN_BYTES; i++)` — signed/unsigned compare (documented in `docs/issues.md` #2); `usleep` undeclared under strict C17 (warns). **Acceptance map**: `test_worker_lifecycle.c:31-113`. Status: done — passes 11/11 + memcheck; two documented cosmetic warnings remain.

---

### 1.31 `tests/unit/test_platform.c` (new)

**Why change**: Unit tests for the platform abstraction: RNG NULL/zero rejection + non-zero + draws-differ; time monotonic/wall-clock sanity; spawn NULL/partial-NULL rejection + success (`/bin/true` / `cmd.exe`) + missing-executable failure.

**Responsibility before**: N/A. **After**: Cross-platform platform-layer coverage.

**Callers**: CTest. **Callees**: `vw_platform_*`.

**Happy path**: all `EXPECT`s pass; main returns 0. **Failure path**: any assertion aborts.

**Boundaries**:

- Input validation: NULL/zero RNG, NULL exe/argv, missing executable.
- Authorization: "draws differ" is the only randomness assertion — **too weak to detect the `rand()` regression** (H-2); two draws from a time-seeded `rand()` almost always differ, so the test passes while the token is cryptographically weak.
- Concurrency: single-threaded.
- I/O: `posix_spawn` of `/bin/true`.
- Persistence: none.

**Acceptance map**: `test_platform.c:18-57`. Status: done — passes 11/11 + memcheck (the `access()` pre-check at `vw_platform_linux.c:33` makes the missing-executable case deterministic under Valgrind, which is why `docs/issues.md` #1 is stale).

---

### 1.32 `tests/unit/test_protocol_codec.c`

**Why change**: `hello.token` → `hello.auth_token`, `16` → `VW_SESSION_ID_BYTES`, `32` → `VW_AUTH_TOKEN_BYTES`.

**Responsibility before**: Codec unit tests. **After**: Same, aligned with renames. Status: done.

---

### 1.33 `worker/include/vw_worker_config.h`

**Why change**: `token[32]` → `auth_token[VW_AUTH_TOKEN_BYTES]`; add `#include "vw_protocol_types.h"`.

**Responsibility before**: Worker config. **After**: Same, aligned. Status: done.

---

### 1.34 `worker/src/vw_worker.c`

**Why change**: (a) reply to `HELLO` with a real `HELLO_ACK` (selected version, capability flags, worker version) via the codec; (b) token rename; (c) partial-read loops for header and payload; (d) comment additions.

**Responsibility before**: Single-threaded IPC loop, token verify, no ACK. **After**: Loop + ACK reply + robust partial reads.

**Callers**: `vw_worker_run` (tests; production `main.c` still a stub). **Callees**: `vw_ipc_*`, codec, `verify_token_constant_time`.

**Happy path**: listen (`:39`) → read header loop (`:46-58`) → decode+validate header (magic/major/payload cap, `vw_protocol_validate.c:3-7`) → payload loop (`:60-77`) → decode+validate payload (`:80-96`) → first-msg=HELLO (`:101`) → constant-time token verify (`:108`) → build+send HELLO_ACK (`:111-142`) → loop continues.

**Failure path**: bad magic/major/oversize → break (exit 1); first frame not HELLO → break; token mismatch → break (exit 1); encode/ACK failure → break. All paths free `payload_buf`.

**Boundaries**:

- Input validation: header magic/major/payload≤1 MB validated before malloc; payload validated post-decode.
- Authorization: constant-time 32-byte compare (correct pattern, `volatile uint8_t diff`); **but the token itself never reaches the worker in production** (H-1).
- Concurrency: single-threaded; ACK read loop `res==0` busy-continues on timeout (no backoff).
- I/O: header+payload partial-read loops handle fragmentation.
- Persistence: none.

**Acceptance map**:

| #   | Criterion                     | Code                  | Test                             | Status |
| --- | ----------------------------- | --------------------- | -------------------------------- | ------ |
| 1   | Worker replies HELLO_ACK      | `vw_worker.c:111-142` | `test_worker_ipc.c:35`           | ✅     |
| 2   | Auth rejection exits non-zero | `vw_worker.c:108-111` | `test_worker_lifecycle.c:31-41`  | ✅     |
| 3   | First frame must be HELLO     | `vw_worker.c:101-104` | `test_worker_lifecycle.c:89-110` | ✅     |

**Assumptions/Tradeoffs**: `sequence` is set to 1 on both HELLO (client) and HELLO_ACK (worker) and never validated — sequence tracking exists in the wire format but is unused. No `SHUTDOWN` handling change.

---

## 2. Happy-Path Request Trace

**Production-intended path (as the design intends):**

1. VLC activates the audio filter → `vw_plugin_open` — `plugin/src/vlc_whisper_module.c:91`
2. Pipe name + worker path built per-OS — `vlc_whisper_module.c:123-131` (`/tmp/vlc-whisper-<pid>.sock`, `vlc-whisper-worker`)
3. `vw_platform_get_random_bytes(sys->auth_token, 32)` — `vlc_whisper_module.c:128` → Win32 `BCryptGenRandom` / Linux `rand()` (see H-2)
4. `vw_worker_client_launch_and_connect(path, pipe, token)` — `vlc_whisper_module.c:130`
5. `vw_platform_spawn_process(path, {path, NULL})` — `plugin/src/vw_worker_client.c:36-39` → Linux `posix_spawn` (`vw_platform_linux.c:31-39`) / Win32 `CreateProcessW` (`vw_platform_win32.c:41-107`)
6. Connect retry (40 × 50 ms) — `vw_worker_client.c:43-53`
7. Client builds `HELLO` (`min_major=max_major=1`, token, client_version "1.0.0") — `vw_worker_client.c:59-66`, encodes payload + header, sends — `:68-82`
8. Worker `vw_worker_run` reads header loop — `worker/src/vw_worker.c:46-58`, decodes/validates — `:60-96`, first-frame HELLO check — `:101`, constant-time token verify — `:108`, sends `HELLO_ACK` (version 1.0, `VW_CAPABILITY_PCM_S16LE_16K_MONO`) — `:111-142`
9. Client `receive_all` reads ACK header — `vw_worker_client.c:84`, decode + type check — `:86-89`, bounded payload decode — `:92-104`
10. `vw_worker_client_t*` returned, stored in `sys->client`; `PLUGIN_OPEN` logged — `vlc_whisper_module.c:138-140`
11. On module unload → `vw_plugin_close` → `vw_worker_client_disconnect` — `vlc_whisper_module.c:153-157` → `vw_worker_client.c:128-135` (close pipe, free)

**Reality check (critical):** Step 5 spawns `vlc-whisper-worker` with **zero arguments**, and `worker/src/main.c:6-7` is still `printf("Hello, VLC Whisper worker!\n"); return 0;`. The worker therefore (a) never binds the pipe and (b) never learns the token — the production happy path **cannot complete**. The path above only works in the integration tests, which bypass the spawn (`executable_path == NULL`) and drive `vw_worker_run` in-process with a pre-populated `vw_worker_config_t` (`tests/integration/test_worker_ipc.c:18-31`, `test_worker_lifecycle.c:18-29`).

---

## 3. Most Important Failure Path

**Worker auth rejection (wrong token) — end-to-end:**

1. Client builds HELLO with an invalid token and sends it — `vw_worker_client.c:59-82`
2. Worker reads + validates frame, first-message-is-HELLO passes — `vw_worker.c:46-104`
3. `verify_token_constant_time(config->auth_token, hello.auth_token)` returns false — `vw_worker.c:108` (constant-time compare, `:12-21`)
4. Worker `free(payload_buf); break;` — `vw_worker.c:110-111` → `vw_worker_run` returns 1 → worker process exits code 1
5. Client `receive_all` on the ACK header gets `vw_ipc_receive` returning `-1` (EOF, peer closed) — `vw_worker_client.c:84` → false → `goto fail`
6. `vw_worker_client_disconnect(client)` closes the pipe and frees — `vw_worker_client.c:123-125`
7. `vw_worker_client_launch_and_connect` returns `NULL` — `:125`
8. `vw_plugin_open` logs `PLUGIN_WORKER_UNAVAILABLE` and continues with `sys->client == NULL` — `vlc_whisper_module.c:133-135`, returns `VLC_SUCCESS`
9. Playback continues in pure passthrough; no audio stutter, no crash

Verified by `test_worker_lifecycle.c:31-41` (asserts `bad_client == NULL` and worker exit code 1). The first-frame-not-HELLO variant follows the same shape (`vw_worker.c:101-104`, test section 4).

**Secondary failure path (production-only):** worker binary missing or not in CWD → Linux `access(F_OK)` fails → `vw_platform_spawn_process` false → client NULL after ~2 s connect-retry time spent waiting — `vw_worker_client.c:36-53`. This is the only path actually reachable in production today (because `main.c` is a stub), and it adds a **2-second stall to every module activation**.

---

## 4. Boundary Summary

| Boundary type        | What to check                                           | Code location                                                 | Status                                                                                 |
| -------------------- | ------------------------------------------------------- | ------------------------------------------------------------- | -------------------------------------------------------------------------------------- |
| **Input validation** | NULL endpoint/token rejected                            | `vw_worker_client.c:30-32`                                    | Validated                                                                              |
| **Input validation** | ACK payload capped before malloc (≤1024)                | `vw_worker_client.c:92`                                       | Validated                                                                              |
| **Input validation** | Header magic/major/payload ≤ 1 MB                       | `vw_protocol_validate.c:3-7`                                  | Validated                                                                              |
| **Input validation** | Partial reads (header + payload)                        | `vw_worker_client.c:19-25`, `vw_worker.c:46-77`               | Validated                                                                              |
| **Input validation** | RNG NULL / zero-size                                    | `vw_platform_linux.c:9-12`, `vw_platform_win32.c:14-16`       | Validated                                                                              |
| **Input validation** | Spawn NULL / missing executable                         | `vw_platform_linux.c:32-34`, `vw_platform_win32.c:42-44`      | Validated                                                                              |
| **Input validation** | `test_platform.c:51` Valgrind determinism               | `vw_platform_linux.c:33` (`access`)                           | Validated                                                                              |
| **Authorization**    | Constant-time 32-byte token compare (worker)            | `vw_worker.c:12-21,108`                                       | Validated                                                                              |
| **Authorization**    | Token reaches worker in production                      | `vw_worker_client.c:36-39` (no args), `main.c:6-7`            | **Gap — token never delivered** (H-1)                                                  |
| **Authorization**    | Client verifies worker (HELLO_ACK) authenticity/version | `vw_worker_client.c:86-104`                                   | **Gap — type check only, no version/authenticity**                                     |
| **Authorization**    | Token is cryptographically random                       | `vw_platform_linux.c:8-25`                                    | **Gap — `rand()` (H-2)**                                                               |
| **Concurrency**      | Audio callback lock-free (Rule 4)                       | `vlc_whisper_module.c:47-87`                                  | Validated (unchanged)                                                                  |
| **Concurrency**      | Module-open blocking                                    | `vw_worker_client.c:43-53`                                    | **Risk — up to ~2 s stall per activation**                                             |
| **Concurrency**      | Worker single-threaded loop                             | `vw_worker.c`                                                 | Consistent with Step 13 scope                                                          |
| **I/O**              | Connect retry / read timeout semantics                  | `vw_worker_client.c:43-53`, `vw_ipc_socket_linux.c:52-53,108` | Partial — 3 s per read; `res==0` handled inconsistently (client=fail, worker=continue) |
| **I/O**              | Spawn flags on Windows                                  | `vw_platform_win32.c:88`                                      | **Doc drift — intent is `CREATE_NO_WINDOW`, passes `0`**                               |
| **Persistence**      | `/tmp/vlc-whisper-<pid>.sock` cleanup                   | `vlc_whisper_module.c:124`                                    | **Gap — no unlink/cleanup path in this diff**                                          |
| **Persistence**      | No PCM/transcript/token logging                         | entire diff                                                   | Validated (log events carry event IDs only)                                            |

---

## 5. Acceptance Criterion → Code Mapping

| #   | Criterion                                                      | Plan Source            | Code                                | Test                                          | Status                                         |
| --- | -------------------------------------------------------------- | ---------------------- | ----------------------------------- | --------------------------------------------- | ---------------------------------------------- |
| 1   | `vw_plugin_open` generates 32-byte token + OS endpoint         | `step13_plan.md:36`    | `vlc_whisper_module.c:123-131`      | `test_platform.c:18-27`                       | ⚠️ partial (token not CSPRNG on Linux)         |
| 2   | `vw_plugin_open` invokes `vw_worker_client_launch_and_connect` | `step13_plan.md:37`    | `vlc_whisper_module.c:130`          | `test_worker_ipc.c:35`                        | ✅                                             |
| 3   | `vw_plugin_close` cleans up via `vw_worker_client_disconnect`  | `step13_plan.md:38`    | `vlc_whisper_module.c:153-157`      | lifecycle suite                               | ✅                                             |
| 4   | Missing worker → warn + `VLC_SUCCESS` passthrough              | `step13_plan.md:39`    | `vlc_whisper_module.c:133-135`      | `test_worker_lifecycle.c:44-46` (no listener) | ✅                                             |
| 5   | Build + unit tests pass 100%                                   | `step13_plan.md:40`    | —                                   | 11/11 ctest                                   | ✅ (verified)                                  |
| 6   | Valgrind memcheck 100% clean                                   | `step13_plan.md:41`    | —                                   | 11/11 memcheck                                | ✅ (verified; contradicts `docs/issues.md` #1) |
| 7   | C17, no blocking in audio callback                             | `step13_plan.md:44-45` | `vlc_whisper_module.c:47-87`        | invariant                                     | ✅                                             |
| 8   | **End-to-end authenticated handshake on module load**          | `step13_plan.md` goal  | `main.c:6-7` (stub), spawn w/o args | tests bypass spawn                            | ❌ **not achievable in production** (H-1)      |
| 9   | Plan/DoD checkboxes reflect implementation                     | `step13_plan.md:36-46` | —                                   | —                                             | ❌ all `[ ]` unchecked                         |
| 10  | Wire/protocol changes → `docs/api-contracts.md` (Rule 14)      | `AGENTS.md:18`         | `vw_protocol_types.h`               | —                                             | ❌ not updated (R-2)                           |

---

## 7. Code Review Findings (Bugs, Risks, Nitpicks)

### Bugs (Sorted by Priority)

| Priority   | Component / Location                                                    | Description                                                                                                                                                                                                                                                                                                                                                                                                                                                          | Impact                                                                                                     | Proposed Fix                                                                                                                                                                     |
| ---------- | ----------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **High**   | `plugin/src/vw_worker_client.c:36-39` + `worker/src/main.c:6-7`         | Production spawn passes **no arguments** (no pipe name, no auth token), and `main.c` is still a stub that prints "Hello, VLC Whisper worker!" and exits. The worker never binds the endpoint and never learns the token, so the Step 13 handshake is **unreachable in production**; `vw_plugin_open` always lands in passthrough after a ~2 s connect timeout. Tests pass only because they bypass the spawn (`executable_path == NULL`) and inject config directly. | Step 13 goal (authenticated handshake on module load) not delivered; every module activation stalls ~2 s   | Implement `main.c` arg parsing (`--pipe`, `--token`, `--model`, etc.) calling `vw_worker_config_init_defaults` + `vw_worker_run`; pass pipe name and hex token in the spawn argv |
| **High**   | `plugin/src/vw_platform_linux.c:8-25`                                   | `vw_platform_get_random_bytes` uses `rand()` seeded once with `time ^ pid` — explicitly not a CSPRNG (admitted in comment `:17`). The 32-byte auth token is guessable by anyone approximating spawn time/PID, defeating the documented "cryptographically secure random" security model and the secret-token privacy invariant.                                                                                                                                      | Token forgery enables unauthorized IPC clients                                                             | Use `getrandom(2)` (or `/dev/urandom` with full-read loop) on Linux; keep BCrypt on Windows                                                                                      |
| **Medium** | `plugin/src/vw_worker_client.c:86-104`                                  | Client decodes `HELLO_ACK` but never validates `selected_major`/version or any ACK authenticity — a mismatched or malicious worker can negotiate any version and the client accepts.                                                                                                                                                                                                                                                                                 | Version-negotiation contract silently ignored; no mutual auth (client→worker only)                         | Validate `ack.selected_major == VW_PROTOCOL_VERSION_MAJOR`; fail otherwise                                                                                                       |
| **Medium** | `docs/issues.md:3-18`                                                   | Issue #1 is stale and inaccurate for this tree: `test_platform` passes under memcheck **11/11** (verified this session, direct `valgrind` and `ctest -T memcheck`). The suggested fix (pre-validate executable before spawn) is **already implemented** (`vw_platform_linux.c:33`), and the test is new in this branch, so "pre-existing" is wrong. The doc also claims CI is blocked.                                                                               | Misleading docs; Rule 14 doc accuracy violated; CI claim disproven                                         | Rewrite Issue #1 as resolved (note the `access()` pre-check), or delete it                                                                                                       |
| **Medium** | `plugin/src/vw_platform_win32.c:88,99-105`                              | `CreateProcessW` called with `dwCreationFlags = 0` (no `CREATE_NO_WINDOW`, contrary to the intent), and both process handles are closed immediately — no handle retained for supervision/wait/cleanup. `vw_worker_client_disconnect` never sends `SHUTDOWN` nor waits for the worker.                                                                                                                                                                                | Console window may flash on Windows; orphaned worker processes possible if the plugin crashes before close | Pass `CREATE_NO_WINDOW`; retain `pi.hProcess` in the client and `WaitForSingleObject`/`TerminateProcess` on disconnect                                                           |
| **Low**    | `plugin/src/vw_worker_client.c:19-25` vs `worker/src/vw_worker.c:50-56` | Timeout semantics inconsistent: client's `receive_all` treats `vw_ipc_receive == 0` (timeout) as failure, worker treats `res == 0` as "keep waiting".                                                                                                                                                                                                                                                                                                                | Different handshake behavior under slow links; no total handshake deadline                                 | Define one contract: treat 0 as retryable only where intended; add a total deadline                                                                                              |
| **Low**    | `plugin/src/vw_platform_linux.c:29`                                     | `vw_platform_get_time_us` is second-resolution wall clock (`time(NULL) * 1e6`) — the header promises microseconds; Win32 gives 100 ns resolution.                                                                                                                                                                                                                                                                                                                    | Coarse timestamps if ever used for timing (Rule 6 caution)                                                 | Use `clock_gettime(CLOCK_REALTIME)` for µs resolution                                                                                                                            |

### Architectural & Operational Risks

| Category                   | Risk Description                                                                                                                                                                                                   | Affected Files                                                    | Mitigation Strategy                                                                                       |
| -------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ----------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------- |
| **Step 13 not end-to-end** | The named deliverable (client↔worker handshake on module load) is only proven in tests; production wiring is a stub-to-stub no-op                                                                                  | `worker/src/main.c`, `vw_worker_client.c`, `vlc_whisper_module.c` | Land `main.c` arg parsing + spawn-arg plumbing as part of Step 13 before marking done (Bug H-1)           |
| **Rule 14 violation**      | Protocol wire contract changed (new version constants, `auth_token` rename, `VW_SESSION_ID_BYTES`, HELLO_ACK now sent) but `docs/api-contracts.md` / `docs/whisper-api.md` were **not updated** in the same change | `protocol/include/vw_protocol_types.h`, `docs/api-contracts.md`   | Add the HELLO/HELLO_ACK version-negotiation and constant table to `api-contracts.md`                      |
| **Worker path resolution** | Bare relative `worker_path` ("vlc-whisper-worker") resolved against VLC's CWD; no plugin-dir discovery or config override                                                                                          | `vlc_whisper_module.c:127-130`                                    | Resolve worker next to the plugin module (or configurable), check existence before spawn                  |
| **Forward-looking ADRs**   | ADR-013/014/015 document worker architecture (reader thread, MF lifecycle, Model-Once) not present in this tree; architecture.md is ahead of code                                                                  | `docs/decisions.md:98-140`, `docs/architecture.md`                | Keep as blueprint (postmortem already frames them for Phase A/D) but label clearly as not-yet-implemented |
| **Windows test gap**       | No Windows test preset runs `test_platform`/IPC suites; Win32 spawn/BCrypt paths verified only by cross-compile                                                                                                    | `tests/CMakeLists.txt:31-42`                                      | Add a windows-x64 test target or document manual verification                                             |

### Code Style & Quality Nitpicks

| Issue Type            | File & Line                                         | Description                                                                                     | Recommendation                                                                                                          |
| --------------------- | --------------------------------------------------- | ----------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------- |
| **Include Order**     | `vlc_whisper_module.c:36-47`                        | `stdio.h`/`stdlib.h`/`string.h` + platform headers inserted mid-file after `vw_plugin_log_sink` | Move all non-VLC includes to the top of the file                                                                        |
| **Doc Drift**         | `vw_platform_win32.c:41-107`                        | Comment/design intent is `CREATE_NO_WINDOW`; code passes `0`                                    | Align comment with code (or add the flag)                                                                               |
| **Semantic Constant** | `vw_platform_win32.c:30`                            | `status != CMC_STATUS_SUCCESS` compares NTSTATUS to a `wincrypt.h` constant                     | Use `STATUS_SUCCESS` (or `!= 0`) for clarity                                                                            |
| **Signed/Unsigned**   | `test_worker_lifecycle.c:26`                        | `int i < VW_AUTH_TOKEN_BYTES` (`-Wsign-compare`)                                                | Use `size_t` loop counter                                                                                               |
| **Strict C17**        | `test_worker_lifecycle.c`                           | `usleep` undeclared under `-std=c17` (warns)                                                    | Use `nanosleep`/`setitimer`                                                                                             |
| **Missing Newline**   | `vw_platform_linux.c:40`, `vw_platform_win32.c:110` | Files end without trailing newline                                                              | Add newline                                                                                                             |
| **Weak Test**         | `test_platform.c:24-26`                             | "consecutive draws must differ" is satisfied by `rand()`; does not detect the CSPRNG regression | Document that Linux RNG is not a CSPRNG; gate a stronger check (e.g., determinism/entropy smoke) once `getrandom` lands |
| **Unchecked Boxes**   | `step13_plan.md:36-46`, `roadmap.md:41`             | All acceptance/DoD boxes `[ ]` while code is committed; roadmap step 13 unchecked               | Check off completed criteria; annotate the end-to-end criterion pending H-1                                             |
| **Count Drift**       | `milestone3_postmortem.md:305`                      | Claims "13/13 tests passing" on milestone-3 baseline; tree has 11 tests                         | Correct the count or clarify the baseline                                                                               |
| **Empty Suppression** | `cmake/valgrind.supp`                               | Zero-byte placeholder not referenced anywhere                                                   | Remove or wire it into memcheck config                                                                                  |

---

**Bottom line**: The Step 13 _code plumbing_ is in good shape — the client-side handshake, constant-time token verification, bounded ACK allocation, partial-read loops, and the new failure-path tests are solid, and the full suite passes 11/11 under both ctest and memcheck (contradicting `docs/issues.md` #1, which should be closed). However, the branch does **not** deliver the Step 13 goal end-to-end: the production worker (`main.c`) is still a stub and the spawn passes no pipe name or token, so the authenticated handshake can only run in tests. Before marking Step 13 complete: implement `main.c` arg parsing + spawn-arg plumbing (H-1), replace the Linux `rand()` token with a real CSPRNG (H-2), update `docs/api-contracts.md` per Rule 14 (R-2), and fix the stale `docs/issues.md` #1. None of these block the current 11/11 test suite, which is why the branch is green while the milestone goal is unmet.
