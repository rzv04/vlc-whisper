# Local ASR Quality Benchmark

## Purpose

The quality benchmark is developer-only tooling for repeatedly measuring how VLC-Whisper's production caption algorithms recognize a fixed, small local speech corpus. It reports corpus-level word error rate (WER) and character error rate (CER) separately for English and Romanian, and separately for live/non-seekable and look-ahead/local-file behavior.

It is intentionally an anecdotal regression corpus, not a statistically representative ASR benchmark. The main use is comparing VLC-Whisper changes against the same local samples and model.

The benchmark is separate from `vw_benchmark.c`. Runtime benchmark reports remain transcript-free and privacy-safe; this developer tool explicitly handles reference/hypothesis text only inside git-ignored local benchmark storage.

## Platforms

- **Windows 10/11 x64:** supported. The C runner launches the ordinary Windows worker and look-ahead uses Media Foundation source decoding.
- **Linux x64:** supported. The C runner launches the ordinary Linux worker. Look-ahead requires the build to have FFmpeg source-decoder support (`libavformat`, `libavcodec`, `libswresample`, `libavutil` development packages available when configuring CMake).
- **macOS:** not supported by the project and not claimed by this benchmark.

The Python orchestration requires Python 3.10+ for the project workflow. Corpus download additionally requires the packages in `tools/quality_benchmark/requirements.txt`. Downloading is the only network-dependent benchmark step.

## Corpus

`vw_download_corpus.py` explicitly downloads a small local subset of the public `google/fleurs` **test** split:

- `en_us` -> benchmark language `en`
- `ro_ro` -> benchmark language `ro`
- default: 10 clips per language
- selection: first deterministic test rows whose decoded duration is between 2.5 and 15 seconds
- audio normalization: 16 kHz mono PCM S16LE WAV
- FLEURS license: CC-BY-4.0

The downloader first resolves the current Hugging Face dataset revision SHA and then loads both language configurations at that exact revision. The local manifest records that SHA, sample IDs, reference transcripts, duration, PCM hash, and provenance. Selection never depends on Whisper output.

All downloaded WAVs and manifests live by default under:

```text
tools/quality_benchmark/local/corpus/
```

The entire `local/` directory is git-ignored. Audio/video fixtures are not part of the repository or pull request.

Download the default 20 clips:

```bash
python -m pip install -r tools/quality_benchmark/requirements.txt
python tools/quality_benchmark/vw_download_corpus.py
```

For a smaller smoke corpus:

```bash
python tools/quality_benchmark/vw_download_corpus.py --per-language 1
```

## What the C runner measures

`vw-quality-benchmark` does not initialize VLC and does not open an audio playback device. It launches the normal `vlc-whisper-worker` process through the existing authenticated local worker-client API and captures the same final `CAPTION_SEGMENT` frames that the VLC plugin receives.

### Live mode

The runner starts a `VW_SOURCE_LIVE_AUDIO` session and sends the WAV as 20 ms, 16 kHz mono S16LE `AUDIO` frames. Sending is paced against monotonic wall time at exactly 1x media speed. This exercises the production live path, including progressive 2 -> 8 second analysis, 1 second steady-state hops, VAD, 500 ms right-edge holdback, segment filtering, and committed-caption deduplication.

After the source PCM, the runner supplies one second of silent PCM at the same 1x pace so the existing right-edge policy gets a final opportunity to commit trailing speech. It then performs a short receive drain. If the worker reports dropped audio, the sample is rejected instead of silently treating an overloaded run as an algorithmic WER result.

### Look-ahead mode

The runner starts the worker with the local WAV path as `VW_SOURCE_LOCAL_FILE` and requires `STARTED(source_active=1)`. A source-decoder fallback is therefore an error, not a silently mislabeled result.

The runner sends media `POSITION` updates every 100 ms at 1x wall-clock speed. The worker itself retains its production 30 second ahead-of-playhead decode policy and VAD-guided non-overlapping source chunking. This means the benchmark paces playback realistically while still testing the actual look-ahead algorithm rather than a Python reimplementation.

### No sound leakage

Neither benchmark mode sends PCM to an audio output API. Live PCM is written only to authenticated local IPC; look-ahead audio is decoded inside the worker. Running the benchmark therefore produces no audible playback unless unrelated software independently plays the same files.

## Build

The benchmark target is developer-only and excluded from normal package builds.

Linux example:

```bash
cmake --preset linux-x64-debug
cmake --build --preset linux-x64-debug --target vw-quality-benchmark vlc-whisper-worker
```

If that preset configures without Vulkan, the worker target may be named `vlc-whisper-worker-cpu`; the Python driver discovers either worker filename automatically.

Windows example:

```powershell
cmake --preset windows-x64-debug
cmake --build --preset windows-x64-debug --target vw-quality-benchmark vlc-whisper-worker
```

A CPU-only preset is also valid. `vw_benchmark.py` discovers both canonical and `-cpu` worker names.

## Run

Linux:

```bash
python tools/quality_benchmark/vw_benchmark.py \
  --build-dir build/linux-x64-debug \
  --model models/ggml-tiny.bin
```

Windows PowerShell:

```powershell
python tools/quality_benchmark/vw_benchmark.py `
  --build-dir build/windows-x64-debug `
  --model models/ggml-tiny.bin
```

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

`--mode both` is the default. The model directory is forwarded to the worker so a colocated Silero VAD model is discovered the same way as normal development runs.

A default run with roughly 3-4 minutes of corpus speech takes approximately the corpus duration once for live plus once for look-ahead, plus model inference/startup and small per-clip settle overhead. Because both modes are wall-clock paced, a 3.5 minute corpus should normally require roughly 7-10 minutes per model on hardware capable of keeping up in real time.

## WER and CER

The Python driver concatenates the worker's final committed captions for each clip, compares them with the FLEURS reference, and aggregates edit counts across all samples in each `{language, mode}` group.

The `vlcw-basic-v1` normalizer:

1. applies Unicode NFC;
2. canonicalizes Romanian cedilla forms `ş/ţ` to comma-below `ș/ț`;
3. lowercases;
4. replaces Unicode punctuation/symbol characters with spaces;
5. collapses whitespace;
6. preserves diacritics.

WER is total word-level Levenshtein errors divided by total reference words. CER is total character-level Levenshtein errors divided by total reference characters after spaces are removed. Scores are corpus-weighted; the tool does not average percentages from short and long clips equally.

Example console shape:

```text
Language  Mode       WER      CER    Word errors   Char errors
--------  --------  -------  -------  ------------  -----------
en        live       10.25%    4.90%     41/400       96/1960
en        lookahead   8.75%    4.10%     35/400       80/1960
ro        live       15.10%    7.20%     ...
ro        lookahead  12.30%    6.10%     ...
```

The numbers above are illustrative only.

## Local reports and reproducibility

Each run writes a JSON report under:

```text
tools/quality_benchmark/local/results/
```

The report includes references, hypotheses, per-sample WER/CER, worker timing/status fields, requested backend/thread count, model path, dataset revision, and aggregate scores. These reports are intentionally local and ignored by Git.

For comparisons across commits, keep constant at minimum:

- FLEURS manifest/revision;
- Whisper model file;
- VAD model availability;
- backend and thread count;
- machine/load conditions.

The benchmark should initially be treated as informational. Do not turn WER/CER into a CI pass/fail threshold until repeated runs establish stable variance and model-specific baselines.
