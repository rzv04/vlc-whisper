# Diff Analysis: milestone-5 P2 remediation

**51 files changed (46 existing files, 5 new tests); base: `e70ea36`**

Fresh review of the current worktree. Each row records the eight required dimensions in compact form: why/before-after, callers/callees, happy path, failure path, boundaries, acceptance mapping, and assumptions/tradeoffs. Line references are post-change. All five untracked tests are included.

## 1. File-by-file analysis

| File | Why / responsibility before→after | Callers / callees | Happy path / failure path | Boundaries | Acceptance / test | Assumptions, tradeoffs, confidence |
|---|---|---|---|---|---|---|
| `README.md` | Documents current build/runtime and release constraints; usage guide→records new behavior. | Users; CMake/docs. | Supported setup / unsupported platform caveat. | User input, model files, platform. | Release docs ✅. | Documentation only; verify packaging. |
| `docs/api-contracts.md` | Records transport limits, credentials, EOF and 120s close contract. | Implementations/tests. | Header+payload transport / fatal EOF or timeout. | Wire size, auth, I/O, lifecycle. | VW-036/037/041/042 ✅. | 1 MiB logical, 64 KiB records; old-peer framing risk. |
| `docs/architecture.md` | Captures translation drain and deliberate 120s watchdog. | Plugin/worker reviewers. | SHUTDOWN drains / watchdog bounds hung worker. | Threading, blocking teardown. | Lifecycle contract ✅. | 120s is explicit; no Whisper cancellation promise. |
| `docs/issues.md` | Updates P2 ledger evidence/status. | Maintainers. | Issue→fix/test / stale status reviewed. | Evidence freshness. | Ledger current ✅. | Must be revalidated after future changes. |
| `docs/roadmap.md` | Marks milestone work. | Release planning. | Status reflects code / incomplete items visible. | Scope/release gates. | Roadmap ✅. | Documentation only. |
| `docs/source-layout.md` | Registers new source/test ownership. | Contributors. | Files discoverable / stale map hurts maintenance. | Naming/ownership. | Layout ✅. | Keep synchronized with renames. |
| `docs/test-strategy.md` | Adds failure-first transport/decoder strategy. | CI/test authors. | Targeted regression / platform gap visible. | Matrix, seams, Valgrind. | Strategy ✅. | Wine/macOS/BSD unavailable locally. |
| `docs/whisper-api.md` | Documents worker/Whisper finalization. | Worker implementation. | Inference/tail flush / failure fatal. | Engine errors/session. | Worker contract ✅. | Third-party API external. |
| `plugin/include/vw_benchmark.h` | Adds epoch reset API. | Sender/module; benchmark source. | Reset reanchors / null inactive safe. | Atomic/session, clock. | VW-022; tests. | Caller resets every epoch. |
| `plugin/src/vw_benchmark.c` | Implements reset of live anchors. | Sender/finalize paths. | New epoch metrics / stale metrics avoided. | Monotonic clock. | VW-022 ✅. | Clock failure caller concern. |
| `plugin/include/vw_plugin_log_scope_override.h` | Adds close accounting/final drain. | Module teardown; client/presenter/benchmark. | SHUTDOWN→drain→EOF / 120s watchdog. | TLS, blocking IPC, teardown. | Close contract ✅. | 120s deliberate; tail may be lost after expiry. |
| `plugin/src/vw_caption_presenter.c` | Flushes old SPU channel before reuse. | Render/clear; VLC vout. | Flush then register / invalid vout guarded. | VLC lifetime/channel IDs. | VW-076; test. | Held vout valid through flush. |
| `plugin/src/vw_platform_linux.c` | Removes PATH launch. | Module; `posix_spawn`. | Absolute executable / relative/missing rejected. | Path/process/errno. | VW-043; test. | Discovery resolves concrete path. |
| `plugin/src/vw_platform_win32.c` | Exclusive per-process worker log. | Module; Win32 file/process. | Unique inherited log / collision fallback. | PID/tick, ANSI path. | Log hardening; MinGW. | Runtime Windows unavailable. |
| `plugin/src/vw_whisper_module.c` | Resets epochs and uses resolved worker path. | VLC sender; platform/client. | Clean epoch / failure passthrough. | Realtime callback, queue, teardown. | VW-013/019/043/079; tests. | Callback remains nonblocking/allocation-free. |
| `plugin/src/vw_worker_client.c` | Forward same-major minor; rejects model-ID truncation. | Plugin sender; protocol. | Compatible HELLO / unsupported or overlong rejected. | Fixed arrays/capability gates. | VW-021/040; test. | New minor gates optional messages. |
| `protocol/src/vw_ipc_pipe_win32.c` | `ERROR_MORE_DATA`, 64 KiB records, one logical 3s send deadline. | Worker/client; overlapped I/O. | Chunk/reassemble / cancel on timeout. | Pipe boundaries/cancellation/deadline. | VW-036/037; MinGW ✅, Wine unavailable. | Receiver loops continuation records. |
| `protocol/src/vw_ipc_socket_linux.c` | Peer credentials, EINTR accept, MSG_TRUNC fatal, chunked bounded send. | Protocol users; `poll`/`recv`/`send`. | Seqpacket records≤64 KiB / EMSGSIZE, EOF, timeout fatal. | UID, truncation, monotonic clock. | VW-037/041/042; Linux test ✅. | macOS/BSD branches not compiled here. |
| `tests/CMakeLists.txt` | Registers regression targets/platform links. | CMake/CTest. | Configure/build/discover / missing dependency fails. | WIN32/MF/winhttp, Valgrind. | Transport at 230–233 precedes Valgrind 235–239 ✅. | Runtime matrix external. |
| `tests/integration/test_live_media_end_flushes_tail.c` | Tests final live audio/caption drain. | Worker stubs/protocol. | STOP/SHUTDOWN tail / inference/transport error. | Queue/session end. | Finding #10. | Deterministic stubs. |
| `tests/integration/test_new_session_resets_all_session_state.c` | Tests clean media-swap state. | Lifecycle harness. | New ID clean / stale flags caught. | IDs/source state. | VW-079 ✅. | Serialized harness. |
| `tests/integration/test_worker_ipc.c` | Process-unique Windows endpoint. | Worker/client. | Concurrent endpoint isolation / auth rejection. | PID naming/cleanup. | VW-044; cross-build. | Windows runtime unavailable. |
| `tests/integration/test_worker_lifecycle.c` | Same endpoint fix for lifecycle. | Worker/client. | Start-stop isolation / abnormal exit. | PID naming/cleanup. | VW-044; cross-build. | Windows runtime unavailable. |
| `tests/integration/vw_test_decoder_boundaries.c` | FFmpeg seek/read/URI seams. | FFmpeg wrappers. | Valid seek/read / explicit error not false EOF. | Demux/seek/resampler/URI. | VW-032/062/Finding #23/#24. | Needs FFmpeg test environment. |
| `tests/integration/vw_test_worker_p2_contracts.c` | Worker language, duplicate START, inference/fatal tests. | Worker/stubs. | Valid progression / fatal nonzero. | Protocol/session/transport. | Finding #14/#15. | Stub fidelity. |
| `tests/support/vw_test_worker_stubs.c` | Language/failure controls. | P2 tests; engine stubs. | Configurable success / forced failure. | Global test state. | Worker contracts. | Reset globals; serialized. |
| `tests/support/vw_test_worker_stubs.h` | Declares test controls. | Contract tests. | Explicit seam / missing reset leaks state. | C17 header ABI. | Worker tests. | Test-only interface. |
| `tests/unit/vw_test_ipc_transport.c` | New POSIX transport suite. | IPC socket; listener threads. | 960 KiB round trip/EINTR / stale errno, truncation, stalled deadline. | Seqpacket, signals, timeout, EOF. | VW-037/041/042; Linux green. | POSIX-only; Win32 compile only. |
| `tests/unit/vw_test_translate_stack.c` | Heap/bounded translation request test. | Translation builder/API. | Large legal request / allocation/size fallback. | Allocation/encoded lengths. | VW-025. | No network. |
| `tests/unit/vw_test_vad_trailing_silence.c` | 300ms trailing silence test. | VAD boundary. | Speech boundary / pathological tail clamp. | Samples, EOF. | VW-030. | 16 kHz assumed. |
| `tests/unit/test_audio_buffer.c` | Negative PTS/start-anchor coverage. | Audio buffer. | Signed origin / discontinuity. | Signed arithmetic/phase. | VW-028. | Valid bounded PCM. |
| `tests/unit/test_benchmark.c` | Benchmark reset coverage. | Benchmark API. | Reanchor / stale latency avoided. | Clock/inactive. | VW-022. | Valid test clock. |
| `tests/unit/test_caption_presenter.c` | Vout/channel flush coverage. | Presenter/VLC doubles. | Flush before reuse / null guarded. | VLC lifetime. | VW-076. | Doubles model ordering. |
| `tests/unit/test_model_download.c` | Directory overflow/cancel coverage. | Download helpers. | Safe path / truncation rejected. | Env/filesystem. | Finding #28. | Isolated temp env. |
| `tests/unit/test_platform.c` | Absolute launch/process coverage. | Platform APIs. | Concrete executable / relative/missing rejected. | Process/path. | VW-043. | POSIX runtime; Windows compile only. |
| `tests/unit/test_source_decoder.c` | Windows embedded-NUL URI assertion. | Decoder open. | Valid URI / `%00` rejected before MF. | `_WIN32`, URI, MF. | Finding #24; MinGW. | Windows runtime unavailable. |
| `tests/unit/test_translate_async.c` | Async pending/state coverage. | Async queue/translation. | Delivery / invalidation waits safely. | Mutex/queue/session. | Translation shutdown. | Mock fidelity. |
| `tests/unit/test_worker_client_compat.c` | Forward-minor handshake test. | Client/mock transport. | Same-major forward minor / incompatible rejected. | Version/payload. | VW-040. | Mock negotiation faithful. |
| `tests/unit/vw_test_worker_client.c` | Client epoch/lifecycle coverage. | Client/platform. | Reset/transport / EOF/fatal. | IDs/process endpoint. | VW-079/VW-044. | Platform differences. |
| `worker/include/vw_translate_async.h` | Pending-work query contract. | Worker drain; async source. | Observe queued/inflight / mutex state. | Mutex/lifetime. | Translation drain. | Caller owns lifetime. |
| `worker/src/main.c` | Exclusive logging and Windows UTF-8 paths. | Startup/config/logging. | Unique log / collision/overflow fallback. | `_wopen`, O_EXCL, path. | Log hardening. | PID unique while active. |
| `worker/src/vw_audio_buffer.c` | Negative initial PTS preservation. | Worker ingestion. | Signed origin / discontinuity reset. | PTS/sample arithmetic. | VW-028. | Bounded samples. |
| `worker/src/vw_model_download.c` | Rejects truncated model directory. | Download setup. | Full path / `snprintf` failure. | Filesystem/path. | Finding #28. | Caller handles false. |
| `worker/src/vw_source_decoder_ffmpeg.c` | URI safety, seek resampler, preroll trim. | Worker source; FFmpeg. | Media-relative PCM / setup/read error. | FFmpeg, delay, rounding, URI. | VW-032/062/#23/#24. | Channel layout initialized. |
| `worker/src/vw_source_decoder_mf.c` | Windows URI safety and target trim. | Worker source; MF. | Samples at/after target / conversion/read error. | COM/timestamps/UTF-8/rounding. | VW-061/#24; MinGW. | No Windows runtime evidence. |
| `worker/src/vw_translate.c` | Heap request buffers and absolute fallback deadline. | Async translator; HTTP/GTX/mobile. | Bounded tier fallback / allocation/network failure source-only. | Heap/size/800ms. | VW-025. | Opt-in network, bounded. |
| `worker/src/vw_translate_async.c` | Tracks queued/inflight/pending work. | Worker/translation thread. | Complete/deliver / stop invalidates safely. | Mutex/thread/queue. | Translation lifecycle. | Producer deadlines bound work. |
| `worker/src/vw_vad.c` | Caps trailing silence. | Worker chunk boundary. | Natural boundary / overflow clamp. | `SIZE_MAX`, EOF, samples. | VW-030. | 16 kHz. |
| `worker/src/vw_worker.c` | Protocol/session/fatal state and final drain. | Reader, decoder, engine, translator, IPC. | HELLO→START→AUDIO→STOP / all inspected send failures set `fatal_exit` (726–747, 807–848, 1131–1258); exit at 1465. | Threads, queue, transport, session, teardown. | Finding #14/#15 ✅ current branches/tests. | Translation deadlines plus documented 120s plugin watchdog. |
| `worker/src/vw_worker_config.c` | Tightens path/argument bounds/classifier. | Worker startup. | Valid config / overflow/invalid rejected. | Fixed arrays/platform paths. | Findings #26/#27. | Platform absolute-path semantics. |

