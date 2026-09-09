# Engineering Invariants

Compact contract reference for high-risk changes. `AGENTS.md` owns workflow; this file defines the required engineering contract. Known violations in the current base are defects to track and fix, not behavior to normalize or document as compliant.

| Invariant | Required behavior |
| --- | --- |
| Playback isolation | Caption failure never stalls/crashes VLC or corrupts playback. |
| Timeline continuity | Drops, seeks, gaps, swaps, and retries never collapse or invent media time. |
| Explicit states | AGAIN/EOF/recoverable/fatal states remain distinct when semantics differ. |
| Session ownership | Session-scoped state has one authoritative reset/finalize path. |
| Identity integrity | Paths, URIs, IDs, endpoints, model/language/corpus identifiers reject overflow; they do not truncate. |
| Failure classification | External failures map explicitly to retry, recover, fail-session, or fail-process/startup. |
| Metric ownership | Every metric has one producer, units, reset scope, and fallback rule. |
| Realtime safety | VLC audio callbacks do bounded non-blocking capture/enqueue work with zero heap allocation. |
| Privacy/network | Audio transcription stays local; no implicit runtime transcript/PCM persistence. Explicit user subtitle exports and git-ignored developer benchmark text artifacts are allowed. |
| Regression permanence | Fixed ledger defects gain named behavioral regressions where practical. |

## Known deviations on this PR base

These requirements are not claims that every inherited `main` path already complies:

- worker CLI parsing still truncates some oversized identity arguments (`--pipe`, `--vad-model`, `--log-file`); the reject-on-overflow runtime fix/regression is tracked in PR #50;
- live/non-seekable `MEDIA_END` can still discard residual buffered speech instead of flushing it exactly once; the runtime fix/regression is tracked in PR #50.

Remove a deviation only after its implementation and regression coverage have landed in the target branch.

## Cross-component changes

Before implementation, write:

`producer -> boundary -> consumer -> lifecycle owner`

Boundaries include queues, IPC, decoders, threads/processes, filesystem/network calls, benchmark hooks, and dependencies. If correctness depends on composition, add/extend a seam test; a local unit test is insufficient.

After a fix, search every producer, copy, serializer, validator, fallback, and consumer of the same invariant—not only the reported call site.

## State and lifecycle

Do not make callers infer state the producer already knows. `0`, empty output, or generic success must not ambiguously mean retry/EOF/error. Widen APIs instead of adding caller heuristics.

When lifecycle behavior changes, classify START/new media, pause/resume, seek, media swap, EOF/media end, STOP/shutdown, worker failure/respawn, and overload/drop as `unchanged`, `reset`, `preserve`, `flush`, `retry`, or `fail`. New session fields are incomplete until their transition behavior is defined.

## External failures

For changed FFmpeg/MF/Whisper/IPC/filesystem/process/network/init boundaries, define handling first:

- **retry** — bounded transient failure;
- **recover** — continue in an explicitly equivalent safe state;
- **fail session** — captions stop/reset; VLC continues;
- **fail process/startup** — continuation would be unsafe or misleading.

Prefer fail-closed over plausible-but-unverified transcript or benchmark output.

## Metrics and realtime work

Metrics document producer, units, lifetime/reset scope, and time domain. Codec round trips do not prove a metric is meaningful; test its producer.

Audio callbacks must not infer, block on IPC/locks, access files, perform potentially blocking logging, or heap-allocate. Realtime-adjacent instrumentation accumulates bounded in-memory state and publishes elsewhere.

## Test contract

New failure/seam C tests use PR #50-style named accumulating checks (`vw_test_check_*` + one `vw_test_finish`). Expectations describe externally meaningful behavior, not implementation details. See `test-strategy.md`.
