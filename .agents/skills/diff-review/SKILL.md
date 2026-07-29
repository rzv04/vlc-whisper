---
name: diff-review
description: "Structured 8-point code diff analysis for developers unfamiliar with the changes. Use when: reviewing uncommitted changes, analyzing a PR diff, understanding what a milestone/feature branch changed, onboarding to new code, or auditing code changes before merge. Produces a comprehensive diff.md file."
argument-hint: "[HEAD|base-ref]"
user-invocable: true
---

# Comprehensive Diff Analysis

Analyze a code diff with 8 structured dimensions per changed file. Output goes to `diff.md` in the workspace root.

## When to Use

- Review uncommitted changes (`git diff`)
- Analyze a PR or branch diff (`git diff HEAD..branch`)
- Onboard a developer to unfamiliar changes
- Audit changes before merging
- Understand the scope and risk of a milestone

## Procedure

### 1. Get the Diff

Run `git diff <base>` to get the full diff. Default base is `HEAD` (unstaged changes). Accept `argument-hint` value as the base ref if provided.

### 2. For Each Changed File, Analyze:

Work through **every** modified, added, or deleted file. For each file, answer all 8 questions:

#### Q1 — Why change?

What gap or requirement drove this file to be modified? Reference the plan, ticket, or architectural invariant.

#### Q2 — Responsibility before vs after

What did this file own before the change? What does it own after? Was it a stub, a placeholder, a working implementation, or new?

#### Q3 — Callers and callees

Which functions/modules call into this file? Which functions/services/modules does this file call? Summarize the dependency graph.

#### Q4 — Happy-path trace

Pick one realistic request that exercises the main success flow. Trace it through execution in order — which line numbers in which files are hit? Use exact file paths and function names.

#### Q5 — Failure-path trace

Pick the most important failure scenario (auth rejection, invalid input, resource exhaustion, protocol violation). Trace the exact path through the code. What error code or exit state results?

#### Q6 — Boundary analysis

Categorize every relevant boundary:

| Boundary type        | What to check                                                            |
| -------------------- | ------------------------------------------------------------------------ |
| **Input validation** | Null checks, bounds checks, type correctness, range limits               |
| **Authorization**    | Token verification, ACLs, first-message enforcement                      |
| **Concurrency**      | Shared state, races, thread safety, synchronization primitives           |
| **I/O**              | Timeouts, partial reads/writes, blocking vs non-blocking, error handling |
| **Persistence**      | File state, socket file cleanup, disk writes                             |

#### Q7 — Acceptance criteria → code map

Map every explicit or implicit acceptance criterion from the plan or spec to exact code lines and test assertions. Mark status: ✅ done, ⚠️ partial, ❌ missing.

#### Q8 — Assumptions, tradeoffs, low-confidence code

- **Assumptions**: What must be true about the environment for this code to work?
- **Tradeoffs**: What was deliberately simplified or deferred?
- **Low-confidence**: Which code paths look fragile, have type/conversion issues, or lack test coverage?

### 3. Write the Output

Create `diff.md` in the project root. Structure:

```markdown
# Diff Analysis: <branch or scope>

**<N> files changed, +<added> / -<deleted> lines**
**Base**: <base-ref>

---

## 1. File-by-File Analysis

### 1.<N> `<path/to/file>`

**Why change**: ...

**Responsibility before**: ... **After**: ...

**Callers**: ... **Callees**: ...

**Happy path**: ...

**Failure path**: ...

**Boundaries**: ...

**Acceptance map**:

| #   | Criterion | Code | Test | Status |
| --- | --------- | ---- | ---- | ------ |

**Assumptions/Tradeoffs**: ...

[repeat for each file]

---

## 2. Happy-Path Request Trace

Full end-to-end trace from entry point to exit, across all files.

---

## 3. Most Important Failure Path

Full trace for the most critical failure scenario.

---

## 4. Boundary Summary

Consolidated table of all boundary types and gaps found.

---

## 5. Acceptance Criterion → Code Mapping

Consolidated table of all criteria across files.

---

## 7. Code Review Findings (Bugs, Risks, Nitpicks)

### Bugs (Sorted by Priority)

| Priority | Component / Location | Description | Impact | Proposed Fix |
| --- | --- | --- | --- | --- |
| **High** | `worker/src/vw_worker.c:39` | Description of critical bug | Worker process crash/deadlock | Proposed fix |
| **Medium** | `protocol/src/vw_ipc_pipe_win32.c:72` | Description of medium bug | Resource leak / failure handling | Proposed fix |
| **Low** | `tests/integration/test_worker_ipc.c:61` | Description of low bug | Minor edge case | Proposed fix |

### Architectural & Operational Risks

| Category | Risk Description | Affected Files | Mitigation Strategy |
| --- | --- | --- | --- |
| **Portability** | Framing behavior differences between Linux and Win32 | `vw_ipc_socket_linux.c`, `vw_ipc_pipe_win32.c` | Ensure full-frame atomic buffer reads |

### Code Style & Quality Nitpicks

| Issue Type | File & Line | Description | Recommendation |
| --- | --- | --- | --- |
| **Compiler Warning** | `vw_worker.c:15` | Signed vs unsigned integer comparison | Cast loop index to `size_t` |
```

### 4. Verify

- Every changed file is covered (check `git diff --stat` count matches sections)
- File paths are exact and absolute within workspace
- Line numbers reference the final (post-diff) file state
- Acceptance criteria map back to the plan/ticket/spec explicitly
- Code review findings (Bugs sorted by priority, Risks, Nitpicks) are formatted strictly as Markdown tables with NO emojis

## References

No external references needed. All inputs come from `git diff` and the workspace files.

## Examples

See the existing `diff.md` in the workspace root for a worked example of this skill's output format.
