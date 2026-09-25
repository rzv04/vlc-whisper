# VLC-Whisper Bergamot Translation Spike

This directory is an isolated feasibility prototype for **fully local subtitle translation** with [Bergamot Translator](https://github.com/browsermt/bergamot-translator), the Marian-based translation engine used by Firefox Translations.

It is deliberately not connected to VLC, the production worker, IPC, the root CMake build, CI, or either installer. The spike answers three questions first:

1. Can a small local translation model translate normal subtitle cues fast enough for realtime use?
2. Can the same engine translate a complete SRT file while preserving cue identity/timestamps?
3. Is the engine/model licensing and build shape practical for later Windows and Ubuntu distribution?

## What is included

- `vw-bergamot-translate-spike`: standalone C++17 executable with two commands:
  - `translate`: translate a UTF-8 `.srt` file locally and write another `.srt`.
  - `benchmark`: translate cues one at a time and report model-load time, p50/p95/p99/max latency, throughput, and deadline hit rate.
- `scripts/fetch_mozilla_model.py`: standard-library-only downloader for Mozilla's public Bergamot model registry. It downloads/decompresses a language pair, verifies the published model SHA-256 when present, and generates `model.yml`.
- `vw_bergamot_engine`: a very small project-owned C++ wrapper around Bergamot. Bergamot headers do not escape this adapter, which makes later replacement or embedding easier.
- `vw_bergamot_spike_core`: Bergamot-independent SRT parsing/rendering and benchmark statistics with unit tests.

The translator uses Bergamot's HTML-aware response mode so basic subtitle markup such as `<i>...</i>` is handled by Bergamot rather than blindly passed through the neural model.

## Fast Ubuntu setup

The repository currently uses Ubuntu development environments, and this spike is easiest to validate there first.

Install build dependencies:

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake ninja-build git python3 \
  libopenblas-dev pkg-config
```

### 1. Run the spike-core tests without downloading Bergamot

This verifies SRT parsing/timestamp preservation and benchmark percentile math only:

```bash
cmake -S spikes/bergamot-translation \
      -B build/bergamot-spike-core \
      -G Ninja \
      -DVW_BERGAMOT_BUILD_ENGINE=OFF
cmake --build build/bergamot-spike-core
ctest --test-dir build/bergamot-spike-core --output-on-failure
```

### 2. Build the full Bergamot CLI

The spike pins `browsermt/bergamot-translator` commit:

```text
9271618ebbdc5d21ac4dc4df9e72beb7ce644774
```

By default CMake fetches that source and its submodules automatically. The Linux spike disables Bergamot's `USE_STATIC_LIBS` option so Marian can discover the system BLAS implementation; this avoids a known native-build failure mode where Marian starts without BLAS support.

```bash
cmake -S spikes/bergamot-translation \
      -B build/bergamot-spike \
      -G Ninja \
      -DCMAKE_BUILD_TYPE=Release
cmake --build build/bergamot-spike -j2
```

If you already have a Bergamot checkout with its submodules initialized, avoid the FetchContent clone:

```bash
git clone https://github.com/browsermt/bergamot-translator.git ~/src/bergamot-translator
cd ~/src/bergamot-translator
git checkout 9271618ebbdc5d21ac4dc4df9e72beb7ce644774
git submodule update --init --recursive
cd -

cmake -S spikes/bergamot-translation \
      -B build/bergamot-spike \
      -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DVW_BERGAMOT_SOURCE_DIR="$HOME/src/bergamot-translator"
cmake --build build/bergamot-spike -j2
```

## Download a translation model

The helper reads Mozilla's current public model registry. `--architecture auto` prefers released models and chooses the smallest released model for the requested direction.

For the first VLC-Whisper test, use English -> Romanian:

```bash
python3 spikes/bergamot-translation/scripts/fetch_mozilla_model.py \
  --source en \
  --target ro \
  --architecture auto \
  --output-dir build/bergamot-models/en-ro
```

The output directory contains roughly this shape:

```text
build/bergamot-models/en-ro/
  model.enro....bin
  vocab.enro.spm
  lex....enro.s2t.bin
  model.yml
  manifest.json
```

`manifest.json` records the selected architecture/release status, model registry timestamp, local SHA-256 values, and licensing note. For a production release, do **not** silently follow the moving registry: pin the exact files/hashes selected during release qualification.

At the time this spike was created, Mozilla's registry selected a released `tiny` EN->RO model whose neural model file is about 17.1 MB uncompressed. Vocabulary and shortlist assets add additional size.

## Translate an SRT file

```bash
build/bergamot-spike/vw-bergamot-translate-spike translate \
  --model-config build/bergamot-models/en-ro/model.yml \
  path/to/movie.en.srt \
  --output path/to/movie.ro.srt
```

If `--output` is omitted, the executable writes `<input>.translated.srt` next to the input file.

The spike keeps each cue number and timing line unchanged. Only the cue text is replaced.

## Benchmark realtime feasibility

Use real subtitle text rather than synthetic single sentences. A few hundred cues is enough for an initial latency distribution; a full movie gives the best tail-latency picture.

```bash
build/bergamot-spike/vw-bergamot-translate-spike benchmark \
  --model-config build/bergamot-models/en-ro/model.yml \
  path/to/movie.en.srt \
  --budget-ms 800 \
  --warmup 10 \
  --repeat 3
```

For a faster first pass:

```bash
build/bergamot-spike/vw-bergamot-translate-spike benchmark \
  --model-config build/bergamot-models/en-ro/model.yml \
  path/to/movie.en.srt \
  --limit 200 \
  --warmup 10 \
  --budget-ms 800
```

Machine-readable output:

```bash
build/bergamot-spike/vw-bergamot-translate-spike benchmark \
  --model-config build/bergamot-models/en-ro/model.yml \
  path/to/movie.en.srt \
  --budget-ms 800 \
  --json > bergamot-benchmark.json
```

The report contains:

- `model_load_ms`: cold model construction/load time. This is deliberately **excluded** from cue percentiles because a production worker would keep the model resident.
- `min_ms`, `mean_ms`, `p50_ms`, `p95_ms`, `p99_ms`, `max_ms`: warm one-cue-at-a-time translation latency.
- `deadline_hit_rate_percent`: percentage of measured cues completing at or below `--budget-ms`.
- `cues_per_second` and `characters_per_second`: overall measured throughput.
- `realtime_candidate_p95`: `yes` when measured p95 is at or below the selected budget.

### How to interpret the 800 ms result

`p95 <= 800 ms` is only a **local translation-engine candidate signal**. It does not include Whisper inference, worker queueing, IPC, VLC presentation, or scheduling overhead. For the current live-ASR translation path, leave meaningful headroom; a p95 of 750-790 ms would technically pass this benchmark but is not a comfortable 800 ms end-to-end design.

For an existing SRT/embedded subtitle track, the requirement is much easier: the subtitle text and timestamps are known ahead of playback, so VLC-Whisper can translate 30-60 seconds ahead and cache results. In that mode even a translation engine slower than the live-ASR budget can still provide zero visible subtitle delay.

The benchmark intentionally translates **one cue at a time** after warmup. That is the relevant shape for genuine live caption translation. Whole-file translation can later use Bergamot batching for higher throughput.

## Architecture and production embedding

The important boundary is intentionally small:

```text
subtitle cue text
    -> vw::spike::BergamotEngine::translate(text)
    -> translated UTF-8 text
```

`vw_bergamot_engine.h` contains no Bergamot types. A production implementation can therefore move this adapter into a worker-owned C++ component, expose a narrow C ABI to the existing C17 worker, or keep it as a dedicated translation sidecar executable without changing the higher-level subtitle policy.

### Ubuntu packaging path

The lowest-risk promotion path is:

1. build the Bergamot adapter/sidecar as part of the Linux release pipeline;
2. package the executable/library plus required runtime dependency notices;
3. either bundle qualified language model assets or keep the current on-demand model-provisioning pattern;
4. keep the model resident for the playback session rather than starting a process/model per cue.

The spike currently links against system OpenBLAS on Linux for easy native development. A production package can declare the appropriate distro dependency or deliberately stage a compatible runtime after a separate licensing/packaging review.

### Windows packaging path

VLC-Whisper's current production Windows preset cross-compiles with MinGW, while Bergamot upstream has explicit MSVC build handling and does not document MinGW as a supported Windows configuration. Do not force the first production experiment into the existing C worker target.

The easiest installer-compatible path is initially a **small MSVC-built local translation sidecar** using this same adapter. NSIS can stage that executable next to the existing worker just as it stages other project executables. Bergamot's upstream MSVC configuration uses the static MSVC runtime, reducing clean-machine runtime setup. Once MinGW compatibility is independently proven, the adapter can instead be linked behind a C ABI into a worker-owned component if that is still desirable.

This keeps the prototype compatible with both installers without imposing C++ or a new toolchain on the realtime VLC plugin itself.

## Licensing and redistribution

The licensing result is favorable for VLC-Whisper distribution, with normal open-source notice/source obligations:

- **Bergamot Translator:** MPL-2.0.
- **Marian:** MIT.
- **Mozilla translation model files:** Mozilla's current `mozilla/translations` README explicitly states that the model files are distributed under **MPL-2.0**.
- **VLC-Whisper project-owned spike code:** remains under VLC-Whisper's existing project license.

Mozilla's MPL 2.0 FAQ explicitly allows MPL code to be statically linked into a larger work without forcing unrelated files to become MPL. When distributing a compiled MPL component, recipients must be told where they can obtain the MPL-covered source, and MPL notices/rights must be preserved.

So **yes: Bergamot and the Mozilla model files can be distributed inside VLC-Whisper's Windows and Ubuntu installers**, provided the release package has the appropriate MPL notices/source-availability information. A practical production release should include a `THIRD_PARTY_NOTICES`/licenses bundle and point at the exact pinned Bergamot source revision used to build the shipped binary.

Before promoting the spike into an installer, do one final transitive dependency inventory (Bergamot/Marian/OpenBLAS and their vendored dependencies) and include every required notice. This README is engineering guidance, not legal advice.

Do not brand the feature as Firefox/Mozilla software or imply Mozilla endorsement; the open-source licenses do not grant Mozilla trademarks.

## Why the model is not bundled in this spike branch

The spike intentionally downloads models on demand rather than committing large binary weights into Git. This keeps review small and lets you benchmark several language directions. Installer bundling is a later packaging choice, not a technical limitation.

For production, there are two sensible policies:

- **Small installer:** ship Bergamot runtime only; download the selected model on demand with hash verification.
- **Zero-setup installer:** bundle one or more qualified models (for example EN<->RO) directly in the installer and preserve their MPL notice/source information.

## Non-goals

- No MKV subtitle demuxing yet.
- No automatic subtitle-track selection.
- No live VLC integration.
- No Google fallback.
- No language auto-detection.
- No production model lifecycle/update policy.
- No root build, CI, installer, protocol, or settings changes.
