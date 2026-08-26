# Design & Evaluation: Phrase-by-Phrase Subtitle Timing & Segmentation

> **2026-08-20 (ADR-018):** the token-boundary suffix-extraction design described in the sections below is
> **SUPERSEDED** — subtitles are final and immutable, with no expansion or revision. See the
> `Step 17d.1 — Final Immutable Subtitles` section at the end of this document and ADR-018 in
> `docs/decisions.md`. The engine token accessors and `vw_segment_builder_push_phrase` are removed; the
> working design is whole-phrase dedup with per-phrase authentic timing.

# Task: Phrase-by-Phrase Subtitle Timing & Segmentation

## Overview & Background

In live media player subtitle rendering (comparable to Daum PotPlayer's real-time speech translation and Netflix/YouTube closed captioning), caption cues must be tightly synchronized with the speaker's vocal cadence.

### The Current Problem: Coarse Window Aggregation
In the current implementation:
1. Whisper receives an **8.0-second acoustic window** to ensure sufficient linguistic context for accurate recognition.
2. In [`worker/src/vw_whisper_engine.c`](file:///home/razvan/vlc-whisper/.worktrees/gemini/worker/src/vw_whisper_engine.c), all internal Whisper segments within that 8-second window are joined into a single string.
3. The worker assigns the combined paragraph a single timestamp span: `[window_pts, window_pts + 8.0s]`.
4. As a result, dialogue that is spoken 4–6 seconds in the future is rendered immediately at the start of the window, causing:
   - **Premature Dialogue Spoilers**: Questions and answers appear at the same instant before the conversation unfolds.
   - **Visual Overcrowding**: Large blocks of text flash and overwhelm the bottom of the screen.

---

## Caption Display Comparison

### Example Scenario
Audio timeline across an 8-second window:
* `0.5s – 2.8s`: Speaker A asks: *"Where are you from, Victoria?"*
* `2.8s – 3.4s`: [0.6s pause / silence]
* `3.4s – 7.1s`: Speaker B answers: *"I'm from Germany, from the north coast of Germany."*

---

### Comparison Matrix

#### 1. Current Coarse Window Aggregation (Flawed)
```text
Timeline: 0.0s ──────────────────────── 3.0s ──────────────────────── 8.0s
Screen:   ┌──────────────────────────────────────────────────────────────┐
          │ Where are you from, Victoria? I'm from Germany, from the     │
          │ north coast of Germany.                                      │
          └──────────────────────────────────────────────────────────────┘
          [Displayed immediately from 0.0s to 8.0s]
```
* **User Experience**: Speaker B's answer is spoiled 3 seconds before she even opens her mouth.

---

#### 2. Proposed Phrase-by-Phrase Timing (Daum PotPlayer / Netflix Style)
```text
Timeline: 0.0s ─── 0.5s ────────────── 2.8s ─── 3.4s ────────────── 7.1s ─── 8.0s
Screen:           ┌────────────────────────┐   ┌──────────────────────────────┐
                  │ Where are you from,    │   │ I'm from Germany, from the   │
                  │ Victoria?              │   │ north coast of Germany.      │
                  └────────────────────────┘   └──────────────────────────────┘
                  [Active 0.5s - 2.8s]          [Active 3.4s - 7.1s]
```
* **User Experience**: Subtitles match the speaker's natural turn-taking and rhythm. The screen clears during pauses and transitions cleanly between speakers.

---

## Whisper C API Support for Phrase Timestamps

`whisper.cpp` natively tracks timestamps for every phrase / segment within the decoding window.

### Key API Functions (`whisper.h`)

| Function | Return Type | Description |
|---|---|---|
| `whisper_full_n_segments(ctx)` | `int` | Number of detected sub-segments in the analyzed audio window. |
| `whisper_full_get_segment_t0(ctx, i)` | `int64_t` | Start time offset of segment `i` in **centiseconds** (10 ms units). |
| `whisper_full_get_segment_t1(ctx, i)` | `int64_t` | End time offset of segment `i` in **centiseconds** (10 ms units). |
| `whisper_full_get_segment_text(ctx, i)`| `const char*`| UTF-8 text for segment `i`. |
| `whisper_full_n_tokens(ctx, i)` | `int` | Number of Whisper tokens in segment `i` (fine-grained phrase units). |
| `whisper_full_get_token_text(ctx, i, j)` | `const char*` | UTF-8 of token `j` in segment `i` (leading space ok). |
| `whisper_full_get_token_data(ctx, i, j)` | `whisper_token_data` | t0/t1; requires `token_timestamps` enabled. |

### Precision & Conversion
Centiseconds are converted to VLC-Whisper microsecond media timestamps (`pts_us`):
$$\text{start\_pts\_us} = \text{window\_pts\_us} + (\text{t0} \times 10\,000LL)$$
$$\text{end\_pts\_us} = \text{window\_pts\_us} + (\text{t1} \times 10\,000LL)$$

---

## Architectural Changes Required

### 1. `vw_whisper_engine.h` & `vw_whisper_engine.c`
Expose individual segment iteration instead of collapsing to a single `last_text` buffer:
```c
typedef struct vw_whisper_segment {
  int64_t t0_us;
  int64_t t1_us;
  const char* text_utf8;
} vw_whisper_segment_t;

int vw_whisper_engine_get_segment_count(const vw_whisper_engine_t* engine);
bool vw_whisper_engine_get_segment(const vw_whisper_engine_t* engine, int index, vw_whisper_segment_t* out_seg);
```

Per-token timing is also exposed (Step 17d.1) so the builder can extract the new suffix of an expanded
overlapping phrase at authentic token boundaries:
```c
#define VW_WHISPER_MAX_TOKEN_BYTES 128
#define VW_WHISPER_MAX_TOKENS_PER_SEGMENT 128

typedef struct vw_whisper_token {
  char text[VW_WHISPER_MAX_TOKEN_BYTES];  // NUL-terminated token text (may include a leading space)
  int64_t t0_us;  // Start offset in microseconds, relative to window start
  int64_t t1_us;  // End offset in microseconds, relative to window start
} vw_whisper_token_t;

int vw_whisper_engine_get_segment_token_count(const vw_whisper_engine_t* engine, int segment_index);
bool vw_whisper_engine_get_segment_token(const vw_whisper_engine_t* engine, int segment_index,
                                         int token_index, vw_whisper_token_t* out_token);
```
These mirror `vw_whisper_engine_get_segment_count`/`get_segment` and are backed by `whisper_full_n_tokens`,
`whisper_full_get_token_text`, and `whisper_full_get_token_data`. Token t0/t1 are in whisper.cpp centiseconds
(10 ms units); the engine scales them by `10000LL` to microseconds, identical to the segment timebase. Token
text is copied bounded to `VW_WHISPER_MAX_TOKEN_BYTES - 1` and NUL-terminated; nothing is stripped.

NOTE: token-level `t0`/`t1` are only populated when whisper runs with `token_timestamps = true` in
`whisper_full_params` (see `whisper.h`). The engine enables this flag; otherwise the token APIs return valid
counts/text but zero (or stale) timing, and the builder falls back to whole-phrase dedup.

### 2. `worker/src/vw_worker.c` (Inference Loop)
Iterate over all sub-segments emitted by Whisper and push each phrase to `vw_segment_builder`. For every
segment, fetch its per-token timing into a stack array bounded by `VW_WHISPER_MAX_TOKENS_PER_SEGMENT`, convert
each token's relative t0/t1 to ABSOLUTE media PTS (`window_pts_us + token.t0_us / token.t1_us`, saturating via
`vw_saturating_add_i64`), and call `vw_segment_builder_push_phrase` with the token view. If token fetch fails or
returns zero tokens, fall back to `vw_segment_builder_push_hypothesis` (NULL tokens => legacy whole-phrase dedup):
```c
int n_segs = vw_whisper_engine_get_segment_count(engine);
for (int i = 0; i < n_segs; i++) {
  vw_whisper_segment_t s;
  if (!vw_whisper_engine_get_segment(engine, i, &s) || !s.text_utf8 || s.text_utf8[0] == '\0') continue;
  int64_t seg_start_pts = window_pts_us + s.t0_us;
  int64_t seg_end_pts   = window_pts_us + s.t1_us;

  vw_whisper_token_t toks[VW_WHISPER_MAX_TOKENS_PER_SEGMENT];
  vw_phrase_token_t  view[VW_WHISPER_MAX_TOKENS_PER_SEGMENT];
  int n_tok = vw_whisper_engine_get_segment_token_count(engine, i);
  if (n_tok > VW_WHISPER_MAX_TOKENS_PER_SEGMENT) n_tok = VW_WHISPER_MAX_TOKENS_PER_SEGMENT;
  size_t n_view = 0;
  for (int j = 0; j < n_tok; j++) {
    if (!vw_whisper_engine_get_segment_token(engine, i, j, &toks[j])) continue;
    view[j].text  = toks[j].text;
    view[j].t0_us = vw_saturating_add_i64(window_pts_us, toks[j].t0_us);
    view[j].t1_us = vw_saturating_add_i64(window_pts_us, toks[j].t1_us);
    n_view++;
  }
  if (n_view > 0) {
    vw_segment_builder_push_phrase(builder, s.text_utf8, seg_start_pts, seg_end_pts, view, n_view);
  } else {
    vw_segment_builder_push_hypothesis(builder, s.text_utf8, seg_start_pts, seg_end_pts);
  }
}
```

### 3. `worker/src/vw_segment_builder.c` (Deduplication & Suffix Extraction)

Audio windows hop every 2.0s, so adjacent windows overlap and Whisper re-emits the same phrase, sometimes
expanded with newly recognized trailing words. `vw_segment_builder` handles this with two entry points:

* `vw_segment_builder_push_hypothesis(builder, text, start_pts, end_pts)` — legacy whole-phrase entry point,
  now a thin wrapper that calls `vw_segment_builder_push_phrase` with `tokens = NULL`, `token_count = 0`.
* `vw_segment_builder_push_phrase(builder, text, start_pts, end_pts, tokens, token_count)` — token-aware
  entry point. `tokens` is a borrowed view (`vw_phrase_token_t`: `text`, absolute `t0_us`/`t1_us` media PTS)
  of the candidate's per-token timing, or `NULL` to disable suffix extraction.

Deduplication against committed history still uses 500ms timestamp proximity (`VW_DEDUP_TIME_TOLERANCE_US`) and
text equality/containment. The Step 17d.1 change is how an EXPANDED overlapping phrase is handled:

* **Before:** a candidate that was a superstring of a committed phrase was rejected wholesale, dropping the new
  trailing words and leaving a permanent subtitle gap.
* **After (token-boundary suffix extraction):** when the candidate is a superstring at the same timestamp and
  per-token timing is available, the builder keeps only the NEW SUFFIX — the trailing tokens whose absolute
  `t1_us` exceeds the committed/last-queued phrase end. The suffix is emitted to the pending queue with its own
  authentic token-boundary start/end, while the FULL candidate text and full window bounds are recorded in
  committed history. No synthetic timing is fabricated; emitted cues keep 100% authentic Whisper bounds.

Additional invariants preserved by the builder:
* The pending queue GROWS DYNAMICALLY (capacity doubles when full) so a committed cue is never dropped for space.
* The last-queued, not-yet-emitted segment is REPLACED IN PLACE when a newer expansion of the same phrase arrives,
  instead of pushing a duplicate.
* History is committed ONLY AFTER a successful queue push, so a failed push never pollutes deduplication.
* NULL-token fallback: when `tokens == NULL` (token timing unavailable), the original whole-phrase dedup behavior
  is preserved — including superstring rejection — with no synthetic timing.

### 4. `plugin/src/vw_caption_presenter.c` (Native SPU Rendering)
VLC's SPU engine receives distinct timed cues with discrete `i_start` and `i_stop`:
* Segment 1 (`Where are you from, Victoria?`): `i_start = now + lead_1`, `i_stop = i_start + duration_1`.
* Segment 2 (`I'm from Germany...`): `i_start = now + lead_2`, `i_stop = i_start + duration_2`.
* VLC automatically schedules each subpicture to appear on screen precisely as the speaker utters the words.

---

## Benefits & Tradeoffs Summary

| Aspect | Current Coarse 8s Block | Proposed Phrase-by-Phrase |
|---|---|---|
| **Reading Experience** | Spoilers 3–5s ahead; paragraph clutter | Synchronized with speech; Daum PotPlayer feel |
| **Speaker Turns** | Joined onto single line | Clean visual separation between speakers |
| **Silence Handling** | Text lingers through silent gaps | Screen blanks during conversational pauses |
| **Bandwidth / IPC** | 1 frame per 2–8s | 2–3 smaller frames per 2–8s (negligible) |
| **Algorithm Complexity** | Trivial concatenation | Precise per-segment offset mapping in engine |

---

## Next Steps for Implementation (Quality / Polish Pass)
1. Extend `vw_whisper_engine` to provide segment count and `(t0, t1, text)` accessors.
2. Update `vw_worker.c` to emit per-phrase hypotheses.
3. Verify deduplication in `vw_segment_builder` across 2s window hops.
4. Validate live rendering in VLC with conversational test videos.

---

## Step 17d.1 — Final Immutable Subtitles (SUPERSEDES token-boundary suffix extraction)

**Status:** Superseded / supersedes — see **ADR-018** (docs/decisions.md).

### Decision (2026-08-20)
Subtitles are **final and immutable**: each Whisper phrase is emitted exactly once with its authentic
per-phrase acoustic bounds (`window_pts + t0/t1`, `whisper_full_get_segment_t0/t1` scaled `×10000LL`).
Overlapping windows that re-recognize already-covered audio are suppressed **wholesale** — exact duplicates,
fragments, and expanded superstrings of committed or pending phrases are all dropped. There is **no suffix
extraction, no in-place revision, no token-boundary splitting, and no synthetic timing**. The first pass is
authoritative.

### Rationale
The token-boundary suffix-extraction design (engine token accessors, `vw_segment_builder_push_phrase`,
`vw_phrase_token_t`, `params.token_timestamps`) repeatedly traded one edge case for another in review
(boundary-spanning tokens, short-prefix repetition, token-timestamp availability). Under ADR-018 these are
moot: expansions are never revised into the stream, so per-token timing is unnecessary.

### Code shape (current)
* `vw_segment_builder_push_hypothesis(builder, text, start_pts_us, end_pts_us)` — whole-phrase dedup:
  exact / fragment / superstring all drop; dynamic queue growth; history commits only after a successful
  enqueue.
* Engine exposes only segment-level accessors (`vw_whisper_engine_get_segment_count`/`get_segment`); the
  token accessors, `vw_whisper_token_t`, and `params.token_timestamps` were removed.
* Worker pushes each Whisper sub-segment with authentic per-phrase PTS (no token view).

### Known limitation (accepted)
A genuinely new word recognized only inside an expanded later window is omitted rather than emitted late or
with synthetic timing. The next non-overlapping phrase resumes coverage.
