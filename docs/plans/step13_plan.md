# Implementation Task Plan: Step 13 (VLC Plugin Worker IPC Client Connection)

# Task: Connect VLC Plugin IPC Client to Worker Process

## Goal
Connect the VLC plugin's IPC client (`vw_worker_client_launch_and_connect`) to the worker process during module initialization (`vw_plugin_open`), establishing the authenticated `HELLO`/`HELLO_ACK` IPC handshake upon VLC audio filter load while guaranteeing graceful passthrough fallback if the worker is unavailable.

## Context
- **Relevant Docs/ADRs**: ADR-001 (External local worker), ADR-004 (Offline-only local IPC), `docs/architecture.md`, `docs/api-contracts.md`.
- **Affected Components**: `plugin/src/vlc_whisper_module.c`, `plugin/include/vw_plugin.h`.
- **Target OS/Builds**: Linux (POSIX named sockets) & Windows (Win32 Named Pipes).

## Scope
- **In scope**:
  - Store worker client handle, token, and pipe endpoint in `vw_plugin_sys_t`.
  - Generate cryptographically secure 32-byte auth token via `vw_platform_get_random_bytes`.
  - Construct OS-specific IPC endpoint name and worker binary path.
  - Invoke `vw_worker_client_launch_and_connect` during `vw_plugin_open`.
  - Disconnect client handle in `vw_plugin_close`.
  - Graceful fallback: log warning `PLUGIN_WORKER_UNAVAILABLE` and continue audio passthrough if worker launch/connect fails.
- **Out of scope**:
  - Background audio streaming thread (Step 14).
  - Caption segment receiver thread and rendering (Step 15).

## Design
- **Inputs and Outputs**:
  - Input: `filter_t* p_filter` passed to `vw_plugin_open()`.
  - Output: Initialized `vw_worker_client_t*` stored in `vw_plugin_sys_t->client`, or `NULL` if launch fails.
- **Ownership/Threading Model**:
  - `sys->client` is created during `vw_plugin_open()` (module setup phase) and freed during `vw_plugin_close()`.
- **Bounds and Failure Behavior**:
  - Worker failure or missing binary logs a warning and leaves `sys->client = NULL`.
  - `vw_plugin_filter()` audio callback remains 100% lock-free passthrough.
- **Privacy/Security**:
  - Secret 32-byte token generated per-instance; passed via command-line / memory only.

## Acceptance Criteria
- [ ] `vw_plugin_open` generates 32-byte secret token and formats OS-specific pipe endpoint.
- [ ] `vw_plugin_open` invokes `vw_worker_client_launch_and_connect`.
- [ ] `vw_plugin_close` cleans up client handle via `vw_worker_client_disconnect`.
- [ ] If worker binary is missing, `vw_plugin_open` logs warning and returns `VLC_SUCCESS` (passthrough mode preserved).
- [ ] Build & unit test suite pass 100% clean.
- [ ] Valgrind memcheck passes 100% clean.

## Definition of Done
- [ ] C17 code compliant with standard.
- [ ] Zero heap allocation or blocking in VLC audio callback.
- [ ] All verification checks pass.
