# Documentation Index

Read only the document that owns the contract you are changing.

| Need | Read |
| --- | --- |
| Agent/contributor rules | `../AGENTS.md` |
| Cross-component invariants | `invariants.md` |
| High-risk task plan | `../ai/task-template.md` |
| User/build/benchmark overview | `../README.md` |
| Architecture/lifecycle | `architecture.md` |
| IPC/API semantics | `api-contracts.md` |
| Test conventions | `test-strategy.md` |
| Component ownership | `source-layout.md` |
| Current/future work | `roadmap.md` |
| Quality benchmark semantics | `quality-benchmark.md` |
| Product scope | `product.md` |
| C style | `coding-style.md` |
| Pinned Whisper integration | `whisper-api.md` |
| Pinned VLC integration gotchas | `vlc-api-essentials.md` |

## Historical/reference evidence

Open only when relevant: `decisions.md` (ADRs), `issues.md` / issue #47 (defect history), `quality-benchmark-report.md` (historical measurements), `diagrams.md` (supplemental visuals). These are evidence, not mandatory task context.

## Rules

- Search large files by component, message, ADR, or VW ID; do not load the whole docs set.
- Source and pinned dependency headers win over stale prose; fix documentation drift in the same change.
- Keep one canonical owner for each fact and link rather than duplicate.
- Update docs only when the documented contract changed.
- Plans belong in `plans/`; they are task records, not permanent architecture.

## Ownership

Architecture/design → `architecture.md` (+ ADR for durable decisions). Protocol → `api-contracts.md`. Tests → `test-strategy.md`. Component ownership → `source-layout.md`. User/build behavior → root `README.md`. Roadmap status → `roadmap.md`. Process/invariants → root `AGENTS.md` / `invariants.md`.
