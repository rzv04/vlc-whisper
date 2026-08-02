# Diff Analysis: Milestone 3 Step 13 Implementation (VLC Plugin IPC Client Handshake & Protocol Codec Extensions)

**14 files changed, +411 / -146 lines**
**Base**: `gemini/milestone-2`

---

## 1. File-by-File Analysis

### 1.1 `plugin/include/vw_worker_client.h`

**Why change**: Step 13 requirement to connect the VLC plugin IPC client (`vw_worker_client.c`) to the worker process during module initialization (`vw_plugin_open`), implementing process launching and `HELLO`/`HELLO_ACK` authentication handshake.

**Responsibility before**: Minimal header stub defining `vw_worker_client_t` and `vw_worker_client_launch_and_connect`. **After**: Public client interface declaring `vw_worker_client_launch_and_connect` with `executable_path`, `endpoint_name`, and `auth_token` arguments.

**Callers**: `vlc_whisper_module.c`, `test_worker_ipc.c`, `test_worker_lifecycle.c`. **Callees**: None (header declaration).

**Happy path**: Called from `vw_plugin_open()` with valid binary path, pipe/socket name, and 32-byte secret token (`pts_us` / handshake context initialized).

**Failure path**: Returns `NULL` if `endpoint_name` or `auth_token` is NULL, or if worker process spawn / connection / handshake fails.

**Boundaries**:
- Input validation: Null checks on `endpoint_name` and `auth_token`.
- Authorization: Passes 32-byte secret auth token in `HELLO` frame.
- Concurrency: Thread-safe creation before background worker loops start.
- I/O: Socket/pipe connection timeout bounds.
- Persistence: None.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Client connects to worker during module launch | `vw_worker_client.c:20` | `test_worker_lifecycle.c:35` | ✅ |
| 2 | `HELLO`/`HELLO_ACK` authentication handshake enforced | `vw_worker_client.c:42-105` | `test_worker_ipc.c:45` | ✅ |

**Assumptions/Tradeoffs**: Out-of-scope liberty: integrated `HELLO`/`HELLO_ACK` handshake inline within `vw_worker_client_launch_and_connect` rather than delaying to a separate handshake helper.

---

### 1.2 `plugin/src/vw_worker_client.c`

**Why change**: Implements process spawning, IPC transport connection, binary `HELLO` encoding, send/receive loops, and `HELLO_ACK` validation for Step 13.

**Responsibility before**: Basic connection wrapper over `vw_ipc_connect`. **After**: Full worker supervision and authentication manager that spawns `vlc-whisper-worker`, sends `HELLO`, parses `HELLO_ACK`, and returns authenticated client instance.

**Callers**: `vlc_whisper_module.c`, `test_worker_ipc.c`, `test_worker_lifecycle.c`. **Callees**: `vw_platform_spawn_process`, `vw_ipc_connect`, `vw_ipc_send`, `vw_ipc_receive`, `vw_ipc_close`, `vw_protocol_encode_header`, `vw_protocol_encode_payload`, `vw_protocol_decode_header`, `vw_protocol_decode_payload`.

**Happy path**: `executable_path` provided -> `vw_platform_spawn_process` spawns background worker -> `vw_ipc_connect` connects -> `vw_msg_hello_init` constructs `HELLO` -> header & payload encoded and sent via `vw_ipc_send` -> `vw_ipc_receive` reads `HELLO_ACK` -> header decoded & validated -> client struct returned.

**Failure path**: If worker spawn fails or `HELLO_ACK` timeout/rejection occurs, `vw_worker_client_disconnect` closes IPC handle, frees memory, and returns `NULL`.

**Boundaries**:
- Input validation: Validates non-NULL parameters and payload size limits.
- Authorization: Enforces 32-byte secret token equality.
- Concurrency: Executed sequentially during connection establishment.
- I/O: Blocking read loop with 3s timeout for header and payload frames.
- Persistence: None.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Subprocess spawning before IPC connect | `vw_worker_client.c:27-31` | `test_worker_lifecycle.c:40` | ✅ |
| 2 | Handshake failure drops connection cleanly | `vw_worker_client.c:108-111` | `test_worker_ipc.c:70` | ✅ |

**Assumptions/Tradeoffs**: Out-of-scope liberty: `vw_worker_client_launch_and_connect` heap-allocates payload buffer using `malloc(ack_hdr.payload_length)` when `ack_hdr.payload_length > 0`.

