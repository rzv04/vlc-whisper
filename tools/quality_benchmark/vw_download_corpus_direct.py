#!/usr/bin/env python3
"""Lightweight direct downloader for FLEURS regression corpus without HF datasets/PyTorch OOM."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import struct
import sys
import tarfile
import urllib.request
import wave
from pathlib import Path
from typing import Any

import numpy as np

DATASET_ID = "google/fleurs"
DATASET_SPLIT = "test"
LANGUAGES = {"en": "en_us", "ro": "ro_ro"}
SAMPLE_RATE = 16_000
DEFAULT_PER_LANGUAGE = 10
DEFAULT_MIN_SECONDS = 2.5
DEFAULT_MAX_SECONDS = 15.0
DEFAULT_SCAN_LIMIT = 1000
HTTP_USER_AGENT = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) VLC-Whisper/0.1.0"


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


def parse_wav_float32_to_pcm16(wav_bytes: bytes) -> tuple[bytes, float]:
    """Parse IEEE 32-bit float RIFF WAV from FLEURS and convert to 16-bit PCM mono."""
    if len(wav_bytes) < 44 or wav_bytes[:4] != b"RIFF" or wav_bytes[8:12] != b"WAVE":
        raise ValueError("invalid RIFF/WAVE container")

    pos = 12
    fmt_tag, channels, rate, bits = 0, 0, 0, 0
    data_bytes: bytes | None = None

    while pos + 8 <= len(wav_bytes):
        tag, size = struct.unpack("<4sI", wav_bytes[pos : pos + 8])
        chunk_start = pos + 8
        chunk_end = chunk_start + size
        if tag == b"fmt ":
            fmt_data = wav_bytes[chunk_start:chunk_end]
            if len(fmt_data) >= 16:
                fmt_tag, channels, rate, _, _, bits = struct.unpack("<HHIIHH", fmt_data[:16])
        elif tag == b"data":
            data_bytes = wav_bytes[chunk_start:chunk_end]
        pos = chunk_end + (size % 2)

    if fmt_tag != 3:
        raise ValueError(f"expected IEEE float format tag 3, got {fmt_tag}")
    if channels != 1:
        raise ValueError(f"expected mono audio (1 channel), got {channels}")
    if rate != SAMPLE_RATE:
        raise ValueError(f"expected sample rate {SAMPLE_RATE}, got {rate}")
    if bits != 32:
        raise ValueError(f"expected 32-bit audio, got {bits}")
    if data_bytes is None:
        raise ValueError("missing data chunk in WAV")

    floats = np.frombuffer(data_bytes, dtype=np.float32)
    floats = np.clip(floats, -1.0, 1.0)
    pcm = np.round(floats * 32767.0).astype(np.int16)
    pcm_bytes = pcm.tobytes()
    duration_seconds = len(pcm) / float(SAMPLE_RATE)
    return pcm_bytes, duration_seconds


def resolve_dataset_revision() -> str:
    """Resolve commit hash for dataset pinning."""
    try:
        from huggingface_hub import HfApi

        sha = HfApi().dataset_info(DATASET_ID).sha
        if sha:
            return sha
    except Exception:
        pass

    # Fallback to direct HTTP HEAD request on Hugging Face API
    api_url = f"https://huggingface.co/api/datasets/{DATASET_ID}"
    req = urllib.request.Request(api_url, headers={"User-Agent": HTTP_USER_AGENT})
    with urllib.request.urlopen(req, timeout=15) as resp:
        data = json.loads(resp.read().decode("utf-8"))
        sha = data.get("sha")
        if sha:
            return str(sha)

    raise RuntimeError("could not resolve FLEURS dataset revision")


def select_candidates_from_tsv(
    language: str,
    config: str,
    revision: str,
    per_language: int,
    min_seconds: float,
    max_seconds: float,
    scan_limit: int,
) -> list[dict[str, Any]]:
    """Fetch test.tsv and select the first deterministic eligible candidates."""
    tsv_url = f"https://huggingface.co/datasets/{DATASET_ID}/resolve/{revision}/data/{config}/test.tsv"
    req = urllib.request.Request(tsv_url, headers={"User-Agent": HTTP_USER_AGENT})
    with urllib.request.urlopen(req, timeout=30) as resp:
        lines = resp.read().decode("utf-8").splitlines()

    # Sort rows by filename to strictly match the canonical tar archive / Parquet row order
    parsed_rows: list[tuple[str, str, str, str, int]] = []
    for line in lines:
        parts = line.split("\t")
        if len(parts) >= 6:
            # id, filename, raw_transcription, transcription, tokenized, num_samples, ...
            source_id = parts[0]
            filename = parts[1]
            transcription = parts[3].strip()
            num_samples = int(parts[5])
            parsed_rows.append((source_id, filename, transcription, filename, num_samples))

    parsed_rows.sort(key=lambda r: r[1])

    candidates: list[dict[str, Any]] = []
    for row_index, (source_id, filename, transcription, _, num_samples) in enumerate(parsed_rows):
        if row_index >= scan_limit:
            break
        duration_seconds = num_samples / float(SAMPLE_RATE)
        if not duration_is_eligible(duration_seconds, min_seconds, max_seconds):
            continue

        sample_id = safe_sample_id(source_id, row_index)
        candidates.append(
            {
                "row_index": row_index,
                "source_id": int(source_id) if source_id.isdigit() else source_id,
                "sample_id": sample_id,
                "tar_filename": filename,
                "target_filename": f"fleurs-{language}-{sample_id}.wav",
                "reference": transcription,
                "expected_duration": duration_seconds,
            }
        )
        if len(candidates) >= per_language:
            break

    if len(candidates) != per_language:
        raise RuntimeError(
            f"only found {len(candidates)}/{per_language} eligible {language} samples in the first {scan_limit} rows"
        )

    return candidates


def download_language_audio(
    output_dir: Path,
    language: str,
    config: str,
    revision: str,
    candidates: list[dict[str, Any]],
) -> list[dict[str, Any]]:
    """Stream only the required audio files from the tar.gz archive directly over HTTP."""
    tar_url = f"https://huggingface.co/datasets/{DATASET_ID}/resolve/{revision}/data/{config}/audio/test.tar.gz"
    req = urllib.request.Request(tar_url, headers={"User-Agent": HTTP_USER_AGENT})

    # Map target filename to candidate metadata for fast lookup
    needed = {c["tar_filename"]: c for c in candidates}
    extracted_samples: list[dict[str, Any]] = []

    print(f"  Streaming audio archive ({len(needed)} files needed)...")
    with urllib.request.urlopen(req, timeout=60) as resp:
        with tarfile.open(mode="r|gz", fileobj=resp) as tar:
            for member in tar:
                # Member names are like 'test/1003119935936341070.wav'
                base_name = member.name.split("/")[-1]
                if base_name in needed:
                    cand = needed[base_name]
                    extracted_file = tar.extractfile(member)
                    if extracted_file is None:
                        continue

                    wav_bytes = extracted_file.read()
                    pcm_bytes, duration_seconds = parse_wav_float32_to_pcm16(wav_bytes)

                    relative_path = Path("audio") / language / cand["target_filename"]
                    wav_path = output_dir / relative_path
                    write_pcm16_wav(wav_path, pcm_bytes)

                    extracted_samples.append(
                        {
                            "id": f"fleurs-{language}-{cand['sample_id']}",
                            "language": language,
                            "dataset": DATASET_ID,
                            "dataset_config": config,
                            "split": DATASET_SPLIT,
                            "dataset_revision": revision,
                            "source_id": cand["source_id"],
                            "reference": cand["reference"],
                            "duration_seconds": round(duration_seconds, 6),
                            "sample_rate": SAMPLE_RATE,
                            "channels": 1,
                            "sha256": hashlib.sha256(pcm_bytes).hexdigest(),
                            "path": relative_path.as_posix(),
                            "license": "CC-BY-4.0",
                        }
                    )
                    del needed[base_name]

                    # Early termination: once all target files are extracted, stop streaming!
                    if not needed:
                        break

    if len(extracted_samples) != len(candidates):
        raise RuntimeError(
            f"failed to extract all required audio samples: got {len(extracted_samples)}/{len(candidates)}"
        )

    # Sort output to match candidate order
    candidate_order = {c["sample_id"]: i for i, c in enumerate(candidates)}
    extracted_samples.sort(key=lambda s: candidate_order.get(s["id"].split("-")[-1], 0))
    return extracted_samples


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

    output_dir = args.output.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    try:
        revision = resolve_dataset_revision()
    except Exception as exc:
        print(f"could not resolve FLEURS dataset revision: {exc}", file=sys.stderr)
        return 1

    try:
        samples: list[dict[str, Any]] = []
        for language, config in LANGUAGES.items():
            print(f"Processing {args.per_language} {language} samples from {config}@{revision[:12]}...")
            candidates = select_candidates_from_tsv(
                language,
                config,
                revision,
                args.per_language,
                args.min_seconds,
                args.max_seconds,
                args.scan_limit,
            )
            extracted = download_language_audio(output_dir, language, config, revision, candidates)
            samples.extend(extracted)
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
