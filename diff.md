# Diff Analysis: Step 17e.1 — Silero VAD, Silence Gating, No-Hop Chunking & Hallucination Suppression (vs 17d.1)

**32 files changed, +1608 / -91**
**Base**: `gemini/milestone-3` = `8ab8abc` (Step 17d.1 merged). Branch `gemini/milestone-3-step-17e-1`.
**Commits**: `2921aae`/`88f9f05`/`2986460`/`46fef5e` (plan + review refinements), `62567bd` (feat: Silero VAD + silence gating + hallucination filter), `210c94e` (fix: filter UTF-8 + VAD streaming), `43da8e0` (feat: VAD-guided **non-overlapping** lookahead chunking / no-hop), `f8a22d1`/`4f5a96a` (diff.md).
**Manual test (user)**: dialogue more stable, cues filtered out, seeking good.
**Line references**: branch HEAD (`43da8e0`). Scout + vendored whisper.cpp verified.

---

## 1. File-by-File Analysis

### 1.1 `worker/src/vw_vad.c` / `worker/include/vw_vad.h`

**Why change**: Tier 1 — Silero GGML VAD (`whisper_vad_*`) with RMS-energy fallback, plus `vw_vad_find_chunk_boundary` for the no-hop lookahead chunker (`step17e_1_plan.md` §1; `43da8e0`).
**Responsibility before**: `vw_vad_detect_speech_energy` only. **After**: `vw_vad_init_default`, `vw_vad_detect_speech` (Silero, energy fallback), `vw_vad_find_chunk_boundary` (silence-gap chunk cutting), `vw_vad_reset_state`, `vw_vad_free`.
**Callers**: `vw_worker.c` (live L546, lookahead L654, trailing L687; chunk loop L660-748; reset sites). **Callees**: `whisper_vad_init_from_file_with_params`, `whisper_vad_detect_speech_no_reset`, `whisper_vad_segments_from_probs`, `whisper_vad_reset_state`/`free`.
**Happy path**: buffer ≥ `VW_CHUNK_MIN_SAMPLES` (6 s) → Silero segments (centiseconds) → cut at first silence gap ≥300 ms between 6 s/24 s, or drain leading/all silence, or forced split at 24 s for continuous speech; worker transcribes exactly `cut_samples`, drains non-overlapping.
**Failure path**: VAD model absent/invalid → `vctx == NULL` → energy fallback; `segments == NULL` → energy fallback; no speech and buffer < MAX and not EOF → wait (holds up to 24 s silence — Finding M1).
**Boundaries**: chunk bounds `VW_CHUNK_MIN_SAMPLES 96000` (6 s), `VW_CHUNK_MAX_SAMPLES 384000` (24 s), `VW_CHUNK_PAD_SAMPLES 2400` (150 ms), `VW_CHUNK_MIN_SILENCE_GAP 4800` (300 ms); NULL/0 guards; EOF tail path drains remainder.
**Acceptance map**: plan §1 VAD + fallback + reset → **Done** (M1 caveat).

### 1.2 `worker/src/vw_hallucination_filter.c` / `worker/include/vw_hallucination_filter.h`

**Why change**: Tier 3 — reject non-speech tags + isolated punctuation, preserve dialogue (plan §3).
**Responsibility**: new — `vw_hallucination_is_isolated_punctuation` (ASCII-aware, UTF-8-safe after `210c94e`), `_is_non_speech_tag` (delimiter-enclosed list + ♪/♫, matched as standalone cues after `210c94e`), `_is_phantom_text`.
**Callers**: `vw_segment_builder_push_hypothesis` (L250, before the coverage gate L262). **Callees**: libc only.
**Boundaries**: `c >= 0x80 → not punctuation` (H1 fixed — non-Latin text preserved); standalone-token matching (H2 fixed — embedded tags no longer drop whole sentences); `NULL` asymmetry (`is_non_speech_tag(NULL)` false vs `is_phantom_text(NULL)` true) is benign.
**Acceptance map**: plan §3 → **Done** (post-`210c94e`).

### 1.3 `worker/src/vw_worker.c`

**Why change**: wire the 3-tier pipeline + the no-hop chunking loop. Tier-2 `no_speech_prob >= 0.60` drop before the builder (L555/663/696); VAD reset on seek/pause/swap/STOP (8 sites); lookahead no-hop loop (L641-748) with `vw_vad_find_chunk_boundary`; live path keeps the 8 s/2 s hop (L546-577).
**Responsibility after**: VAD lifecycle owner + Tier-2 gate + no-hop chunker + live overlapping-window path.
**Boundaries**: live and lookahead are independent paths feeding the same builder; the coverage model handles both (live overlap dropped, no-hop chunks time-adjacent — scout-proven no wrong drops); `vad_ctx` init-once (L202-204), freed on all paths; trailing EOF flush.
**Acceptance map**: plan tiers 1-2 + reset → **Done**.

