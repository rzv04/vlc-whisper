# Local ASR Quality Benchmark

Developer-only regression tooling for comparing VLC-Whisper caption algorithms on the same small local corpus. It is anecdotal, not a statistically representative ASR benchmark. Runtime telemetry (`vw_benchmark.c`) remains transcript-free; this tool handles reference/hypothesis text only in git-ignored local storage.

## Platform / headless operation

- Windows x64 developer/test builds: supported; look-ahead uses Media Foundation.
- Linux x64: supported; look-ahead requires FFmpeg development libraries.
- No VLC GUI, X11/Wayland, desktop environment, or audio device is required.
- Configure the worker with `VW_QUALITY_BENCHMARK_HOOKS=ON`; a run rejects an unhooked worker rather than scoring unverifiable partial output.

## Corpus

`vw_download_corpus.py` downloads a deterministic subset of FLEURS test data:

- `en_us` → `en`, `ro_ro` → `ro`;
- default 10 clips/language;
- 16 kHz mono S16LE WAV;
- corpus/media stored under `tools/quality_benchmark/local/` (git-ignored);
- manifest records dataset revision, IDs, reference text, duration, SHA-256, and provenance.

Before inference, the runner validates schema/version, unique IDs/paths, non-empty normalized references, SHA-256, WAV format, and duration. Invalid evidence is **invalid**, never a zero-error sample.

```bash
python -m pip install -r tools/quality_benchmark/requirements.txt
python tools/quality_benchmark/vw_download_corpus.py
```

## Modes

### Live

Starts `VW_SOURCE_LIVE_AUDIO` and sends 20 ms PCM frames at exactly 1x. This exercises the production progressive live path, VAD, right-edge holdback, filtering, and caption deduplication. A bounded silence tail allows held-back speech to cross the next inference frontier.

Completion uses worker FIFO/shutdown/IPC EOF, not an arbitrary sleep. Any worker queue drop, timeout, premature exit, or incomplete processing invalidates the sample; partial live transcripts are not scored.

### Look-ahead

Starts `VW_SOURCE_LOCAL_FILE`, requires `STARTED(source_active=1)`, and sends playback `POSITION` updates at 1x while the worker retains its normal source look-ahead/chunking behavior. Source-decoder fallback is an error for this mode.

Completion waits for the worker's real source EOF transition and then drains through shutdown/IPC EOF. EOF/error ambiguity, timeout, or premature worker exit invalidates the sample.

Neither mode sends PCM to an audio output API; the benchmark is silent.

## Build / run

```bash
cmake --preset linux-x64-debug -DVW_QUALITY_BENCHMARK_HOOKS=ON
cmake --build --preset linux-x64-debug --target vw-quality-benchmark vlc-whisper-worker
python tools/quality_benchmark/vw_benchmark.py \
  --build-dir build/linux-x64-debug \
  --model models/ggml-tiny.bin
```

Windows uses the corresponding developer/test preset. CPU-only worker targets are valid when built with the hooks; the Python driver discovers canonical/`-cpu` worker names.

Useful options:

```text
--mode both|live|lookahead
--backend auto|gpu|cpu
--threads 1..16
--manifest PATH
--worker PATH
--runner PATH
--output PATH
```

With ~3–4 minutes of corpus speech and both modes paced at 1x, hardware capable of realtime inference normally needs roughly 7–10 minutes plus startup/drain overhead. Slower inference extends bounded completion rather than truncating into a partial score.

## Scoring

The `vlcw-basic-v1` normalizer applies Unicode NFC, canonicalizes Romanian cedilla forms to comma-below, lowercases, replaces punctuation/symbols with spaces, collapses whitespace, and preserves diacritics.

- **WER:** corpus-total word Levenshtein errors / reference words.
- **CER:** corpus-total character errors / reference characters after spaces are removed.
- Aggregation is corpus-weighted per `{language, mode}`, not an average of sample percentages.

The root README contains the currently published anecdotal table. `quality-benchmark-report.md` retains the detailed historical analysis.

## Reports / reproducibility

JSON reports live under `tools/quality_benchmark/local/results/` and are git-ignored. They may contain references/hypotheses, per-sample scores, worker status/timing, requested backend/threads, model path, corpus revision, and aggregates.

For comparisons, keep constant at minimum:

- corpus manifest/revision;
- Whisper model bytes/hash;
- VAD model availability;
- backend/thread count;
- machine/load conditions.

Treat WER/CER as informational until repeated runs establish variance/baselines. Benchmark hooks must remain bounded and avoid per-audio-frame filesystem I/O or other observer effects.
