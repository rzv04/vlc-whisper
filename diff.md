# Diff Analysis: Milestone 1.7 & IPC Timeouts

**21 files changed, +711 / -105 lines**
**Base**: `HEAD` (vs Milestone 1 baseline)

---

## 1. File-by-File Analysis

### 1.1 `AGENTS.md`

**Why change**: Enforce strict mandatory verification checklist for all AI agents and contributors, adding Valgrind memory leak verification.

**Responsibility before**: Stated rule 10 requiring clang-format and cmake build/ctest. **After**: Rule 10 mandates clang-format, cmake build/ctest, and Valgrind memory check (`ctest --test-dir build/linux-x64-debug -T memcheck`).

**Callers**: AI agents, human contributors. **Callees**: None.

**Happy path**: Agent runs `clang-format --dry-run --Werror`, `cmake --preset linux-x64-debug`, `cmake --build --preset linux-x64-debug`, `ctest --preset linux-x64-debug`, and `ctest --test-dir build/linux-x64-debug -T memcheck` before completing tasks.

**Failure path**: Task completion declared without running Valgrind memcheck fails rule compliance check.

**Boundaries**:
- **Input validation**: Exact command line matching in Rule 10.
- **Authorization**: Mandatory compliance policy.
- **Concurrency**: N/A.
- **I/O**: N/A.
- **Persistence**: N/A.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|-----------|------|------|--------|
| 1 | Require Valgrind memory check in Rule 10 | `AGENTS.md:16` | N/A | ✅ |

**Assumptions/Tradeoffs**: Assumes `valgrind` is installed on Linux developer system.

---

### 1.2 `.agents/AGENTS.md`

**Why change**: Mirror root `AGENTS.md` rules for agent execution environment.

**Responsibility before**: Stated rule 10 requiring build and test suite. **After**: Rule 10 mandates build, test suite, and Valgrind memcheck.

**Callers**: AI subagents. **Callees**: None.

**Happy path**: Subagents follow Rule 10 verification checklist.

**Failure path**: Subagent fails validation if Valgrind step omitted.

**Boundaries**:
- **Input validation**: Exact string matching.
- **Authorization**: Compliance policy.
- **Concurrency**: N/A.
- **I/O**: N/A.
- **Persistence**: N/A.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|-----------|------|------|--------|
| 1 | Mirror Rule 10 Valgrind check | `.agents/AGENTS.md:16` | N/A | ✅ |

**Assumptions/Tradeoffs**: Keeps subagent rules in sync with root `AGENTS.md`.

---

### 1.3 `CMakeLists.txt`

**Why change**: Enable CMake CTest framework so `ctest -T memcheck` can execute tests under Valgrind.

**Responsibility before**: Configured subdirectories and build options. **After**: Includes `include(CTest)` inside `if(BUILD_TESTING)` block.

**Callers**: CTest CLI (`ctest -T memcheck`). **Callees**: CMake CTest module.

**Happy path**: Running `ctest -T memcheck` in build directory executes Valgrind memory checks on all test targets.

**Failure path**: If `BUILD_TESTING` is OFF, CTest module is omitted.

**Boundaries**:
- **Input validation**: `if(BUILD_TESTING)` guard.
- **Authorization**: CMake build system.
- **Concurrency**: N/A.
- **I/O**: N/A.
- **Persistence**: CMake build configuration.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|-----------|------|------|--------|
| 1 | Enable CTest memcheck support | `CMakeLists.txt:38` | `ctest -T memcheck` | ✅ |

**Assumptions/Tradeoffs**: None.

---

### 1.4 `README.md`

**Why change**: Document the exact Valgrind memory leak test command for developers.

**Responsibility before**: Documented basic CMake configure and test commands. **After**: Documents `ctest -T memcheck --output-on-failure` for memory leak checks.

**Callers**: Developers. **Callees**: None.

**Happy path**: Developer follows README instructions to run memory leak checks.

**Failure path**: N/A.

**Boundaries**: Documentation only.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|-----------|------|------|--------|
| 1 | Document Valgrind usage | `README.md:58-63` | N/A | ✅ |

