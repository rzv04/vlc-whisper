# Pinned whisper.cpp Integration

The authoritative vendor API is the exact pinned header at `worker/third_party/whisper.cpp/include/whisper.h`. This file records only the VLC-Whisper subset and project-specific assumptions; do not maintain a duplicate exhaustive vendor reference here.

## Ownership

Only the worker links `whisper.cpp`; the VLC plugin must not. Project code uses the vendor's C API from C17-owned wrappers.

## Core pipeline

1. `whisper_context_default_params()`
2. configure backend/device
3. `whisper_init_from_file_with_params()`
4. `whisper_full_default_params(WHISPER_SAMPLING_GREEDY)`
5. configure language/threads/decoding policy
6. `whisper_full()` on 16 kHz mono float32 PCM
7. read finalized segments with `whisper_full_n_segments()`, `whisper_full_get_segment_text()`, `whisper_full_get_segment_t0/t1()`
8. `whisper_free()`

Segment `t0/t1` values are centiseconds; VLC-Whisper converts them to microseconds with `* 10000LL` before adding the owning audio-window/source origin.

## Backend contract

`vw_whisper_engine` owns effective backend selection. CPU explicitly disables GPU. Auto/GPU-first modes enable the pinned backend and may fall back according to the exact vendor behavior/build. Do not infer the actual backend from requested settings or device enumeration; report the inference owner's resolved result through `STATUS.resolved_backend`.

Any review/change depending on GPU fallback, return values, VAD behavior, timestamp units, or parameter defaults must check the pinned header/source, not current upstream documentation.

## Parameters used by VLC-Whisper

The worker currently favors deterministic bounded decoding. Relevant fields include:

- `n_threads`
- concrete `language` (worker rejects `auto` for normal caption sessions)
- greedy strategy
- `temperature`, `temperature_inc`, `entropy_thold`
- `no_context`
- `suppress_blank`, `suppress_nst`
- `no_speech_thold`
- timestamp/print controls

Exact values are production policy and belong in `vw_whisper_engine.*` plus regression tests; avoid copying every vendor default into documentation.

## Segment and no-speech accessors

The project relies on segment count/text/time accessors and `whisper_full_get_segment_no_speech_prob()` for post-inference gating. Output text is untrusted until project bounds/UTF-8/render rules are applied.

## Language validation

Use `whisper_lang_id()` against the pinned model/library. Unsupported or disallowed language configuration fails explicitly before a caption session proceeds.

## VAD subset

VLC-Whisper can use the pinned standalone VAD API, primarily:

- `whisper_vad_init_from_file_with_params()`
- `whisper_vad_detect_speech()` / streaming no-reset variant where appropriate
- probability/segment accessors
- `whisper_vad_reset_state()`
- VAD context/segment cleanup

Recurrent VAD state is reset on lifecycle boundaries that invalidate acoustic continuity. If the configured/pinned VAD model is unavailable where fallback is allowed, the fallback policy is owned by project VAD code—not by guessed vendor semantics.

## Wrapper contract

`vw_whisper_engine.*` owns model lifetime, backend configuration, PCM transcription, segment extraction, actual-backend reporting, and inference timing. Callers should consume wrapper-level typed success/error semantics rather than reaching through it and recreating vendor assumptions.

## Performance/timing

`whisper_full()` is not realtime-callback safe and runs only in the worker. Inference timing used by VLC-Whisper metrics is measured by the project owner around the actual inference call; vendor diagnostic timing APIs are not a substitute for protocol metric ownership.

For a complete API symbol list, inspect the pinned `whisper.h`. For VLC-Whisper policy, inspect `worker/src/vw_whisper_engine.c`, `worker/src/vw_vad.c`, `architecture.md`, and the relevant tests.
