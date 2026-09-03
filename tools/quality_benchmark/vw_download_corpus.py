#!/usr/bin/env python3
"""Download a small deterministic local FLEURS EN/RO regression corpus."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import sys
import wave
from pathlib import Path
from typing import Any

DATASET_ID = "google/fleurs"
DATASET_SPLIT = "test"
LANGUAGES = {"en": "en_us", "ro": "ro_ro"}
SAMPLE_RATE = 16_000
DEFAULT_PER_LANGUAGE = 10
DEFAULT_MIN_SECONDS = 2.5
DEFAULT_MAX_SECONDS = 15.0
DEFAULT_SCAN_LIMIT = 1000


def duration_is_eligible(duration_seconds: float, minimum: float, maximum: float) -> bool:
    return math.isfinite(duration_seconds) and minimum <= duration_seconds <= maximum


def safe_sample_id(value: Any, fallback: int) -> str:
    raw = str(value if value is not None else fallback)
    safe = "".join(ch if ch.isalnum() or ch in "-_" else "_" for ch in raw)
    return safe[:80] or str(fallback)


def write_pcm16_wav(path: Path, pcm_bytes: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(SAMPLE_RATE)
        wav.writeframes(pcm_bytes)


def tensor_to_pcm16(audio_decoder: Any) -> tuple[bytes, float]:
    samples = audio_decoder.get_all_samples()
    data = samples.data
    sample_rate = int(samples.sample_rate)
    if sample_rate != SAMPLE_RATE:
        raise RuntimeError(f"decoded FLEURS sample rate is {sample_rate}, expected {SAMPLE_RATE}")
    if getattr(data, "ndim", 0) == 2:
        if data.shape[0] > 1:
            data = data.mean(dim=0)
        else:
            data = data[0]
    elif getattr(data, "ndim", 0) != 1:
        raise RuntimeError(f"unexpected decoded audio tensor shape: {getattr(data, 'shape', None)}")
    data = data.detach().cpu().clamp(-1.0, 1.0)
    import torch

    pcm = data.mul(32767.0).round().to(dtype=torch.int16)
    pcm_bytes = pcm.numpy().astype("<i2", copy=False).tobytes()
    duration_seconds = len(pcm_bytes) / 2 / SAMPLE_RATE
    return pcm_bytes, duration_seconds


def download_language(
    output_dir: Path,
    language: str,
    config: str,
    revision: str,
    per_language: int,
    min_seconds: float,
    max_seconds: float,
    scan_limit: int,
) -> list[dict[str, Any]]:
    try:
        from datasets import Audio, load_dataset
    except ImportError as exc:
        raise RuntimeError(
            'missing downloader dependency; run: python -m pip install -r tools/quality_benchmark/requirements.txt'
        ) from exc

    dataset = load_dataset(DATASET_ID, config, split=DATASET_SPLIT, streaming=True, revision=revision)
    dataset = dataset.cast_column("audio", Audio(sampling_rate=SAMPLE_RATE))

    selected: list[dict[str, Any]] = []
    for row_index, row in enumerate(dataset):
        if row_index >= scan_limit:
            break
        pcm_bytes, duration_seconds = tensor_to_pcm16(row["audio"])
        if not duration_is_eligible(duration_seconds, min_seconds, max_seconds):
            continue

        sample_id = safe_sample_id(row.get("id"), row_index)
        filename = f"fleurs-{language}-{sample_id}.wav"
        relative_path = Path("audio") / language / filename
        wav_path = output_dir / relative_path
        write_pcm16_wav(wav_path, pcm_bytes)

        selected.append(
            {
                "id": f"fleurs-{language}-{sample_id}",
                "language": language,
                "dataset": DATASET_ID,
                "dataset_config": config,
                "split": DATASET_SPLIT,
                "dataset_revision": revision,
                "source_id": row.get("id"),
                "reference": str(row.get("transcription", "")).strip(),
                "duration_seconds": round(duration_seconds, 6),
                "sample_rate": SAMPLE_RATE,
                "channels": 1,
                "sha256": hashlib.sha256(pcm_bytes).hexdigest(),
                "path": relative_path.as_posix(),
                "license": "CC-BY-4.0",
            }
        )
        if len(selected) >= per_language:
            break

    if len(selected) != per_language:
        raise RuntimeError(
            f"only found {len(selected)}/{per_language} eligible {language} samples in the first {scan_limit} rows"
        )
    return selected


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(__file__).resolve().parent / "local" / "corpus",
        help="local corpus directory (default: tools/quality_benchmark/local/corpus)",
    )
    parser.add_argument("--per-language", type=int, default=DEFAULT_PER_LANGUAGE)
    parser.add_argument("--min-seconds", type=float, default=DEFAULT_MIN_SECONDS)
    parser.add_argument("--max-seconds", type=float, default=DEFAULT_MAX_SECONDS)
    parser.add_argument("--scan-limit", type=int, default=DEFAULT_SCAN_LIMIT)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.per_language < 1 or args.per_language > 100:
        print("--per-language must be in 1..100", file=sys.stderr)
        return 2
    if args.min_seconds <= 0 or args.max_seconds <= args.min_seconds:
        print("invalid duration bounds", file=sys.stderr)
        return 2
    if args.scan_limit < args.per_language:
        print("--scan-limit must be >= --per-language", file=sys.stderr)
        return 2

    try:
        from huggingface_hub import HfApi
    except ImportError:
        print(
            "missing downloader dependency; run: python -m pip install -r tools/quality_benchmark/requirements.txt",
            file=sys.stderr,
        )
        return 2

    output_dir = args.output.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    revision = HfApi().dataset_info(DATASET_ID).sha
    if not revision:
        print("could not resolve FLEURS dataset revision", file=sys.stderr)
        return 1

    try:
        samples: list[dict[str, Any]] = []
        for language, config in LANGUAGES.items():
            print(f"Downloading {args.per_language} {language} samples from {config}@{revision[:12]}...")
            samples.extend(
                download_language(
                    output_dir,
                    language,
                    config,
                    revision,
                    args.per_language,
                    args.min_seconds,
                    args.max_seconds,
                    args.scan_limit,
                )
            )
    except Exception as exc:
        print(f"corpus download failed: {exc}", file=sys.stderr)
        return 1

    manifest = {
        "schema_version": 1,
        "dataset": DATASET_ID,
        "dataset_revision": revision,
        "split": DATASET_SPLIT,
        "license": "CC-BY-4.0",
        "selection": {
            "method": "first deterministic rows satisfying duration bounds",
            "per_language": args.per_language,
            "min_seconds": args.min_seconds,
            "max_seconds": args.max_seconds,
            "scan_limit": args.scan_limit,
        },
        "samples": samples,
    }
    manifest_path = output_dir / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    total_seconds = sum(float(sample["duration_seconds"]) for sample in samples)
    print(f"Wrote {len(samples)} local samples ({total_seconds:.1f}s) to {output_dir}")
    print(f"Manifest: {manifest_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