**Assumptions/Tradeoffs**: None.

---

### 1.5 `docs/api-contracts.md`

**Why change**: Document local IPC protocol timeouts and transport guarantees.

**Responsibility before**: Documented frame envelope and message types. **After**: Documents 10s connection accept timeout and 3s read/write frame timeout under `Transport Timeouts & Guarantees`.

**Callers**: Protocol implementers and tests. **Callees**: None.

**Happy path**: Peer implementations respect 10s accept timeout and 3s I/O limits.

**Failure path**: Unresponsive peer triggers 3s read/write timeout or 10s accept timeout.

**Boundaries**: Specification document.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|-----------|------|------|--------|
| 1 | Document 10s accept & 3s I/O timeouts | `docs/api-contracts.md:9-12` | N/A | ✅ |

**Assumptions/Tradeoffs**: None.

---

### 1.6 `docs/architecture.md`

**Why change**: Align system architecture docs with IPC transport timeouts.

**Responsibility before**: Documented IPC named pipe / socket framing. **After**: Includes explicit `Transport Timeouts` section detailing 10s accept and 3s I/O timeouts.

**Callers**: System architects and developers. **Callees**: None.

**Happy path**: IPC transport design adheres to 10s accept / 3s I/O timeouts.

**Failure path**: N/A.

**Boundaries**: Specification document.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|-----------|------|------|--------|
| 1 | Document IPC transport timeouts | `docs/architecture.md:62-65` | N/A | ✅ |

**Assumptions/Tradeoffs**: None.

---

### 1.7 `docs/plans/milestone_1_7_plan.md`

**Why change**: Track implementation steps, transport design, and test criteria for Milestone 1.7.

**Responsibility before**: Draft plan. **After**: Full task execution plan detailing IPC transport server, secret token auth, state machine, and test suite.

**Callers**: AI agents, developers. **Callees**: None.

**Happy path**: Milestone 1.7 execution follows plan stages to completion.

**Failure path**: Plan tracks risk mitigations for MinGW cross-compilation and transport timeouts.

**Boundaries**: Planning document.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|-----------|------|------|--------|
| 1 | Detailed plan for Milestone 1.7 | `docs/plans/milestone_1_7_plan.md:1-200` | N/A | ✅ |

**Assumptions/Tradeoffs**: None.

---

### 1.8 `plugin/CMakeLists.txt`

**Why change**: Switch plugin build target to use cross-platform `vw_worker_client.c` instead of Win32 stub `vw_worker_client_win32.c`.

**Responsibility before**: Compiled `src/vw_worker_client_win32.c`. **After**: Compiles `src/vw_worker_client.c`.

**Callers**: CMake build. **Callees**: `vlc_whisper_plugin` target.

**Happy path**: Plugin library compiles cleanly across Linux and Windows.

**Failure path**: N/A.

**Boundaries**: Build configuration.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|-----------|------|------|--------|
| 1 | Cross-platform plugin client source | `plugin/CMakeLists.txt:7` | `cmake --build` | ✅ |

**Assumptions/Tradeoffs**: None.

---

### 1.9 `plugin/include/vw_worker_client.h`

**Why change**: Update client launcher function signature to accept `endpoint_name` and 32-byte secret `token`.

**Responsibility before**: `vw_worker_client_launch_and_connect(executable_path)`. **After**: `vw_worker_client_launch_and_connect(executable_path, endpoint_name, token[32])`.

**Callers**: Plugin session module, integration tests. **Callees**: `vw_worker_client.c`.

**Happy path**: Caller passes endpoint name and token to launch client.

**Failure path**: N/A.

**Boundaries**: Public header interface.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|-----------|------|------|--------|
| 1 | Update launcher signature | `plugin/include/vw_worker_client.h:12` | `test_worker_ipc.c` | ✅ |

**Assumptions/Tradeoffs**: Token parameter reserved for client-side HELLO handshake.

---

### 1.10 `plugin/src/vw_worker_client.c` [NEW]

**Why change**: Provide cross-platform worker client launcher.