### 1.4 `worker/src/vw_whisper_engine.c` / `worker/include/vw_whisper_engine.h`

**Why change**: `no_speech_prob` per-segment getter; whisper params `suppress_nst = true`, `suppress_blank = true`, `no_speech_thold = 0.60f` (L78-80).
**Boundaries**: getter bounds-checked; `logprob_thold` NOT set (whisper default -1.0 disabled) — plan diagram claims it (Finding L2).
**Acceptance map**: plan Tier 2 → Done; logprob_thold → ⚠️ missing.

### 1.5 `worker/src/vw_worker_config.c` / `vw_worker_config.h`

**Why change**: `--vad-model` CLI + auto-discovery (`ggml-silero-vad.bin` next to the model, then standard dirs). snprintf sizeof-safe; explicit path wins.
**Acceptance map**: plan CLI → **Done**.

### 1.6 `worker/src/vw_segment_builder.c`

**Why change**: Tier-3 filter at the builder entrance (L250) before the coverage gate (L262) and before `commit_history` (L345) — rejected phantoms never advance `covered_end_us` (no coverage pollution).
**Boundaries**: ordering verified safe; coverage model provably compatible with adjacent no-hop chunks.

### 1.7 Tests (`test_vad.c` +185, `test_hallucination_filter.c` +102, `test_worker_config.c` +12, `test_whisper_engine.c` +1, `test_segment_builder.c` +7) + `tests/CMakeLists.txt`

**Why change**: VAD (energy + Silero + chunk boundary), filter (case/punctuation/tags/notes/UTF-8/dialogue), config, engine getter, builder 5 rejection asserts.
**Coverage gap**: `test_vad`'s Silero chunk test feeds a single fresh buffer — the multi-chunk streaming path (LSTM context, 24 s silence hold, tail re-feed) is untested (Finding L4).

### 1.8 Models + Docs (`models/download-vad-model.{sh,cmd}`, `models/manifest.json`, `README.md`, `docs/*`: ADR-019, architecture, api-contracts, source-layout, test-strategy, roadmap, plan)

**Why change**: VAD model download helpers, ADR-019 (Multi-Tier VAD & Silence Gating), docs (Rule 14).

---

## 2. Happy-Path Request Trace (lookahead, source mode)

1. Worker starts: `config->vad_model_path` (CLI or discovery) → `vw_vad_init_default` → `vad_ctx` (or NULL → energy).
2. Source decoder feeds 2 s chunks into the 60 s ring. When count ≥ 6 s (or EOF), `vw_vad_find_chunk_boundary` (vw_vad.c:59) runs Silero (`_no_reset`, centisecond segments) → returns `cut_samples` at the first silence gap ≥300 ms in [6 s, 24 s], or a silence drain, or a 24 s forced split.
3. Worker transcribes `cut_samples` (vw_worker.c ~L682), drains them non-overlapping (PTS advances exactly).
4. Per segment: `get_segment` → `no_speech_prob`; `>= 0.60` → dropped (L663).
5. `vw_segment_builder_push_hypothesis`: phantom filter (L250) → coverage-drop (L262) → dedup/trim → emit clamped.
6. SPU channel 9. Manual result: stable dialogue, filtered cues, good seeking.

Live path (L546-577) still uses 8 s/2 s overlapping windows with the Silero boolean gate; the coverage dedup absorbs the overlap.

## 3. Most Important Failure Path

**Long silence before speech (Finding M1)**: `vw_vad_find_chunk_boundary` Case A holds a pure-silence buffer until `sample_count >= 384000` (24 s) or EOF (vw_vad.c:80-90); the decoder keeps appending, so a long silence transiently stalls the lookahead frontier and delays post-silence speech transcription. Masked by the 30 s lookahead horizon (manual test passed); on longer silences it is an unnecessary latency/efficiency defect. Fix: drain a confirmed silence run progressively (e.g. once it exceeds a small threshold), not only at MAX/EOF.

## 4. Boundary Summary

| Boundary | Implementation | Status |
| --- | --- | --- |
| Input validation | VAD NULL/0 guards; config snprintf-safe; engine getter bounds; chunk bounds 6 s/24 s | OK |
| Concurrency | VAD on worker main loop only; reader thread untouched; vctx freed on all paths | OK |
| I/O | VAD model read at init; energy fallback offline | OK |
| Memory | `segments_from_probs` alloc/free per chunk (worker loop) | OK |
| Locale/UTF-8 | ASCII-range check + `c>=0x80 → not punctuation` (H1 fixed) | OK |
| Text matching | Standalone-token tag match (H2 fixed); delimiter-enclosed list avoids partial-word hits | OK |
| Latency | All-silence holds up to 24 s before drain | **M1** |

