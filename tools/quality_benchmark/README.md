# Local ASR quality benchmark

Headless on Windows x64/Linux x64: no VLC GUI/process, audio output, X11/Wayland, or desktop environment. Use a developer/test build with `VW_QUALITY_BENCHMARK_HOOKS=ON`. Corpus/results stay under ignored `tools/quality_benchmark/local/`.

```bash
python -m pip install -r tools/quality_benchmark/requirements.txt
python tools/quality_benchmark/vw_download_corpus.py
cmake --preset linux-x64-debug -DVW_QUALITY_BENCHMARK_HOOKS=ON
cmake --build --preset linux-x64-debug --target vw-quality-benchmark vlc-whisper-worker
python tools/quality_benchmark/vw_benchmark.py --build-dir build/linux-x64-debug --model models/ggml-tiny.bin
```

Windows: use a Windows developer/test preset/build directory in the last two commands. Details: `docs/quality-benchmark.md`.
