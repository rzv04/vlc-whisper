# Local ASR quality benchmark

Supported: Windows x64 and Linux x64. Corpus/results stay under ignored `tools/quality_benchmark/local/`. No audio is played.

```bash
python -m pip install -r tools/quality_benchmark/requirements.txt
python tools/quality_benchmark/vw_download_corpus.py
cmake --preset linux-x64-debug
cmake --build --preset linux-x64-debug --target vw-quality-benchmark vlc-whisper-worker
python tools/quality_benchmark/vw_benchmark.py --build-dir build/linux-x64-debug --model models/ggml-tiny.bin
```

Windows: use a Windows preset/build directory in the last two commands. Detailed behavior: `docs/quality-benchmark.md`.
