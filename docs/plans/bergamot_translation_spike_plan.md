# Task: Build an isolated Bergamot subtitle translation spike

## Outcome

Provide a standalone `spikes/bergamot-translation` prototype that translates UTF-8 SRT files locally with Bergamot, reports per-cue latency percentiles for realtime feasibility, and documents a packaging path for Windows and Ubuntu without changing production plugin/worker behavior.

## Scope

- In:
  - Isolated C++17 spike executable under `spikes/bergamot-translation/`.
  - UTF-8 SRT parsing/rendering with cue number/timestamp preservation.
  - Direct native `bergamot-translator` library integration pinned to a known upstream commit.
  - Translation mode for a complete SRT file.
  - Benchmark mode reporting cold model-load time, p50/p95/p99/max cue latency, throughput, and the share of cues meeting a configurable realtime budget (default 800 ms).
  - Small standard-library-only helper to download a Mozilla Bergamot model from Mozilla's public model registry, verify the model hash when provided, decompress it, and generate a ready-to-use Bergamot YAML config.
  - README with Ubuntu build/run/benchmark steps, Windows packaging notes, and license/redistribution obligations.
  - Narrow `AGENTS.md` C++ exception for this explicitly requested isolated spike; production C17 rules remain unchanged.
- Out:
  - No root CMake integration.
  - No plugin, worker, protocol, Lua/Qt settings, installer, CI, or production translation changes.
  - No MKV subtitle demuxing in the spike; input is an existing `.srt` file.
  - No Google translation fallback.
  - No production model downloader or auto-update policy.
- Components/files:
  - `spikes/bergamot-translation/CMakeLists.txt`
  - `spikes/bergamot-translation/src/*`
  - `spikes/bergamot-translation/tests/*`
  - `spikes/bergamot-translation/scripts/fetch_mozilla_model.py`
  - `spikes/bergamot-translation/README.md`

## Contract map

`SRT file -> spike parser -> ordered cue vector -> Bergamot native model -> translated cue text -> SRT renderer -> output SRT`

`ordered cue vector -> latency benchmark runner -> percentile/deadline statistics -> stdout report`

`Mozilla model registry -> model fetch helper -> verified/decompressed model files + generated YAML -> Bergamot model loader`

- Invariants touched: standalone spike only; subtitle cue identity/timestamps must remain unchanged; benchmark units are milliseconds; network access exists only in the explicit model-fetch helper; translation inference is local.

## Failure semantics

| Boundary/failure | retry | recover | fail command | fail process/startup |
| --- | --- | --- | --- | --- |
| malformed SRT cue | no | no | yes | no |
| missing/unreadable input | no | no | yes | no |
| Bergamot config/model load failure | no | no | yes | no |
| translation exception/empty response | no | no | yes | no |
| model registry/download transient error | helper may be rerun | yes | yes | no |
| downloaded model hash mismatch | no | delete temp/partial | yes | no |

## Lifecycle impact

None. The spike is not loaded by VLC and is not part of the production worker lifecycle.

## Identity / metrics / hot path

- Identity-bearing values changed; overflow behavior: cue numbers and timestamps are parsed as strings and emitted unchanged.
- Metrics changed; owner, units, reset domain, fallback: spike-only benchmark owns per-cue durations in milliseconds; every benchmark invocation starts from an empty sample set. Model-load time is reported separately and excluded from warm per-cue percentiles.
- Realtime-adjacent code changed; why callback restrictions remain satisfied: none. No VLC callback code is touched.

## Tests first

- Specs were authored before production spike implementation for:
  - parse multiline SRT cues while preserving cue IDs and timestamps;
  - reject malformed timestamp blocks;
  - render translated cues without changing timing metadata;
  - percentile calculation is deterministic for small/odd/even sample sets;
  - realtime deadline hit-rate calculation is correct;
  - benchmark summary keeps model-load time separate from cue latency.
- Existing ledger regressions affected: none.
- Faults injected: malformed SRT and empty benchmark samples; the deterministic sample set also exercises deadline misses.
- Limitation: the connector-only repository environment did not permit executing the test-only commit before implementation, so red-before-green execution evidence could not be captured even though tests were committed first.

## Implementation

1. Add pure spike-core SRT and benchmark-stat helpers with unit tests.
2. Add a thin Bergamot engine adapter using `BlockingService`, `TranslationModel`, `parseOptionsFromFilePath`, and `ResponseOptions::HTML` so basic subtitle markup can survive translation.
3. Add a small CLI with `translate` and `benchmark` subcommands.
4. Pin Bergamot source in the spike CMake via `FetchContent`; keep the dependency isolated from the repository root build.
5. Add the Mozilla model-fetch helper, defaulting to the smallest matching `Release` model when available, supporting both shared and split source/target vocabularies, and generating a local YAML config.
6. Document Ubuntu setup, benchmark interpretation, Windows/Ubuntu installer staging, and MPL-2.0/MIT obligations.

## Verification

- [x] Spike-core contract tests compile under strict C++17 (`-Wall -Wextra -Werror -pedantic`) and all 21 checks pass.
- [ ] Full Bergamot-backed target builds in this execution environment. The sandbox cannot resolve external Git hosts, so the pinned dependency cannot be fetched here; README provides the exact VM build command.
- [x] `translate` and `benchmark` command usage is documented, including JSON benchmark output and the 800 ms realtime-candidate interpretation.
- [x] Model downloader and generated YAML were source-reviewed against Mozilla's current 2026 model registry and Mozilla's own Bergamot evaluation config; shared and split vocab schemas are supported.
- [ ] `clang-format --dry-run --Werror` was not available in the execution sandbox; source was kept to repository formatting conventions and should be run on the development VM.
- [x] Branch comparison against `main` confirms no production CMake, plugin, worker, protocol, settings, CI, or installer implementation file changed.

## Evidence

- Upstream Bergamot is MPL-2.0 and explicitly exposes a native library intended for embedding.
- Bergamot's Marian dependency is MIT licensed.
- Mozilla's current `mozilla/translations` README states that the published model files are distributed under MPL-2.0 and compatible with Bergamot.
- MPL-2.0 permits static linking into a larger work while keeping non-MPL files under their own license, provided recipients are informed how to obtain the MPL-covered source and MPL notices/rights are preserved.
- Mozilla's current registry exposes EN -> RO as a released `tiny` model with a 17,141,051-byte uncompressed neural model file; the spike helper selects it automatically for `--source en --target ro`.
- Known follow-up before production installer promotion: run a full transitive dependency/license inventory for the exact pinned Bergamot build, add required notices/source links, and independently validate the Windows toolchain path.