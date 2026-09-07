# Engineering Invariants

This is the compact contract reference for high-risk changes. `AGENTS.md` defines workflow; this file defines what must remain true.

## Core contracts

| Invariant | Required behavior | Typical evidence |
| --- | --- | --- |
| Playback isolation | Caption failure never stalls/crashes VLC or corrupts playback | failure-path/seam test |
| Timeline continuity | Audio/caption PTS stays media-correct across drops, seeks, gaps, and swaps | gap/discontinuity test |
| Explicit states | Transient, EOF, recoverable error, and fatal error are distinct when semantics differ | typed result + negative test |
| Session ownership | One owner/reset/finalize path covers every session-scoped field | lifecycle test |
| Identity integrity | Paths/URIs/IDs/endpoints never silently truncate | boundary test |
| Failure classification | External/API failures map explicitly to retry, recover, session-fail, or process-fail | injected failure test |
| Metric ownership | Every metric has one producer, units, reset domain, and fallback rule | producer behavioral test |
| Realtime safety | VLC audio callback performs bounded non-blocking work only | review + targeted test where practical |
| Privacy/network | Audio transcription stays local; only documented opt-in/explicit network paths exist | boundary/review evidence |
| Regression permanence | Fixed known defects remain represented by named behavioral regressions where practical | test named for behavior/ledger ID |

## Cross-component rule

For a changed behavior, write the path before implementation:

`producer -> boundary -> consumer -> lifecycle owner`

A boundary can be a queue, IPC frame, decoder API, filesystem object, process, thread handoff, benchmark hook, or external dependency. If correctness depends on composition, a unit test of one module is insufficient; add a seam/integration test.

After fixing an invariant, search every producer, copy, serializer, validator, fallback, and consumer of that invariant. Do not stop at the reported call site.

## State/API rule

Do not make callers guess a state the producer already knows. In particular:

- `0`, empty output, or success must not ambiguously mean AGAIN/EOF/ERROR.
- Failed operations must not be converted into plausible clean EOF/success unless an explicit recovery policy says so.
- A new feature that needs more semantic states should widen the API instead of adding caller heuristics.

## Lifecycle matrix

When session state or control flow changes, record every affected cell as `unchanged`, `reset`, `preserve`, `flush`, `retry`, or `fail`.

| Transition | Questions that must be answered |
| --- | --- |
| START/new media | Which fields/queues/anchors/generations initialize? |
| Pause/resume | What is retained; what is invalidated? |
| Seek/discontinuity | Which epoch/session IDs, captions, buffers, decoder anchors, translations reset? |
| Media swap | Can any state from the previous item survive? |
| EOF/media end | Is residual/held-back speech finalized exactly once? |
| STOP/shutdown | What is flushed, cancelled, joined, or deliberately discarded? |
| Worker failure/respawn | What state is rebuilt; what must not be reused? |
| Overload/drop | Is media time preserved despite data loss? |

Session-scoped fields should live behind one authoritative reset/finalize transition when practical. Adding a session field without defining these transitions is incomplete.

## External failure semantics

For every changed external boundary (FFmpeg, Media Foundation, whisper.cpp, IPC, filesystem, process, network, allocation/init), define expected handling before implementation:

- **retry** — transient and bounded;
- **recover** — continue with an explicitly equivalent safe state;
- **fail session** — captions stop/reset while VLC playback continues;
- **fail process/startup** — continuing would be unsafe or misleading.

Prefer fail-closed over a plausible but unverified transcript/benchmark result.

## Identity rule

Identity-bearing data is reject-on-overflow: filesystem paths, URIs, pipe/socket names, model IDs, session/corpus IDs, language/config identifiers, hashes, and similar keys. Checked helpers must return failure when the complete value does not fit. Truncation is reserved for human-facing diagnostics.

## Metric rule

Any new or changed metric documents:

- producer/owner;
- units;
- per-session vs process lifetime;
- reset point;
- input/media/wall/inference-time meaning;
- whether fallback values are permitted.

Codec round-trip tests do not prove a metric is meaningful; test the producer.

## Realtime rule

VLC audio callbacks must not perform inference, IPC or filesystem I/O, logging that can block, blocking waits/locks, or unbounded allocation. Benchmark/instrumentation hooks on realtime-adjacent paths should accumulate bounded in-memory state and publish outside the measured hot path.

## Test contract

Behavioral tests use concise human-readable expectation names and, for new failure/seam tests, accumulate independent failures before one final result (`vw_test_check_*` + `vw_test_finish`). Tests assert externally meaningful contracts rather than internal implementation details.
