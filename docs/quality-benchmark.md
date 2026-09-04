# Local ASR Quality Benchmark

## Purpose

The quality benchmark is developer-only tooling for repeatedly measuring how VLC-Whisper's production caption algorithms recognize a fixed, small local speech corpus. It reports corpus-level word error rate (WER) and character error rate (CER) separately for English and Romanian, and separately for live/non-seekable and look-ahead/local-file behavior.

It is intentionally an anecdotal regression corpus, not a statistically representative ASR benchmark. The main use is comparing VLC-Whisper changes against the same local samples and model.

The benchmark is separate from `vw_benchmark.c`. Runtime benchmark reports remain transcript-free and privacy-safe; this developer tool explicitly handles reference/hypothesis text only inside git-ignored local benchmark storage.

## Platforms and headless operation

- **Windows 10/11 x64:** supported with a GNU/MinGW developer/test build. Look-ahead uses Media Foundation source decoding.
- **Linux x64:** supported with a GNU/Clang developer/test build. Look-ahead requires FFmpeg source-decoder support (`libavformat`, `libavcodec`, `libswresample`, and `libavutil` development packages available when configuring CMake).
- **macOS:** not supported by the project and not claimed by this benchmark.

The benchmark is fully headless. It does **not** launch VLC, initialize a VLC GUI, require X11/Wayland, require a Linux desktop environment, or open an audio playback device. It can be run from a plain terminal, SSH session, or other headless environment as long as the normal build/runtime dependencies are present.

Configure the worker with `VW_QUALITY_BENCHMARK_HOOKS=ON`. The option is deliberately OFF by default so normal development, release, and package workers do not include benchmark-only link wrappers. A quality run rejects a worker built without these hooks instead of silently scoring an unverified partial result.

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

`vw-quality-benchmark` does not initialize VLC and does not open an audio playback device. It launches the project worker through the existing authenticated local worker-client API and captures the same final `CAPTION_SEGMENT` frames that the VLC plugin receives.

A worker configured with `VW_QUALITY_BENCHMARK_HOOKS=ON` contains two environment-gated benchmark hooks. They are inactive during ordinary worker use and do not change protocol v1.6. During a quality run they expose only temporary completion metadata: the source-decoder EOF boundary and the worker queue's cumulative dropped-audio duration. No PCM or transcript is written by these hooks.

### Live mode

The runner starts a `VW_SOURCE_LIVE_AUDIO` session and sends the WAV as 20 ms, 16 kHz mono S16LE `AUDIO` frames. Sending is paced against monotonic wall time at exactly 1x media speed. This exercises the production live path, including progressive 2 -> 8 second analysis, 1 second steady-state hops, VAD, 500 ms right-edge holdback, segment filtering, and committed-caption deduplication.

After the source PCM, the runner supplies **1.5 seconds** of silent PCM at the same 1x pace. The 500 ms portion clears the production right-edge holdback and the additional full 1 second guarantees that even a clip ending immediately after an inference hop crosses the next progressive/steady-state inference frontier. For example, a 2.8 second clip is paced through 4.3 seconds, so the 4.0 second pass can commit speech held at the 3.0 second pass. The runner then sends `SHUTDOWN` after all audio frames and keeps draining worker output until the authenticated IPC pipe reaches EOF and the worker process exits. Because worker input is a FIFO queue, that shutdown is the completion barrier for all earlier accepted audio work. The barrier has a bounded 120 second timeout; timeout or premature worker failure invalidates the sample.

The worker queue hook records the true cumulative dropped-audio counter. Any nonzero drop invalidates the sample, including an audio frame evicted while making room for the final shutdown frame. WER/CER is therefore never reported from a knowingly partial live run. To absorb 600 ms – 1,500 ms CPU batch inference passes when 20 ms audio packets arrive continuously at 50 Hz, the worker queue is configured with `VW_WORKER_FRAME_QUEUE_CAPACITY` (512 frames), providing ~10.2 s of absorption buffer to prevent spurious queue drops and sample invalidation.

### Look-ahead mode

The runner starts the worker with the local WAV path as `VW_SOURCE_LOCAL_FILE` and requires `STARTED(source_active=1)`. A source-decoder fallback is therefore an error, not a silently mislabeled result.

The runner sends media `POSITION` updates every 100 ms at 1x wall-clock speed. The worker itself retains its production 30 second ahead-of-playhead decode policy and VAD-guided non-overlapping source chunking. This means the benchmark paces playback realistically while still testing the actual look-ahead algorithm rather than a Python reimplementation.

After the final playback position, the runner waits for the worker's real source EOF boundary: the same third consecutive zero-length decoder read that causes `vw_worker.c` to enter its EOF flush path. The runner continues receiving caption/status frames while waiting. Once that marker appears, the worker still completes the current synchronous EOF flush and caption emission before it can dequeue the runner's subsequent `SHUTDOWN`. The runner then drains output through IPC EOF and worker exit. Both EOF and shutdown waits are bounded to 120 seconds and fail closed.

The Python parent watchdog is deliberately looser than these C-level barriers. It budgets media pacing, worker/model startup, one 120 second completion phase for live mode, two possible 120 second phases for look-ahead mode, and an additional outer grace interval. This prevents the orchestrator from killing a valid slow CPU run before the C runner can report its own bounded timeout.

### No sound leakage

Neither benchmark mode sends PCM to an audio output API. Live PCM is written only to authenticated local IPC; look-ahead audio is decoded inside the worker. Running the benchmark therefore produces no audible playback unless unrelated software independently plays the same files.

## Build

The benchmark target is developer-only and excluded from normal package builds. The worker completion hooks are separately opt-in and must be enabled explicitly with `-DVW_QUALITY_BENCHMARK_HOOKS=ON`.

Linux example:

```bash
cmake --preset linux-x64-debug -DVW_QUALITY_BENCHMARK_HOOKS=ON
cmake --build --preset linux-x64-debug --target vw-quality-benchmark vlc-whisper-worker
```

If that preset configures without Vulkan, the worker target may be named `vlc-whisper-worker-cpu`; the Python driver discovers either worker filename automatically.

Windows example:

```powershell
cmake --preset windows-x64-debug -DVW_QUALITY_BENCHMARK_HOOKS=ON
cmake --build --preset windows-x64-debug --target vw-quality-benchmark vlc-whisper-worker
```

A CPU-only developer/test preset is also valid when configured with the same hook option. `vw_benchmark.py` discovers both canonical and `-cpu` worker names. Normal release/package workers intentionally leave `VW_QUALITY_BENCHMARK_HOOKS` OFF and are not supported benchmark workers.

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

A default run with roughly 3-4 minutes of corpus speech takes approximately the corpus duration once for live plus once for look-ahead, plus model inference/startup and bounded completion draining. Because both modes pace the media timeline at 1x, a 3.5 minute corpus should normally require roughly 7-10 minutes per model on hardware capable of keeping up in real time. Slower CPU-only inference can extend the completion phase rather than being truncated into an invalid partial score.

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

Temporary worker completion markers contain only EOF state and an integer dropped-audio duration. They are placed in the OS temporary directory, removed by the runner, and contain neither PCM nor transcripts.

For comparisons across commits, keep constant at minimum:

- FLEURS manifest/revision;
- Whisper model file;
- VAD model availability;
- backend and thread count;
- machine/load conditions.

The benchmark should initially be treated as informational. Do not turn WER/CER into a CI pass/fail threshold until repeated runs establish stable variance and model-specific baselines.
