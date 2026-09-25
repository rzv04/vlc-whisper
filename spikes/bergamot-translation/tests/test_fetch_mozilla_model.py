#!/usr/bin/env python3
"""Regression tests for the standard-library model fetch helper."""

from __future__ import annotations

import importlib.util
import gzip
import hashlib
import io
import tempfile
import unittest
from pathlib import Path
from unittest import mock

SCRIPT_PATH = Path(__file__).resolve().parents[1] / "scripts" / "fetch_mozilla_model.py"
SPEC = importlib.util.spec_from_file_location("fetch_mozilla_model", SCRIPT_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"Unable to load {SCRIPT_PATH}")
FETCH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(FETCH)


class DownloadCleanupTests(unittest.TestCase):
    def test_successful_gzip_download_writes_only_final_file(self) -> None:
        payload = b"model contents"

        class Response(io.BytesIO):
            def __enter__(self):
                return self

            def __exit__(self, *args):
                self.close()

        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory)
            compressed = gzip.compress(payload)
            with mock.patch.object(
                FETCH.urllib.request,
                "urlopen",
                return_value=Response(compressed),
            ):
                output, digest = FETCH.download_gzip(
                    "https://example.invalid",
                    "model.bin.gz",
                    destination,
                )

            self.assertEqual(output.read_bytes(), payload)
            self.assertEqual(digest, hashlib.sha256(payload).hexdigest())
            self.assertEqual(list(destination.iterdir()), [output])

    def test_failed_transfer_removes_partial_download(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory)
            with mock.patch.object(
                FETCH.urllib.request,
                "urlopen",
                side_effect=OSError("transfer failed"),
            ):
                with self.assertRaisesRegex(OSError, "transfer failed"):
                    FETCH.download_gzip(
                        "https://example.invalid",
                        "model.bin.gz",
                        destination,
                    )

            self.assertEqual(list(destination.iterdir()), [])


if __name__ == "__main__":
    unittest.main()
