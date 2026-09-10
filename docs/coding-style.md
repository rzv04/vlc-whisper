# Coding Style

Formatting is governed by root `.clang-format`; do not duplicate formatter rules here.

## Project rules

- Project-authored runtime code is standard **C17**.
- Use 2-space indentation and the repository's 120-column `clang-format` limit.
- Project symbols/files use the `vw_` namespace; typedefs end in `_t`, constants/macros use `VW_`.
- Headers are self-contained and use guards; include the corresponding project header first in `.c` files, then standard, third-party, and project headers.
- New/changed public header functions need a brief behavior/ownership/realtime comment.
- Do not place inference, blocking IPC/locks, filesystem operations, or heap allocation in VLC audio callbacks.
- Identities (paths/URIs/IDs/endpoints) reject overflow instead of truncating.
- Use signed 64-bit microsecond time values where the protocol/owner specifies them; never mix clock domains implicitly.
- Logs never contain PCM, subtitle bodies, auth tokens, or credentials.

## Commands

```bash
clang-format -i path/to/file.c
clang-format --dry-run --Werror <modified-c-files>
```

Architectural behavior belongs in `invariants.md` / `architecture.md`, not in this style guide.
