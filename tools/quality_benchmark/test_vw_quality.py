import tempfile
import unittest
from pathlib import Path

from vw_benchmark import add_counts, hypothesis_from_result, runner_timeout_seconds
from vw_download_corpus import duration_is_eligible, safe_sample_id, write_pcm16_wav
from vw_quality import ErrorCounts, character_tokens, edit_distance, normalize_text, score_pair, word_tokens


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

    def test_aggregate_counts(self):
        total = add_counts(ErrorCounts(1, 4, 2, 10), ErrorCounts(2, 6, 3, 20))
        self.assertEqual(total, ErrorCounts(3, 10, 5, 30))

    def test_hypothesis_joins_final_segments(self):
        result = {"segments": [{"text": "hello"}, {"text": " world "}]}
        self.assertEqual(hypothesis_from_result(result), "hello world")

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


if __name__ == "__main__":
    unittest.main()
