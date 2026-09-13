# Agent Entry Point

Use the repository-root `AGENTS.md` as the single source of truth for coding, invariant, test, documentation, and verification rules.

Before substantial work:

1. Read root `AGENTS.md`.
2. Read the changed code and only the technical documentation relevant to that contract.
3. Use `docs/invariants.md` for cross-component invariants.
4. Use `ai/task-template.md` for meaningful/high-risk implementation plans.
5. Load a skill under `.agents/skills/` only when that skill is directly needed for the task.

Do not duplicate root rules here; duplicated policy drifts and wastes context.