## 2. Happy-path request trace

`plugin/src/vw_whisper_module.c` resolves and launches through `vw_platform_linux.c` or `vw_platform_win32.c`; `vw_worker_client.c` sends HELLO/START. `worker/src/vw_worker.c` authenticates, queues frames, feeds audio/decoder/VAD/Whisper, optionally drains translation, and returns segments to client/presenter while benchmark state updates. Ordered STOP/SHUTDOWN reaches the close override, which drains through EOF or the documented 120-second watchdog.

## 3. Most important failure path

Oversized or stalled logical writes are split into bounded records with one monotonic three-second deadline. Linux detects `MSG_TRUNC`, sets `errno=EMSGSIZE`, and returns fatal; Win32 preserves `ERROR_MORE_DATA` bytes and cancels on deadline. Worker send failures set both `fatal_exit` and `running=false`; cleanup closes IPC and line 1465 returns nonzero for authenticated fatal failures.

## 4. Boundary summary

| Boundary | Evidence | Residual gap |
|---|---|---|
| Input | Fixed lengths, URI NUL/truncation, model IDs, protocol validation. | No additional defect found in reviewed diff. |
| Authorization | Secret token plus Linux/BSD/macOS credential branches. | macOS/BSD runtime not compiled here. |
| Concurrency | Reader/main queues, translator mutex, TLS teardown. | Shutdown remains lifecycle-sensitive. |
| I/O | Chunking, EINTR, truncation/continuation, cumulative deadlines, EOF. | Win32 runtime unavailable; old peers may assume one record. |
| Persistence | Exclusive logs and checked model paths. | Explicit user log paths retain documented overwrite semantics. |

