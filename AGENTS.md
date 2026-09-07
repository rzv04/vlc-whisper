# VLC-Whisper Agent Rules

`AGENTS.md` is the canonical agent/contributor rule file. Read `docs/README.md` for targeted documentation routing and `docs/invariants.md` for system contracts.

## Before changing code

1. Read the affected code, its callers/consumers, and the relevant contract. Do not plan from memory.
2. Read only the relevant sections linked by `docs/README.md`; do not load every large reference document by default.
3. For meaningful/high-risk work, use `ai/task-template.md` and save the plan under `docs/plans/`.
4. If behavior depends on VLC, whisper.cpp, FFmpeg, Media Foundation, or another pinned dependency, verify the exact pinned version/commit.
5. If a local dependency graph exists, use it to find affected callers/boundaries, then verify against source.

## Hard invariants

- **C17:** project-authored C is C17; no project-authored C++. Use 2-space Google-style formatting, 120 columns, `vw_` namespacing, and `clang-format`.
- **Playback first:** caption failure may disable captions; it must not stall/crash VLC or corrupt playback.
- **Realtime callback:** VLC audio callbacks may only do bounded non-blocking capture/queue work. No inference, IPC/filesystem I/O, blocking locks/waits, or unbounded allocation.
- **Timeline:** media/caption time is signed 64-bit microseconds. Preserve PTS continuity explicitly; gaps/discontinuities must never be silently collapsed.
- **State semantics:** never collapse semantically different states (for example AGAIN/EOF/ERROR) into one successful-looking value or infer terminal state from a heuristic when the producer can report it explicitly.
- **Lifecycle ownership:** session-scoped state must have one authoritative owner/reset/finalize path. Any new session field must define START, STOP, seek, media swap, EOF, failure, and respawn behavior.
- **Identity:** paths, URIs, model/session/corpus IDs, endpoints, and other identity-bearing values are reject-on-overflow. Only presentation/diagnostic text may truncate.
- **Metrics:** each metric/state field has one authoritative producer, units, reset domain, and fallback policy. Never derive a plausible substitute silently.
- **Privacy/network:** transcription audio remains local. Authenticated local IPC only. Model downloads and explicitly enabled translation may use documented worker network paths; no cloud transcription, telemetry, remote logging, or transcript/PCM persistence.

## Invariant-first change workflow

For every meaningful behavior change:

1. **Map the contract:** producer -> queue/API/IPC/filesystem boundary -> consumer -> lifecycle owner.
2. **State failure semantics first:** classify relevant failures as retry/recover/session-fail/process-fail; fail closed when correctness cannot be proven.
3. **Write failure/boundary/seam specs first:** cross-thread/process/queue/decoder/IPC/lifecycle/filesystem/network changes require at least one cross-component behavioral test.
4. **Use explicit states:** widen APIs instead of adding caller heuristics when the old contract cannot represent the new behavior.
5. **Search by invariant after the fix:** inspect every producer, copy, serializer, validator, and consumer of the affected identity/state/metric—not only the original call site.
6. **Exercise lifecycle edges when affected:** START, STOP, pause/resume, seek, media swap, EOF/tail flush, worker failure/respawn, and overload/drop behavior.
7. **Turn regressions into specs:** a fixed ledger defect gets a named regression test where practical; do not weaken a test to make implementation green.

## Test style

New behavioral/failure-path tests follow the PR #50 Jasmine-like convention: use short human-readable contract names, accumulate independent failures, and finish once so one run reports multiple violated expectations. Prefer `vw_test_check_true` / `vw_test_check_false` + `vw_test_finish` for new C contract tests. Test names describe behavior (`decoder_again_never_becomes_eof`), not implementation details.

## Documentation

Update only documentation whose contract changed. Keep canonical facts in one place and link instead of copying. Architecture -> `docs/architecture.md`; invariants -> `docs/invariants.md`; protocol/API -> `docs/api-contracts.md`; tests -> `docs/test-strategy.md`; user-visible setup/config -> `README.md`; roadmap status -> `docs/roadmap.md`. Historical/reference docs are not mandatory reading unless relevant.

Header declarations need a brief useful behavior/ownership comment. Commits use Conventional Commits and repository `.github/` contribution templates.

## Verification

Do not declare completion without relevant formatting/build/tests and the existing memory-check gate when available:

```bash
clang-format --dry-run --Werror <modified-c-files>
cmake --preset linux-x64-debug
cmake --build --preset linux-x64-debug
ctest --preset linux-x64-debug --output-on-failure
ctest --test-dir build/linux-x64-debug -T memcheck
```

No new sanitizer/static-analysis CI gate is required by these rules.
