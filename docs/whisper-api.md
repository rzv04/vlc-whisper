# whisper.cpp C API Reference

> **Vendor dependency:** [`whisper.cpp`](https://github.com/ggerganov/whisper.cpp) — pinned in `worker/third_party/whisper.cpp/`.  
> **Header:** `worker/third_party/whisper.cpp/include/whisper.h`  
> **Linked by:** `vlc-whisper-worker` only (the plugin *must not* link Whisper).

This document describes the public C API surface of the pinned `whisper.cpp` library as exposed by `whisper.h`. The worker uses these functions to load a GGML model, feed PCM audio, and retrieve transcribed text segments.

---

## Constants

| Macro | Value | Meaning |
|---|---|---|
| `WHISPER_SAMPLE_RATE` | `16000` | Expected input sample rate in Hz |
| `WHISPER_N_FFT` | `400` | FFT size for mel spectrogram |
| `WHISPER_HOP_LENGTH` | `160` | Hop length (10 ms at 16 kHz) |
| `WHISPER_CHUNK_SIZE` | `30` | Default chunk duration in seconds |

All PCM audio fed to the library must be 16 kHz mono 32-bit float.

---

## Core Types

### `struct whisper_context`

Opaque handle representing a loaded model and its default processing state. Created by `whisper_init_*` and freed by `whisper_free()`. All `whisper_full_*` accessor functions operate on this handle.

### `struct whisper_state`

Opaque handle representing an additional processing state attached to a context via `whisper_init_state()`. Enables concurrent or interleaved transcription sessions on the same model. Most `whisper_full_*_from_state()` functions accept this parameter.

### `struct whisper_context_params`

Controls model loading behaviour.

| Field | Type | Default | Description |
|---|---|---|---|
| `use_gpu` | `bool` | implementation-defined | Offload computation to GPU |
| `flash_attn` | `bool` | `false` | Enable flash attention (reduces memory, may affect quality) |
| `gpu_device` | `int` | `0` | CUDA device index |
| `dtw_token_timestamps` | `bool` | `false` | [EXPERIMENTAL] Token-level timestamps via DTW |
| `dtw_aheads_preset` | `enum` | — | Alignment heads preset for DTW |
| `dtw_n_top` | `int` | — | Number of top alignment heads for DTW |
| `dtw_aheads` | `struct` | — | Custom alignment heads (when preset is `CUSTOM`) |

Return the default via `whisper_context_default_params()`.

### `typedef int32_t whisper_token`

A token identifier in the model's vocabulary.

### `typedef int32_t whisper_pos`

A position index in the encoder/decoder.

### `struct whisper_token_data`

Data for a single decoded token.

| Field | Type | Description |
|---|---|---|
| `id` | `whisper_token` | Token ID |
| `tid` | `whisper_token` | Forced timestamp token ID |
| `p` | `float` | Probability of the token |
| `plog` | `float` | Log probability of the token |
| `pt` | `float` | Probability of the timestamp token |
| `ptsum` | `float` | Sum of probabilities of all timestamp tokens |
| `t0` | `int64_t` | Token-level start time (10 ms units, see note) |
| `t1` | `int64_t` | Token-level end time (10 ms units, see note) |
| `t_dtw` | `int64_t` | [EXPERIMENTAL] DTW-aligned token time (10 ms units) |
| `vlen` | `float` | Voice length of the token |

> **Note on time units:** `t0`/`t1` are in **centiseconds** (10 ms units). The VLC-Whisper worker converts them to microsecond PTS via `pts_us = t0 * 10000`.

### `enum whisper_sampling_strategy`

| Enumerator | Description |
|---|---|
| `WHISPER_SAMPLING_GREEDY` | Greedy decoding (deterministic, faster) |
| `WHISPER_SAMPLING_BEAM_SEARCH` | Beam search decoding (higher quality, slower) |

### `struct whisper_full_params`

Comprehensive parameters block passed to `whisper_full()`. Return default via `whisper_full_default_params(strategy)`.

| Field | Type | Default | Description |
|---|---|---|---|
| `strategy` | `enum` | — | Sampling strategy |
| `n_threads` | `int` | `1` | Thread count for computation |
| `n_max_text_ctx` | `int` | — | Max tokens from past text as decoder prompt |
| `offset_ms` | `int` | `0` | Start offset in ms |
| `duration_ms` | `int` | `0` | Audio duration to process (0 = entire input) |
| `translate` | `bool` | `false` | Translate to English |
| `no_context` | `bool` | `false` | Don't use past transcription as prompt |
| `no_timestamps` | `bool` | `false` | Suppress timestamp tokens |
| `single_segment` | `bool` | `false` | Force single segment output |
| `print_special` | `bool` | `false` | Print special tokens to stdout |
| `print_progress` | `bool` | `false` | Print progress to stdout |
| `print_realtime` | `bool` | `false` | Print results as they are generated |
| `print_timestamps` | `bool` | `false` | Print timestamps with segments |
| `token_timestamps` | `bool` | `false` | Enable token-level timestamps |
| `thold_pt` | `float` | `0.01` | Timestamp token probability threshold |
| `thold_ptsum` | `float` | `0.01` | Timestamp sum probability threshold |
| `max_len` | `int` | `0` | Max segment length in characters |
| `split_on_word` | `bool` | `false` | Split on word boundaries |
| `max_tokens` | `int` | `0` | Max tokens per segment (0 = no limit) |
| `debug_mode` | `bool` | `false` | Dump debug info (log mel, etc.) |
| `audio_ctx` | `int` | `0` | Override audio context size (0 = default) |
| `tdrz_enable` | `bool` | `false` | [EXPERIMENTAL] Tinydiarize speaker turns |
| `suppress_regex` | `const char*` | `NULL` | Regex matching tokens to suppress |
| `initial_prompt` | `const char*` | `NULL` | Initial prompt text |
| `carry_initial_prompt` | `bool` | `true` | Always prepend initial prompt to every decode window |
| `prompt_tokens` | `const whisper_token*` | `NULL` | Pre-tokenized prompt tokens |
| `prompt_n_tokens` | `int` | `0` | Number of prompt tokens |
| `language` | `const char*` | `"auto"` | Language code or `"auto"` for detection |
| `detect_language` | `bool` | `false` | Auto-detect language |
| `suppress_blank` | `bool` | `true` | Suppress blank tokens |
| `suppress_nst` | `bool` | `true` | Suppress non-speech tokens |
| `temperature` | `float` | `0.0` | Initial decoding temperature |
| `max_initial_ts` | `float` | `1.0` | Max initial timestamp |
| `length_penalty` | `float` | `-1.0` | Length penalty |
| `temperature_inc` | `float` | `0.2` | Temperature increase fallback |
| `entropy_thold` | `float` | `2.4` | Entropy threshold fallback |
| `logprob_thold` | `float` | `-1.0` | Log-probability threshold fallback |
| `no_speech_thold` | `float` | `0.6` | No-speech probability threshold |
| `greedy.best_of` | `int` | `5` | Best-of candidates (greedy) |
| `beam_search.beam_size` | `int` | `5` | Beam size (beam search) |
| `beam_search.patience` | `float` | `-1.0` | Beam search patience (not implemented) |
| `new_segment_callback` | callback | `NULL` | Called per new text segment |
| `progress_callback` | callback | `NULL` | Called on progress updates |
| `encoder_begin_callback` | callback | `NULL` | Called before encoder starts; return `false` to abort |
| `abort_callback` | callback | `NULL` | ggml abort callback |
| `logits_filter_callback` | callback | `NULL` | Called to filter logits after temperature |
| `grammar_rules` | `const whisper_grammar_element**` | `NULL` | Grammar rules for constrained decoding |
| `n_grammar_rules` | `size_t` | `0` | Number of grammar rules |
| `i_start_rule` | `size_t` | `0` | Start rule index |
| `grammar_penalty` | `float` | `0.0` | Grammar penalty |
| `vad` | `bool` | `false` | Enable built-in VAD |
| `vad_model_path` | `const char*` | `NULL` | Path to VAD model |
| `vad_params` | `struct` | — | VAD parameters |

### `struct whisper_vad_params`

| Field | Type | Description |
|---|---|---|
| `threshold` | `float` | Probability threshold for speech detection |
| `min_speech_duration_ms` | `int` | Minimum duration for a valid speech segment |
| `min_silence_duration_ms` | `int` | Minimum silence to end a speech segment |
| `max_speech_duration_s` | `float` | Maximum speech duration before forcing new segment |
| `speech_pad_ms` | `int` | Padding before/after speech segments |
| `samples_overlap` | `float` | Overlap in seconds when copying audio from speech segments |

### `enum whisper_gretype`

Grammar element types used with `whisper_grammar_element` for constrained decoding:

| Enumerator | Description |
|---|---|
| `WHISPER_GRETYPE_END` | End of rule definition |
| `WHISPER_GRETYPE_ALT` | Alternate definition within a rule |
| `WHISPER_GRETYPE_RULE_REF` | Reference to another rule |
| `WHISPER_GRETYPE_CHAR` | Single character (code point) |
| `WHISPER_GRETYPE_CHAR_NOT` | Inverse character class (`[^a]`) |
| `WHISPER_GRETYPE_CHAR_RNG_UPPER` | Inclusive range upper bound (`[a-z]`) |
| `WHISPER_GRETYPE_CHAR_ALT` | Alternate character to match (`[ab]`) |

---

## Lifecycle: Initialization & Teardown

```c
struct whisper_context_params whisper_context_default_params(void);
```

Returns a default-initialised `whisper_context_params` with safe defaults (GPU enabled if available, flash attention off).

### Model Loading

```c
struct whisper_context * whisper_init_from_file_with_params(
    const char * path_model,
    struct whisper_context_params params);

struct whisper_context * whisper_init_from_buffer_with_params(
    void * buffer,
    size_t buffer_size,
    struct whisper_context_params params);

struct whisper_context * whisper_init_with_params(
    struct whisper_model_loader * loader,
    struct whisper_context_params params);
```

Load a GGML model from file, memory buffer, or custom loader. Returns `NULL` on failure. These are the preferred entry points — the deprecated `whisper_init_from_file()` etc. omit parameters.

### State Management

```c
struct whisper_state * whisper_init_state(struct whisper_context * ctx);
```

Allocate an additional processing state for the same model. Use with `whisper_full_with_state()` for interleaved or concurrent sessions.

### Cleanup

```c
void whisper_free(struct whisper_context * ctx);
void whisper_free_state(struct whisper_state * state);
void whisper_free_params(struct whisper_full_params * params);
void whisper_free_context_params(struct whisper_context_params * params);
```

Free all memory associated with the context, state, or parameter blocks.

---

## PCM → Mel Spectrogram

```c
int whisper_pcm_to_mel(
    struct whisper_context * ctx,
    const float * samples,
    int n_samples,
    int n_threads);

int whisper_pcm_to_mel_with_state(
    struct whisper_context * ctx,
    struct whisper_state * state,
    const float * samples,
    int n_samples,
    int n_threads);
```

Convert raw PCM audio (16 kHz mono 32-bit float) to a log mel spectrogram stored inside the context/state. Returns `0` on success.

```c
int whisper_set_mel(
    struct whisper_context * ctx,
    const float * data,
    int n_len,
    int n_mel);

int whisper_set_mel_with_state(
    struct whisper_context * ctx,
    struct whisper_state * state,
    const float * data,
    int n_len,
    int n_mel);
```

Set a pre-computed log mel spectrogram directly. `n_mel` must be `80`. Returns `0` on success.

---

## Encoder / Decoder (Low-Level)

```c
int whisper_encode(struct whisper_context * ctx, int offset, int n_threads);
int whisper_encode_with_state(struct whisper_context * ctx, struct whisper_state * state, int offset, int n_threads);
```

Run the encoder on the spectrogram at `offset`. Call `whisper_pcm_to_mel()` first.

```c
int whisper_decode(struct whisper_context * ctx, const whisper_token * tokens, int n_tokens, int n_past, int n_threads);
int whisper_decode_with_state(struct whisper_context * ctx, struct whisper_state * state, const whisper_token * tokens, int n_tokens, int n_past, int n_threads);
```

Run the decoder to obtain logits for the next token. `tokens` + `n_tokens` is the prompt context; `n_past` is the number of tokens already decoded.

---

## Full Transcription

```c
int whisper_full(
    struct whisper_context * ctx,
    struct whisper_full_params params,
    const float * samples,
    int n_samples);
```

**The primary entry point.** Runs the entire pipeline: PCM → log mel spectrogram → encoder → decoder → text. Returns `0` on success. Results are accessed via `whisper_full_n_segments()` and related getters.

```c
int whisper_full_with_state(
    struct whisper_context * ctx,
    struct whisper_state * state,
    struct whisper_full_params params,
    const float * samples,
    int n_samples);
```

Same as `whisper_full()` but operates on the given state instead of the context's default state.

```c
int whisper_full_parallel(
    struct whisper_context * ctx,
    struct whisper_full_params params,
    const float * samples,
    int n_samples,
    int n_processors);
```

Split audio into chunks and process each chunk separately with `whisper_full_with_state()`. May offer speedup at the cost of accuracy at chunk boundaries.

```c
struct whisper_full_params whisper_full_default_params(enum whisper_sampling_strategy strategy);
```

Returns a default-initialised `whisper_full_params` for the given sampling strategy.

---

## Segment & Token Accessors

Call these after `whisper_full()` succeeds.

### Segment Count & Metadata

```c
int whisper_full_n_segments(struct whisper_context * ctx);
int whisper_full_n_segments_from_state(struct whisper_state * state);
```

Number of generated text segments.

```c
int whisper_full_lang_id(struct whisper_context * ctx);
int whisper_full_lang_id_from_state(struct whisper_state * state);
```

Detected language ID for the transcription.

### Segment Timestamps

```c
int64_t whisper_full_get_segment_t0(struct whisper_context * ctx, int i_segment);
int64_t whisper_full_get_segment_t1(struct whisper_context * ctx, int i_segment);

int64_t whisper_full_get_segment_t0_from_state(struct whisper_state * state, int i_segment);
int64_t whisper_full_get_segment_t1_from_state(struct whisper_state * state, int i_segment);
```

Start and end time of segment `i` in **centiseconds** (10 ms units). Convert to microseconds via `* 10000`.

### Segment Text

```c
const char * whisper_full_get_segment_text(struct whisper_context * ctx, int i_segment);
const char * whisper_full_get_segment_text_from_state(struct whisper_state * state, int i_segment);
```

UTF-8 text of the segment.

```c
bool whisper_full_get_segment_speaker_turn_next(struct whisper_context * ctx, int i_segment);
bool whisper_full_get_segment_speaker_turn_next_from_state(struct whisper_state * state, int i_segment);
```

Whether the next segment is predicted as a speaker turn (tinydiarize).

### Token-Level Data

```c
int whisper_full_n_tokens(struct whisper_context * ctx, int i_segment);
int whisper_full_n_tokens_from_state(struct whisper_state * state, int i_segment);

const char * whisper_full_get_token_text(struct whisper_context * ctx, int i_segment, int i_token);
const char * whisper_full_get_token_text_from_state(struct whisper_context * ctx, struct whisper_state * state, int i_segment, int i_token);

whisper_token whisper_full_get_token_id(struct whisper_context * ctx, int i_segment, int i_token);
whisper_token whisper_full_get_token_id_from_state(struct whisper_state * state, int i_segment, int i_token);

whisper_token_data whisper_full_get_token_data(struct whisper_context * ctx, int i_segment, int i_token);
whisper_token_data whisper_full_get_token_data_from_state(struct whisper_state * state, int i_segment, int i_token);

float whisper_full_get_token_p(struct whisper_context * ctx, int i_segment, int i_token);
float whisper_full_get_token_p_from_state(struct whisper_state * state, int i_segment, int i_token);
```

Access individual tokens within a segment, including text, ID, probability, and full token data.

### Token Timestamps

```c
int64_t whisper_full_get_token_t0(struct whisper_context * ctx, int i_segment, int i_token);
int64_t whisper_full_get_token_t0_from_state(struct whisper_state * state, int i_segment, int i_token);
int64_t whisper_full_get_token_t1(struct whisper_context * ctx, int i_segment, int i_token);
int64_t whisper_full_get_token_t1_from_state(struct whisper_state * state, int i_segment, int i_token);
```

Token-level timestamps in centiseconds. When VAD is enabled, these are mapped to the original audio timeline (silence-removed gaps are adjusted); without VAD they match `whisper_full_get_token_data().t0/t1`.

### No-Speech Probability

```c
float whisper_full_get_segment_no_speech_prob(struct whisper_context * ctx, int i_segment);
float whisper_full_get_segment_no_speech_prob_from_state(struct whisper_state * state, int i_segment);
```

---

## Language Utilities

```c
int whisper_lang_max_id(void);
```

Largest language ID (number of supported languages − 1).

```c
int whisper_lang_id(const char * lang);
```

Look up language ID by code or name (e.g. `"de"` or `"german"` → `2`). Returns `-1` if not found.

```c
const char * whisper_lang_str(int id);
const char * whisper_lang_str_full(int id);
```

Short (e.g. `"de"`) or full (e.g. `"german"`) language name for ID. Returns `NULL` if not found.

```c
int whisper_lang_auto_detect(
    struct whisper_context * ctx,
    int offset_ms,
    int n_threads,
    float * lang_probs);
int whisper_lang_auto_detect_with_state(
    struct whisper_context * ctx,
    struct whisper_state * state,
    int offset_ms,
    int n_threads,
    float * lang_probs);
```

Auto-detect language from mel data at `offset_ms`. Returns top language ID, or negative on failure. If `lang_probs` is non-NULL, fills with probabilities for all languages (array must be `whisper_lang_max_id() + 1` in size).

---

## Tokenization

```c
int whisper_tokenize(
    struct whisper_context * ctx,
    const char * text,
    whisper_token * tokens,
    int n_max_tokens);
```

Convert text to tokens. Returns number of tokens on success (≤ `n_max_tokens`), or negative number indicating the required buffer size on failure.

```c
int whisper_token_count(struct whisper_context * ctx, const char * text);
```

Equivalent to `-whisper_tokenize(ctx, text, NULL, 0)` — returns the number of tokens the text would produce.

---

## Special Tokens

```c
whisper_token whisper_token_eot   (struct whisper_context * ctx);  // End of transcription
whisper_token whisper_token_sot   (struct whisper_context * ctx);  // Start of transcription
whisper_token whisper_token_solm  (struct whisper_context * ctx);  // Start of transcription (no language)
whisper_token whisper_token_prev  (struct whisper_context * ctx);  // Previous token
whisper_token whisper_token_nosp  (struct whisper_context * ctx);  // No speech
whisper_token whisper_token_not   (struct whisper_context * ctx);  // Not
whisper_token whisper_token_beg   (struct whisper_context * ctx);  // Begin timestamp
whisper_token whisper_token_lang  (struct whisper_context * ctx, int lang_id);  // Language tag
```

Return the special token ID for the given purpose.

```c
whisper_token whisper_token_translate (struct whisper_context * ctx);
whisper_token whisper_token_transcribe(struct whisper_context * ctx);
```

Task tokens: translate (to English) or transcribe (in original language).

---

## Logits

```c
float * whisper_get_logits(struct whisper_context * ctx);
float * whisper_get_logits_from_state(struct whisper_state * state);
```

Raw logits from the last `whisper_decode()` call. Shape: `[n_tokens, n_vocab]`. The last row contains logits for the next token.

---

## Model Information

```c
int whisper_n_len           (struct whisper_context * ctx);
int whisper_n_len_from_state(struct whisper_state * state);  // Mel length
int whisper_n_vocab         (struct whisper_context * ctx);  // Vocabulary size
int whisper_n_text_ctx      (struct whisper_context * ctx);  // Max text context tokens
int whisper_n_audio_ctx     (struct whisper_context * ctx);  // Audio context size
int whisper_is_multilingual (struct whisper_context * ctx);  // Whether model is multilingual
```

```c
int whisper_model_n_vocab      (struct whisper_context * ctx);
int whisper_model_n_audio_ctx  (struct whisper_context * ctx);
int whisper_model_n_audio_state(struct whisper_context * ctx);
int whisper_model_n_audio_head (struct whisper_context * ctx);
int whisper_model_n_audio_layer(struct whisper_context * ctx);
int whisper_model_n_text_ctx   (struct whisper_context * ctx);
int whisper_model_n_text_state (struct whisper_context * ctx);
int whisper_model_n_text_head  (struct whisper_context * ctx);
int whisper_model_n_text_layer (struct whisper_context * ctx);
int whisper_model_n_mels       (struct whisper_context * ctx);
int whisper_model_ftype        (struct whisper_context * ctx);
int whisper_model_type         (struct whisper_context * ctx);
```

Detailed model architecture dimensions.

```c
const char * whisper_token_to_str(struct whisper_context * ctx, whisper_token token);
const char * whisper_model_type_readable(struct whisper_context * ctx);
```

Token-to-string lookup and human-readable model type name.

---

## VAD (Voice Activity Detection)

### Standalone VAD Context

```c
struct whisper_vad_context;

struct whisper_vad_params whisper_vad_default_params(void);
struct whisper_vad_context_params whisper_vad_default_context_params(void);

struct whisper_vad_context * whisper_vad_init_from_file_with_params(
    const char * path_model,
    struct whisper_vad_context_params params);

struct whisper_vad_context * whisper_vad_init_with_params(
    struct whisper_model_loader * loader,
    struct whisper_vad_context_params params);
```

Load a separate VAD model for standalone speech detection.

### Detection

```c
bool whisper_vad_detect_speech(
    struct whisper_vad_context * vctx,
    const float * samples,
    int n_samples);

bool whisper_vad_detect_speech_no_reset(
    struct whisper_vad_context * vctx,
    const float * samples,
    int n_samples);
```

Returns `true` if speech is detected. The `_no_reset` variant does not reset LSTM state between calls for streaming use.

```c
void whisper_vad_reset_state(struct whisper_vad_context * vctx);
```

Reset LSTM hidden/cell states to zero.

### Probability Access

```c
int   whisper_vad_n_probs(struct whisper_vad_context * vctx);
float * whisper_vad_probs(struct whisper_vad_context * vctx);
```

Access internal speech probability array.

### VAD Segments

```c
struct whisper_vad_segments;

struct whisper_vad_segments * whisper_vad_segments_from_probs(
    struct whisper_vad_context * vctx,
    struct whisper_vad_params    params);

struct whisper_vad_segments * whisper_vad_segments_from_samples(
    struct whisper_vad_context * vctx,
    struct whisper_vad_params    params,
    const float * samples,
    int n_samples);
```

Derive speech/non-speech segment boundaries from probabilities or raw samples.

```c
int   whisper_vad_segments_n_segments(struct whisper_vad_segments * segments);
float whisper_vad_segments_get_segment_t0(struct whisper_vad_segments * segments, int i_segment);
float whisper_vad_segments_get_segment_t1(struct whisper_vad_segments * segments, int i_segment);
```

Access VAD segment boundaries (times in seconds).

```c
void whisper_vad_free_segments(struct whisper_vad_segments * segments);
void whisper_vad_free(struct whisper_vad_context * ctx);
```

### Built-in VAD (via `whisper_full_params`)

```c
int whisper_full_n_vad_segments(struct whisper_context * ctx);
int whisper_full_n_vad_segments_from_state(struct whisper_state * state);

int64_t whisper_full_get_vad_segment_t0(struct whisper_context * ctx, int i);
int64_t whisper_full_get_vad_segment_t0_from_state(struct whisper_state * state, int i);
int64_t whisper_full_get_vad_segment_t1(struct whisper_context * ctx, int i);
int64_t whisper_full_get_vad_segment_t1_from_state(struct whisper_state * state, int i);
```

Access VAD segment boundaries discovered during `whisper_full()` when `params.vad = true`. Times are in centiseconds on the original audio timeline.

---

## Callbacks

### `whisper_new_segment_callback`

```c
typedef void (*whisper_new_segment_callback)(
    struct whisper_context * ctx,
    struct whisper_state * state,
    int n_new,
    void * user_data);
```

Called for every newly generated text segment during `whisper_full()`. Use `whisper_full_n_segments()` and related getters inside the callback to access the new segments.

### `whisper_progress_callback`

```c
typedef void (*whisper_progress_callback)(
    struct whisper_context * ctx,
    struct whisper_state * state,
    int progress,
    void * user_data);
```

Called on progress updates. `progress` ranges from 0 to 100.

### `whisper_encoder_begin_callback`

```c
typedef bool (*whisper_encoder_begin_callback)(
    struct whisper_context * ctx,
    struct whisper_state * state,
    void * user_data);
```

Called before the encoder starts. Return `false` to abort the computation.

### `whisper_logits_filter_callback`

```c
typedef void (*whisper_logits_filter_callback)(
    struct whisper_context * ctx,
    struct whisper_state * state,
    const whisper_token_data * tokens,
    int n_tokens,
    float * logits,
    void * user_data);
```

Called after temperature is applied to logits. Can modify `logits` in-place to influence token selection.

---

## Performance & System Info

```c
struct whisper_timings {
    float sample_ms;
    float encode_ms;
    float decode_ms;
    float batchd_ms;
    float prompt_ms;
};
```

```c
struct whisper_timings * whisper_get_timings(struct whisper_context * ctx);
void whisper_print_timings(struct whisper_context * ctx);
void whisper_reset_timings(struct whisper_context * ctx);
```

Access accumulated timing information for the default state.

```c
const char * whisper_print_system_info(void);
```

Returns a string describing the system configuration (CPU features, build flags, etc.).

```c
int  whisper_bench_memcpy(int n_threads);
const char * whisper_bench_memcpy_str(int n_threads);
int  whisper_bench_ggml_mul_mat(int n_threads);
const char * whisper_bench_ggml_mul_mat_str(int n_threads);
```

Run micro-benchmarks for memcpy and matrix multiplication performance.

---

## Logging

```c
void whisper_log_set(ggml_log_callback log_callback, void * user_data);
```

Override the default log handler (prints to stderr). The `ggml_log_callback` signature is:

```c
typedef void (*ggml_log_callback)(
    enum ggml_log_level level,
    const char * text,
    void * user_data);
```

---

## OpenVINO

```c
int whisper_ctx_init_openvino_encoder_with_state(
    struct whisper_context * ctx,
    struct whisper_state * state,
    const char * model_path,
    const char * device,
    const char * cache_dir);

int whisper_ctx_init_openvino_encoder(
    struct whisper_context * ctx,
    const char * model_path,
    const char * device,
    const char * cache_dir);
```

Enable OpenVINO for encoder inference acceleration. Returns `0` on success, `1` if OpenVINO is not enabled in the build.

---

## Version

```c
const char * whisper_version(void);
```

Returns the version string of the compiled whisper.cpp library.

---

---

## VLC-Whisper Engine Wrapper (`vw_whisper_engine.h`)

The worker wraps `whisper.cpp` behind a dedicated C17 engine abstraction:

### Types

```c
typedef enum vw_worker_backend {
  VW_WORKER_BACKEND_AUTO = 0, // Enable GPU; whisper.cpp probes GPU/IGPU and falls back to CPU if none found
  VW_WORKER_BACKEND_GPU  = 1, // Force GPU-first path (same as AUTO)
  VW_WORKER_BACKEND_CPU  = 2, // Force CPU-only path (never consults GPU devices)
} vw_worker_backend_t;

typedef struct vw_whisper_engine {
  struct whisper_context* ctx;  // Opaque whisper.cpp context
  char* last_text;              // Concatenated UTF-8 output of last transcribe run
  size_t last_text_bytes;       // Capacity of last_text buffer
} vw_whisper_engine_t;
```

### Functions

```c
// Initializes whisper.cpp engine instance from the specified model file path (ADR-015: model-once lifetime).
// backend selects inference: AUTO/GPU set use_gpu=true (whisper picks the first GPU/IGPU device and falls
// back to CPU at runtime when none exists), CPU forces use_gpu=false. gpu_device is the GPU/IGPU ordinal.
// Runs a silent warmup inference pass on load. Returns NULL if model file is missing or invalid.
vw_whisper_engine_t* vw_whisper_engine_init(const char* model_path, vw_worker_backend_t backend, int gpu_device);

// Safely destroys whisper.cpp engine instance and frees associated model memory.
void vw_whisper_engine_free(vw_whisper_engine_t* engine);

// Runs whisper.cpp transcription on normalized float32 PCM samples at 16kHz.
bool vw_whisper_engine_transcribe_pcm(vw_whisper_engine_t* engine, const float* pcm32, size_t sample_count);

// Returns pointer to concatenated UTF-8 text from the last transcribe run, or "" if empty/NULL.
const char* vw_whisper_engine_get_text(const vw_whisper_engine_t* engine);
```

---

## Usage Pattern (VLC-Whisper Worker)

The worker's typical flow with Vulkan GPU acceleration (and transparent CPU fallback):

```c
// 1. Load model with requested backend & device ordinal
struct whisper_context_params cparams = whisper_context_default_params();
cparams.use_gpu = (backend != VW_WORKER_BACKEND_CPU);
cparams.gpu_device = (gpu_device >= 0) ? gpu_device : 0;
struct whisper_context * ctx = whisper_init_from_file_with_params(model_path, cparams);

// 2. Configure inference
struct whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
wparams.print_realtime   = false;
wparams.print_progress   = false;
wparams.print_timestamps = false;
wparams.language         = "en";
wparams.n_threads        = 4;

// 3. Run transcription
whisper_full(ctx, wparams, pcm_samples, (int)n_samples);

// 4. Read results (convert centiseconds to pts_us)
int n_seg = whisper_full_n_segments(ctx);
for (int i = 0; i < n_seg; ++i) {
    const char * text = whisper_full_get_segment_text(ctx, i);
    int64_t start_pts_us = whisper_full_get_segment_t0(ctx, i) * 10000;
    int64_t end_pts_us   = whisper_full_get_segment_t1(ctx, i) * 10000;
    // ... send caption segment via IPC
}

// 5. Cleanup
whisper_free(ctx);
```

See `samples/snippets/vw_sample_whisper_pcm.c` for a complete working example.
