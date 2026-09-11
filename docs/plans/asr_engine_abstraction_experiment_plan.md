# Task: Introduce the experimental ASR engine boundary

## Outcome

Create a small worker-owned ASR abstraction that preserves current `whisper.cpp` behavior exactly while making engine identity, capabilities, normalized result semantics, and backend selection explicit enough for a later native Nemotron streaming adapter and optional remote/BYOK adapters.

This PR does **not** link or execute NeMo-Speech.cpp, download Nemotron weights, render mutable subtitles, or send transcription audio to a network provider.

## Scope

- In:
  - Add a compact `vw_asr_engine` facade in the worker.
  - Adapt the existing `vw_whisper_engine` behind that facade without changing Whisper windowing, VAD, source look-ahead, live rolling-buffer policy, translation, or caption presentation.
  - Define normalized ASR result semantics: UTF-8 text, signed 64-bit microsecond offsets, and explicit `is_final`.
  - Define small capability metadata sufficient to distinguish final-only/windowed engines from native streaming/partial engines and local engines from future explicit remote/BYOK adapters.
  - Add an engine identity/config path with `whisper` as the default and `nemotron` as a known-but-unavailable experimental engine in this branch.
  - Add Lua settings selection for ASR engine plus the existing Auto / Vulkan GPU / CPU backend selection. Nemotron is visibly marked experimental/unavailable until the next implementation branch.
  - Fail closed and explicitly when an unavailable engine is selected; never silently fall back to Whisper under a different engine choice.
  - Document Nemotron runtime/backend/licensing/size facts relevant to the next branch.
- Out:
  - No NeMo-Speech.cpp source/binary dependency.
  - No Nemotron inference implementation.
  - No mutable-SPU/partial-caption presenter implementation.
  - No cloud transcription, API keys, credential persistence, or PCM egress.
  - No installer inclusion of Nemotron weights/runtime in this PR.
  - No changes to Whisper decoding/windowing/LocalAgreement behavior.
- Components/files expected:
  - `worker/include/vw_asr_engine.h`
  - `worker/src/vw_asr_engine.c`
  - `worker/include/vw_whisper_engine.h` / `worker/src/vw_whisper_engine.c` only where needed by the adapter
  - `worker/include/vw_worker_config.h` / `worker/src/vw_worker_config.c`
  - `worker/src/vw_worker.c`
  - `plugin/include/vw_worker_client.h` / `plugin/src/vw_worker_client.c`
  - `plugin/src/vw_whisper_module.c`
  - `lua/extensions/vlc_whisper_settings.lua`
  - focused tests and contract docs

## Design: keep it deliberately small

Do **not** introduce a registry, class hierarchy, dependency-injection framework, or generic provider object graph.

`vw_asr_engine_t` is one tagged facade owned by the worker. The first implementation contains only a Whisper implementation pointer. Engine-specific code stays in engine-specific modules. The facade uses an explicit `switch` on engine kind; adding a second local engine later means adding one enum value and one adapter path, not building a framework.

The generic contract contains only facts that every ASR consumer genuinely needs:

- engine identity;
- capabilities;
- backend request/runtime truth;
- normalized transcript result;
- lifecycle ownership;
- cumulative inference timing.

Provider-specific configuration remains provider-specific. Future BYOK credentials must **not** be added to generic CLI arguments, logs, or protocol payloads; protected credential storage/handles belong to the later remote-provider milestone.

## Normalized result contract

Every adapter returns a `vw_asr_result_t` with:

- `int64_t start_offset_us`;
- `int64_t end_offset_us`;
- `bool is_final`;
- borrowed UTF-8 text;
- optional engine-specific confidence/silence metadata only when it already has an established consumer.

Native engine timestamp units never leave the adapter. Whisper centisecond offsets are converted to microseconds inside the Whisper adapter. A future Nemotron adapter may use NeMo-Speech word offsets when enabled, or derive cue timing from the worker-owned audio/sample timeline and endpoint boundaries; either way the public worker contract remains signed 64-bit microseconds.