## 5. Acceptance criterion mapping

| Criterion | Code/tests | Status |
|---|---|---|
| No PATH worker hijack | Linux platform + `test_platform` | ✅ |
| Forward minor/model-ID bounds | Worker client + compatibility tests | ✅ |
| Large legal payload/no truncation/one deadline | Both transports + `vw_test_ipc_transport` | ✅ Linux; ⚠️ Win32 compile only |
| EINTR/peer credentials | Linux transport + native test | ✅ Linux; ⚠️ non-Linux runtime unavailable |
| MF preroll/NUL | MF decoder + source test | ⚠️ MinGW compile only |
| FFmpeg seek/error/path | FFmpeg decoder + boundary test | ✅ where target available |
| Fatal authenticated worker failure nonzero | `vw_worker.c:726–1465` + P2 test | ✅ |
| Final captions/translations | Worker drain/close + live-end tests | ✅; watchdog can lose tail after 120s |
| CMake/Valgrind registration | `tests/CMakeLists.txt:230–239` | ✅ |

## 6. Code review findings

### Bugs

| Priority | Component / location | Description | Impact | Proposed fix |
|---|---|---|---|---|
| Medium | `protocol/src/vw_ipc_socket_linux.c`, `protocol/src/vw_ipc_pipe_win32.c` | Logical chunking changes raw record count. Current in-tree consumers loop records, but an older same-major peer assuming one payload record could reject a legal frame. | Interoperability failure with old peers. | Add an old-peer fixture or negotiate chunked transport capability before release. |