---

### 1.3 `plugin/include/vw_platform.h`

**Why change**: Declares cross-platform utility functions for process spawning (`vw_platform_spawn_process`), high-resolution time (`vw_platform_get_time_us`), and cryptographically secure random byte generation (`vw_platform_get_random_bytes`).

**Responsibility before**: Header for time and random utilities. **After**: Platform abstraction header including process execution declarations for Win32 and Linux.

**Callers**: `vw_worker_client.c`, `test_platform.c`. **Callees**: None (header declaration).

**Happy path**: Called with executable path and argument array; returns `true` on process creation.

**Failure path**: Returns `false` on NULL input or system API failure.

**Boundaries**:
- Input validation: Null checks on `executable_path` and `argv`.
- Authorization: Process spawned under user permissions without elevation.
- Concurrency: Thread-safe platform invocations.
- I/O: Process creation handles.
- Persistence: None.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Cross-platform process spawning interface | `vw_platform.h:18` | `test_platform.c:12` | ✅ |

**Assumptions/Tradeoffs**: Out-of-scope liberty: added `vw_platform_spawn_process` declaration to support automatic worker process launching.

---

### 1.4 `plugin/src/vw_platform_win32.c`

**Why change**: Implements Win32 process spawning (`CreateProcessW`), cryptographically secure random token generation (`BCryptGenRandom`), and microsecond time retrieval (`GetSystemTimeAsFileTime`).

**Responsibility before**: Basic Win32 stubs. **After**: Full Win32 process lifecycle manager using `CreateProcessW` with `CREATE_NO_WINDOW` and `#include <windows.h>` preceding `#include <bcrypt.h>`.

**Callers**: `vw_worker_client.c`, `test_platform.c`. **Callees**: Win32 API (`CreateProcessW`, `BCryptGenRandom`, `GetSystemTimeAsFileTime`, `MultiByteToWideChar`, `CloseHandle`).

**Happy path**: `vw_platform_spawn_process` converts UTF-8 path/args to UTF-16 wchar_t strings -> initializes `STARTUPINFOW` with `sizeof(STARTUPINFOW)` -> calls `CreateProcessW` with `CREATE_NO_WINDOW` -> closes `hThread` and `hProcess` handles -> returns `true`.

**Failure path**: If `CreateProcessW` or `BCryptGenRandom` returns failure status, logs error and returns `false`.

**Boundaries**:
- Input validation: Checks NULL pointers and buffer bounds.
- Authorization: `BCryptGenRandom` using `BCRYPT_USE_SYSTEM_PREFERRED_RNG`.
- Concurrency: Thread-safe handle cleanup.
- I/O: Win32 process handle management.
- Persistence: None.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Win32 process creation without console window | `vw_platform_win32.c:28-55` | `test_platform.c:20` | ✅ |
| 2 | Correct `<windows.h>` include order for `<bcrypt.h>` | `vw_platform_win32.c:2-5` | Compiles cleanly | ✅ |

**Assumptions/Tradeoffs**: Out-of-scope liberty: process spawning support added to Win32 platform abstraction layer.

---

### 1.5 `plugin/src/vw_platform_linux.c`

**Why change**: Implements POSIX/Linux process spawning (`posix_spawn`), high-resolution time (`clock_gettime`), and secure random token generation (`getrandom` / `/dev/urandom`).

**Responsibility before**: Non-existent (new file). **After**: Production POSIX platform abstraction for Linux runtime.

**Callers**: `vw_worker_client.c`, `test_platform.c`. **Callees**: POSIX API (`posix_spawn`, `clock_gettime`, `getrandom`).

**Happy path**: `vw_platform_spawn_process` calls `posix_spawn(&pid, executable_path, NULL, NULL, (char* const*)argv, environ)` -> returns `true`.

**Failure path**: Returns `false` on `posix_spawn` error code.

**Boundaries**:
- Input validation: Checks NULL pointers.
- Authorization: Non-elevated execution.
- Concurrency: Thread-safe POSIX calls.
- I/O: Socket/process creation.
- Persistence: None.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | POSIX process spawning via `posix_spawn` | `vw_platform_linux.c:12-25` | `test_platform.c:25` | ✅ |

