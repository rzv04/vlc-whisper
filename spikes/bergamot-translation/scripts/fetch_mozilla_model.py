#!/usr/bin/env python3
"""Download one Mozilla Bergamot model and generate a local Marian/Bergamot config.

Uses only the Python standard library. Model metadata comes from Mozilla's public
translation model registry. The generated config uses paths relative to itself.
"""

from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import shutil
import sys
import tempfile
import urllib.request
from pathlib import Path

REGISTRY_URL = (
    "https://storage.googleapis.com/"
    "moz-fx-translations-data--303e-prod-translations-data/db/models.json"
)
MODEL_LICENSE = "MPL-2.0"


def load_json(url: str) -> dict:
    request = urllib.request.Request(url, headers={"User-Agent": "VLC-Whisper-Bergamot-Spike/1"})
    with urllib.request.urlopen(request, timeout=60) as response:
        return json.load(response)


def is_release_status(value: object) -> bool:
    return isinstance(value, str) and value.lower().startswith("release")


def choose_model(registry: dict, source: str, target: str, architecture: str) -> dict:
    pair = f"{source}-{target}"
    candidates = list(registry.get("models", {}).get(pair, []))
    if not candidates:
        raise RuntimeError(f"Mozilla registry has no model for {pair}")

    released = [item for item in candidates if is_release_status(item.get("releaseStatus"))]
    if released:
        candidates = released

    if architecture != "auto":
        candidates = [item for item in candidates if item.get("architecture") == architecture]
        if not candidates:
            raise RuntimeError(
                f"No {architecture!r} model for {pair} in the selected release set; "
                "try --architecture auto"
            )

    def model_size(item: dict) -> int:
        return int(item.get("files", {}).get("model", {}).get("uncompressedSize") or 2**63 - 1)

    return min(candidates, key=model_size)


def download_gzip(base_url: str, relative_path: str, destination_dir: Path) -> tuple[Path, str]:
    url = base_url.rstrip("/") + "/" + relative_path.lstrip("/")
    gz_name = Path(relative_path).name
    output_name = gz_name[:-3] if gz_name.endswith(".gz") else gz_name
    output_path = destination_dir / output_name

    print(f"Downloading {url}", file=sys.stderr)
    request = urllib.request.Request(url, headers={"User-Agent": "VLC-Whisper-Bergamot-Spike/1"})
    with tempfile.NamedTemporaryFile(prefix=output_name + ".", suffix=".download", dir=destination_dir, delete=False) as tmp:
        temp_path = Path(tmp.name)
        with urllib.request.urlopen(request, timeout=120) as response:
            shutil.copyfileobj(response, tmp)

    decompressed_tmp = output_path.with_suffix(output_path.suffix + ".partial")
    digest = hashlib.sha256()
    try:
        if gz_name.endswith(".gz"):
            source_stream = gzip.open(temp_path, "rb")
        else:
            source_stream = temp_path.open("rb")
        with source_stream, decompressed_tmp.open("wb") as output:
            while True:
                chunk = source_stream.read(1024 * 1024)
                if not chunk:
                    break
                digest.update(chunk)
                output.write(chunk)
        decompressed_tmp.replace(output_path)
    finally:
        temp_path.unlink(missing_ok=True)
        decompressed_tmp.unlink(missing_ok=True)

    return output_path, digest.hexdigest()


def gemm_precision(model_filename: str) -> str:
    if ".intgemm.alphas.bin" in model_filename:
        return "int8shiftAlphaAll"
    if ".intgemm8.bin" in model_filename:
        return "int8shiftAll"
    raise RuntimeError(f"Unsupported Bergamot model precision naming: {model_filename}")