### Architectural and operational risks

| Category | Risk | Affected files | Mitigation |
|---|---|---|---|
| Portability | Windows/MF is MinGW cross-compiled; macOS/BSD is source-reviewed only. No runtime evidence for those platforms here. | Transport, MF, platform tests | Native-platform smoke tests. |
| Lifecycle | Hung worker can hold teardown for documented 120s; expiry may lose final tail. | Close override, architecture docs | Keep explicit policy and add runtime watchdog acceptance test. |
| Build | Platform-specific test libraries are distributed through CMake conditionals. | `tests/CMakeLists.txt` | Keep Linux and MinGW configure/build checks plus native CTest. |

### Code quality

| Issue type | Location | Description | Recommendation |
|---|---|---|---|
| Test coverage | Windows/macOS/BSD runtime | Cross-build proves compile/link, not behavior. | Add native/Wine CI coverage. |
| Compatibility | Transport chunking | Record granularity is an intentional wire-level tradeoff. | Add old-peer compatibility test or capability bit. |

## 7. Verification evidence

- Baseline red: e70ea36 transport harness reproduced stale-`errno` oversized-record failure (`rc=134`), and baseline large logical send failed (`rc=134`).
- Current green: focused Linux CTest passed transport and worker IPC/lifecycle; transport covers a 960,000-byte round trip, EINTR accept, stale errno, truncation, and cumulative deadline. `clang-format --dry-run --Werror` passed for changed C/header files.
- Current cross-build: `cmake --preset windows-x64-debug-cpu` and MinGW protocol/worker/plugin/source-decoder targets built successfully. Wine was unavailable; no Windows runtime claim is made, and no macOS/BSD compilation claim is made.
- Final 2026-09-09 verification: native debug configure/build passed; CTest had 48 passes, one model-dependent skip,
  and no failures. Full Valgrind had no reported memory-check defects. All Windows CPU MinGW targets, including tests,
  compiled. Nine Python quality-tool tests passed. Changed C/header formatting and `git diff --check` passed.