Whisper adaptation rule: **every Whisper result is `is_final=true`**. No partial Whisper cue may be invented by this abstraction.

Future native-streaming rule: a streaming engine may emit `is_final=false` revisions followed by `is_final=true`. Mutable presentation is a later presenter change and is capability-gated; current Whisper presentation remains immutable.

## Capability contract

Use a small bitfield/descriptor, not subclasses. Required distinctions:

- local vs remote/networked;
- window/final-only vs native streaming;
- partial/mutable result support;
- source look-ahead support;
- CPU support;
- Vulkan GPU support;
- API-key/credential requirement for future remote adapters.

Initial descriptors:

- `whisper`: local, final-only/windowed, source-lookahead capable, CPU + Vulkan capable, no credential requirement.
- `nemotron`: local, native-streaming, partial/final capable, same streaming architecture for seekable and live media, CPU + Vulkan intended, no credentials; `available=false` in this PR.

The descriptor is product/runtime metadata, not evidence that an unimplemented adapter exists.

## Confirmed Nemotron considerations for the next branch

Target runtime: NVIDIA NeMo-Speech.cpp stable C ABI (`include/nemo_speech/asr.h`). It exposes opaque C handles, `stream_push_f32`, `stream_next`, explicit `result_is_final`, endpoint/finish operations, and optional word time offsets. Its build supports CPU and ggml Vulkan; CUDA is optional and is **not** required for the planned VLC-Whisper integration.

Target English Q8 model: `nemotron-speech-streaming-en-0.6b.q8_0.gguf`, currently reported as 700 MB (699,872,960 bytes / about 667.5 MiB), SHA-256 `d9a01898d2a611c8764e23a1c2f45e70bbd5a425dc4de93692ac951dd603812d`.

Licensing:

- NeMo-Speech.cpp NVIDIA-authored runtime code: Apache-2.0, with its NOTICE/third-party notices retained.
- English Nemotron model: NVIDIA Open Model License. Redistribution is permitted, including commercially, but distributing the model requires providing the NVIDIA Open Model License and a `Notice` text file containing `Licensed by NVIDIA Corporation under the NVIDIA Open Model License`.

Therefore bundling is legally possible, but the ~700 MB Q8 artifact makes explicit on-demand verified download the preferred product default. Bundling is a later packaging decision, not part of this abstraction PR.

References:

- https://github.com/NVIDIA/NeMo-Speech.cpp/blob/main/include/nemo_speech/asr.h
- https://github.com/NVIDIA/NeMo-Speech.cpp
- https://huggingface.co/nvidia/nemotron-speech-streaming-en-0.6b/blob/main/nemotron-speech-streaming-en-0.6b.q8_0.gguf
- https://www.nvidia.com/en-us/agreements/enterprise-software/nvidia-open-model-license/

## Contract map

### Whisper inference

`vw_worker session/window scheduler -> vw_asr_engine facade -> vw_whisper_engine adapter -> vw_asr_result -> segment builder -> local IPC SEGMENT -> plugin presenter`

Lifecycle owner: `vw_worker_run` session state. Whisper source/live scheduling remains owned by the existing worker paths.

### Engine selection

`Lua/VLC config -> plugin worker launch argv -> vw_worker_config -> vw_asr_engine create -> worker START error/success -> plugin caption-session state`

Lifecycle owner: plugin worker respawn for engine/backend/model changes; worker owns selected engine instance for its process lifetime.

### Future Nemotron

`plugin PCM/source decode -> worker canonical 16 kHz PCM -> future Nemotron stream adapter -> partial/final vw_asr_result -> future mutable-caption seam -> plugin presenter`

The same native streaming recognizer policy is intended for seekable and live media. Local source look-ahead remains a Whisper-only capability; selecting a native-streaming engine must not silently route seekable media through Whisper's source-lookahead policy.

### Future remote/BYOK

`explicit user selection + protected credential handle -> worker remote adapter -> explicit audio egress -> normalized vw_asr_result`

