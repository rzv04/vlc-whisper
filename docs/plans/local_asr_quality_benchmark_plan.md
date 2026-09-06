# Task: Add a local ASR quality regression benchmark

## Goal

Provide a reproducible developer-only workflow that downloads a small English/Romanian speech corpus locally, runs VLC-Whisper's live and look-ahead caption paths at 1x playback pacing without audio output, and reports WER/CER by language and mode.

## Context

- Relevant docs/ADR: `AGENTS.md`, `docs/architecture.md`, `docs/product.md`, `docs/source-layout.md`, `docs/api-contracts.md`, `docs/whisper-api.md`, `docs/test-strategy.md`, ADR-017/019/020/021 behavior already documented there.
- VLC/worker/protocol version affected: developer tooling only; existing worker and Protocol v1.6 APIs are reused unchanged.
- Assumptions and explicit non-goals: corpus media remains local and git-ignored; FLEURS test data is fetched on explicit developer action; this is anecdotal regression measurement, not a statistically representative model benchmark; no VLC UI/audio output is used; no runtime user transcript logging or telemetry is added.
- Dependency graph inspection: `graphify-out/` is git-ignored and not present in the repository, so affected callers/contracts were verified directly against source and documentation.

## Scope

- In scope: local FLEURS EN/RO downloader, deterministic small-corpus manifest, 16 kHz mono WAV validation, C worker-driving benchmark executable, 1x live PCM pacing, 1x look-ahead playhead pacing, caption capture to local text results, Python orchestration/normalization/WER/CER scoring, git-ignore rules, tests, CMake/CI wiring, and developer documentation.
- Out of scope: checking audio/video fixtures into Git, modifying worker scheduling or IPC, offline `whisper_full()` baseline scoring, automatic CI corpus downloads, cloud transcription, telemetry, audio playback, statistical benchmark claims, or changing release packaging.
- Files/components expected to change: root CMake/ignore/CI, `tools/quality_benchmark/`, `docs/source-layout.md`, `docs/test-strategy.md`, `README.md`, this plan.

## Design

- Inputs and outputs: downloader streams the FLEURS `test` split for `en_us` and `ro_ro`, writes selected WAVs plus a local JSON manifest under an ignored directory, and records reference transcripts/provenance. Python orchestration invokes the C runner once per sample/mode and writes ignored JSON results; scoring aggregates edit counts into per-language WER/CER.
- Ownership/threading model: the C tool is a standalone developer executable. It reuses `vw_worker_client` to launch the ordinary worker and authenticated local IPC. It never initializes VLC or an audio device. Live mode sends 20 ms S16LE chunks paced against monotonic wall time; look-ahead mode starts the same local WAV as a source and sends 1x `POSITION` updates while the worker retains its normal 30 s look-ahead behavior.
- Bounds, time units, and failure behavior: corpus count defaults to 10 clips/language and duration selection is bounded; live pacing uses signed microsecond PTS starting at zero plus a bounded 1 s silent tail to let the existing 500 ms live right-edge holdback settle; caption text storage is bounded; worker errors, source fallback, dropped audio, malformed WAV, or nonzero runner exit make that sample fail rather than silently scoring partial output.
- Privacy/security implications: only the explicit downloader performs network access, solely to the public FLEURS dataset through Hugging Face `datasets`; corpus audio/transcripts/results stay in ignored developer-local paths. The benchmark runner itself is offline/local IPC only and does not open an audio playback device.
- Protocol change: none.

## Acceptance criteria

- [ ] `python3 tools/quality_benchmark/vw_download_corpus.py` can create a small local EN/RO FLEURS corpus without adding tracked media.
- [ ] The downloader emits a deterministic manifest containing sample IDs, language, reference text, source dataset/config/split, duration, and local WAV path.
- [ ] `vw-quality-benchmark` drives the existing worker in `live` and `lookahead` modes without VLC/audio playback and writes only explicit developer-requested local result files.
- [ ] Live input and look-ahead playhead progression are paced at 1x wall-clock speed; look-ahead still uses the worker's production ahead-of-playhead decoder.
- [ ] The Python benchmark command reports corpus-level WER and CER separately for English and Romanian, with separate live/look-ahead rows and per-sample diagnostics.
- [ ] Worker/source errors, queue drops, invalid source-mode activation, or invalid corpus media fail clearly instead of producing misleading quality scores.
- [ ] Automated tests cover normalization/edit-distance scoring and downloader selection/manifest helpers without network access.
- [ ] Documentation explains that this is a local anecdotal regression corpus and that media/results are never committed.

## Test plan

- `clang-format --dry-run --Werror tools/quality_benchmark/*.c plugin/include/*.h plugin/src/*.c protocol/include/*.h protocol/src/*.c worker/include/*.h worker/src/*.c tests/include/*.h tests/unit/*.c tests/integration/*.c`
- `python3 -m unittest tools.quality_benchmark.test_vw_quality`
- `cmake --preset linux-x64-debug`
- `cmake --build --preset linux-x64-debug`
- `ctest --preset linux-x64-debug --output-on-failure`
- `ctest --test-dir build/linux-x64-debug -T memcheck --output-on-failure`
- CI repeats strict formatting, coverage build/CTest, Python quality-tool unit tests, and Valgrind on the PR.
- Manual/local corpus smoke (requires network + model): download 1 EN/1 RO sample, run live and look-ahead with the built worker, confirm no audio device is opened, source mode stays active for look-ahead, and report rows contain finite WER/CER.

## Definition of done

- [ ] C17 code; no project-authored C++ introduced.
- [ ] No blocking work is added to the VLC audio callback; benchmark is a separate process.
- [ ] No unapproved network access, telemetry, or production transcript/PCM persistence is introduced; local corpus download is explicit developer tooling and documented.
- [ ] Text buffers, file sizes, corpus counts, polling intervals, retries, and tail padding are bounded.
- [ ] Error paths fail the benchmark sample without affecting VLC/player code.
- [ ] Unit/contract/integration tests pass as applicable.
- [ ] Formatting, warnings-as-errors, and static checks pass.
- [ ] Protocol contract and compatibility version remain unchanged.
- [ ] Source layout, test strategy, README, and this plan agree with implementation.
- [ ] Reviewer can reproduce the result from a clean checkout after supplying a local model.

## Evidence

- Build/test outputs or CI links: pending implementation/PR verification.
- Measured performance: quality run is intentionally 1x-paced; corpus duration therefore dominates wall time per mode.
- Known limitations/follow-ups: FLEURS is read speech and CC-BY-4.0; corpus is intentionally small/anecdotal and should not be published as a representative ASR benchmark. Offline full-context baseline comparison and harder conversational/broadcast corpora remain follow-ups.
