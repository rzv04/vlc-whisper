## Summary

What externally observable behavior changes, and why?

## Contract / invariant impact

- Changed path(s): `producer -> boundary -> consumer -> lifecycle owner`
- Invariants touched: timeline / state semantics / lifecycle / identity / metrics / realtime / privacy-network / none
- Failure policy: retry / recover / fail session / fail process-startup
- Exact dependency pins checked (if relevant):

## Tests

- Red-before-implementation failure/boundary/seam spec(s):
- Green result(s):
- Existing ledger/regression coverage affected:

New C contract tests use human-readable named accumulating expectations (`vw_test_check_*`) and one `vw_test_finish`.

## Checklist

- [ ] Project-authored code remains C17, formatted, and `vw_`-namespaced.
- [ ] Cross-component behavior has seam/integration coverage when changed.
- [ ] Distinct AGAIN/EOF/error/terminal states are not collapsed into heuristics.
- [ ] Session fields define/reset the affected START/STOP/seek/swap/EOF/respawn transitions.
- [ ] Identity-bearing values reject overflow; no silent path/URI/ID truncation.
- [ ] Changed metrics identify authoritative producer, units, reset domain, and fallback.
- [ ] VLC audio callback still has no inference, IPC/filesystem I/O, blocking wait/lock, or unbounded allocation.
- [ ] Relevant external/API failures have explicit handling and failure-path coverage.
- [ ] A fixed known defect has a named regression where practical.
- [ ] Privacy/network behavior remains within `docs/invariants.md`.
- [ ] Only relevant canonical docs were updated; facts were linked rather than duplicated.

## Verification

- [ ] `clang-format --dry-run --Werror <modified-c-files>`
- [ ] `cmake --preset linux-x64-debug && cmake --build --preset linux-x64-debug`
- [ ] `ctest --preset linux-x64-debug --output-on-failure`
- [ ] Existing memcheck gate run when applicable

No new sanitizer/static-analysis CI gate is required by this checklist.