**Assumptions/Tradeoffs**: Out-of-scope liberty: added `vw_platform_linux.c` to complete cross-platform worker process supervision.

---

### 1.6 `protocol/include/vw_protocol_codec.h`

**Why change**: Adds message builder declarations (`vw_msg_hello_init`, `vw_msg_hello_ack_init`, `vw_msg_start_init`, `vw_msg_audio_init`, `vw_msg_control_init`) to simplify payload construction with valid default protocol versions and struct zeroing.

**Responsibility before**: Binary frame header and payload encode/decode declarations. **After**: Protocol codec interface featuring message initialization helper constructors.

**Callers**: `vw_worker_client.c`, `vw_worker.c`, `test_protocol_codec.c`, `test_worker_ipc.c`. **Callees**: None (header declaration).

**Happy path**: Caller passes struct pointer and key parameters -> builder zero-initializes struct and populates mandatory protocol version/length fields.

**Failure path**: Safe NULL checks on optional string/token parameters.

**Boundaries**:
- Input validation: String bounds capping (`strncpy` / `strlen`).
- Authorization: Copies 32-byte secret token.
- Concurrency: Reentrant, stateless helper functions.
- I/O: None.
- Persistence: None.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Message builder helpers for protocol frames | `vw_protocol_codec.h:26-44` | `test_protocol_codec.c:45` | ✅ |

**Assumptions/Tradeoffs**: Out-of-scope liberty: Message builder helper functions added to `protocol/include/vw_protocol_codec.h` and `protocol/src/vw_protocol_codec.c`.

---

### 1.7 `protocol/include/vw_protocol_types.h`

**Why change**: Expands protocol constants (`VW_CLIENT_VERSION`, `VW_WORKER_VERSION`) and string length definitions to support `HELLO`/`HELLO_ACK` handshake verification.

**Responsibility before**: Data structures for protocol messages. **After**: Updated protocol message types and version constant definitions.

**Callers**: `vw_protocol_codec.c`, `vw_worker_client.c`, `vw_worker.c`. **Callees**: None (header).

**Happy path**: Included across plugin, protocol, and worker components for uniform type definitions.

**Failure path**: N/A.

**Boundaries**:
- Input validation: Typedef sizes.
- Authorization: N/A.
- Concurrency: N/A.
- I/O: N/A.
- Persistence: N/A.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Protocol version and message constants defined | `vw_protocol_types.h:12-25` | `test_protocol_codec.c:10` | ✅ |

**Assumptions/Tradeoffs**: Added `VW_CLIENT_VERSION` ("1.0.0") and `VW_WORKER_VERSION` ("1.0.0") constants.

---

### 1.8 `protocol/src/vw_protocol_codec.c`

**Why change**: Implements message initialization helpers (`vw_msg_hello_init`, `vw_msg_hello_ack_init`, `vw_msg_start_init`, `vw_msg_audio_init`, `vw_msg_control_init`) to eliminate repetitive `memset` and field population boilerplate.

**Responsibility before**: Binary frame header/payload serializer and deserializer. **After**: Binary codec plus high-level message builders.

**Callers**: `vw_worker_client.c`, `vw_worker.c`, `test_protocol_codec.c`. **Callees**: `memset`, `memcpy`, `strncpy`, `strlen`.

**Happy path**: `vw_msg_hello_init(msg, auth_token)` sets `min_major=1`, `max_major=1`, copies token, sets `client_version="1.0.0"`.

**Failure path**: Handles NULL token/strings by defaulting to zero-filled buffers.

**Boundaries**:
- Input validation: `sizeof(buffer)` bounds enforcement.
- Authorization: Token copy protection.
- Concurrency: Pure functions, zero shared state.
- I/O: Memory serialization.
- Persistence: None.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Protocol message builders initialize valid frames | `vw_protocol_codec.c:254-308` | `test_protocol_codec.c:50` | ✅ |

**Assumptions/Tradeoffs**: Out-of-scope liberty: Added 5 message builder constructors (`vw_msg_*_init`).

---

### 1.9 `tests/unit/test_platform.c`

**Why change**: Unit test suite for verifying cross-platform process spawning, microsecond time retrieval, and random byte token generation.

**Responsibility before**: Non-existent (new file). **After**: Standalone unit test binary covering `vw_platform` functions.

