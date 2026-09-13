import json
import math
import struct
import tempfile
import unittest
from pathlib import Path

from vw_benchmark import (
    add_counts,
    hypothesis_from_result,
    load_manifest,
    runner_timeout_seconds,
    save_report_atomically,
    validate_executable_path,
)
from vw_download_corpus import duration_is_eligible, safe_sample_id, write_pcm16_wav
from vw_download_corpus_direct import parse_wav_float32_to_pcm16
from vw_quality import (
    ErrorCounts,
    char_error_rate,
    character_tokens,
    edit_distance,
    normalize_text,
    score_pair,
    word_error_rate,
    word_tokens,
)


class QualityHelpersTest(unittest.TestCase):
    def test_normalizer_preserves_romanian_diacritics(self):
        self.assertEqual(normalize_text("Ştiinţă, ȚARĂ!"), "știință țară")

    def test_normalizer_collapses_symbols_and_whitespace(self):
        self.assertEqual(normalize_text("  Hello—world...  €42  "), "hello world 42")

    def test_tokenizers(self):
        self.assertEqual(word_tokens("Bună, lume!"), ["bună", "lume"])
        self.assertEqual(character_tokens("Bună, lume!"), list("bunălume"))

    def test_edit_distance(self):
        self.assertEqual(edit_distance(list("kitten"), list("sitting")), 3)
        self.assertEqual(edit_distance([], ["a", "b"]), 2)
        self.assertEqual(edit_distance(["a", "b"], []), 2)

    def test_score_pair(self):
        score = score_pair("one two three", "one too three")
        self.assertEqual(score.word_errors, 1)
        self.assertEqual(score.reference_words, 3)
        self.assertAlmostEqual(score.wer, 1 / 3)
        self.assertGreater(score.cer, 0.0)

    def test_wer_cer_empty_reference_with_insertions(self):
        counts = ErrorCounts(word_errors=2, reference_words=0, char_errors=5, reference_chars=0)
        self.assertEqual(counts.wer, float("inf"))
        self.assertEqual(counts.cer, float("inf"))
        self.assertEqual(counts.word_error_rate, float("inf"))
        self.assertEqual(counts.char_error_rate, float("inf"))
        self.assertEqual(word_error_rate(2, 0), float("inf"))
        self.assertEqual(char_error_rate(5, 0), float("inf"))
        self.assertEqual(word_error_rate(0, 0), 0.0)
        self.assertEqual(char_error_rate(0, 0), 0.0)
        self.assertEqual(word_error_rate("", "inserted words"), float("inf"))
        self.assertEqual(char_error_rate("", "inserted chars"), float("inf"))

    def test_aggregate_counts(self):
        total = add_counts(ErrorCounts(1, 4, 2, 10), ErrorCounts(2, 6, 3, 20))
        self.assertEqual(total, ErrorCounts(3, 10, 5, 30))

    def test_hypothesis_joins_final_segments(self):
        result = {"segments": [{"text": "hello"}, {"text": " world "}]}
        self.assertEqual(hypothesis_from_result(result), "hello world")

    def test_hypothesis_from_result_validation(self):
        with self.assertRaises(ValueError):
            hypothesis_from_result("not a dict")  # type: ignore
        with self.assertRaises(ValueError):
            hypothesis_from_result({})
        with self.assertRaises(ValueError):
            hypothesis_from_result({"segments": "not a list"})
        with self.assertRaises(ValueError):
            hypothesis_from_result({"segments": ["not a dict"]})
        with self.assertRaises(ValueError):
            hypothesis_from_result({"segments": [{}]})
        with self.assertRaises(ValueError):
            hypothesis_from_result({"segments": [{"text": 123}]})

    def test_load_manifest_validation(self):
        with tempfile.TemporaryDirectory() as tmp:
            manifest_file = Path(tmp) / "manifest.json"

            # Missing dataset_revision
            manifest_file.write_text(json.dumps({"samples": []}), encoding="utf-8")
            with self.assertRaises(ValueError):
                load_manifest(manifest_file)

            # Invalid samples type
            manifest_file.write_text(json.dumps({"dataset_revision": "r1", "samples": "bad"}), encoding="utf-8")
            with self.assertRaises(ValueError):
                load_manifest(manifest_file)

            # Sample missing id
            manifest_file.write_text(
                json.dumps({"dataset_revision": "r1", "samples": [{"audio_file": "a.wav", "reference_text": "t", "duration_seconds": 1.0}]}),
                encoding="utf-8",
            )
            with self.assertRaises(ValueError):
                load_manifest(manifest_file)

            # Sample missing audio_file
            manifest_file.write_text(
                json.dumps({"dataset_revision": "r1", "samples": [{"id": "s1", "reference_text": "t", "duration_seconds": 1.0}]}),
                encoding="utf-8",
            )
            with self.assertRaises(ValueError):
                load_manifest(manifest_file)

            # Sample missing reference_text
            manifest_file.write_text(
                json.dumps({"dataset_revision": "r1", "samples": [{"id": "s1", "audio_file": "a.wav", "duration_seconds": 1.0}]}),
                encoding="utf-8",
            )
            with self.assertRaises(ValueError):
                load_manifest(manifest_file)

            # Sample invalid duration_seconds (negative)
            manifest_file.write_text(
                json.dumps({"dataset_revision": "r1", "samples": [{"id": "s1", "audio_file": "a.wav", "reference_text": "t", "duration_seconds": -1.0}]}),
                encoding="utf-8",
            )
            with self.assertRaises(ValueError):
                load_manifest(manifest_file)

            # Sample invalid duration_seconds (nan)
            manifest_file.write_text(
                json.dumps({"dataset_revision": "r1", "samples": [{"id": "s1", "audio_file": "a.wav", "reference_text": "t", "duration_seconds": "nan"}]}),
                encoding="utf-8",
            )
            with self.assertRaises(ValueError):
                load_manifest(manifest_file)

            # Valid manifest with audio_file and reference_text
            manifest_file.write_text(
                json.dumps({"dataset_revision": "r1", "samples": [{"id": "s1", "audio_file": "a.wav", "reference_text": "t", "duration_seconds": 2.5}]}),
                encoding="utf-8",
            )
            data = load_manifest(manifest_file)
            self.assertEqual(data["samples"][0]["path"], "a.wav")
            self.assertEqual(data["samples"][0]["reference"], "t")

            # Valid manifest with legacy path and reference
            manifest_file.write_text(
                json.dumps({"dataset_revision": "r1", "samples": [{"id": "s1", "path": "b.wav", "reference": "ref", "duration_seconds": 3.0}]}),
                encoding="utf-8",
            )
            data2 = load_manifest(manifest_file)
            self.assertEqual(data2["samples"][0]["audio_file"], "b.wav")
            self.assertEqual(data2["samples"][0]["reference_text"], "ref")

    def test_atomic_report_saving(self):
        with tempfile.TemporaryDirectory() as tmp:
            dest = Path(tmp) / "sub" / "report.json"
            save_report_atomically(dest, '{"test": true}')
            self.assertTrue(dest.is_file())
            self.assertEqual(dest.read_text(encoding="utf-8"), '{"test": true}')
            self.assertFalse(dest.with_suffix(".tmp").exists())
            self.assertEqual(list(dest.parent.glob("*.tmp")), [])

    def test_runner_timeout_preserves_completion_budget(self):
        self.assertEqual(runner_timeout_seconds(15.0, "live"), 161.5)
        self.assertEqual(runner_timeout_seconds(15.0, "lookahead"), 280.0)

    def test_downloader_helpers(self):
        self.assertTrue(duration_is_eligible(8.0, 2.5, 15.0))
        self.assertFalse(duration_is_eligible(1.0, 2.5, 15.0))
        self.assertEqual(safe_sample_id("a/b c", 3), "a_b_c")
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "clip.wav"
            write_pcm16_wav(path, b"\0\0" * 160)
            self.assertTrue(path.is_file())

    def test_riff_chunk_boundary_validation(self):
        malformed_wav = b"RIFF" + struct.pack("<I", 100) + b"WAVE" + b"fmt " + struct.pack("<I", 1000)
        with self.assertRaises(ValueError):
            parse_wav_float32_to_pcm16(malformed_wav)

    def test_write_pcm16_wav_atomic_cleanup(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "audio.wav"
            write_pcm16_wav(path, b"\0\0" * 160)
            self.assertTrue(path.is_file())
            self.assertFalse(path.with_suffix(".tmp").exists())


if __name__ == "__main__":
    unittest.main()