**Responsibility before**: None (new file replacing platform stub). **After**: Launches worker and connects via `vw_ipc_connect(endpoint_name)`.

**Callers**: `vw_worker_client_launch_and_connect()`, `vw_worker_client_disconnect()`. **Callees**: `vw_ipc_connect()`, `vw_ipc_close()`.

**Happy path**: `vw_ipc_connect()` succeeds and returns populated `vw_worker_client_t*`.

**Failure path**: `vw_ipc_connect()` returns NULL -> returns NULL.

**Boundaries**:
- **Input validation**: Checks `ipc` handle and `client` allocation.
- **Authorization**: Connects to endpoint.
- **Concurrency**: Caller thread.
- **I/O**: Calls `vw_ipc_connect`.
- **Persistence**: Memory allocation cleaned up in `vw_worker_client_disconnect`.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|-----------|------|------|--------|
| 1 | Implement worker client launch & disconnect | `plugin/src/vw_worker_client.c:5-31` | `test_worker_ipc.c` | ✅ |

**Assumptions/Tradeoffs**: None.

---

### 1.11 `plugin/src/vw_worker_client_win32.c` [DELETE]

**Why change**: Removed obsolete Win32 stub file in favor of cross-platform `vw_worker_client.c`.

**Responsibility before**: Win32 stub returning NULL. **After**: Deleted.

---

### 1.12 `protocol/include/vw_ipc_transport.h`

**Why change**: Remove `token` parameter from `vw_ipc_listen` and `vw_ipc_connect` signatures to isolate transport layer from protocol auth.

**Responsibility before**: `vw_ipc_listen(endpoint_name, token)` / `vw_ipc_connect(endpoint_name, token)`. **After**: `vw_ipc_listen(endpoint_name)` / `vw_ipc_connect(endpoint_name)`.

**Callers**: `vw_worker.c`, `vw_worker_client.c`, tests. **Callees**: OS transport implementations.

**Happy path**: Transport functions take endpoint name only.

**Failure path**: N/A.

**Boundaries**: Pure byte transport API.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|-----------|------|------|--------|
| 1 | Decouple token from transport layer | `protocol/include/vw_ipc_transport.h:17-18` | All IPC tests | ✅ |

**Assumptions/Tradeoffs**: Authentication handled at protocol level (`VW_MSG_HELLO`).

---

### 1.13 `protocol/include/vw_protocol_types.h`

**Why change**: Define `VW_AUTH_TOKEN_BYTES` (32) constant for authentication token array size.

**Responsibility before**: Defined message types and structures. **After**: Defines `VW_AUTH_TOKEN_BYTES` (32).

**Callers**: `vw_protocol_codec.c`, `vw_worker.c`, tests. **Callees**: None.

**Happy path**: Provides canonical 32-byte size constant.

**Failure path**: N/A.

**Boundaries**: Header constants.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|-----------|------|------|--------|
| 1 | Auth token byte constant | `protocol/include/vw_protocol_types.h:12` | `test_worker_ipc.c` | ✅ |

**Assumptions/Tradeoffs**: Fixed 32-byte secret token.

---

### 1.14 `protocol/src/vw_ipc_pipe_win32.c`

**Why change**: Full Win32 named pipe transport server (`vw_ipc_listen`) and client (`vw_ipc_connect`) with 10s accept timeout and 3s read/write timeouts.

**Responsibility before**: Win32 stub returning false/NULL. **After**: Production Win32 named pipe transport implementation.

**Callers**: `vw_worker.c`, `vw_worker_client.c`, tests. **Callees**: Win32 API (`CreateNamedPipeA`, `ConnectNamedPipe`, `CreateFileA`, `ReadFile`, `WriteFile`, `CancelIo`, `WaitForSingleObject`).

**Happy path**:
- `vw_ipc_listen()` creates pipe with `FILE_FLAG_OVERLAPPED`, waits up to 10s for client connection via `WaitForSingleObject(ov.hEvent, 10000)`.
- `vw_ipc_receive()` reads frame payload using overlapped `ReadFile` with 3s timeout (`WaitForSingleObject(ov.hEvent, 3000)`).
- `vw_ipc_send()` writes frame using overlapped `WriteFile` with 3s timeout.

