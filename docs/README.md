# Documentation Index

Use this page to avoid loading unrelated documentation. Read the smallest document/section that owns the contract you are changing.

## Start here

| Need | Read |
| --- | --- |
| Agent/contributor rules | `../AGENTS.md` |
| Cross-component invariants | `invariants.md` |
| High-risk implementation plan | `../ai/task-template.md` |
| User install/config/build basics | `../README.md` |
| Test approach/conventions | `test-strategy.md` |
| Repository ownership/layout | `source-layout.md` |

## Reference docs — open only when relevant

| Topic | Document |
| --- | --- |
| Architecture/process boundaries | `architecture.md` |
| Binary IPC/API behavior | `api-contracts.md` |
| whisper.cpp integration details | `whisper-api.md` |
| VLC API notes | `vlc-api-essentials.md` |
| Product intent | `product.md` |
| Current/future work | `roadmap.md` |
| Historical architectural decisions | `decisions.md` |
| Known defect history | `issues.md` and GitHub issue #47 |
| Quality benchmark usage/semantics | `quality-benchmark.md` |
| Historical benchmark findings | `quality-benchmark-report.md` |
| Diagrams | `diagrams.md` |
| Style details | `coding-style.md` |

## Reading policy

- Do **not** read every document before a task.
- Search large reference files for the affected component, message, invariant, ADR, or VW defect ID and open only the relevant section.
- Prefer current source code over stale narrative when they disagree; document the discrepancy in the same change.
- Put a fact in one canonical document and link to it elsewhere instead of copying it.
- Plans belong in `plans/` and should be task-specific; they are not permanent architecture documentation.
- Historical reports/ledgers are evidence, not mandatory context for unrelated work.

## Documentation ownership

- Architecture/design contract changed -> `architecture.md` and, for a durable decision, `decisions.md`.
- Invariant/process contract changed -> `invariants.md` / root `AGENTS.md`.
- Protocol/wire/API changed -> `api-contracts.md`.
- Tests/fixtures/conventions changed -> `test-strategy.md`.
- File/component ownership changed -> `source-layout.md`.
- User-visible installation/configuration/build behavior changed -> root `README.md`.
- Roadmap status changed -> `roadmap.md`.

Avoid incidental documentation churn when the contract did not change.