def write_config(output_dir: Path, model: Path, vocabs: list[Path], shortlist: Path) -> Path:
    if len(vocabs) not in (1, 2):
        raise RuntimeError("Bergamot config needs one shared vocab or separate source/target vocabs")
    source_vocab = vocabs[0]
    target_vocab = vocabs[0] if len(vocabs) == 1 else vocabs[1]

    config_path = output_dir / "model.yml"
    precision = gemm_precision(model.name)
    config_path.write_text(
        "\n".join(
            [
                "models:",
                f"- {model.name}",
                "vocabs:",
                f"- {source_vocab.name}",
                f"- {target_vocab.name}",
                "shortlist:",
                f"- {shortlist.name}",
                "- false",
                "beam-size: 1",
                "normalize: 1.0",
                "word-penalty: 0",
                "max-length-break: 128",
                "mini-batch-words: 1024",
                "workspace: 128",
                "max-length-factor: 2.0",
                "skip-cost: true",
                "cpu-threads: 0",
                "quiet: true",
                "quiet-translation: true",
                f"gemm-precision: {precision}",
                "alignment: soft",
                "",
            ]
        ),
        encoding="utf-8",
    )
    return config_path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", default="en", help="source language code (default: en)")
    parser.add_argument("--target", default="ro", help="target language code (default: ro)")
    parser.add_argument(
        "--architecture",
        choices=("auto", "tiny", "base-memory", "base"),
        default="auto",
        help="model architecture; auto chooses the smallest released model (default: auto)",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        help="destination directory (default: ./models/<source>-<target>)",
    )
    parser.add_argument("--registry-url", default=REGISTRY_URL, help=argparse.SUPPRESS)
    args = parser.parse_args()

    output_dir = args.output_dir or Path("models") / f"{args.source}-{args.target}"
    output_dir.mkdir(parents=True, exist_ok=True)

    registry = load_json(args.registry_url)
    selected = choose_model(registry, args.source, args.target, args.architecture)
    files = selected.get("files", {})
    for required in ("model", "lexicalShortlist"):
        if required not in files or not files[required].get("path"):
            raise RuntimeError(f"Registry entry is missing required file metadata: {required}")

    has_joint_vocab = bool(files.get("vocab", {}).get("path"))
    has_split_vocabs = bool(files.get("srcVocab", {}).get("path")) and bool(
        files.get("trgVocab", {}).get("path")
    )
    if not has_joint_vocab and not has_split_vocabs:
        raise RuntimeError(
            "Registry entry has neither a shared vocab nor separate source/target vocabs"
        )

    base_url = registry["baseUrl"]
    model_path, model_hash = download_gzip(base_url, files["model"]["path"], output_dir)
    expected_hash = files["model"].get("uncompressedHash")
    if expected_hash and model_hash.lower() != expected_hash.lower():
        model_path.unlink(missing_ok=True)
        raise RuntimeError(
            f"Model SHA-256 mismatch: expected {expected_hash}, got {model_hash}"
        )

    vocab_entries: list[tuple[Path, str]] = []
    if has_joint_vocab:
        vocab_entries.append(download_gzip(base_url, files["vocab"]["path"], output_dir))
    else:
        vocab_entries.append(download_gzip(base_url, files["srcVocab"]["path"], output_dir))
        vocab_entries.append(download_gzip(base_url, files["trgVocab"]["path"], output_dir))

    shortlist_path, shortlist_hash = download_gzip(
        base_url, files["lexicalShortlist"]["path"], output_dir
    )
    vocab_paths = [entry[0] for entry in vocab_entries]
    config_path = write_config(output_dir, model_path, vocab_paths, shortlist_path)

    manifest_vocabs = [
        {"name": path.name, "sha256": digest} for path, digest in vocab_entries
    ]
    manifest = {
        "sourceLanguage": args.source,
        "targetLanguage": args.target,
        "architecture": selected.get("architecture"),
        "releaseStatus": selected.get("releaseStatus"),
        "registryGenerated": registry.get("generated"),
        "registryUrl": args.registry_url,
        "baseUrl": base_url,
        "license": MODEL_LICENSE,
        "licenseEvidence": "mozilla/translations README states model files are distributed under MPL 2.0",
        "files": {
            "model": {"name": model_path.name, "sha256": model_hash},
            "vocabs": manifest_vocabs,
            "lexicalShortlist": {"name": shortlist_path.name, "sha256": shortlist_hash},
            "config": {"name": config_path.name},
        },
    }
    (output_dir / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )

    print(f"Ready: {config_path}")
    print(
        f"Selected {args.source}->{args.target} {selected.get('architecture')} "
        f"({selected.get('releaseStatus')})"
    )
    print(f"Model SHA-256: {model_hash}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