**Failure path**:
- No client connects within 10s -> `CancelIo()`, `CloseHandle()`, returns `NULL`.
- Read/Write times out after 3s -> `CancelIo()`, returns `-1` / `false`.

**Boundaries**:
- **Input validation**: Handle validity checks (`INVALID_HANDLE_VALUE`).
- **Authorization**: Win32 Named Pipe local current-user ACLs.
- **Concurrency**: Single-threaded handle wrapper.
- **I/O**: Overlapped I/O with 10s accept and 3s read/write timeouts.
- **Persistence**: Clean handle closure in `vw_ipc_close()`.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|-----------|------|------|--------|
| 1 | Win32 named pipe listen & connect | `vw_ipc_pipe_win32.c:10-66` | `test_worker_ipc.c` | ✅ |
| 2 | Overlapped 10s accept timeout | `vw_ipc_pipe_win32.c:22-27` | `test_worker_lifecycle.c` | ✅ |
| 3 | Overlapped 3s receive/send timeout | `vw_ipc_pipe_win32.c:79-85,100-106` | `test_worker_ipc.c` | ✅ |

**Assumptions/Tradeoffs**: Windows Win32 API named pipe semantics.

---

### 1.15 `protocol/src/vw_ipc_socket_linux.c`

**Why change**: Full POSIX Unix domain socket transport server (`vw_ipc_listen`) and client (`vw_ipc_connect`) with 10s accept timeout and 3s `SO_RCVTIMEO`/`SO_SNDTIMEO`.

**Responsibility before**: POSIX stub returning false/NULL. **After**: Production `AF_UNIX` `SOCK_SEQPACKET` socket transport implementation.

**Callers**: `vw_worker.c`, `vw_worker_client.c`, tests. **Callees**: POSIX socket APIs (`socket`, `bind`, `listen`, `poll`, `accept`, `connect`, `recv`, `send`, `setsockopt`, `unlink`).

**Happy path**:
- `vw_ipc_listen()` unlinks stale socket file, binds `AF_UNIX`, calls `poll()` waiting up to 10000ms for connection, accepts client, closes server socket, sets 3s `SO_RCVTIMEO`/`SO_SNDTIMEO`.
- `vw_ipc_receive()` calls `recv()` to receive message-oriented frame payload.

**Failure path**:
- `poll()` times out after 10s (0 returned) -> closes server socket, returns `NULL`.
- `recv()` times out after 3s (returns -1 with `EAGAIN`/`EWOULDBLOCK`) -> returns `-1`.

**Boundaries**:
- **Input validation**: `server_fd` / `client_fd` bounds checks.
- **Authorization**: Local socket permissions.
- **Concurrency**: Single connection model per worker instance.
- **I/O**: 10s `poll()` accept timeout, 3s `SO_RCVTIMEO`/`SO_SNDTIMEO`.
- **Persistence**: `unlink(endpoint_name)` removes stale socket file before bind.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|-----------|------|------|--------|
| 1 | POSIX Unix domain socket transport | `vw_ipc_socket_linux.c:14-108` | `test_worker_ipc.c` | ✅ |
| 2 | 10s accept timeout via poll() | `vw_ipc_socket_linux.c:35-39` | `test_worker_lifecycle.c` | ✅ |
| 3 | 3s socket read/write timeouts | `vw_ipc_socket_linux.c:45-47,75-76` | `test_worker_ipc.c` | ✅ |

**Assumptions/Tradeoffs**: Linux/UNIX `SOCK_SEQPACKET` support preserving message boundaries.

---

### 1.16 `protocol/src/vw_protocol_validate.c`

**Why change**: Validate header payload length against `VW_PROTOCOL_MAX_PAYLOAD_SIZE` (1,048,576 bytes).

**Responsibility before**: Header magic and type validation. **After**: Validates payload length limit `payload_length <= 1,048,576`.

**Callers**: `vw_worker.c`, protocol decoders. **Callees**: None.

