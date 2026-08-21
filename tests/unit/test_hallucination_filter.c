// Copyright 2026 VLC-Whisper Contributors. All rights reserved.
// Use of this source code is governed by the MIT License that can be found in the LICENSE file.

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "vw_hallucination_filter.h"

static void vw_test_isolated_punctuation(void) {
  // NULL and empty
  assert(vw_hallucination_is_isolated_punctuation(NULL));
  assert(vw_hallucination_is_isolated_punctuation(""));
  assert(vw_hallucination_is_isolated_punctuation("   "));

  // Isolated punctuation with zero alphanumeric characters (MUST be rejected)
  assert(vw_hallucination_is_isolated_punctuation("."));
  assert(vw_hallucination_is_isolated_punctuation("..."));
  assert(vw_hallucination_is_isolated_punctuation(". . ."));
  assert(vw_hallucination_is_isolated_punctuation("---"));
  assert(vw_hallucination_is_isolated_punctuation("- - -"));
  assert(vw_hallucination_is_isolated_punctuation("! ! !"));
  assert(vw_hallucination_is_isolated_punctuation("???"));
  assert(vw_hallucination_is_isolated_punctuation(", , ,"));
  assert(vw_hallucination_is_isolated_punctuation(" * * * "));
  assert(vw_hallucination_is_isolated_punctuation("; ; ;"));

  // Valid phrasing with sentence punctuation (MUST NOT be rejected)
  assert(!vw_hallucination_is_isolated_punctuation("Hello"));
  assert(!vw_hallucination_is_isolated_punctuation("Hello, world!"));
  assert(!vw_hallucination_is_isolated_punctuation("Wait..."));
  assert(!vw_hallucination_is_isolated_punctuation("Look out!"));
  assert(!vw_hallucination_is_isolated_punctuation("What?"));
  assert(!vw_hallucination_is_isolated_punctuation("A"));
  assert(!vw_hallucination_is_isolated_punctuation("123"));
  assert(!vw_hallucination_is_isolated_punctuation("Yes... exactly."));
  assert(!vw_hallucination_is_isolated_punctuation("I think that... we should go."));

  // Multilingual / UTF-8 international text (H1 fix: MUST NOT be rejected)
  assert(!vw_hallucination_is_isolated_punctuation("Да."));
  assert(!vw_hallucination_is_isolated_punctuation("Привет"));
  assert(!vw_hallucination_is_isolated_punctuation("été"));
  assert(!vw_hallucination_is_isolated_punctuation("こんにちは"));
  assert(!vw_hallucination_is_isolated_punctuation("¿Cómo estás?"));
}

static void vw_test_non_speech_tags(void) {
  // NULL and empty
  assert(vw_hallucination_is_non_speech_tag(NULL));
  assert(vw_hallucination_is_non_speech_tag(""));
  assert(vw_hallucination_is_non_speech_tag("   "));

  // Standalone non-speech descriptor tags (MUST be rejected)
  assert(vw_hallucination_is_non_speech_tag("[music]"));
  assert(vw_hallucination_is_non_speech_tag("[MUSIC]"));
  assert(vw_hallucination_is_non_speech_tag("[ music ]"));
  assert(vw_hallucination_is_non_speech_tag("(music)"));
  assert(vw_hallucination_is_non_speech_tag("[applause]"));
  assert(vw_hallucination_is_non_speech_tag("(applause)"));
  assert(vw_hallucination_is_non_speech_tag("[laughter]"));
  assert(vw_hallucination_is_non_speech_tag("( laughter )"));
  assert(vw_hallucination_is_non_speech_tag("[silence]"));
  assert(vw_hallucination_is_non_speech_tag("(silence)"));
  assert(vw_hallucination_is_non_speech_tag("[cheering]"));
  assert(vw_hallucination_is_non_speech_tag("(cheering)"));
  assert(vw_hallucination_is_non_speech_tag("[screaming]"));
  assert(vw_hallucination_is_non_speech_tag("(screaming)"));
  assert(vw_hallucination_is_non_speech_tag("[gasp]"));
  assert(vw_hallucination_is_non_speech_tag("(gasp)"));
  assert(vw_hallucination_is_non_speech_tag("[sigh]"));
  assert(vw_hallucination_is_non_speech_tag("(sigh)"));
  assert(vw_hallucination_is_non_speech_tag("*music*"));
  assert(vw_hallucination_is_non_speech_tag("[blank_audio]"));
  assert(vw_hallucination_is_non_speech_tag("♪"));
  assert(vw_hallucination_is_non_speech_tag("♫"));
  assert(vw_hallucination_is_non_speech_tag("♪ ♪ ♪"));
  assert(vw_hallucination_is_non_speech_tag("[♪]"));

  // Legitimate spoken sentences containing words (MUST NOT be rejected)
  assert(!vw_hallucination_is_non_speech_tag("Music is my favorite hobby."));
  assert(!vw_hallucination_is_non_speech_tag("The audience burst into applause."));
  assert(!vw_hallucination_is_non_speech_tag("Laughter filled the entire room."));
  assert(!vw_hallucination_is_non_speech_tag("Silence fell over the crowd."));

  // Embedded tags/notes inside real dialogue (H2 fix: MUST NOT drop spoken sentence)
  assert(!vw_hallucination_is_non_speech_tag("He introduced [music] loudly."));
  assert(!vw_hallucination_is_non_speech_tag("Singing a song ♪ with friends"));
  assert(!vw_hallucination_is_non_speech_tag("The [applause] was deafening"));
}

static void vw_test_phantom_composite_filter(void) {
  // Composite filter rejects phantom cues
  assert(vw_hallucination_is_phantom_text(NULL));
  assert(vw_hallucination_is_phantom_text(""));
  assert(vw_hallucination_is_phantom_text("..."));
  assert(vw_hallucination_is_phantom_text("---"));
  assert(vw_hallucination_is_phantom_text("[MUSIC]"));
  assert(vw_hallucination_is_phantom_text("(Applause)"));
  assert(vw_hallucination_is_phantom_text("♪"));
  assert(vw_hallucination_is_phantom_text("[♪]"));

  // Composite filter preserves all legitimate dialogue, multilingual text, and sentence punctuation
  assert(!vw_hallucination_is_phantom_text("Good morning, ladies and gentlemen."));
  assert(!vw_hallucination_is_phantom_text("Where are you from, Victoria?"));
  assert(!vw_hallucination_is_phantom_text("I'm from Germany."));
  assert(!vw_hallucination_is_phantom_text("Wait... what did you say?"));
  assert(!vw_hallucination_is_phantom_text("Yes! That's 100% correct."));
  assert(!vw_hallucination_is_phantom_text("Thank you for watching this presentation."));
  assert(!vw_hallucination_is_phantom_text("You know, you know, it's fine."));
  assert(!vw_hallucination_is_phantom_text("Да, конечно."));
  assert(!vw_hallucination_is_phantom_text("Singing a song ♪ with friends"));
  assert(!vw_hallucination_is_phantom_text("He introduced [music] loudly."));
}

int main(void) {
  vw_test_isolated_punctuation();
  vw_test_non_speech_tags();
  vw_test_phantom_composite_filter();
  printf("All hallucination filter unit tests passed!\n");
  return 0;
}
