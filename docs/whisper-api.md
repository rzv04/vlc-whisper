# whisper.cpp C API Reference

> **Vendor dependency:** [`whisper.cpp`](https://github.com/ggerganov/whisper.cpp) — pinned in `worker/third_party/whisper.cpp/`.  
> **Header:** `worker/third_party/whisper.cpp/include/whisper.h`  
> **Linked by:** `vlc-whisper-worker` only (the plugin *must not* link Whisper).

This document describes the public C API surface of the pinned `whisper.cpp` library used by VLC-Whisper. The worker loads a GGML model, feeds 16 kHz float PCM, retrieves timed text, and in the experimental live LocalAgreement path reads authentic per-token timestamps.

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

Opaque loaded-model handle. Created by `whisper_init_*` and freed by `whisper_free()`. The normal worker uses its default state for one inference stream.

### `struct whisper_state`

Optional extra processing state created with `whisper_init_state()`. Most public `whisper_full_*_from_state()` accessors mirror their default-state counterparts.

### `struct whisper_context_params`

Controls model loading.

| Field | Type | Default | Description |
|---|---|---|---|
| `use_gpu` | `bool` | implementation-defined | Enable GPU/IGPU offload; whisper.cpp may fall back to CPU |
| `flash_attn` | `bool` | implementation-defined | Enable flash attention |
| `gpu_device` | `int` | `0` | GPU ordinal |
| `dtw_token_timestamps` | `bool` | `false` | Experimental DTW token alignment |
| `dtw_aheads_preset` | `enum` | — | DTW alignment-head preset |
| `dtw_n_top` | `int` | — | Number of top DTW heads |
| `dtw_aheads` | `struct` | — | Custom DTW alignment heads |

VLC-Whisper obtains defaults with `whisper_context_default_params()`. The worker independently reports the effective runtime backend after initialization rather than assuming the requested backend was usable.

### `typedef int32_t whisper_token`

A model vocabulary token identifier.

### `typedef int32_t whisper_pos`

A decoder/encoder position index.

### `struct whisper_token_data`

Decoded-token metadata.

| Field | Type | Description |
|---|---|---|
| `id` | `whisper_token` | Token ID |
| `tid` | `whisper_token` | Timestamp token ID |
| `p` | `float` | Token probability |
| `plog` | `float` | Log probability |
| `pt` | `float` | Timestamp-token probability |
| `ptsum` | `float` | Sum of timestamp-token probabilities |
| `t0` | `int64_t` | Token start in centiseconds |
| `t1` | `int64_t` | Token end in centiseconds |
| `t_dtw` | `int64_t` | Experimental DTW-aligned token time |
| `vlen` | `float` | Voice length |

The project converts centiseconds to microseconds with `pts_us = t * 10000LL` and then adds the rolling window's absolute start PTS.

### `enum whisper_sampling_strategy`

| Enumerator | Description |
|---|---|
| `WHISPER_SAMPLING_GREEDY` | Deterministic greedy decoding |
| `WHISPER_SAMPLING_BEAM_SEARCH` | Beam-search decoding |

### `struct whisper_full_params`

Parameters passed to `whisper_full()`.

| Field | Type | Typical/default meaning |
|---|---|---|
| `strategy` | `enum` | Sampling strategy |
| `n_threads` | `int` | Compute thread count |
| `n_max_text_ctx` | `int` | Maximum past-text context |
| `offset_ms` | `int` | Processing offset |
| `duration_ms` | `int` | Processing duration; zero means full input |
| `translate` | `bool` | Whisper translation mode |
| `no_context` | `bool` | Disable past transcription conditioning |
| `no_timestamps` | `bool` | Suppress timestamp tokens |
| `single_segment` | `bool` | Force a single output segment |
| `print_special` | `bool` | Print special tokens |
| `print_progress` | `bool` | Print progress |
| `print_realtime` | `bool` | Print generated text in realtime |
| `print_timestamps` | `bool` | Print segment timestamps |
| `token_timestamps` | `bool` | Enable experimental token-level timestamps |
| `thold_pt` | `float` | Timestamp-token probability threshold |
| `thold_ptsum` | `float` | Timestamp sum threshold |
| `max_len` | `int` | Maximum segment length |
| `split_on_word` | `bool` | Prefer word-boundary splitting |
| `max_tokens` | `int` | Maximum tokens per segment |
| `audio_ctx` | `int` | Override audio context size |
| `initial_prompt` | `const char*` | Initial textual prompt |
| `carry_initial_prompt` | `bool` | Reapply initial prompt to decode windows |
| `prompt_tokens` | `const whisper_token*` | Explicit prompt-token buffer |
| `prompt_n_tokens` | `int` | Explicit prompt-token count |
| `language` | `const char*` | Concrete language or auto-detection selector |
| `detect_language` | `bool` | Enable language detection |
| `suppress_blank` | `bool` | Suppress blank tokens |
| `suppress_nst` | `bool` | Suppress non-speech tokens |
| `temperature` | `float` | Initial decode temperature |
| `temperature_inc` | `float` | Fallback temperature increment |
| `entropy_thold` | `float` | Entropy fallback threshold |
| `logprob_thold` | `float` | Log-probability threshold |
| `no_speech_thold` | `float` | No-speech threshold |
| `vad` | `bool` | Enable whisper.cpp built-in VAD |
| `vad_model_path` | `const char*` | Built-in VAD model path |
| `vad_params` | `struct` | Built-in VAD parameters |

The production worker uses deterministic greedy decoding with `temperature = 0`, bounded fallback, `no_context = true`, `single_segment = false`, and `suppress_nst = true`. The LocalAgreement experiment retains those choices and changes only the live path's token-timestamp exposure.

VLC-Whisper requires a concrete supported language at worker startup. `vw_worker_config` rejects `auto`, and `vw_whisper_engine_set_language()` validates the code with `whisper_lang_id()`.

---

## Lifecycle: Initialization & Teardown

```c
struct whisper_context_params whisper_context_default_params(void);
struct whisper_full_params whisper_full_default_params(enum whisper_sampling_strategy strategy);
```

Return default parameter blocks.

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

Load a GGML model from a path, memory buffer, or custom loader.

```c
struct whisper_state * whisper_init_state(struct whisper_context * ctx);
void whisper_free(struct whisper_context * ctx);
void whisper_free_state(struct whisper_state * state);
void whisper_free_params(struct whisper_full_params * params);
void whisper_free_context_params(struct whisper_context_params * params);
```

Create extra state and release associated resources.

---

## PCM, Mel, Encoder, and Decoder

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

Convert 16 kHz float PCM to the internal log-mel representation.

```c
int whisper_set_mel(struct whisper_context * ctx, const float * data, int n_len, int n_mel);
int whisper_set_mel_with_state(
    struct whisper_context * ctx,
    struct whisper_state * state,
    const float * data,
    int n_len,
    int n_mel);
```

Supply a precomputed mel spectrogram.

```c
int whisper_encode(struct whisper_context * ctx, int offset, int n_threads);
int whisper_encode_with_state(struct whisper_context * ctx, struct whisper_state * state, int offset, int n_threads);

int whisper_decode(
    struct whisper_context * ctx,
    const whisper_token * tokens,
    int n_tokens,
    int n_past,
    int n_threads);

int whisper_decode_with_state(
    struct whisper_context * ctx,
    struct whisper_state * state,
    const whisper_token * tokens,
    int n_tokens,
    int n_past,
    int n_threads);
```

Low-level encoder/decoder entry points. VLC-Whisper's normal transcription path uses `whisper_full()` instead.

---

## Full Transcription

```c
int whisper_full(
    struct whisper_context * ctx,
    struct whisper_full_params params,
    const float * samples,
    int n_samples);

int whisper_full_with_state(
    struct whisper_context * ctx,
    struct whisper_state * state,
    struct whisper_full_params params,
    const float * samples,
    int n_samples);

int whisper_full_parallel(
    struct whisper_context * ctx,
    struct whisper_full_params params,
    const float * samples,
    int n_samples,
    int n_processors);
```

`whisper_full()` is the primary worker entry point: PCM → mel → encoder → decoder → timed text. Results remain owned by whisper.cpp until the next transcription/state mutation.

---

## Segment Accessors

```c
int whisper_full_n_segments(struct whisper_context * ctx);
int whisper_full_n_segments_from_state(struct whisper_state * state);

int whisper_full_lang_id(struct whisper_context * ctx);
int whisper_full_lang_id_from_state(struct whisper_state * state);
```

Read segment count and resolved language.

```c
int64_t whisper_full_get_segment_t0(struct whisper_context * ctx, int i_segment);
int64_t whisper_full_get_segment_t1(struct whisper_context * ctx, int i_segment);
int64_t whisper_full_get_segment_t0_from_state(struct whisper_state * state, int i_segment);
int64_t whisper_full_get_segment_t1_from_state(struct whisper_state * state, int i_segment);
```

Segment start/end values are centiseconds relative to the supplied inference window.

```c
const char * whisper_full_get_segment_text(struct whisper_context * ctx, int i_segment);
const char * whisper_full_get_segment_text_from_state(struct whisper_state * state, int i_segment);

bool whisper_full_get_segment_speaker_turn_next(struct whisper_context * ctx, int i_segment);
bool whisper_full_get_segment_speaker_turn_next_from_state(struct whisper_state * state, int i_segment);

float whisper_full_get_segment_no_speech_prob(struct whisper_context * ctx, int i_segment);
float whisper_full_get_segment_no_speech_prob_from_state(struct whisper_state * state, int i_segment);
```

Read borrowed segment text, diarization metadata, and no-speech probability.

---

## Token Accessors

These calls are central to the LocalAgreement experiment.

```c
int whisper_full_n_tokens(struct whisper_context * ctx, int i_segment);
int whisper_full_n_tokens_from_state(struct whisper_state * state, int i_segment);

const char * whisper_full_get_token_text(struct whisper_context * ctx, int i_segment, int i_token);
const char * whisper_full_get_token_text_from_state(
    struct whisper_context * ctx,
    struct whisper_state * state,
    int i_segment,
    int i_token);

whisper_token whisper_full_get_token_id(struct whisper_context * ctx, int i_segment, int i_token);
whisper_token whisper_full_get_token_id_from_state(struct whisper_state * state, int i_segment, int i_token);

whisper_token_data whisper_full_get_token_data(struct whisper_context * ctx, int i_segment, int i_token);
whisper_token_data whisper_full_get_token_data_from_state(struct whisper_state * state, int i_segment, int i_token);

float whisper_full_get_token_p(struct whisper_context * ctx, int i_segment, int i_token);
float whisper_full_get_token_p_from_state(struct whisper_state * state, int i_segment, int i_token);
```

`whisper_full_get_token_text()` returns tokenizer pieces, not normalized words. Pieces may contain leading whitespace or represent subwords; languages such as Chinese/Japanese do not require whitespace boundaries.

### Authentic token timestamps

```c
int64_t whisper_full_get_token_t0(struct whisper_context * ctx, int i_segment, int i_token);
int64_t whisper_full_get_token_t0_from_state(struct whisper_state * state, int i_segment, int i_token);
int64_t whisper_full_get_token_t1(struct whisper_context * ctx, int i_segment, int i_token);
int64_t whisper_full_get_token_t1_from_state(struct whisper_state * state, int i_segment, int i_token);
```

Times are centiseconds. Without whisper.cpp built-in VAD they correspond directly to the current inference window. With built-in VAD they are mapped to the original audio timeline. VLC-Whisper's separate VAD is outside `whisper_full`, so the experimental live path adds the rolling-window start PTS after converting each token timestamp to microseconds.

---

## Language, Tokenization, and Special Tokens

```c
int whisper_lang_max_id(void);
int whisper_lang_id(const char * lang);
const char * whisper_lang_str(int id);
const char * whisper_lang_str_full(int id);
```

Language metadata helpers.

```c
int whisper_lang_auto_detect(
    struct whisper_context * ctx,
    int offset_ms,
    int n_threads,
    float * lang_probs);
```

Automatic detection exists upstream but is not used as the worker's configured language mode.

```c
int whisper_tokenize(
    struct whisper_context * ctx,
    const char * text,
    whisper_token * tokens,
    int n_max_tokens);

int whisper_token_count(struct whisper_context * ctx, const char * text);
```

Tokenize arbitrary text. The LocalAgreement experiment does **not** retokenize rendered segment text; it consumes the decoder's actual tokens directly.

```c
whisper_token whisper_token_eot(struct whisper_context * ctx);
whisper_token whisper_token_sot(struct whisper_context * ctx);
whisper_token whisper_token_solm(struct whisper_context * ctx);
whisper_token whisper_token_prev(struct whisper_context * ctx);
whisper_token whisper_token_nosp(struct whisper_context * ctx);
whisper_token whisper_token_not(struct whisper_context * ctx);
whisper_token whisper_token_beg(struct whisper_context * ctx);
whisper_token whisper_token_lang(struct whisper_context * ctx, int lang_id);
whisper_token whisper_token_translate(struct whisper_context * ctx);
whisper_token whisper_token_transcribe(struct whisper_context * ctx);
```

Special-token helpers. LocalAgreement ignores token IDs at or above the EOT boundary and stores only ordinary text-token pieces.

---

## Logits and Model Information

```c
float * whisper_get_logits(struct whisper_context * ctx);
float * whisper_get_logits_from_state(struct whisper_state * state);
```

Raw decoder logits from the most recent decode step.

```c
int whisper_n_len(struct whisper_context * ctx);
int whisper_n_vocab(struct whisper_context * ctx);
int whisper_n_text_ctx(struct whisper_context * ctx);
int whisper_n_audio_ctx(struct whisper_context * ctx);
int whisper_is_multilingual(struct whisper_context * ctx);

int whisper_model_n_vocab(struct whisper_context * ctx);
int whisper_model_n_audio_ctx(struct whisper_context * ctx);
int whisper_model_n_audio_state(struct whisper_context * ctx);
int whisper_model_n_audio_head(struct whisper_context * ctx);
int whisper_model_n_audio_layer(struct whisper_context * ctx);
int whisper_model_n_text_ctx(struct whisper_context * ctx);
int whisper_model_n_text_state(struct whisper_context * ctx);
int whisper_model_n_text_head(struct whisper_context * ctx);
int whisper_model_n_text_layer(struct whisper_context * ctx);
int whisper_model_n_mels(struct whisper_context * ctx);
int whisper_model_ftype(struct whisper_context * ctx);
int whisper_model_type(struct whisper_context * ctx);

const char * whisper_token_to_str(struct whisper_context * ctx, whisper_token token);
const char * whisper_model_type_readable(struct whisper_context * ctx);
```

Model dimensions and vocabulary helpers.

---

## VAD

VLC-Whisper uses its own `vw_vad` wrapper around the pinned Silero VAD model, but the pinned whisper.cpp also exposes standalone and built-in VAD APIs.

```c
struct whisper_vad_params whisper_vad_default_params(void);
struct whisper_vad_context_params whisper_vad_default_context_params(void);

struct whisper_vad_context * whisper_vad_init_from_file_with_params(
    const char * path_model,
    struct whisper_vad_context_params params);

bool whisper_vad_detect_speech(
    struct whisper_vad_context * vctx,
    const float * samples,
    int n_samples);

bool whisper_vad_detect_speech_no_reset(
    struct whisper_vad_context * vctx,
    const float * samples,
    int n_samples);

void whisper_vad_reset_state(struct whisper_vad_context * vctx);
void whisper_vad_free(struct whisper_vad_context * ctx);
```

Additional helpers expose probability arrays and detected VAD segments. `whisper_full_params.vad` enables built-in VAD during transcription; this is separate from the project's current worker-side VAD policy.

---

## Callbacks

The public parameter block supports new-segment, progress, encoder-begin, abort, and logits-filter callbacks. VLC-Whisper currently uses normal synchronous `whisper_full()` completion rather than streaming text directly from a callback.

```c
typedef void (*whisper_new_segment_callback)(
    struct whisper_context * ctx,
    struct whisper_state * state,
    int n_new,
    void * user_data);

typedef bool (*whisper_encoder_begin_callback)(
    struct whisper_context * ctx,
    struct whisper_state * state,
    void * user_data);

typedef void (*whisper_logits_filter_callback)(
    struct whisper_context * ctx,
    struct whisper_state * state,
    const whisper_token_data * tokens,
    int n_tokens,
    float * logits,
    void * user_data);
```

---

## Performance, Logging, and Version

```c
struct whisper_timings * whisper_get_timings(struct whisper_context * ctx);
void whisper_print_timings(struct whisper_context * ctx);
void whisper_reset_timings(struct whisper_context * ctx);
const char * whisper_print_system_info(void);

void whisper_log_set(ggml_log_callback log_callback, void * user_data);
const char * whisper_version(void);
```

The project reports its own cumulative `whisper_full()` wall time in `vw_whisper_engine_t`; that is the metric used by the benchmark report.

The pinned library also exposes memcpy/matrix-multiply microbenchmarks and optional OpenVINO encoder initialization APIs. Those are not part of the current worker execution path.

---

## VLC-Whisper Engine Wrapper (`vw_whisper_engine.h`)

The worker wraps whisper.cpp behind a C17 engine abstraction.

```c
typedef enum vw_worker_backend {
  VW_WORKER_BACKEND_AUTO = 0,
  VW_WORKER_BACKEND_GPU,
  VW_WORKER_BACKEND_CPU,
} vw_worker_backend_t;

typedef struct vw_whisper_engine {
  struct whisper_context* ctx;
  char* last_text;
  size_t last_text_bytes;
  char language[16];
  int n_threads;
  uint64_t last_inference_us;
  uint64_t total_inference_us;
  bool gpu_active;
} vw_whisper_engine_t;
```

Important wrapper functions:

```c
vw_whisper_engine_t* vw_whisper_engine_init(
    const char* model_path,
    vw_worker_backend_t backend,
    int gpu_device);

bool vw_whisper_engine_set_language(vw_whisper_engine_t* engine, const char* language);
bool vw_whisper_engine_set_n_threads(vw_whisper_engine_t* engine, int n_threads);
bool vw_whisper_engine_is_gpu_active(const vw_whisper_engine_t* engine);
void vw_whisper_engine_free(vw_whisper_engine_t* engine);

bool vw_whisper_engine_transcribe_pcm(
    vw_whisper_engine_t* engine,
    const float* pcm32,
    size_t sample_count);

const char* vw_whisper_engine_get_text(const vw_whisper_engine_t* engine);
int vw_whisper_engine_get_segment_count(const vw_whisper_engine_t* engine);
uint64_t vw_whisper_engine_get_total_inference_us(const vw_whisper_engine_t* engine);
bool vw_whisper_engine_get_segment(
    const vw_whisper_engine_t* engine,
    int index,
    vw_whisper_segment_t* out_seg);
```

Production `vw_whisper_engine_transcribe_pcm()` keeps token timestamps disabled because normal source/lookahead and local-file PCM paths require only segment-level timing.

---

## Experimental LocalAgreement-2 Live Mapping

PR #46 adds a branch-local worker interposition for `VW_SOURCE_LIVE_AUDIO`. It deliberately isolates the commitment policy while preserving current `main` scheduling: first live inference at about 2 seconds, then 1-second updates while the acoustic context grows to the existing 8-second rolling maximum.

### Decode parameters

`vw_local_agreement_transcribe_pcm()` mirrors the production greedy decode settings and retains:

- `no_context = true`
- `temperature = 0.0f`
- bounded temperature fallback
- `single_segment = false`
- `suppress_blank = true`
- `suppress_nst = true`
- the configured concrete language and thread count

For live sessions only it additionally sets:

```c
wparams.token_timestamps = true;
```

Local/lookahead sessions still delegate to `vw_whisper_engine_transcribe_pcm()` unchanged.

### Agreement units

The experiment reads decoder output with:

```c
whisper_full_n_segments(ctx);
whisper_full_n_tokens(ctx, segment);
whisper_full_get_token_id(ctx, segment, token);
whisper_full_get_token_text(ctx, segment, token);
whisper_full_get_token_t0(ctx, segment, token);
whisper_full_get_token_t1(ctx, segment, token);
whisper_token_eot(ctx);
```

Agreement units are the **actual raw Whisper text tokens**, including leading-space and subword bytes. The implementation does not split rendered text with `isspace()` and therefore does not collapse a Chinese or Japanese segment into a single pseudo-word. Each collected token also retains its originating Whisper segment index strictly as presentation metadata; segment identity does not participate in LocalAgreement equality, so harmless segment-boundary jitter across passes cannot block text confirmation.

Token timestamps are authentic whisper.cpp token boundaries. Each centisecond timestamp becomes an absolute worker PTS as:

```text
absolute_pts_us = rolling_window_start_pts_us + token_time_cs * 10000
```

This is the timestamp used for committed caption boundaries and experimental live latency metrics; segment-duration interpolation is not used.

### Commitment and overlap rules

The first non-empty live hypothesis remains hidden. Each later hypothesis commits only the exact raw-token prefix shared with the immediately preceding hypothesis. Empty or invalid passes clear the pending hypothesis so agreement cannot bridge silence or failed decoding.

After a token run is committed, a later rolling window may reproduce that same acoustic occurrence. The committed-tail filter strips a candidate only when:

1. raw token text matches the stored committed token; and
2. the authentic absolute token timestamp intervals substantially overlap (at least half of the shorter non-zero interval; zero-duration entries require exact interval equality).

Proximity alone is insufficient. Therefore a legitimate adjacent repetition such as `no, no` is preserved when the second `no` occupies a new non-overlapping acoustic interval.

Confirmed output is transactional with immutable delivery. The hook first previews the stable raw-token prefix against a temporary agreement-state copy. If that prefix spans multiple Whisper segments, it is then published one segment run at a time using the existing `output_capacity` bound: a successful `vw_segment_builder_push_hypothesis()` advances the real agreement state by exactly that cue, while a rejected later run leaves that suffix uncommitted and eligible for confirmation on a later pass. This preserves phrase-by-phrase timing and conversational silence gaps without adding rollback state or making segment identity part of the agreement rule.

START/STOP live-mode changes are staged when dequeued but applied only when the worker reaches its already-validated segment-builder clear path. A stale or duplicate control cannot disable LocalAgreement before the worker's session-ID validation.

Only committed text enters the existing immutable segment builder and SPU path; raw hypotheses are never displayed. This branch does not enable `no_context=false`, prompt tokens, fuzzy agreement, AlignAtt, or DTW alignment.

---

## Usage Pattern (Production Worker)

```c
struct whisper_context_params cparams = whisper_context_default_params();
cparams.use_gpu = (backend != VW_WORKER_BACKEND_CPU);
cparams.gpu_device = (gpu_device >= 0) ? gpu_device : 0;
struct whisper_context * ctx = whisper_init_from_file_with_params(model_path, cparams);

struct whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
wparams.print_realtime = false;
wparams.print_progress = false;
wparams.print_timestamps = false;
wparams.no_context = true;
wparams.language = "en";
wparams.n_threads = 4;

whisper_full(ctx, wparams, pcm_samples, (int)n_samples);

int n_seg = whisper_full_n_segments(ctx);
for (int i = 0; i < n_seg; ++i) {
    const char * text = whisper_full_get_segment_text(ctx, i);
    int64_t start_pts_us = whisper_full_get_segment_t0(ctx, i) * 10000;
    int64_t end_pts_us = whisper_full_get_segment_t1(ctx, i) * 10000;
    (void)text;
    (void)start_pts_us;
    (void)end_pts_us;
}

whisper_free(ctx);
```

See `samples/snippets/vw_sample_whisper_pcm.c` for a standalone example and `worker/src/vw_local_agreement_hooks.c` for the experimental live token-timestamp path.