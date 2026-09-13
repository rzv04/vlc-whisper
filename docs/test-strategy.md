# Test Strategy

**Captioning must never harm playback.** Prefer externally meaningful contracts over implementation details; cross-component seams are the highest-risk class.

| Layer | Purpose |
| --- | --- |
| Unit | Pure/local logic and bounds |
| Contract | API states, validation, ownership, overflow |
| Seam/integration | Threads, queues, decoder->worker, IPC, lifecycle, process/filesystem boundaries |
| VLC/E2E | Real plugin/playback behavior |
| Performance | Latency, RTF, queue pressure, bounded resources |
| ASR regression | Local EN/RO WER/CER |
| Security/privacy | Auth, path/log/network boundaries |

## Named accumulating specs

New failure/seam C tests follow PR #50's Jasmine-like convention:

```c
vw_test_check_true("decoder AGAIN does not become EOF", condition_a);
vw_test_check_false("failed seek leaves stale captions visible", condition_b);
return vw_test_finish("test_decoder_contract");
```

Expectation names describe behavior; independent checks accumulate; one `vw_test_finish` returns the result. Legacy `EXPECT` remains acceptable for small existing fail-fast unit tests. Never weaken a spec or special-case production code to obtain green output.

## Failure-path first

For meaningful behavior changes, write the failure/boundary/seam spec first and confirm it fails for the intended reason. PR evidence identifies red-before/green-after behavior.

A change crossing a thread, process, queue, decoder, IPC frame, lifecycle epoch, filesystem, or network boundary requires a seam test unless equivalent named regression coverage already exists.

## Contract families

Cover the affected family:

- **Timeline:** drops/gaps/discontinuities preserve time.
- **Decoder:** AGAIN, EOF, recoverable, fatal remain distinct; partial failure is not clean success.
- **Lifecycle:** START, pause/resume, seek, swap, EOF/tail flush, STOP/shutdown, respawn, overload.
- **Reset:** new sessions do not inherit stale pause/decoder/buffer/VAD/translation/caption/benchmark state unless specified.
- **Identity:** oversized paths/URIs/models/corpus IDs/endpoints reject before truncation.
- **IPC:** semantic validation plus observable send/receive/disconnect handling.
- **Metrics:** authoritative producers populate documented units/reset scope.
- **Benchmark:** malformed schema, empty reference, hash/duration mismatch, incomplete processing, or output failure cannot emit valid-looking scores.
- **Realtime instrumentation:** no per-frame blocking I/O or material observer effect.

## Fault injection

Inject relevant transient/no-progress, explicit EOF vs error, seek/reset, IPC disconnect, allocation/init, child-process, filesystem, malformed-input, and network failures where practical. Every injected failure maps to the plan's `retry`, `recover`, `fail session`, or `fail process/startup` classification.

## Regressions and fixtures

Treat issue #47 / `docs/issues.md` as a regression specification. Fixed defects gain named behavioral tests where practical; search by invariant or VW ID rather than loading the ledger wholesale.

The P1 defect resolution suite adds regression coverage across:
- `tests/unit/test_log.c`: Multi-instance logger concurrency, `g_log_mutex` synchronization, and atomic `vw_log_flush()` (VW-018).
- `tests/unit/test_caption_presenter.c`: Non-ephemeral SPU subpicture persistence (`b_ephemer = false`, VW-001), OSD channel 1 preservation (VW-020), and model progress channel lifecycle (VW-002).
- `tests/unit/test_audio_capture.c`: High-playback-rate (>4.0x) audio drop throttling (VW-019).
- `tests/unit/test_oversized_uri_rejected_before_truncation.c`: Safe URI length validation before copy (Finding #22).
- `tests/unit/test_protocol_start_failure_paths.c` & `tests/unit/test_worker_config_failure_paths.c`: Reject-on-overflow boundary checks.
- `tests/integration/test_queue_audio_timeline.c`, `test_decoder_again_never_becomes_eof.c`, `test_source_error_never_becomes_clean_eof.c`, `test_live_media_end_flushes_tail.c`, and `test_new_session_resets_all_session_state.c`: Seam tests for queue timeline gaps, decoder three-way state transitions, and media-end tail flushing.

Fixtures must be legal, small, deterministic, and versioned when committed. Never commit user/proprietary media, personal transcripts, or production model binaries. Pin model hash and exact Whisper revision for model-sensitive regressions.

The EN/RO benchmark is developer-only and headless; invalid corpus evidence is rejected, not scored. See `quality-benchmark.md`.

## Verification

```bash
clang-format --dry-run --Werror <modified-c-files>
cmake --preset linux-x64-debug
cmake --build --preset linux-x64-debug
ctest --preset linux-x64-debug --output-on-failure
ctest --test-dir build/linux-x64-debug -T memcheck
```

Model-gated tests may skip when their documented local model is absent. This invariant-rule work intentionally adds no UBSan/TSan/static-analysis CI gates.
