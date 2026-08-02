# Known Issues

## Issue #1 — `test_platform` fails under Valgrind memcheck (pre-existing)

- **Priority**: P1 (blocks `-T memcheck` gate in the verification checklist)
- **Status**: Open, pre-existing (not introduced by the 2026-08-02 codec/handshake refactor).
- **Symptom**: `ctest --test-dir build/linux-x64-debug -T memcheck` reports 10/11 passed; only `test_platform` fails. Normal `ctest --preset linux-x64-debug` passes 11/11.
- **Reproduce**: `valgrind -q --tool=memcheck --leak-check=yes --show-reachable=yes ./build/linux-x64-debug/tests/test_platform`
- **Failure**: `Test failed: !vw_platform_spawn_process(kSpawnMissing, argv_missing) at tests/unit/test_platform.c:51`. The test expects spawning a non-existent executable to return `false`; under Valgrind's process interception (`posix_spawn`/fork) the call behaves differently, so the assertion fails.
- **Root cause**: Valgrind runtime alters process-spawn semantics. Not a code defect in `vw_platform_spawn_process`; the MemoryChecker log is empty (zero memory errors reported).
- **History**: Failing under memcheck since at least 2026-07-30 (see `build/linux-x64-debug/Testing/Temporary/LastTestsFailed_*.log`).
- **Suggested fix / upgrade path**:
  - Make the spawn-missing assertion Valgrind-aware (skip when `RUNNING_ON_VALGRIND`), or
  - Validate executable existence before spawning in `vw_platform_spawn_process` so the failure is deterministic regardless of runtime, or
  - Add a CTest/Valgrind suppression for this specific test.

## Issue #2 — Pre-existing warnings in `tests/integration/test_worker_lifecycle.c` (cosmetic)

- **Priority**: P2 (non-blocking; tests pass)
- `usleep` is undeclared under strict C17 (`-std=c17`); should be `nanosleep`/`setitimer`. Only warns because the file compiles without `-Werror` in this preset.
- `-Wsign-compare` at line 26 (`int i < VW_AUTH_TOKEN_BYTES`); fix by using `size_t`/unsigned loop counter.
- Not blocking; tests pass. Add when the test file is next touched.
