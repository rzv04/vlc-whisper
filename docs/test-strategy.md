# Test Strategy

## Principle

**Captioning must never harm playback.** Prefer tests of externally meaningful contracts over implementation details. Cross-component failures are the highest-risk class; unit coverage alone is not sufficient for a changed seam.

See `invariants.md` for the contracts tests must preserve.

## Layers

| Layer | Purpose |
| --- | --- |
| Unit | Pure/local logic, bounds, codecs, helpers |
| Contract | API semantic states, validation, ownership, overflow behavior |
| Seam/integration | Threads, queues, decoder->worker, IPC, lifecycle, process/filesystem boundaries |
| VLC/E2E | Actual plugin lifecycle and playback behavior |
| Performance | Latency, RTF, queue pressure, bounded resource use |
| ASR regression | Local EN/RO WER/CER quality checks |
| Security/privacy | IPC/auth, path handling, log/network boundaries |

## New test style: named accumulating specs

New behavioral/failure-path C tests follow the PR #50 Jasmine-like convention:

```c
vw_test_check_true("decoder AGAIN does not become EOF", condition_a);
vw_test_check_false("failed seek leaves stale captions visible", condition_b);
return vw_test_finish("test_decoder_contract");
```

Rules:

- expectation names describe behavior in plain language;
- independent checks accumulate so one execution reports multiple violated contracts;
- use `vw_test_check_true` / `vw_test_check_false` + one `vw_test_finish` for new contract/seam tests;
- legacy `EXPECT` macros remain valid for existing small fail-fast unit tests;
- never weaken a spec or special-case production code merely to make it green.

## Failure-path first

For meaningful behavior changes, write the failure/boundary/seam spec before implementation and confirm it fails for the intended reason. PR evidence should identify the red-before/green-after spec.

When a change crosses a thread, process, queue, decoder, IPC frame, lifecycle epoch, filesystem, or network boundary, include at least one seam test unless the behavior is already covered by an equivalent named regression.

## Required contract families

Add/extend tests when the affected invariant changes:

- **Timeline:** queue drop/gap/discontinuity preserves media time; PTS never silently collapses.
- **Decoder states:** AGAIN, EOF, recoverable error, and fatal error remain distinguishable; partial failure is not scored/reported as clean success.
- **Lifecycle:** START/new media, pause/resume, seek, media swap, EOF/tail flush, STOP/shutdown, worker failure/respawn, overload/drop.
- **State reset:** a new session cannot inherit pause, decoder anchors, EOF flags, audio/VAD/translation generations, captions, or benchmark state unless explicitly specified.
- **Identity:** oversized path/URI/model/corpus/endpoint IDs are rejected before any truncating copy.
- **IPC:** validators enforce semantic as well as structural bounds; send/receive failures have observable handling.
- **Metrics:** production owner populates meaningful values with documented units/reset scope; codec round trips alone are insufficient.
- **Benchmark integrity:** malformed schema, empty reference, hash/duration mismatch, failure/EOF ambiguity, and output-write failure cannot emit a valid-looking score.
- **Realtime instrumentation:** benchmark/test hooks do not add per-frame blocking I/O or materially change queue/drop behavior.

## Fault injection

For changed external boundaries, deliberately cover relevant failure classes where practical:

- transient/no-progress (`EAGAIN`, NULL/no sample without EOF);
- explicit EOF vs non-EOF decoder error;
- resampler/conversion/seek dependent-reset failure;
- IPC send/receive/disconnect failure;
- allocation/init failure where injectable;
- process child timeout/nonzero exit;
- filesystem unwritable/full/invalid output;
- malformed JSON/schema/hash/truncated fixture;
- network/subprocess failure for explicitly networked features.

Each injected failure must map to the handling declared in the plan: retry, recover, fail session, or fail process/startup.

## Known-defect regressions

Treat the master ledger (GitHub issue #47 / `docs/issues.md`) as a regression specification. When a defect is fixed, add a named behavioral regression where practical. Prefer names that state the invariant (`queue_drop_preserves_media_timeline`) and optionally reference the VW ID in a short comment/CTest label rather than encoding implementation details in the assertion.

Do not require every historical ledger entry to be loaded into agent context; search by affected invariant or VW ID.

## Fixtures

Fixtures must be legal, small, deterministic, and versioned when committed. Never commit proprietary media, user audio/transcripts, or production model binaries.

For model-dependent tests, pin the model hash and exact whisper.cpp commit. Do not silently re-baseline output after dependency changes.

## Local ASR regression corpus

`tools/quality_benchmark/` is developer-only and headless. Corpus audio and reports stay local/git-ignored. Before scoring, validate schema, unique IDs, non-empty references, SHA-256, WAV format, and actual duration. Invalid fixtures are invalid—not zero-error samples.

Live/look-ahead benchmark completion must use production-observable lifecycle state rather than synthetic timing assumptions. Benchmark hooks accumulate bounded in-memory state and must not perform synchronous filesystem work per audio frame. Full usage and scoring semantics live in `quality-benchmark.md`; terse commands live in `../tools/quality_benchmark/README.md`.

## Verification

Run the smallest focused test while iterating, then the repository verification before completion:

```bash
clang-format --dry-run --Werror <modified-c-files>
cmake --preset linux-x64-debug
cmake --build --preset linux-x64-debug
ctest --preset linux-x64-debug --output-on-failure
ctest --test-dir build/linux-x64-debug -T memcheck
```

Model-gated tests may skip when their documented fixture/model is absent. Do not add UBSan/TSan or other new CI gates as part of the invariant-rule pass.