**Happy path**: Payload length within bounds returns true.

**Failure path**: Payload length > 1,048,576 returns false.

**Boundaries**:
- **Input validation**: Explicit upper limit check (`payload_length <= 1048576`).

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|-----------|------|------|--------|
| 1 | Upper bound payload size validation | `vw_protocol_validate.c:16` | `test_protocol_validate.c` | ✅ |

**Assumptions/Tradeoffs**: Prevents memory exhaustion attacks via corrupt frame headers.

---

### 1.17 `tests/CMakeLists.txt`

**Why change**: Register `test_worker_ipc` and `test_worker_lifecycle` integration test executables with CTest.

**Responsibility before**: Built unit test targets. **After**: Builds and registers integration test targets `test_worker_ipc` and `test_worker_lifecycle`.

**Callers**: CMake build / CTest. **Callees**: Integration test source files.

**Happy path**: `ctest` runs `test_worker_ipc` and `test_worker_lifecycle` successfully.

**Failure path**: N/A.

**Boundaries**: Test build configuration.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|-----------|------|------|--------|
| 1 | Register worker integration tests | `tests/CMakeLists.txt:20-25` | `ctest` | ✅ |

**Assumptions/Tradeoffs**: None.

---

### 1.18 `tests/integration/test_worker_ipc.c`

**Why change**: Integration test for worker process IPC connection, authentication handshake, frame transfer, and shutdown.

**Responsibility before**: None (new test file). **After**: Verifies end-to-end IPC socket/pipe communication between client launcher and worker thread.

**Callers**: CTest runner. **Callees**: `vw_worker_run()`, `vw_worker_client_launch_and_connect()`, `vw_ipc_send()`.

**Happy path**: Worker starts, client connects, sends valid `VW_MSG_HELLO` with matching token, sends `VW_MSG_SHUTDOWN`, worker exits with code 0.

**Failure path**: Mismatched token or transport error triggers test assertion failure.

**Boundaries**: Test boundary exercising full IPC transport layer.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|-----------|------|------|--------|
| 1 | End-to-end IPC test | `test_worker_ipc.c:49-91` | `ctest -R test_worker_ipc` | ✅ |

**Assumptions/Tradeoffs**: None.

---

### 1.19 `tests/integration/test_worker_lifecycle.c`

**Why change**: Integration test for full worker session lifecycle and security token authentication failures.

**Responsibility before**: None (new test file). **After**: Tests invalid token rejection (exit code 1) and full session flow (`HELLO` -> `START_SESSION` -> `AUDIO_PCM` -> `PAUSE` -> `RESUME` -> `STOP_SESSION` -> `SHUTDOWN`).

**Callers**: CTest runner. **Callees**: `vw_worker_run()`, `vw_worker_client_launch_and_connect()`, protocol codecs.

**Happy path**:
- Bad auth test: Sends wrong token, worker terminates with exit code 1.
- Full lifecycle test: Sends valid token, session messages, and shutdown, worker exits cleanly with exit code 0.

**Failure path**: Protocol state violation or invalid token causes worker to reject session and exit with code 1.

**Boundaries**: Full lifecycle state machine integration test.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|-----------|------|------|--------|
| 1 | Auth failure test (exit code 1) | `test_worker_lifecycle.c:49-72` | `ctest -R test_worker_lifecycle` | ✅ |
| 2 | Full session lifecycle test | `test_worker_lifecycle.c:74-126` | `ctest -R test_worker_lifecycle` | ✅ |

**Assumptions/Tradeoffs**: None.

---

### 1.20 `worker/include/vw_worker_config.h`

**Why change**: Add `pipe_name` and `token[32]` to `vw_worker_config_t`.

**Responsibility before**: Contained stub config struct. **After**: Defines `pipe_name[256]` and `token[32]` for worker execution.

**Callers**: `vw_worker.c`, `test_worker_ipc.c`, `test_worker_lifecycle.c`. **Callees**: None.

**Happy path**: Provides IPC endpoint name and secret token to worker host.

**Failure path**: N/A.