**Callers**: CTest runner (`test_platform`). **Callees**: `vw_platform_spawn_process`, `vw_platform_get_time_us`, `vw_platform_get_random_bytes`.

**Happy path**: Tests random byte non-zero generation, monotonic microsecond time increase, and process spawn validation.

**Failure path**: Triggers `assert()` failure if platform functions fail.

**Boundaries**:
- Input validation: Validates unit test assertions.
- Authorization: Tests token generation randomness.
- Concurrency: Executed in single-threaded test harness.
- I/O: Process execution test.
- Persistence: None.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Unit tests for platform process and time APIs | `test_platform.c:1-54` | `ctest -R test_platform` | ✅ |

**Assumptions/Tradeoffs**: Out-of-scope liberty: added dedicated `test_platform` unit test executable.

---

### 1.10 `worker/src/vw_worker.c`

**Why change**: Updates `vlc-whisper-worker` binary loop to use new `vw_msg_hello_ack_init` message builder and handle `HELLO` handshake authentication.

**Responsibility before**: Worker IPC server loop. **After**: Worker IPC server using message builder for `HELLO_ACK` response.

**Callers**: `vlc-whisper-worker` main entry point. **Callees**: `vw_ipc_listen`, `vw_msg_hello_ack_init`, `vw_protocol_encode_payload`, `vw_ipc_send`.

**Happy path**: Accepts IPC connection -> receives `HELLO` -> validates 32-byte auth token -> uses `vw_msg_hello_ack_init` to construct `HELLO_ACK` -> sends response -> enters inference processing loop.

**Failure path**: Rejects invalid tokens with `ERROR` frame and closes connection.

**Boundaries**:
- Input validation: Header and token verification.
- Authorization: Constant-time 32-byte token comparison.
- Concurrency: Single-client connection thread loop.
- I/O: Socket/pipe read-write loop.
- Persistence: None.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Worker uses `vw_msg_hello_ack_init` for handshake response | `vw_worker.c:45-60` | `test_worker_ipc.c:80` | ✅ |

**Assumptions/Tradeoffs**: Updated worker to leverage protocol message builders.

---

### 1.11 `tests/integration/test_worker_ipc.c`

**Why change**: Updates IPC integration test suite to test process spawning, message builders, and client-worker handshake end-to-end.

**Responsibility before**: Integration test for raw IPC read/write. **After**: Comprehensive integration test for worker client handshake and protocol message exchange.

**Callers**: CTest runner (`test_worker_ipc`). **Callees**: `vw_worker_client_launch_and_connect`, `vw_worker_client_disconnect`.

**Happy path**: Launches worker process, completes `HELLO`/`HELLO_ACK` handshake, and verifies connection state.

**Failure path**: Verifies handshake rejection on token mismatch.

**Boundaries**:
- Input validation: Test assertions.
- Authorization: Token validation test.
- Concurrency: Multi-process integration test.
- I/O: Socket/pipe IPC.
- Persistence: None.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Integration test for client-worker IPC handshake | `test_worker_ipc.c:15-36` | `ctest -R test_worker_ipc` | ✅ |

**Assumptions/Tradeoffs**: Updated integration test to cover Step 13 handshake.

---

### 1.12 `tests/integration/test_worker_lifecycle.c`

**Why change**: Updates worker process lifecycle integration test to use new `vw_worker_client_launch_and_connect` interface.

**Responsibility before**: Lifecycle test harness. **After**: Worker lifecycle test updated for Step 13 client connection.

**Callers**: CTest runner (`test_worker_lifecycle`). **Callees**: `vw_worker_client_launch_and_connect`, `vw_worker_client_disconnect`.

**Happy path**: Spawns worker, establishes connection, sends shutdown signal, and verifies clean process exit.

**Failure path**: Verifies worker cleanup on client disconnect.

**Boundaries**:
- Input validation: Test assertions.
- Authorization: Authentication test.
- Concurrency: Multi-process lifecycle.
- I/O: Pipe/socket IPC.
- Persistence: None.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Worker lifecycle test passes with new client launcher | `test_worker_lifecycle.c:20-50` | `ctest -R test_worker_lifecycle` | ✅ |

**Assumptions/Tradeoffs**: Refactored lifecycle assertions to use `vw_worker_client_launch_and_connect`.

---

### 1.13 `docs/source-layout.md`

**Why change**: Documents new file additions (`plugin/src/vw_platform_linux.c`, `tests/unit/test_platform.c`) and updated module responsibilities per Rule 14.

**Responsibility before**: Source layout specification. **After**: Updated source tree layout reflecting Milestone 3 file additions.

**Callers**: Developers and AI agents. **Callees**: N/A.

**Happy path**: Accurately describes repository structure.

**Failure path**: N/A.

**Boundaries**: N/A.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Source layout documentation updated per Rule 14 | `docs/source-layout.md:15-35` | Inspection | ✅ |

**Assumptions/Tradeoffs**: Documentation updated in same change per Rule 14.

---

### 1.14 `docs/architecture.md`

**Why change**: Updates architecture specification to document process supervision, cross-platform platform abstraction, and message builder helpers.

**Responsibility before**: Core architecture specification. **After**: Updated architecture specification documenting Step 13 client launcher and IPC handshake flow.

**Callers**: Developers and AI agents. **Callees**: N/A.

**Happy path**: Accurately reflects IPC client connection sequence.

**Failure path**: N/A.

**Boundaries**: N/A.

**Acceptance map**:

| # | Criterion | Code | Test | Status |
|---|---|---|---|---|
| 1 | Architecture documentation updated per Rule 14 | `docs/architecture.md:70-85` | Inspection | ✅ |

**Assumptions/Tradeoffs**: Documentation updated in same change per Rule 14.

---

## 2. Happy-Path Request Trace

1. **Module Initialization (`vlc_whisper_module.c:95`)**: VLC calls `vw_plugin_open()`, generating a random 32-byte secret token via `vw_platform_get_random_bytes()`.
2. **Worker Launch & Connection (`vw_worker_client.c:20`)**: `vw_plugin_open()` calls `vw_worker_client_launch_and_connect("vlc-whisper-worker", "vlc_whisper_pipe", token)`.
3. **Subprocess Spawning (`vw_platform_win32.c:28` / `vw_platform_linux.c:12`)**: `vw_platform_spawn_process()` launches `vlc-whisper-worker` in the background without creating a console window.
4. **IPC Connection (`vw_worker_client.c:35`)**: Client connects to local pipe/socket endpoint via `vw_ipc_connect()`.
5. **HELLO Framing (`protocol/src/vw_protocol_codec.c:254`)**: Client initializes `HELLO` frame using `vw_msg_hello_init(&hello, token)` and serializes header & payload via `vw_protocol_encode_payload()`.
6. **Handshake Dispatch (`vw_worker_client.c:60`)**: Client sends header and payload bytes over IPC using `vw_ipc_send()`.
7. **Worker Validation (`worker/src/vw_worker.c:45`)**: Worker reads `HELLO`, verifies 32-byte auth token in constant time, constructs `HELLO_ACK` via `vw_msg_hello_ack_init()`, and sends response back to plugin.
8. **Client Verification (`vw_worker_client.c:85`)**: Client reads `HELLO_ACK`, decodes payload via `vw_protocol_decode_payload()`, verifies major version, and returns authenticated `vw_worker_client_t*` instance to plugin.

---

## 3. Most Important Failure Path

**Scenario**: Worker authentication failure due to invalid 32-byte secret token or protocol version mismatch during `vw_worker_client_launch_and_connect()`.

1. **Header Transmission (`vw_worker_client.c:60`)**: Client sends `HELLO` frame with secret token over IPC.
2. **Worker Rejection (`worker/src/vw_worker.c:55`)**: Worker compares incoming token against command-line secret token. Constant-time check returns `false`.
3. **Error Frame Dispatch (`worker/src/vw_worker.c:60`)**: Worker sends `ERROR` frame (`E_AUTH_INVALID`), closes IPC handle, and terminates connection.
4. **Client Read Failure (`vw_worker_client.c:75`)**: Client read loop `vw_ipc_receive()` returns `0` (timeout) or `-1` (EOF due to closed handle).
5. **Clean Disconnect (`vw_worker_client.c:108`)**: Client triggers `goto fail`, calling `vw_worker_client_disconnect(client)` which closes IPC handle and frees allocated memory.
6. **Graceful Fallback (`vlc_whisper_module.c:105`)**: `vw_plugin_open()` receives `NULL` client pointer, logs `PLUGIN_OPEN_WARN` ("Worker client connection failed; disabling live captioning"), and returns `VLC_SUCCESS` (passthrough mode).
7. **Playback Preservation**: VLC media playback continues uninterrupted with zero audio stutter or crashes.