## 5. Acceptance Criterion → Code Mapping (plan `step17e_1_plan.md`)

| # | Criterion | Status |
| --- | --- | --- |
| 1 | VAD skips inference on silence/instrumental music | Done |
| 2 | Energy fallback without `--vad-model` | Done |
| 3 | `no_speech_prob >= 0.60` dropped pre-builder | Done |
| 4 | Standalone tags + isolated punctuation rejected; 100% dialogue preserved | Done (post-210c94e) |
| 5 | Seek/pause reset VAD state, no crashes/leaks | Done |
| 6 | 100% tests pass | Done (18/18) |
| 7 | Valgrind 0 errors | Done |
| 8 | Docs + ADR updated | Done (ADR-019) |

## 7. Code Review Findings

### Bugs (Sorted by Priority)

| Priority | Component / Location | Description | Impact | Proposed Fix |
| --- | --- | --- | --- | --- |
| **Medium** | `vw_vad.c:80-90` + `vw_worker.c:665-672` (M1) | Pure-silence buffer is held until `VW_CHUNK_MAX_SAMPLES` (24 s) or EOF before draining; up to 24 s of silence retained, stalling the lookahead frontier and delaying post-silence speech. | Latency/efficiency (masked by the 30 s horizon) | Drain a confirmed silence run progressively (small threshold), not only at MAX/EOF |
| **Low** | `vw_vad.c:110-113` | Effective silence-gap cut threshold is 450 ms, not the documented 300 ms (`seg_end_sample` already includes `+PAD` 150 ms and the test adds `+VW_CHUNK_MIN_SILENCE_GAP` 300 ms). | More conservative cuts than specified; harmless | Compare `next_start > seg_end_without_pad + gap` |
| **Low** | `vw_vad.c:133-140` | Forced-MAX fallback bundles up to ~23 s of trailing silence into one 24 s transcription call. | Wasteful, correct output | Cut at the last speech end instead of MAX |
| **Low** | `vw_worker.c:668-673` | The full residual is re-fed to the persistent Silero LSTM each iteration (only `cut_samples` drains) — samples fed more than once to the recurrent state. | Negligible (Silero memory is short) | Feed only incremental new audio to the LSTM |
| **Low** | `vw_whisper_engine.c:78-80` (L2) | `logprob_thold` claimed in the plan diagram but never wired (default -1.0 disabled). | Low-confidence segments rely on Tier-2/Tier-3 only | Set explicitly or drop the claim |
| **Low** | `tests/unit/test_vad.c` (L4) | Silero chunk test feeds a single fresh buffer; multi-chunk streaming, 24 s silence hold, and tail re-feed are untested. | Regression blind spot in the no-hop loop | Add a multi-chunk streaming test that drains cuts across iterations |

### Architectural & Operational Risks

| Category | Risk Description | Affected Files | Mitigation |
| --- | --- | --- | --- |
| **Streaming VAD** | `_no_reset` + buffer-relative centisecond segments verified correct, but the LSTM tail re-feed means audio is not fed exactly once | vw_vad.c, vw_worker.c | Clean incremental feed; multi-chunk test |
| **Silence latency** | 24 s all-silence hold (M1) can delay post-silence captions on long pauses | vw_vad.c | Progressive silence drain |
| **Live/lookahead asymmetry** | Live keeps 8 s/2 s overlap (coverage dedup absorbs); lookahead is no-hop — a future no-hop live port would change dedup pressure | vw_worker.c | Document; keep coverage model |
| **VAD cost** | Silero per-chunk cost on the single-threaded loop is unmeasured; plan claims `< 0.5 ms` | vw_vad.c | Measure CPU vs GPU |

### Code Style & Quality Nitpicks

| Issue Type | File & Line | Description | Recommendation |
| --- | --- | --- | --- |
| Doc drift | plan Evidence §"< 0.5 ms" | Unverified VAD cost claim | Replace with measured value |
| Duplicate | `vw_hallucination_filter.c` | Duplicate `(applause)` entry in the tag list (pre-210c94e) | Remove |
| Dead guard | `vw_worker.c:555/663/696` | `&& seg_info.text_utf8` always true (getter returns `""`) | Remove |

---

*Step 17e.1 vs 17d.1 (`8ab8abc`). Scout-verified: no-hop chunking fundamentally correct (cut math, bounds, EOF tail, drain loop sound; coverage model provably compatible with adjacent chunks); my earlier H1/H2/B1 findings fixed by `210c94e`; the only real defect is the 24 s all-silence hold (Medium), plus minor deviations. Manual test corroborates (stable dialogue, filtered cues, good seeking).*
