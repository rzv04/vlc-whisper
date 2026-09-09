# Contributing to VLC-Whisper

Start with root `AGENTS.md`. It is the canonical coding/invariant rule set. Open only the technical references relevant to your change.

## Before implementation

For meaningful behavior changes:

1. Inspect the changed component, callers/consumers, and affected contract.
2. Map `producer -> boundary -> consumer -> lifecycle owner`.
3. Define external failure handling before the happy path.
4. Write failure/boundary/seam specs first when behavior crosses components.

Use `ai/task-template.md` for high-risk work and `docs/invariants.md` for the canonical contract set.

## Test convention

New C failure/contract tests use PR #50-style named accumulating expectations (`vw_test_check_true` / `vw_test_check_false`) and one `vw_test_finish`. Expectation names state behavior in plain language. Existing small fail-fast unit tests may keep legacy `EXPECT` macros.

A fixed known defect should gain a named regression where practical. Do not weaken tests to make an implementation green.

## Core code rules

- Project-authored C is C17, 2-space Google style, 120 columns, `vw_` namespacing.
- VLC audio callbacks perform bounded non-blocking capture/queue work only, with zero heap allocation.
- Preserve signed 64-bit media PTS and explicit discontinuities.
- Distinct transient/EOF/error states stay distinct.
- Identity-bearing values reject overflow instead of truncating.
- Session-scoped state defines reset/finalize behavior for affected lifecycle transitions.
- Metrics have one authoritative producer, units, reset domain, and fallback policy.
- Transcription remains local; only documented explicit model-download/opt-in translation network paths are allowed.
- No implicit runtime transcript/PCM persistence; explicit local subtitle exports and git-ignored developer benchmark text artifacts are allowed.

## Verification

```bash
clang-format --dry-run --Werror <modified-c-files>
cmake --preset linux-x64-debug
cmake --build --preset linux-x64-debug
ctest --preset linux-x64-debug --output-on-failure
ctest --test-dir build/linux-x64-debug -T memcheck
```

Update only documentation whose canonical contract changed. Use Conventional Commits and the pull-request template.