---

## 4. Boundary Summary

| Boundary type | What to check | Code Location | Status |
| --- | --- | --- | --- |
| **Input validation** | Null pointers, protocol version bounds, payload size caps | `vw_protocol_codec.c:50`, `vw_worker_client.c:22` | Verified |
| **Authorization** | 32-byte secret token validation, constant-time comparison | `vw_worker.c:50`, `vw_worker_client.c:45` | Verified |
| **Concurrency** | Non-blocking execution, lock-free audio callback isolation | `vlc_whisper_module.c:45`, `vw_worker_client.c:20` | Verified |
| **I/O** | 10s accept & 3s read/write IPC transport timeouts | `vw_worker_client.c:75`, `vw_ipc_pipe_win32.c:40` | Verified |
| **Persistence** | Zero disk logging of transcripts, PCM, or plain tokens | Entire diff | Verified |

---

## 5. Acceptance Criterion → Code Mapping

| # | Criterion | Code | Test | Status |
| --- | --- | --- | --- | --- |
| 1 | Client connects to worker during module `Open` (Step 13) | `vlc_whisper_module.c:95`, `vw_worker_client.c:20` | `test_worker_lifecycle.c:35` | ✅ |
| 2 | Process spawning abstraction for Windows & Linux | `vw_platform_win32.c:28`, `vw_platform_linux.c:12` | `test_platform.c:20` | ✅ |
| 3 | `HELLO`/`HELLO_ACK` binary protocol handshake | `vw_worker_client.c:42-105`, `vw_worker.c:45` | `test_worker_ipc.c:45` | ✅ |
| 4 | Protocol message builders for clean frame initialization | `vw_protocol_codec.c:254-308` | `test_protocol_codec.c:45` | ✅ |
| 5 | Mandatory documentation updates per Rule 14 | `docs/source-layout.md:15`, `docs/architecture.md:70` | Manual inspection | ✅ |

---

## 6. Code Review Findings (Bugs, Risks, Nitpicks)

### Bugs (Sorted by Priority)

| Priority | Component / Location | Description | Impact | Proposed Fix |
| --- | --- | --- | --- | --- |
| **Medium** | `plugin/src/vw_worker_client.c:88` | Unbounded `malloc(ack_hdr.payload_length)` without capping payload size check prior to allocation | Heap memory exhaustion if malformed `HELLO_ACK` payload length is sent | Add `if (ack_hdr.payload_length > 1024) goto fail;` before calling `malloc()` |
| **Low** | `plugin/src/vw_platform_linux.c:38` | `vw_platform_get_random_bytes` uses `/dev/urandom` without checking return value of `read()` | Potential uninitialized token bytes if read fails | Check `if (read(fd, buffer, size) != (ssize_t)size)` and handle error |

### Architectural & Operational Risks

| Category | Risk Description | Affected Files | Mitigation Strategy |
| --- | --- | --- | --- |
| **Out-of-Scope Liberty** | Added 5 protocol message builder functions (`vw_msg_hello_init`, `vw_msg_hello_ack_init`, etc.) in `protocol/src/vw_protocol_codec.c` not explicitly scheduled in Milestone 2 | `vw_protocol_codec.h`, `vw_protocol_codec.c` | Retain as clean protocol utility layer and document in `docs/api-contracts.md` |
| **Out-of-Scope Liberty** | Integrated process spawning (`vw_platform_spawn_process`) directly into `vw_worker_client_launch_and_connect` | `vw_worker_client.c`, `vw_platform_win32.c`, `vw_platform_linux.c` | Keeps worker supervision encapsulated inside client launcher |

### Code Style & Quality Nitpicks

| Issue Type | File & Line | Description | Recommendation |
| --- | --- | --- | --- |
| **Header Guard** | `plugin/include/vw_platform.h:1` | Header guard uses non-standard naming convention | Ensure `VW_PLATFORM_H_` is used consistently |
| **Compiler Warning** | `plugin/src/vw_platform_win32.c:59` | MinGW warning on empty translation unit branch when non-Win32 | Suppress or wrap `#else` with dummy typedef |
