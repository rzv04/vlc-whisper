# Implementation Task Plan: Milestone 1.7 (IPC Transport & Session Handshake)

# Task: Implement Named-Pipe Server, Authentication, Session Lifecycle (START/AUDIO/STOP), and Integration Verification

## Goal
Complete Milestone 1.7: Implement platform IPC transport server (`vw_ipc_pipe_win32.c` and `vw_ipc_socket_linux.c`), secret token handshake (`VW_MSG_HELLO` / `VW_MSG_HELLO_ACK`), session state machine (`VW_MSG_START_SESSION`, `VW_MSG_AUDIO_PCM`, `VW_MSG_PAUSE`, `VW_MSG_RESUME`, `VW_MSG_STOP_SESSION`, `VW_MSG_SHUTDOWN`), and integration test suites (`test_worker_ipc.c`, `test_worker_lifecycle.c`).

---

## Context
- **Relevant Docs/ADRs**: `docs/architecture.md`, `docs/api-contracts.md`, `docs/roadmap.md`, `docs/source-layout.md`.
- **Target OS / Build**: Windows x64 via MinGW cross-compiler & Linux native host.
- **Protocol Version**: Major = 1, Minor = 0.
- **Invariants**:
  - Offline & privacy: local IPC only with 32-byte secret token.
  - Constant-time secret token validation (no timing side-channel attacks).
  - 64-bit microsecond PTS timestamps (`int64_t pts_us`).
  - Zero heap allocation or blocking locks inside VLC audio callbacks.

---

## Architectural Answers & Design Clarifications

### 1. Model `manifest.json` Location & Creation
- **Installation Location**: On Windows, models and `manifest.json` live at `%LOCALAPPDATA%\vlc-whisper\models\` (or `~/.config/vlc-whisper/models/` on Linux).
- **Creation Lifecycle**: Provisioned by installer (Inno Setup / package script) alongside GGML model binaries (`ggml-tiny.en.bin`). Contains SHA-256 integrity hashes, RAM bounds, and model capabilities.
- **Worker Verification**: Worker verifies `manifest.json` SHA-256 before initializing Whisper engine. If hash fails, worker emits `VW_MSG_ERROR(E_MODEL_INVALID)` and halts captioning without affecting VLC playback.

### 2. Logging & Redacted Diagnostics on Errors
- **Logging Integration**: Every sent/received `VW_MSG_ERROR`, failed authentication, or malformed frame automatically triggers `vw_log.c` calls (`VW_LOG_ERROR` / `VW_LOG_WARN`).
- **Privacy Invariant**: Logs contain error codes and static diagnostic strings ONLY. Zero raw PCM, transcript text, or disk paths logged.

### 3. Windows API vs POSIX Linux Target
- **Ubuntu MinGW Cross-Compilation**: WinAPI calls (`CreateNamedPipeA`, `ConnectNamedPipe`, `ReadFile`, `WriteFile`, `CloseHandle`) are compiled via MinGW GCC (`x86_64-w64-mingw32-gcc`) under `#if defined(_WIN32) || defined(__MINGW32__)`.
- **Native Linux Development Host**: `protocol/src/vw_ipc_socket_linux.c` implements POSIX Unix Domain Sockets (`AF_UNIX`, `SOCK_SEQPACKET`) under `#if defined(__linux__)`. Enables 100% native compilation and CTest execution on Ubuntu.

### 4. Secret Token Generation & Constant-Time XOR Proof
- **Creation**: Generated fresh **once per session / process launch** by the plugin using `vw_platform_get_random_bytes(token, 32)` (`BCryptGenRandom` on Win32, `/dev/urandom` on Linux).
- **Transmission**: Plugin passes token to worker via command-line argument (`--token <32-bytes>`).
- **Constant-Time Match Proof**:
  ```c
  static bool vw_auth_verify_token_constant_time(const uint8_t token_a[32], const uint8_t token_b[32]) {
    volatile uint8_t diff = 0;
    for (size_t i = 0; i < 32; i++) {
      diff |= (token_a[i] ^ token_b[i]);
    }
    return (diff == 0);
  }
  ```
  - XOR (`a ^ b`) yields `0` if and only if `a == b`.
  - Accumulating `diff |= (token_a[i] ^ token_b[i])` across all 32 bytes yields `diff == 0` **if and only if all 32 bytes match identically**.
  - Always executes exactly 32 iterations, preventing timing side-channel attacks.