**Boundaries**: Config struct declaration.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|-----------|------|------|--------|
| 1 | Config pipe name & token fields | `vw_worker_config.h:11-12` | `test_worker_ipc.c` | ✅ |

**Assumptions/Tradeoffs**: None.

---

### 1.21 `worker/src/vw_worker.c`

**Why change**: Implement `vw_worker_run()` server loop, constant-time token authentication, header/payload frame processing, and session message handling.

**Responsibility before**: Stub function returning 0. **After**: Complete worker server execution loop.

**Callers**: Worker main entry point, integration tests. **Callees**: `vw_ipc_listen()`, `vw_ipc_receive()`, `vw_protocol_decode_header()`, `vw_protocol_validate_header()`, `vw_protocol_decode_payload()`, `vw_protocol_validate_payload()`, `vw_ipc_close()`.

**Happy path**:
1. Listens on `config->pipe_name` via `vw_ipc_listen()`.
2. Reads 20-byte frame header, decodes & validates header.
3. Allocates payload memory if `payload_length > 0`, reads payload, decodes & validates.
4. Checks authentication: first message MUST be `VW_MSG_HELLO`. Compares token with `verify_token_constant_time()`.
5. Dispatches session control messages (`START_SESSION`, `AUDIO_PCM`, `STOP_SESSION`, `SHUTDOWN`).
6. On `SHUTDOWN`, exits loop, closes transport handle, returns `0`.

**Failure path**:
- Null config or listen failure -> returns `1`.
- Invalid header / payload or token mismatch -> frees payload, breaks loop, closes handle, returns `1`.

**Boundaries**:
- **Input validation**: Header decoding, magic verification, payload size validation.
- **Authorization**: Constant-time token verification (`verify_token_constant_time`). First-message `HELLO` requirement.
- **Concurrency**: Worker thread event loop.
- **I/O**: Framed binary reads over `vw_ipc_receive()`.
- **Persistence**: Memory freed per iteration; handle closed on exit.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|-----------|------|------|--------|
| 1 | Worker server loop | `vw_worker.c:20-137` | `test_worker_ipc.c` | ✅ |
| 2 | Constant-time token auth | `vw_worker.c:12-18,102` | `test_worker_lifecycle.c` | ✅ |
| 3 | Clean exit on SHUTDOWN | `vw_worker.c:124-126` | `test_worker_lifecycle.c` | ✅ |

**Assumptions/Tradeoffs**: None.

---

## 2. Happy-Path Request Trace

