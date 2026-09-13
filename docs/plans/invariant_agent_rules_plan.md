# Task: Harden invariant-driven development

## Goal
Reduce defects in large/high-risk changes by making cross-component invariants, failure semantics, seam tests, lifecycle checks, identity rules, metric ownership, and realtime constraints explicit and reviewable.

## Scope
- Agent/contributor rules and task/PR templates.
- Compact canonical invariant and test guidance.
- Documentation navigation/cleanup to reduce redundant reading.
- PR #50-style named accumulating test helpers/conventions.
- No sanitizer/UBSan/TSan CI gates.
- No production behavior changes in this branch.

## Invariant map
| Contract | Required evidence |
| --- | --- |
| Cross-component behavior | Producer -> boundary -> consumer map + seam test when behavior changes |
| Lifecycle | START/STOP/seek/swap/EOF/respawn/reset impact stated and exercised when affected |
| Failure states | Typed/explicit success, retry, EOF, recoverable, fatal semantics; no heuristic collapsing |
| Identity | Reject overflow/truncation for paths, URIs, IDs, endpoints, language/model identifiers |
| Metrics | One authoritative producer, units, reset domain, fallback policy |
| Realtime | No blocking I/O/locks, inference, filesystem work, or heap allocation in VLC audio callbacks |
| Regression | Fixed ledger defects gain named behavioral tests where practical |

## Test policy
Tests added by future work use PR #50's Jasmine-like convention: human-readable named behavioral checks accumulate failures and finish once, with seam/failure-path tests written before implementation for meaningful behavior changes.

## Documentation policy
Agents start from root `AGENTS.md` and `docs/invariants.md`, then read only documents/sections relevant to the changed contract; large historical/reference files are searched narrowly rather than loaded wholesale.

## Acceptance
- [x] Root agent rules are concise and invariant-first.
- [x] Duplicate `.agents` rules are removed.
- [x] Task and PR templates require invariant/failure/seam evidence.
- [x] Canonical invariant and compact test docs exist.
- [x] Source-layout and user README are simplified without losing essential usage/build paths.
- [x] PR #50-style accumulating named test helper is available.
- [x] No sanitizer/static-analysis CI gates are added.

## Verification note
This branch changes policy/docs plus `tests/include/vw_test.h`; it adds no production behavior and no CI workflow changes. PR #50's accumulating test-helper semantics were used directly. Repository pushes did not trigger a pull-request CI run, so no CI result is claimed here.