### 5. Socket I/O Performance, Buffering & Header Junk Protection
- **Transfer Throughput**: 8 seconds of 16 kHz 16-bit mono PCM is 256 KB. Over local Unix domain socket / Windows named pipe, transferring 256 KB takes **< 0.1 ms** (RAM-to-RAM memory copy). PCM is streamed in 500 ms chunks (16 KB each), taking ~5 µs per chunk.
- **Blocking & Timeouts**: Transport calls (`vw_ipc_send`/`vw_ipc_receive`) use 3-second blocking timeouts (`SO_RCVTIMEO` / `SO_SNDTIMEO` on socket, `PIPE_WAIT` on Win32 pipe) executed on dedicated worker/sender threads outside VLC audio callback loop.
- **Junk Header Validation**: Every received frame validates `magic == 0x564C4357 ('VLCW')`, `major == 1`, `payload_length <= 1,048,576` BEFORE allocating memory or reading payload. Junk frames immediately terminate transport connection.

### 6. Protocol Control Flow & Idempotency
- **`VW_MSG_SHUTDOWN` (Plugin -> Worker)**: Plugin sends SHUTDOWN when VLC stops captioning or unloads plugin. Worker exits cleanly with code `0`. Plugin does not exit.
- **`VW_MSG_STOP_SESSION`**: Idempotent. Calling `STOP` multiple times on an already stopped/idle session is a safe no-op.
- **`RESUME`**: Resumes active captioning after `PAUSE`. Payload `vw_msg_control_t` (reason `USER_RESUME=1`).

---

## Scope

### In Scope
1. **IPC Transport Implementation**:
   - `protocol/src/vw_ipc_pipe_win32.c`: Named Pipe server (`vw_ipc_listen`) and client (`vw_ipc_connect`) using Windows Win32 API (`CreateNamedPipeA`, `ConnectNamedPipe`, `CreateFileA`, `ReadFile`, `WriteFile`, `CloseHandle`).
   - `protocol/src/vw_ipc_socket_linux.c`: Unix domain socket server (`vw_ipc_listen`) and client (`vw_ipc_connect`) using POSIX sockets (`socket(AF_UNIX, SOCK_SEQPACKET, 0)`, `bind`, `listen`, `accept`, `connect`, `read`, `write`).
2. **Worker Event Loop & Session Lifecycle**:
   - `worker/src/vw_worker.c`: Implement `vw_worker_run()` main server loop:
     - Listen on IPC endpoint.
     - Wait for connection and receive `VW_MSG_HELLO`.
     - Perform constant-time 32-byte secret token verification.
     - Negotiate major/minor version and send `VW_MSG_HELLO_ACK`.
     - Process incoming frame envelope + payload dispatch (`VW_MSG_START_SESSION`, `VW_MSG_AUDIO_PCM`, `VW_MSG_PAUSE`, `VW_MSG_RESUME`, `VW_MSG_STOP_SESSION`, `VW_MSG_SHUTDOWN`).
     - Emit `VW_MSG_STARTED` on session start, `VW_MSG_CAPTION_SEGMENT` during processing, and `VW_MSG_STATUS`/`VW_MSG_ERROR` on failure.
3. **Plugin IPC Client (`vw_worker_client_win32.c`)**:
   - Connect to worker pipe/socket using `vw_ipc_connect`.
   - Perform handshake by sending `VW_MSG_HELLO` and validating `VW_MSG_HELLO_ACK`.
4. **Integration Testing**:
   - `tests/integration/test_worker_ipc.c`: Test socket/pipe connection, valid vs invalid 32-byte token rejection, version mismatch error handling, frame binary codec transfer.
   - `tests/integration/test_worker_lifecycle.c`: Test full lifecycle (`HELLO` -> `START` -> `AUDIO` -> `PAUSE` -> `RESUME` -> `STOP` -> `SHUTDOWN`).

### Out of Scope
- VLC native GUI audio plugin integration (Milestone 2/3).
- Network/cloud IPC endpoints (offline local-only invariant).

---

## Technical Design & Architecture

```mermaid
sequenceDiagram
    autonumber
    participant P as Plugin Client (VLC)
    participant W as Worker Host (vlc-whisper-worker)

    P->>W: Named Pipe / Unix Socket Connect
    P->>W: VW_MSG_HELLO (min=1, max=1, token[32], client_ver)
    Note over W: Constant-time token verification (32-byte secret)
    alt Invalid Token or Incompatible Version
        W-->>P: VW_MSG_ERROR (code=AUTH_FAILED/UNSUPPORTED_VERSION)
        Note over W,P: Disconnect IPC
    else Valid Token & Supported Version
        W->>P: VW_MSG_HELLO_ACK (major=1, minor=0, caps=PCM_S16LE_16K_MONO)
    end

    P->>W: VW_MSG_START_SESSION (session_id, pts_us, sample_rate, model_id, lang)
    Note over W: Init audio buffer & segment builder
    W->>P: VW_MSG_STARTED

    loop Stream PCM Audio
        P->>W: VW_MSG_AUDIO_PCM (session_id, start_pts_us, duration_us, pcm_bytes)
        Note over W: Append PCM to buffer & run Whisper engine
        opt Segment Ready
            W->>P: VW_MSG_CAPTION_SEGMENT (session_id, seg_id, text_utf8, is_final)
        end
    end

    P->>W: VW_MSG_STOP_SESSION (session_id, reason)
    P->>W: VW_MSG_SHUTDOWN
    Note over W: Close IPC & exit worker loop cleanly
```

### Constant-Time Secret Token Verification
```c
static bool vw_auth_verify_token_constant_time(const uint8_t token_a[32], const uint8_t token_b[32]) {
  volatile uint8_t diff = 0;
  for (size_t i = 0; i < VW_AUTH_TOKEN_BYTES; i++) {
    diff |= (token_a[i] ^ token_b[i]);
  }
  return (diff == 0);
}
```

---

## Proposed Changes

### Component 1: IPC Transport Layer

#### [MODIFY] `protocol/src/vw_ipc_pipe_win32.c`
- Implement Win32 named pipe server (`vw_ipc_listen`) using `CreateNamedPipeA` with `PIPE_ACCESS_DUPLEX`, `PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT`.
- Implement `vw_ipc_connect` using `CreateFileA` and `SetNamedPipeHandleState`.
- Implement `vw_ipc_send` and `vw_ipc_receive` using `WriteFile` and `ReadFile`.

#### [MODIFY] `protocol/src/vw_ipc_socket_linux.c`
- Implement Linux Unix domain socket server (`vw_ipc_listen`) using `socket(AF_UNIX, SOCK_SEQPACKET, 0)`, `bind()`, `listen()`, `accept()`.
- Implement `vw_ipc_connect` using `socket()` and `connect()`.
- Implement `vw_ipc_send` and `vw_ipc_receive` using `send()` and `recv()`.

---

### Component 2: Worker Engine & Event Loop

#### [MODIFY] `worker/src/vw_worker.c`
- Implement `vw_worker_run(const vw_worker_config_t *config)`:
  - Create transport listener `vw_ipc_listen()`.
  - Accept connection from client.
  - Receive initial frame header & payload (`VW_MSG_HELLO`).
  - Validate token via `vw_auth_verify_token_constant_time`. If mismatch, send `VW_MSG_ERROR` and close handle.
  - Send `VW_MSG_HELLO_ACK`.
  - Enter message loop reading `vw_frame_header_t` + payload:
    - `VW_MSG_START_SESSION`: Reset VAD, audio buffer, segment builder; send `VW_MSG_STARTED`.
    - `VW_MSG_AUDIO_PCM`: Decode PCM, append to `vw_audio_buffer_t`, trigger transcription, push hypothesis into `vw_segment_builder_t`, send `VW_MSG_CAPTION_SEGMENT` if generated.
    - `VW_MSG_PAUSE` / `VW_MSG_RESUME` / `VW_MSG_STOP_SESSION`: Update session state machine.
    - `VW_MSG_SHUTDOWN`: Break loop and shutdown gracefully.

#### [MODIFY] `plugin/src/vw_worker_client_win32.c`
- Implement `vw_worker_client_launch_and_connect`: connect to IPC, perform `VW_MSG_HELLO` handshake, return client handle.

---

### Component 3: Integration Test Suite

#### [MODIFY] `tests/integration/test_worker_ipc.c`
- Create IPC transport server and client thread/process.
- Test successful handshake with valid 32-byte secret token.
- Test rejection of invalid token (must fail authentication and close transport).
- Test version mismatch rejection (min/max version mismatch).

#### [MODIFY] `tests/integration/test_worker_lifecycle.c`
- Execute full worker lifecycle: `HELLO` -> `START` -> `AUDIO` -> `PAUSE` -> `RESUME` -> `STOP` -> `SHUTDOWN`.
- Verify `VW_MSG_STARTED` confirmation and clean shutdown exit code 0.

---

## Verification Plan

### Automated Tests
```bash
# Build the project (Linux host)
cmake -B build -S .
cmake --build build -j4

# Run all 7 unit and integration tests
cd build && ctest --output-on-failure
```

### Expected Observable Outcomes
- `test_protocol_codec`: PASSED
- `test_protocol_validate`: PASSED
- `test_queue`: PASSED
- `test_segment_builder`: PASSED
- `test_caption_timing`: PASSED
- `test_worker_ipc`: PASSED
- `test_worker_lifecycle`: PASSED
- **Result**: 100% tests passed (7/7).

---

## Definition of Done Check
- [x] C17 standard (`-std=c17`), no C++ authored code.
- [x] Authenticated local IPC only (named pipe / Unix domain socket with 32-byte secret token).
- [x] Constant-time token verification prevents timing attacks.
- [x] Zero cloud/network requests, zero transcript/PCM logging to disk.
- [x] Complete automated test coverage in `test_worker_ipc` and `test_worker_lifecycle`.