This path is design-only here. Current privacy invariant continues to prohibit transcription-audio egress.

- Invariants touched: state/API, lifecycle, identity, metrics, realtime, privacy/network, timeline.

## Failure semantics

| Boundary/failure | retry | recover | fail session | fail process/startup |
| --- | --- | --- | --- | --- |
| Unknown `--asr-engine` identity | | | | yes: CLI configuration error |
| Known but unavailable `nemotron` adapter | | | yes: explicit engine-unavailable error; playback continues | |
| Whisper model missing/invalid | | | yes: existing model error behavior | |
| ASR result malformed/non-monotonic | | | yes: do not emit plausible caption | |
| Worker/plugin IPC failure | existing bounded policy | existing policy | existing policy | existing fatal transport policy |
| Future remote provider/network failure | bounded provider policy only | only explicitly equivalent state | yes | only if worker continuation unsafe |

Never silently substitute Whisper when `nemotron` or a future remote engine was explicitly selected.

## Lifecycle impact

- START/new media: Whisper behavior unchanged. Engine instance is process-owned; session state remains worker-owned.
- Pause/resume: unchanged for Whisper.
- Seek/discontinuity: unchanged for Whisper, including source look-ahead epoch behavior and live rolling-buffer reset behavior.
- Media swap: unchanged for Whisper.
- EOF/tail flush: unchanged on this `main` base, including known deviations documented by `docs/invariants.md`.
- STOP/shutdown: generic facade destroys only its owned adapter; no cross-engine stale state.
- Worker failure/respawn: selected engine identity is re-read/re-forwarded with the same model/backend settings.
- Overload/drop: unchanged for Whisper.

Future Nemotron requirements are recorded but not implemented: reset/close streaming state on every new caption epoch, partials are session-scoped, and finalization must be explicit at EOU/EOF rather than inferred from silence in plugin code.

## Identity / metrics / hot path

- Identity-bearing values changed: engine ID. Unknown/oversized values reject; they never truncate to another engine.
- Metrics: cumulative `inference_us` remains worker/engine-owned and microseconds. The facade delegates Whisper's existing authoritative metric; no synthetic replacement.
- Backend truth: STATUS continues to report actual CPU/GPU runtime truth, never only the requested backend.
- Realtime callback: unchanged. Engine selection and inference remain outside VLC's audio callback. No heap, IPC, filesystem, inference, network, or new blocking work enters the callback.
- Timeline: signed 64-bit microseconds remain canonical across worker/result/caption contracts.

## Core invariants that must remain unchanged

1. Playback always wins; caption failures cannot stall/crash VLC.
2. VLC audio callback stays bounded/non-blocking and allocation-free.
3. Authenticated local IPC remains the only transcription transport in this PR.
4. Whisper local-file source mode retains current source-decode look-ahead behavior.
5. Whisper live/non-seekable mode retains current progressive rolling Whisper behavior.
6. Whisper captions remain immutable/final and keep the current presenter path.
7. Translation continues to receive finalized text only.
8. Seek/media/session epoch stale-caption rejection remains unchanged.
9. No engine selection may silently alter timestamp domains or collapse discontinuities.
10. No unavailable/new engine silently falls back to another engine after an explicit user choice.

## New architectural invariants

1. Engine-native units are normalized to signed microseconds before leaving an adapter.
2. `is_final` is authoritative engine/adaptor state: Whisper always true; native streaming adapters may emit false then true.
3. Mutable subtitle behavior is permitted only for engines advertising partial-result capability; final-only engines cannot enter that presenter path.
4. Native-streaming engines keep one streaming architecture across local/seekable and live/network media; Whisper-specific look-ahead/window policies must not leak into them.
5. Engine/backend capability is explicit. Vulkan support is represented separately from generic GPU intent.
6. Future remote/BYOK ASR must be explicit, capability-marked as network/credential-requiring, worker-confined, and privacy-visible; no generic abstraction grants network permission by itself.

## Tests first

Red-before-implementation specs to add before production code:

