# Implementation Task Template

# Task: <short imperative title>

## Outcome
One externally verifiable result.

## Scope
- In:
- Out:
- Components/files:

## Contract map
For each changed behavior, record:

`producer -> boundary -> consumer -> lifecycle owner`

- Invariants touched: timeline | state/API | lifecycle | identity | metric | realtime | privacy/network | other

## Failure semantics
| Boundary/failure | retry | recover | fail session | fail process/startup |
| --- | --- | --- | --- | --- |
| <case> | | | | |

Do not use empty/zero/success as a substitute for distinct AGAIN/EOF/error states.

## Lifecycle impact
Mark affected transitions only: START/new media, pause/resume, seek/discontinuity, media swap, EOF/tail flush, STOP/shutdown, worker failure/respawn, overload/drop.

For each affected transition, state what resets, survives, flushes, retries, or fails.

## Identity / metrics / hot path
- Identity-bearing values changed; overflow behavior:
- Metrics changed; owner, units, reset domain, fallback:
- Realtime-adjacent code changed; why callback restrictions remain satisfied:

## Tests first
List failure/boundary/seam specs that establish the contract before implementation. New C contract tests use PR #50-style named accumulating checks (`vw_test_check_*`) and one `vw_test_finish`.

- Red-before-implementation specs:
- Existing ledger regressions affected:
- Faults to inject:

## Implementation
Smallest vertical change that makes the contract specs pass. After fixing, search every producer/copy/serializer/validator/consumer of the invariant, not only the original call site.

## Verification
- [ ] Relevant new specs failed for the intended reason before implementation
- [ ] New/affected specs pass
- [ ] Existing relevant regression suite passes
- [ ] `clang-format --dry-run --Werror <modified-c-files>`
- [ ] `cmake --preset linux-x64-debug && cmake --build --preset linux-x64-debug`
- [ ] `ctest --preset linux-x64-debug --output-on-failure`
- [ ] Existing memcheck gate run when available/applicable
- [ ] Only contract-relevant docs updated

## Evidence
- Commands/results:
- Known limitations/follow-ups:
