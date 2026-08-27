# Implementation Task Template

# Task: <short imperative title>

## Goal
One user-visible or externally verifiable outcome:

## Context
- Relevant docs/ADR:
- VLC/worker/protocol version affected:
- Assumptions and explicit non-goals:

## Scope
- In scope:
- Out of scope:
- Files/components expected to change:

## Design
- Inputs and outputs:
- Ownership/threading model:
- Bounds, time units, and failure behavior:
- Privacy/security implications:
- Protocol change: none | compatible minor | breaking major

## Acceptance criteria
- [ ] Observable expected behavior
- [ ] Failure behavior preserves VLC playback
- [ ] Limits/validation are explicit
- [ ] Automated tests cover success and failure
- [ ] Documentation/version metadata updated

## Test plan
Exact commands, fixtures, target OS/VLC build, and manual verification steps.

## Definition of done
- [ ] C17 code; no project-authored C++ introduced
- [ ] No blocking work in VLC audio callback
- [ ] No unapproved network access, telemetry, transcript/PCM persistence, or sensitive logs introduced; any approved egress is documented and opt-in
- [ ] Memory, audio queue, frame, text, and retry limits are bounded
- [ ] Error path is safe: captions may stop, playback does not
- [ ] Unit/contract/integration tests pass as applicable
- [ ] Formatting, warnings-as-errors, and static checks pass
- [ ] Protocol contract and compatibility version updated if needed
- [ ] `docs/decisions.md`, roadmap, and AI context updated when assumptions change
- [ ] Reviewer can reproduce the result from a clean checkout

## Evidence
- Build/test outputs or CI links:
- Measured performance (if relevant):
- Known limitations/follow-ups:
```

## Slice rule

A task should cut vertically through the smallest necessary layers: e.g., “show a deterministic timed caption from a worker” includes protocol fixture, receiver validation, presenter integration, and end-to-end proof. Do not create UI/API/database layers merely because a generic template expects them: MVP has no GUI, HTTP API, or database.
