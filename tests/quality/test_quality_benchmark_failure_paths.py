#!/usr/bin/env python3
"""Failure-path contracts for the optional local ASR quality benchmark."""

from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

SKIP = 77
ROOT = Path(__file__).resolve().parents[2]
QUALITY_DIR = ROOT / "tools" / "quality_benchmark"
QUALITY_MODULE = QUALITY_DIR / "vw_quality.py"
BENCHMARK_DRIVER = QUALITY_DIR / "vw_benchmark.py"


def require_quality_tool() -> bool:
    if QUALITY_MODULE.is_file() and BENCHMARK_DRIVER.is_file():
        return True
    print("quality benchmark is not present on this base branch; contract test staged for when it lands")
    return False


def empty_reference_contract() -> int:
    if not require_quality_tool():
        return SKIP
    sys.path.insert(0, str(QUALITY_DIR))
    from vw_quality import score_pair  # type: ignore

    try:
        counts = score_pair("", "inserted hypothesis")
    except (AssertionError, ValueError):
        return 0

    word_false_pass = counts.reference_words == 0 and counts.word_errors > 0 and counts.wer == 0.0
    char_false_pass = counts.reference_chars == 0 and counts.char_errors > 0 and counts.cer == 0.0
    if word_false_pass or char_false_pass:
        print("empty reference with insertions was reported as perfect WER/CER", file=sys.stderr)
        return 1
    return 0


def hash_mismatch_contract() -> int:
    if not require_quality_tool():
        return SKIP
    if os.name == "nt":
        print("hash mismatch subprocess fixture is currently POSIX-only")
        return SKIP

    with tempfile.TemporaryDirectory(prefix="vw-quality-hash-") as temp_dir_raw:
        temp_dir = Path(temp_dir_raw)
        audio = temp_dir / "sample.wav"
        model = temp_dir / "model.bin"
        runner = temp_dir / "fake-runner"
        manifest = temp_dir / "manifest.json"
        report = temp_dir / "report.json"

        audio.write_bytes(b"fixture bytes do not match the declared digest")
        model.write_bytes(b"stub model")
        runner.write_text("#!/usr/bin/env python3\nimport json\nprint(json.dumps({'segments': []}))\n", encoding="utf-8")
        runner.chmod(0o755)
        manifest.write_text(
            json.dumps(
                {
                    "schema_version": 1,
                    "dataset_revision": "test-revision",
                    "samples": [
                        {
                            "id": "hash-mismatch",
                            "language": "en",
                            "reference": "hello world",
                            "path": audio.name,
                            "duration_seconds": 0.01,
                            "sha256": "00" * 32,
                        }
                    ],
                }
            ),
            encoding="utf-8",
        )

        completed = subprocess.run(
            [
                sys.executable,
                str(BENCHMARK_DRIVER),
                "--manifest",
                str(manifest),
                "--build-dir",
                str(temp_dir),
                "--model",
                str(model),
                "--runner",
                str(runner),
                "--mode",
                "offline",
                "--output",
                str(report),
            ],
            capture_output=True,
            text=True,
            encoding="utf-8",
            check=False,
        )
        diagnostic = (completed.stderr + "\n" + completed.stdout).lower()
        hash_diagnostic = "hash" in diagnostic or "sha" in diagnostic or "checksum" in diagnostic
        if completed.returncode == 0 or report.exists() or not hash_diagnostic:
            print("fixture SHA-256 mismatch did not fail closed before scoring/reporting", file=sys.stderr)
            print(diagnostic, file=sys.stderr)
            return 1
    return 0


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: test_quality_benchmark_failure_paths.py <empty-reference|hash-mismatch>", file=sys.stderr)
        return 2
    if sys.argv[1] == "empty-reference":
        return empty_reference_contract()
    if sys.argv[1] == "hash-mismatch":
        return hash_mismatch_contract()
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
