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

- Red-before-implementation specs:
  - parse multiline SRT cues while preserving cue IDs and timestamps;
  - reject malformed timestamp blocks;
  - render translated cues without changing timing metadata;
  - percentile calculation is deterministic for small/odd/even sample sets;
  - realtime deadline hit-rate calculation is correct;
  - benchmark summary keeps model-load time separate from cue latency.
- Existing ledger regressions affected: none.
- Faults to inject: malformed SRT, empty samples, and deadline misses.

## Implementation

1. Add pure spike-core SRT and benchmark-stat helpers with unit tests.
2. Add a thin Bergamot engine adapter using `BlockingService`, `TranslationModel`, `parseOptionsFromFilePath`, and `ResponseOptions::HTML` so basic subtitle markup can survive translation.
3. Add a small CLI with `translate` and `benchmark` subcommands.
4. Pin Bergamot source in the spike CMake via `FetchContent`; keep the dependency isolated from the repository root build.
5. Add the Mozilla model-fetch helper, defaulting to the smallest matching `Release` model when available and generating a local YAML config.
6. Document Ubuntu setup, benchmark interpretation, Windows/Ubuntu installer staging, and MPL-2.0/MIT obligations.

## Verification

- [ ] New spike unit tests pass.
- [ ] Spike configures and builds on the available Ubuntu environment when network/build dependencies permit.
- [ ] `translate --help` and `benchmark --help`/usage path are documented.
- [ ] Model downloader syntax and generated YAML are source-reviewed against Mozilla's current registry schema.
- [ ] `git diff --check` equivalent review for created text files.
- [ ] Production root build files remain unchanged.

## Evidence

- Upstream Bergamot is MPL-2.0 and explicitly exposes a native library intended for embedding.
- Bergamot's Marian dependency is MIT licensed.
- Mozilla's current `mozilla/translations` README states that the published model files are distributed under MPL-2.0.
- MPL-2.0 permits static linking into a larger work while keeping non-MPL files under their own license, provided recipients are informed how to obtain the MPL-covered source and MPL notices/rights are preserved.
- Known limitation/follow-up: before production installer promotion, inventory all transitive third-party notices generated by the pinned Bergamot dependency and add them to the installer/legal notice bundle.