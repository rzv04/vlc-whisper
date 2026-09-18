#!/usr/bin/env python3
"""Run VLC-Whisper live/look-ahead ASR quality benchmarks and report WER/CER."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import subprocess
import sys
import tempfile
import uuid
import wave
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from vw_quality import ErrorCounts, normalize_text, score_pair

DEFAULT_MANIFEST = Path(__file__).resolve().parent / "local" / "corpus" / "manifest.json"
DEFAULT_RESULTS_DIR = Path(__file__).resolve().parent / "local" / "results"
SUPPORTED_MODES = ("offline", "live", "lookahead")
RUNNER_COMPLETION_TIMEOUT_SECONDS = 120.0
RUNNER_STARTUP_ALLOWANCE_SECONDS = 15.0
RUNNER_OUTER_GRACE_SECONDS = 10.0
LIVE_TAIL_SECONDS = 1.5


def sha256_wav_frames(path: Path) -> str:
    digest = hashlib.sha256()
    with wave.open(str(path), "rb") as source:
        for chunk in iter(lambda: source.readframes(8192), b""):
            digest.update(chunk)
    return digest.hexdigest()


def find_executable(build_dir: Path, relative_dir: str, stems: tuple[str, ...]) -> Path:
    suffix = ".exe" if os.name == "nt" else ""
    directory = build_dir / relative_dir
    for stem in stems:
        candidate = directory / f"{stem}{suffix}"
        if candidate.is_file():
            return candidate.resolve()
    names = ", ".join(f"{stem}{suffix}" for stem in stems)
    raise FileNotFoundError(f"could not find {names} under {directory}")


def validate_executable_path(path: Path | str, description: str = "executable") -> Path:
    """Validate that path exists and is an executable file."""
    p = Path(path).resolve()
    if not (os.path.isfile(p) and os.access(p, os.X_OK)):
        raise ValueError(f"{description} path is not an executable file: {p}")
    return p


def hypothesis_from_result(result: dict[str, Any]) -> str:
    """Extract joined hypothesis from runner result, validating segments schema."""
    if not isinstance(result, dict):
        raise ValueError(f"runner result must be a dict, got {type(result).__name__}")
    if "segments" not in result or not isinstance(result["segments"], list):
        raise ValueError("runner result missing or invalid 'segments' (expected list)")
    segments = result["segments"]
    for idx, seg in enumerate(segments):
        if not isinstance(seg, dict):
            raise ValueError(f"segment at index {idx} must be a dict, got {type(seg).__name__}")
        if "text" not in seg or not isinstance(seg["text"], str):
            raise ValueError(f"segment at index {idx} missing or invalid 'text' field (expected str)")
    return " ".join(seg["text"].strip() for seg in segments).strip()


def load_manifest(manifest_path: Path | str) -> dict[str, Any]:
    """Load and strictly validate the quality benchmark manifest JSON schema."""
    path = Path(manifest_path)
    try:
        raw_text = path.read_text(encoding="utf-8")
        manifest = json.loads(raw_text)
    except OSError as exc:
        raise ValueError(f"failed reading manifest file {path}: {exc}") from exc
    except json.JSONDecodeError as exc:
        raise ValueError(f"malformed JSON in manifest {path}: {exc}") from exc

    if not isinstance(manifest, dict):
        raise ValueError(f"manifest root must be a JSON object, got {type(manifest).__name__}")

    dataset_revision = manifest.get("dataset_revision")
    if not isinstance(dataset_revision, str) or not dataset_revision:
        raise ValueError("manifest missing or invalid 'dataset_revision' (expected non-empty str)")

    samples = manifest.get("samples")
    if not isinstance(samples, list):
        raise ValueError("manifest missing or invalid 'samples' (expected list)")

    for idx, sample in enumerate(samples):
        if not isinstance(sample, dict):
            raise ValueError(f"sample at index {idx} must be a JSON object, got {type(sample).__name__}")

        sample_id = sample.get("id")
        if not isinstance(sample_id, str) or not sample_id:
            raise ValueError(f"sample at index {idx} missing or invalid 'id' (expected non-empty str)")

        # Validate and canonicalize audio_file / path aliases.
        audio_file = sample.get("audio_file")
        path_alias = sample.get("path")
        if audio_file is None:
            audio_file = path_alias
        if not isinstance(audio_file, str) or not audio_file:
            raise ValueError(
                f"sample '{sample_id}' (index {idx}) missing or invalid 'audio_file' (expected non-empty str)"
            )
        if path_alias is not None and path_alias != audio_file:
            raise ValueError(
                f"sample '{sample_id}' (index {idx}) has conflicting 'audio_file' and 'path' values"
            )
        sample["audio_file"] = audio_file
        sample["path"] = audio_file

        # Validate reference_text / reference
        reference_text = sample.get("reference_text")
        reference_alias = sample.get("reference")
        if reference_text is None:
            reference_text = reference_alias
        if not isinstance(reference_text, str):
            raise ValueError(
                f"sample '{sample_id}' (index {idx}) missing or invalid 'reference_text' (expected str)"
            )
        if reference_alias is not None and reference_alias != reference_text:
            raise ValueError(
                f"sample '{sample_id}' (index {idx}) has conflicting 'reference_text' and 'reference' values"
            )
        sample["reference_text"] = reference_text
        sample["reference"] = reference_text

        # Validate duration_seconds
        if "duration_seconds" not in sample:
            raise ValueError(f"sample '{sample_id}' (index {idx}) missing 'duration_seconds'")
        duration = sample["duration_seconds"]
        if (
            isinstance(duration, bool)
            or not isinstance(duration, (int, float))
            or not math.isfinite(duration)
            or duration < 0
        ):
            raise ValueError(
                f"sample '{sample_id}' (index {idx}) has invalid 'duration_seconds' ({duration!r}); "
                "expected non-negative finite float"
            )

    return manifest


def save_report_atomically(report_path: Path, content: str) -> None:
    """Atomically write report content to destination path via temp file and rename."""
    report_path.parent.mkdir(parents=True, exist_ok=True)
    tmp_path: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            "w",
            dir=report_path.parent,
            prefix=f"{report_path.name}.",
            suffix=".tmp",
            delete=False,
            encoding="utf-8",
        ) as tmp_file:
            tmp_path = Path(tmp_file.name)
            tmp_file.write(content)
            tmp_file.flush()
            os.fsync(tmp_file.fileno())
        os.replace(tmp_path, report_path)
    except Exception as exc:
        if tmp_path is not None and tmp_path.exists():
            try:
                tmp_path.unlink()
            except OSError:
                pass
        raise OSError(f"failed to atomically save report to {report_path}: {exc}") from exc


def add_counts(left: ErrorCounts, right: ErrorCounts) -> ErrorCounts:
    return ErrorCounts(
        word_errors=left.word_errors + right.word_errors,
        reference_words=left.reference_words + right.reference_words,
        char_errors=left.char_errors + right.char_errors,
        reference_chars=left.reference_chars + right.reference_chars,
    )


def runner_timeout_seconds(duration_seconds: float, mode: str) -> float:
    """Keep Python's outer watchdog strictly outside the C runner's bounded waits."""
    pacing_seconds = max(0.0, duration_seconds) + (LIVE_TAIL_SECONDS if mode == "live" else 0.0)
    completion_phases = 0 if mode == "offline" else (1 if mode == "live" else 2)
    return (
        pacing_seconds
        + RUNNER_STARTUP_ALLOWANCE_SECONDS
        + max(1, completion_phases) * RUNNER_COMPLETION_TIMEOUT_SECONDS
        + RUNNER_OUTER_GRACE_SECONDS
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
    parser.add_argument("--mode", choices=("all", "both") + SUPPORTED_MODES, default="all")
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

    if args.mode == "all":
        modes = ("offline", "live", "lookahead")
    elif args.mode == "both":
        modes = ("live", "lookahead")
    else:
        modes = (args.mode,)

    try:
        runner = args.runner.resolve() if args.runner else find_executable(
            build_dir, "tools/quality_benchmark", ("vw-quality-benchmark",)
        )
        validate_executable_path(runner, "runner")
        worker = None
        if any(m in ("live", "lookahead") for m in modes):
            worker = args.worker.resolve() if args.worker else find_executable(
                build_dir, "worker", ("vlc-whisper-worker", "vlc-whisper-worker-cpu")
            )
            validate_executable_path(worker, "worker")
    except (FileNotFoundError, ValueError) as exc:
        print(str(exc), file=sys.stderr)
        return 2

    try:
        manifest = load_manifest(manifest_path)
    except ValueError as exc:
        print(f"invalid manifest schema: {exc}", file=sys.stderr)
        return 2

    samples = manifest.get("samples", [])
    if not samples:
        print("manifest contains no samples", file=sys.stderr)
        return 2

    if any(sample.get("language") == "ro" for sample in samples) and ".en." in model_path.name:
        print("warning: an English-only model was selected while Romanian samples are present", file=sys.stderr)

    for sample in samples:
        reference = str(sample.get("reference", ""))
        if not normalize_text(reference):
            print(f"empty normalized reference for sample {sample.get('id', '<unknown>')}", file=sys.stderr)
            return 2
        audio_path = (manifest_path.parent / str(sample.get("path", ""))).resolve()
        if not audio_path.is_file():
            print(f"missing corpus audio: {audio_path}", file=sys.stderr)
            return 1
        expected_sha256 = str(sample.get("sha256", "")).lower()
        try:
            actual_sha256 = sha256_wav_frames(audio_path)
        except (EOFError, wave.Error) as exc:
            print(f"invalid corpus WAV for sample {sample.get('id', '<unknown>')}: {exc}", file=sys.stderr)
            return 1
        if expected_sha256 != actual_sha256:
            print(
                f"SHA-256 mismatch for sample {sample.get('id', '<unknown>')}: "
                f"expected {expected_sha256}, got {actual_sha256}",
                file=sys.stderr,
            )
            return 1

    per_sample: list[dict[str, Any]] = []
    aggregate: dict[tuple[str, str], ErrorCounts] = defaultdict(lambda: ErrorCounts(0, 0, 0, 0))

    for mode in modes:
        print(f"\n=== Running benchmark phase: {mode.upper()} ===")
        for sample_index, sample in enumerate(samples, start=1):
            language = str(sample["language"])
            reference = str(sample["reference"])
            audio_path = (manifest_path.parent / str(sample.get("path", ""))).resolve()
            if not audio_path.is_file():
                print(f"missing corpus audio: {audio_path}", file=sys.stderr)
                return 1
            duration_seconds = float(sample.get("duration_seconds", 0.0))

            print(f"[{sample_index}/{len(samples)}] {language} {sample['id']} {mode} ({duration_seconds:.1f}s)")
            command = [
                str(runner),
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
            if worker is not None:
                command.extend(["--worker", str(worker)])
            timeout_seconds = runner_timeout_seconds(duration_seconds, mode)
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
                print(
                    f"benchmark runner timed out for {sample['id']} ({mode}) after {timeout_seconds:.1f}s",
                    file=sys.stderr,
                )
                return 1
            except OSError as exc:
                print(
                    f"failed to execute benchmark runner for {sample['id']} ({mode}): {exc}",
                    file=sys.stderr,
                )
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

            try:
                hypothesis = hypothesis_from_result(result)
            except ValueError as exc:
                print(f"invalid runner result schema for {sample['id']} ({mode}): {exc}", file=sys.stderr)
                return 1
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
    print("--------  ---------  -------  -------  ------------  -----------")
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
                f"{language:<8}  {mode:<9}  {counts.wer * 100:6.2f}%  {counts.cer * 100:6.2f}%  "
                f"{counts.word_errors:5d}/{counts.reference_words:<5d}  "
                f"{counts.char_errors:5d}/{counts.reference_chars:<5d}"
            )

    output_path = args.output
    if output_path is None:
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S_%fZ")
        unique_suffix = uuid.uuid4().hex[:8]
        output_path = DEFAULT_RESULTS_DIR / f"quality-{stamp}-{unique_suffix}.json"
    output_path = output_path.resolve()
    report = {
        "schema_version": 1,
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "manifest": str(manifest_path),
        "dataset_revision": manifest.get("dataset_revision"),
        "model": str(model_path),
        "runner": str(runner),
        "worker": str(worker) if worker else None,
        "backend_requested": args.backend,
        "threads": args.threads,
        "normalizer": "vlcw-basic-v1",
        "aggregate": rows,
        "samples": per_sample,
    }
    report_content = json.dumps(report, ensure_ascii=False, indent=2) + "\n"
    try:
        save_report_atomically(output_path, report_content)
    except OSError as exc:
        print(f"failed writing report: {exc}", file=sys.stderr)
        return 1
    print(f"\nLocal report: {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
