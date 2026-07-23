# Test Strategy

## Quality principle

The primary invariant is: **captioning must never harm playback**. A transcription error is recoverable; a VLC crash, audio glitch, deadlock, uncontrolled memory growth, or privacy leak is a release blocker.

## Test layers

| Layer | Scope | Examples |
|---|---|---|
| Unit | Pure C logic | Ring buffer, PTS arithmetic/overflow, session state, UTF-8 validation, dedupe |
| Protocol | Binary compatibility | Golden frames, unknown fields/types, max lengths, token failure, sequence/stale session |
| Worker integration | Local inference | Fixed PCM fixtures, VAD boundaries, model missing/hash mismatch, deterministic settings |
| VLC integration | Module behavior | Load/unload, PCM capture format, display scheduling, pause/end/stop |
| End-to-end | Pinned Windows VLC | Install, local video, visible captions, worker crash, seek rejection, uninstall |
| Performance | Reference machines | Real-time factor, p50/p95 caption latency, CPU/RAM, queue drops |
| Security/privacy | Local boundary | Pipe ACLs, random name/token, no listener, no remote traffic, log redaction |

## Fixtures

Keep legal, small, versioned fixtures: synthetic tones/silence, public-domain or licensed English speech with known transcript/timestamps, short local MP4/MKV containers, malformed frames, and controlled PTS discontinuities. Never commit proprietary films, user audio, production model binaries, or personal transcripts.

Golden expected text should tolerate model-version variance only through explicit normalization policy. Pin model hash and whisper.cpp commit for exact regression tests; if either changes, review differences intentionally rather than silently re-baselining.

## Required cases

- Start local English media, captions appear after bounded warm-up, and final captions have valid ordered PTS.
- Pause stops AUDIO forwarding and clears partial state; resume does not reuse a stale worker session.
- End/stop clears captions and closes worker cleanly.
- User seeks, changes rate, replaces media, or creates non-monotonic PTS: generated captions clear, VLC keeps playing, a single diagnostic appears, no crash.
- Worker absent, wrong version, invalid token, model missing/corrupt, pipe disconnect, bad payload, invalid UTF-8, or worker nonzero exit: safe disable, no playback impact.
- Sustained slow inference: queue stays bounded, old audio is dropped by policy, memory stays bounded, and drop counter rises.
- Existing subtitle track and VLC-whisper behavior follow the documented coexistence policy.

## Performance contract

Define the reference machine before claiming “real time”: CPU model/core count, RAM, Windows build, VLC build, model hash, worker flags, and fixture. Record:

- Real-time factor = inference processing time divided by audio duration; target steady-state below 1.0 for tiny.en on reference hardware.
- End-to-caption latency: target p95 below 5 seconds under the selected 8-second/2-second default windowing, measured from segment end PTS to display scheduling.
- No unbounded queue; backlog hard limit 15 seconds; zero intentional playback stalls.

These targets are engineering gates, not a guarantee for every PC or noisy source.

## CI gates

Every merge: format check, C compilation with warnings-as-errors, unit/protocol tests, sanitizer build where target permits, dependency/license scan, and Windows cross-build. Nightly: fuzz corpus, worker integration with pinned model fixture, reproducibility/hash check, and Windows VM VLC smoke test when infrastructure is available.

Release requires all gates green, manual local-file acceptance on clean Windows, documented known failures, protocol/version manifest, model hash verification, and review of diagnostics to ensure no PCM/transcript/path leakage.
