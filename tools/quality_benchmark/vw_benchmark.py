#!/usr/bin/env python3
"""Run VLC-Whisper live/look-ahead ASR quality benchmarks and report WER/CER."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from vw_quality import ErrorCounts, normalize_text, score_pair

DEFAULT_MANIFEST = Path(__file__).resolve().parent / "local" / "corpus" / "manifest.json"
DEFAULT_RESULTS_DIR = Path(__file__).resolve().parent / "local" / "results"
SUPPORTED_MODES = ("live", "lookahead")


def find_executable(build_dir: Path, relative_dir: str, stems: tuple[str, ...]) -> Path:
    suffix = ".exe" if os.name == "nt" else ""
    directory = build_dir / relative_dir
    for stem in stems:
        candidate = directory / f"{stem}{suffix}"
        if candidate.is_file():
            return candidate.resolve()
    names = ", ".join(f"{stem}{suffix}" for stem in stems)
    raise FileNotFoundError(f"could not find {names} under {directory}")


def hypothesis_from_result(result: dict[str, Any]) -> str:
    return " ".join(str(segment.get("text", "")).strip() for segment in result.get("segments", [])).strip()


def add_counts(left: ErrorCounts, right: ErrorCounts) -> ErrorCounts:
    return ErrorCounts(
        word_errors=left.word_errors + right.word_errors,
        reference_words=left.reference_words + right.reference_words,
        char_errors=left.char_errors + right.char_errors,
        reference_chars=left.reference_chars + right.reference_chars,
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--worker", type=Path)
    parser.add_argument("--runner", type=Path)
    parser.add_argument("--backend", choices=("auto", "gpu", "cpu"), default="auto")
    parser.add_argument("--threads", type=int, default=4)
    parser.add_argument("--mode", choices=("both",) + SUPPORTED_MODES, default="both")
    parser.add_argument("--output", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    manifest_path = args.manifest.resolve()
    build_dir = args.build_dir.resolve()
    model_path = args.model.resolve()
    if not manifest_path.is_file():
        print(f"manifest not found: {manifest_path}", file=sys.stderr)
        return 2
    if not model_path.is_file():
        print(f"model not found: {model_path}", file=sys.stderr)
        return 2
    if args.threads < 1 or args.threads > 16:
        print("--threads must be in 1..16", file=sys.stderr)
        return 2

    try:
        runner = args.runner.resolve() if args.runner else find_executable(
            build_dir, "tools/quality_benchmark", ("vw-quality-benchmark",)
        )
        worker = args.worker.resolve() if args.worker else find_executable(
            build_dir, "worker", ("vlc-whisper-worker", "vlc-whisper-worker-cpu")
        )
    except FileNotFoundError as exc:
        print(str(exc), file=sys.stderr)
        return 2

    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    samples = manifest.get("samples", [])
    if not samples:
        print("manifest contains no samples", file=sys.stderr)
        return 2
    modes = SUPPORTED_MODES if args.mode == "both" else (args.mode,)

    if any(sample.get("language") == "ro" for sample in samples) and ".en." in model_path.name:
        print("warning: an English-only model was selected while Romanian samples are present", file=sys.stderr)

    per_sample: list[dict[str, Any]] = []
    aggregate: dict[tuple[str, str], ErrorCounts] = defaultdict(lambda: ErrorCounts(0, 0, 0, 0))

    for sample_index, sample in enumerate(samples, start=1):
        language = str(sample["language"])
        reference = str(sample["reference"])
        audio_path = (manifest_path.parent / str(sample["path"])).resolve()
        if not audio_path.is_file():
            print(f"missing corpus audio: {audio_path}", file=sys.stderr)
            return 1
        duration_seconds = float(sample.get("duration_seconds", 0.0))

        for mode in modes:
            print(f"[{sample_index}/{len(samples)}] {language} {sample['id']} {mode} ({duration_seconds:.1f}s)")
            command = [
                str(runner),
                "--worker",
                str(worker),
                "--model",
                str(model_path),
                "--audio",
                str(audio_path),
                "--language",
                language,
                "--mode",
                mode,
                "--backend",
                args.backend,
                "--threads",
                str(args.threads),
                "--model-dir",
                str(model_path.parent),
            ]
            timeout_seconds = max(60.0, duration_seconds * 3.0 + 30.0)
            try:
                completed = subprocess.run(
                    command,
                    check=False,
                    capture_output=True,
                    text=True,
                    encoding="utf-8",
                    timeout=timeout_seconds,
                )
            except subprocess.TimeoutExpired:
                print(f"benchmark runner timed out for {sample['id']} ({mode})", file=sys.stderr)
                return 1
            if completed.returncode != 0:
                print(completed.stderr.rstrip(), file=sys.stderr)
                print(f"benchmark runner failed for {sample['id']} ({mode})", file=sys.stderr)
                return 1
            try:
                result = json.loads(completed.stdout)
            except json.JSONDecodeError as exc:
                print(f"invalid runner JSON for {sample['id']} ({mode}): {exc}", file=sys.stderr)
                if completed.stderr:
                    print(completed.stderr.rstrip(), file=sys.stderr)
                return 1

            hypothesis = hypothesis_from_result(result)
            counts = score_pair(reference, hypothesis)
            aggregate[(language, mode)] = add_counts(aggregate[(language, mode)], counts)
            per_sample.append(
                {
                    "id": sample["id"],
                    "language": language,
                    "mode": mode,
                    "reference": reference,
                    "reference_normalized": normalize_text(reference),
                    "hypothesis": hypothesis,
                    "hypothesis_normalized": normalize_text(hypothesis),
                    "wer": counts.wer,
                    "cer": counts.cer,
                    "word_errors": counts.word_errors,
                    "reference_words": counts.reference_words,
                    "char_errors": counts.char_errors,
                    "reference_chars": counts.reference_chars,
                    "runner": result,
                }
            )

    rows: list[dict[str, Any]] = []
    print("\nLanguage  Mode       WER      CER    Word errors   Char errors")
    print("--------  --------  -------  -------  ------------  -----------")
    for language in sorted({str(sample["language"]) for sample in samples}):
        for mode in modes:
            counts = aggregate[(language, mode)]
            row = {
                "language": language,
                "mode": mode,
                "wer": counts.wer,
                "cer": counts.cer,
                "word_errors": counts.word_errors,
                "reference_words": counts.reference_words,
                "char_errors": counts.char_errors,
                "reference_chars": counts.reference_chars,
            }
            rows.append(row)
            print(
                f"{language:<8}  {mode:<8}  {counts.wer * 100:6.2f}%  {counts.cer * 100:6.2f}%  "
                f"{counts.word_errors:5d}/{counts.reference_words:<5d}  "
                f"{counts.char_errors:5d}/{counts.reference_chars:<5d}"
            )

    output_path = args.output
    if output_path is None:
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        output_path = DEFAULT_RESULTS_DIR / f"quality-{stamp}.json"
    output_path = output_path.resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    report = {
        "schema_version": 1,
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "manifest": str(manifest_path),
        "dataset_revision": manifest.get("dataset_revision"),
        "model": str(model_path),
        "runner": str(runner),
        "worker": str(worker),
        "backend_requested": args.backend,
        "threads": args.threads,
        "normalizer": "vlcw-basic-v1",
        "aggregate": rows,
        "samples": per_sample,
    }
    output_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"\nLocal report: {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
