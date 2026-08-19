# Design & Evaluation: Phrase-by-Phrase Subtitle Timing & Segmentation

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

### 2. `worker/src/vw_worker.c` (Inference Loop)
Iterate over all sub-segments emitted by Whisper and push each phrase as an individual hypothesis to `vw_segment_builder`:
```c
int n_segs = vw_whisper_engine_get_segment_count(engine);
for (int i = 0; i < n_segs; i++) {
  vw_whisper_segment_t s;
  if (vw_whisper_engine_get_segment(engine, i, &s) && s.text_utf8 && s.text_utf8[0] != '\0') {
    int64_t seg_start_pts = window_pts_us + s.t0_us;
    int64_t seg_end_pts   = window_pts_us + s.t1_us;
    vw_segment_builder_push_hypothesis(builder, s.text_utf8, seg_start_pts, seg_end_pts);
  }
}
```

### 3. `worker/src/vw_segment_builder.c` (Deduplication & Smoothing)
Because audio windows hop every 2.0s, adjacent windows will overlap.
* The segment builder matches incoming hypotheses by timestamp overlap:
  $$\Delta \text{start} = |\text{hyp.start\_pts} - \text{active.start\_pts}| < 500\,000\,\mu\text{s}$$
* When timestamps align, the text hypothesis is updated / confirmed as `is_final`.
* Completed phrases are emitted as standalone `VW_MSG_CAPTION_SEGMENT` frames.

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