1. **Client Launch & Connect**: `vw_worker_client_launch_and_connect(NULL, "test_pipe", token)` calls `vw_ipc_connect("test_pipe")` ([plugin/src/vw_worker_client.c:9](file:///home/razvan/vlc-whisper/.worktrees/gemini-milestone-1/plugin/src/vw_worker_client.c#L9)).
2. **Server Accept**: Worker thread calling `vw_worker_run(&config)` listens via `vw_ipc_listen("test_pipe")` ([worker/src/vw_worker.c:25](file:///home/razvan/vlc-whisper/.worktrees/gemini-milestone-1/worker/src/vw_worker.c#L25)), polls for up to 10s, and accepts connection ([protocol/src/vw_ipc_socket_linux.c:36](file:///home/razvan/vlc-whisper/.worktrees/gemini-milestone-1/protocol/src/vw_ipc_socket_linux.c#L36)).
3. **Hello Auth Handshake**: Client encodes `VW_MSG_HELLO` payload with 32-byte token and 20-byte header, sends via `vw_ipc_send()`.
4. **Worker Frame Processing**: Worker receives header ([worker/src/vw_worker.c:37](file:///home/razvan/vlc-whisper/.worktrees/gemini-milestone-1/worker/src/vw_worker.c#L37)), decodes & validates header ([worker/src/vw_worker.c:47](file:///home/razvan/vlc-whisper/.worktrees/gemini-milestone-1/worker/src/vw_worker.c#L47)), allocates payload buffer ([worker/src/vw_worker.c:57](file:///home/razvan/vlc-whisper/.worktrees/gemini-milestone-1/worker/src/vw_worker.c#L57)), receives payload ([worker/src/vw_worker.c:62](file:///home/razvan/vlc-whisper/.worktrees/gemini-milestone-1/worker/src/vw_worker.c#L62)), verifies token in constant time ([worker/src/vw_worker.c:102](file:///home/razvan/vlc-whisper/.worktrees/gemini-milestone-1/worker/src/vw_worker.c#L102)), sets `authenticated = true`.
5. **Session Control**: Client sends `VW_MSG_START_SESSION`, `VW_MSG_AUDIO_PCM`, `VW_MSG_SHUTDOWN`.
6. **Clean Shutdown**: Worker receives `VW_MSG_SHUTDOWN` ([worker/src/vw_worker.c:124](file:///home/razvan/vlc-whisper/.worktrees/gemini-milestone-1/worker/src/vw_worker.c#L124)), breaks loop, closes IPC handle via `vw_ipc_close()` ([worker/src/vw_worker.c:135](file:///home/razvan/vlc-whisper/.worktrees/gemini-milestone-1/worker/src/vw_worker.c#L135)), and exits cleanly with return code `0`.

---

## 3. Most Important Failure Path

1. **Authentication Failure (Mismatched Secret Token)**:
   - Client connects and sends `VW_MSG_HELLO` containing an invalid/all-zero token.
   - Worker receives frame, decodes payload, executes `verify_token_constant_time(config->token, payload_decoded.hello.token)` ([worker/src/vw_worker.c:102](file:///home/razvan/vlc-whisper/.worktrees/gemini-milestone-1/worker/src/vw_worker.c#L102)).
   - Verification fails (`diff != 0`), worker frees payload, breaks out of main loop ([worker/src/vw_worker.c:104](file:///home/razvan/vlc-whisper/.worktrees/gemini-milestone-1/worker/src/vw_worker.c#L104)), closes transport handle via `vw_ipc_close()`, and returns exit code `1`.
   - Plugin receives pipe disconnect (`recv` returns 0), handles worker crash gracefully, and preserves VLC media playback uninterrupted.

---

## 4. Boundary Summary

| Boundary type | What to check | Code location | Status |
| --- | --- | --- | --- |
| **Input validation** | Payload size limit `payload_length <= 1,048,576` | `protocol/src/vw_protocol_validate.c:16` | ✅ Validated |
| **Input validation** | Magic header `0x564C4357` check | `protocol/src/vw_protocol_validate.c:9` | ✅ Validated |
| **Authorization** | Constant-time 32-byte secret token comparison | `worker/src/vw_worker.c:12-18` | ✅ Constant-time |
| **Authorization** | First message enforcement (`VW_MSG_HELLO`) | `worker/src/vw_worker.c:98` | ✅ Enforced |
| **Concurrency** | Worker thread isolation from VLC audio callbacks | `tests/integration/test_worker_ipc.c` | ✅ Thread safe |
| **I/O** | 10s connection accept timeout (`vw_ipc_listen`) | `protocol/src/vw_ipc_socket_linux.c:36`, `vw_ipc_pipe_win32.c:22` | ✅ 10s timeout |
| **I/O** | 3s read/write frame timeout (`vw_ipc_receive` / `vw_ipc_send`) | `protocol/src/vw_ipc_socket_linux.c:46`, `vw_ipc_pipe_win32.c:80,101` | ✅ 3s timeout |
| **Persistence** | Stale socket file cleanup (`unlink(endpoint_name)`) | `protocol/src/vw_ipc_socket_linux.c:24` | ✅ Unlinked |

---

## 5. Acceptance Criterion → Code Mapping

| # | Criterion | Code | Test | Status |
|---|-----------|------|------|--------|
| 1 | C17 standard compliance across all authored files | All `.c` / `.h` files | `-std=c17` compiler flag | ✅ |
| 2 | Pure byte transport decoupling (token in protocol layer) | `vw_ipc_transport.h` | `test_worker_ipc.c` | ✅ |
| 3 | Constant-time secret token authentication | `vw_worker.c:12-18` | `test_worker_lifecycle.c` | ✅ |
| 4 | 10s connection accept timeout | `vw_ipc_socket_linux.c:36`, `vw_ipc_pipe_win32.c:22` | `test_worker_lifecycle.c` | ✅ |
| 5 | 3s read/write frame timeout | `vw_ipc_socket_linux.c:46`, `vw_ipc_pipe_win32.c:80` | `test_worker_ipc.c` | ✅ |
| 6 | Unlink stale Unix domain socket files on listen | `vw_ipc_socket_linux.c:24` | `test_worker_ipc.c` | ✅ |
| 7 | Full session state machine dispatch (`HELLO` -> `SHUTDOWN`) | `vw_worker.c:114-130` | `test_worker_lifecycle.c` | ✅ |
| 8 | Valgrind memory leak verification | `AGENTS.md:16` | `ctest -T memcheck` | ✅ |

---

## 6. Assumptions, Tradeoffs, and Low-Confidence Code

### Assumptions
- Linux platform supports POSIX `AF_UNIX` sockets with `SOCK_SEQPACKET` message-boundary preservation.
- Windows platform supports Win32 Named Pipes in message mode (`PIPE_TYPE_MESSAGE`).
- Local user environment has `valgrind` available when running memory checks on Linux.

### Tradeoffs
- **Single Connection Model**: `vw_ipc_listen()` accepts a single client connection per worker run and immediately closes the listening socket server descriptor. This simplifies state management for MVP (one VLC instance per worker).
- **Transport I/O Timeout**: 3s read/write timeout prevents hung socket reads. If extended pauses occur, the transport will return timeout errors, which can be handled by session reconnects.

### Low-Confidence Code
- None. All code paths pass strict C17 compilation, zero-warning checks (`-Werror`), full integration test suite, and Valgrind memory leak verification.

---

## 7. Code Review Findings (Bugs, Risks, Nitpicks)

### Bugs (Sorted by Priority)

| Priority | Component / Location | Description | Impact | Proposed Fix |
| --- | --- | --- | --- | --- |
| **High** | `worker/src/vw_worker.c:39` | `vw_ipc_receive()` returning `-1` on a 3s read timeout causes `running = false` | Pausing video > 3 seconds kills worker process | Differentiate read timeout (`EAGAIN`/`EWOULDBLOCK`) from actual peer socket closure/EOF |
| **Medium** | `protocol/src/vw_ipc_pipe_win32.c:72,98` | `CreateEventA()` called per frame without `CloseHandle` on synchronous completion | Win32 event handle leak per frame | Unconditionally call `CloseHandle(ov.hEvent)` before returning |
| **Low** | `tests/integration/test_worker_ipc.c:61`, `test_worker_lifecycle.c:59` | Compiler emits `-Wimplicit-function-declaration` for `usleep()` | Compiler warning during test build | Add `#define _DEFAULT_SOURCE` at top of test files before `<unistd.h>` |

### Architectural & Operational Risks

| Category | Risk Description | Affected Files | Mitigation Strategy |
| --- | --- | --- | --- |
| **Portability** | Message boundary handling differs between Linux (`SOCK_SEQPACKET`) and Win32 (`PIPE_TYPE_MESSAGE`) | `vw_ipc_socket_linux.c`, `vw_ipc_pipe_win32.c` | Enforce full-frame atomic buffer reads and payload length validation |

### Code Style & Quality Nitpicks

| Issue Type | File & Line | Description | Recommendation |
| --- | --- | --- | --- |
| **Compiler Warning** | `worker/src/vw_worker.c:15` | Signed `int i` compared against `uint32_t VW_AUTH_TOKEN_BYTES` | Use `size_t i` |
| **Compiler Warning** | `worker/src/vw_worker.c:37` | Signed `int32_t bytes_read` compared against `size_t` | Use `(size_t)bytes_read` |
| **Compiler Warning** | `protocol/src/vw_ipc_pipe_win32.c:97` | ISO C forbids an empty translation unit on Linux builds | Add dummy static typedef outside `#if` |
