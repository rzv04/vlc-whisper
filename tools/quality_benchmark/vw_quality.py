#!/usr/bin/env python3
"""Shared normalization and edit-distance helpers for VLC-Whisper quality benchmarks."""

from __future__ import annotations

import re
import unicodedata
from dataclasses import dataclass

_WHITESPACE_RE = re.compile(r"\s+")
_ROMANIAN_COMPAT = str.maketrans({"ş": "ș", "Ş": "Ș", "ţ": "ț", "Ţ": "Ț"})


def normalize_text(text: str) -> str:
    """Apply the language-neutral benchmark normalizer while preserving letters and diacritics."""
    text = unicodedata.normalize("NFC", text.translate(_ROMANIAN_COMPAT)).lower()
    out: list[str] = []
    for ch in text:
        category = unicodedata.category(ch)
        if category.startswith("P") or category.startswith("S"):
            out.append(" ")
        else:
            out.append(ch)
    return _WHITESPACE_RE.sub(" ", "".join(out)).strip()


def word_tokens(text: str) -> list[str]:
    normalized = normalize_text(text)
    return normalized.split() if normalized else []


def character_tokens(text: str) -> list[str]:
    normalized = normalize_text(text)
    return [ch for ch in normalized if not ch.isspace()]


def edit_distance(reference: list[str], hypothesis: list[str]) -> int:
    """Compute Levenshtein distance with O(min(n,m)) memory."""
    if len(reference) < len(hypothesis):
        shorter, longer = reference, hypothesis
    else:
        shorter, longer = hypothesis, reference

    previous = list(range(len(shorter) + 1))
    for i, long_item in enumerate(longer, start=1):
        current = [i]
        for j, short_item in enumerate(shorter, start=1):
            substitution = previous[j - 1] + (long_item != short_item)
            insertion = current[j - 1] + 1
            deletion = previous[j] + 1
            current.append(min(substitution, insertion, deletion))
        previous = current
    return previous[-1]


@dataclass(frozen=True)
class ErrorCounts:
    word_errors: int
    reference_words: int
    char_errors: int
    reference_chars: int

    @property
    def wer(self) -> float:
        return self.word_errors / self.reference_words if self.reference_words else 0.0

    @property
    def cer(self) -> float:
        return self.char_errors / self.reference_chars if self.reference_chars else 0.0


def score_pair(reference: str, hypothesis: str) -> ErrorCounts:
    ref_words = word_tokens(reference)
    hyp_words = word_tokens(hypothesis)
    ref_chars = character_tokens(reference)
    hyp_chars = character_tokens(hypothesis)
    return ErrorCounts(
        word_errors=edit_distance(ref_words, hyp_words),
        reference_words=len(ref_words),
        char_errors=edit_distance(ref_chars, hyp_chars),
        reference_chars=len(ref_chars),
    )
