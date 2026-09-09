# Pinned VLC 3 Integration Essentials

Authoritative behavior comes from the exact vendored VLC 3.0.23 headers/source used by the build. This file records only VLC-Whisper-specific contracts and traps; it is not a general VLC API manual.

## Module boundary

The plugin is a native VLC audio filter. Project-owned worker code must not link VLC. Include `<vlc_common.h>` before other VLC headers and preserve any required include ordering from the pinned headers/build.

`filter_t::pf_audio_filter` runs synchronously on VLC's audio path. It may inspect/normalize/copy bounded PCM and enqueue it, but must not infer, block on IPC/locks, access files, or heap-allocate. The filter is passthrough: audio format/output behavior must remain compatible with VLC's chain so captioning cannot alter playback.

## `block_t` timing gotcha

On pinned VLC 3.0, audio-filter block `i_pts` is in the audio-output **system-date presentation domain**, not the media position returned by `INPUT_GET_TIME`. This is the critical clock-domain trap.

- live PCM/session timestamps can originate from audio-filter/system-date PTS;
- `INPUT_GET_TIME` is media-relative;
- native source-decoder timestamps are media-relative;
- never compare/subtract these without an explicit mapping.

The current reliable filter-pushed caption path on the supported VLC 3.0.23 Windows configuration uses the OSD/system-date render domain. Source look-ahead scheduling maps media-relative cue PTS against sampled playhead state. Keep tests around this mapping; do not "simplify" clocks based on generic VLC documentation.

## Discontinuities and capabilities

`BLOCK_FLAG_DISCONTINUITY`, backward/non-monotonic timeline observations, explicit seek state, large clock jumps, rate changes, and media swaps can require a fresh caption epoch. Detection remains bounded in callback-adjacent code; heavy reset/IPC work is handed to the control thread.

From a plugin, seek/pause/rate capabilities are read through input variables such as `can-seek`, `can-pause`, and `can-rate`. Internal demux queries like `DEMUX_CAN_CONTROL_PACE` are not generally available through a public plugin API; do not invent `INPUT_CAN_*` queries that are absent from the pinned headers.

Source classification must be based on explicit project policy/capabilities, not one weak heuristic.

## Input/vout ownership

Audio filters and the active `vout` can live on different VLC object-tree branches. Use the project's tested presenter/input lookup rather than assuming a direct parent relationship.

VLC object references returned as held references must be released; raw tree pointers must be held before escaping the traversal. `vlc_object_hold/release` provides lifetime, not mutual exclusion. Any code changing object traversal must be verified against the pinned object API and teardown races.

## Caption rendering

The presenter owns the generated-caption SPU/OSD channel and its lifetime. Important rules:

- use a dedicated registered subpicture channel where available; do not clear VLC's unrelated system OSD channel;
- text/timestamps are validated before presentation;
- blank/seek/teardown flush the generated-caption channel as required;
- live replacement and source future scheduling have different queue/flush behavior;
- the visible reading-floor extension is presentation policy, not authentic transcript timing.

`b_subtitle` selects a different VLC render clock from OSD-style subpictures. Because filter-pushed media-clock subtitles have had pinned-build-specific behavior, changes to this area require a focused VLC acceptance/regression check rather than relying on documentation alone.

## Pause/seek/media lifecycle

Pause stops/invalidates the relevant capture/inference state without freezing VLC itself. Resume does not reuse invalid acoustic state. Seek/media epoch changes clear generated captions and create a fresh worker session ID. Module close detaches state, joins worker/control activity, clears presenter channels, and only then destroys owned transport/queue resources.

## Module/worker discovery

The plugin's worker-path discovery must use trusted/checked paths and reject identity truncation. Do not reintroduce bare CWD-dependent resolution where the project already has explicit executable/install/path probing. Worker respawn must re-resolve the executable according to the owning path policy.

## Before changing VLC-facing code

Inspect the exact vendored declaration/implementation plus the relevant project test. Pay special attention to:

- callback thread/realtime behavior;
- object lifetime/refcount ownership;
- clock domain;
- public vs internal API availability;
- ABI/exported symbols on Windows;
- pinned VLC version behavior vs current upstream docs.

For overall lifecycle see `architecture.md`; for wire clocks see `api-contracts.md`.