- `test_asr_engine_contract`
  - default/parsed Whisper identity is stable;
  - Nemotron is a known engine descriptor but unavailable in this build;
  - unknown engine identity rejects;
  - Whisper descriptor is final-only/source-lookahead/local and CPU+Vulkan capable;
  - Nemotron descriptor is native-streaming/partial/local and CPU+Vulkan intended;
  - Whisper segment adaptation always returns `is_final=true` and preserves microsecond offsets/text.
- `test_worker_config_asr_engine`
  - default is Whisper;
  - `--asr-engine whisper` accepted;
  - `--asr-engine nemotron` accepted as known identity;
  - unknown/oversized engine identity rejected before truncation.
- worker/client seam test
  - selected unavailable Nemotron produces an explicit unavailable-engine failure without loading Whisper, making network calls, or accepting audio as a successful session.
  - explicit Whisper selection preserves current START behavior.
- Lua/config static/smoke checks where existing test infrastructure permits:
  - new `whisper-asr-engine` default is `whisper`;
  - engine and backend are independent settings;
  - Nemotron label says experimental/unavailable in this branch.

Existing ledger regressions affected: none should be weakened. Existing worker lifecycle, seek epoch, protocol, segment builder, presenter, quality benchmark, and settings behavior must stay green.

Faults to inject: unknown engine ID; known-but-unavailable engine; malformed/oversized engine identity; model missing under explicit Whisper.

## Implementation sequence

1. Commit this plan only.
2. Add/register the tests above with no production implementation and obtain the expected red CI evidence.
3. Add `vw_asr_engine` result/capability/identity facade and Whisper adapter.
4. Replace worker direct Whisper calls with facade calls while preserving scheduling and segment-builder behavior.
5. Add engine selection to worker config/client spawn/plugin config; keep Whisper default and fail closed for unavailable Nemotron.
6. Split Lua's current backend-labelled `Engine` UI into `ASR engine` and `Backend`; expose Whisper and an explicitly unavailable experimental Nemotron choice.
7. Update only contract-relevant docs.
8. Run/observe formatting, build, CTest, and memory-check gates; compare branch against `main` for unintended behavior changes.
9. Create a draft PR targeting `main`, title/body prominently marked EXPERIMENTAL and high risk, apply existing enhancement/high-risk labels plus `experimental` if available, then comment `@codex review` to request Codex review.

## Verification

- [ ] Plan committed before tests/implementation.
- [ ] Relevant new specs fail for the intended reason before implementation.
- [ ] New/affected specs pass.
- [ ] Existing relevant regression suite passes.
- [ ] `clang-format --dry-run --Werror <modified-c-files>`.
- [ ] `cmake --preset linux-x64-debug && cmake --build --preset linux-x64-debug`.
- [ ] `ctest --preset linux-x64-debug --output-on-failure`.
- [ ] Existing memcheck gate run when available/applicable.
- [ ] Lua 5.1 syntax check when available.
- [ ] Only contract-relevant docs updated.
- [ ] No Nemotron runtime/model binary/vendor dependency appears in the diff.
- [ ] No transcription PCM network path appears in the diff.

## Evidence

- Branch base: `main` at `c1fbb44670e488844a7ef5207f978140c377f66c`.
- `graphify-out/` is git-ignored/not present in the repository; source and repository search remain authoritative.
- Current protocol already carries `vw_caption_segment_t.is_final`; this abstraction makes the producer semantics explicit rather than inventing a second finality field.
- NeMo-Speech.cpp C ABI confirms native streaming push/pull, `is_final`, endpointing, and optional word timing, validating the result/finality design without requiring the dependency in this PR.
- Known limitations/follow-ups:
  - Nemotron adapter/runtime, mutable SPU presentation, endpoint policy, and benchmarking are the next experimental branch.
  - Remote/BYOK adapters require a separate privacy/credential design and must not reuse CLI/plaintext key transport.
  - Model bundling is legally possible under NVIDIA terms but intentionally deferred; on-demand verified download is preferred because the Q8 artifact is ~700 MB.
