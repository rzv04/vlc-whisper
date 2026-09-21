# Task: Make Qt settings downloads and translation tests independent of playback

## Outcome
From VLC's existing settings launcher, download/abort a verified model and translate typed test text in a secondary dialog with no media playing.

## Scope
- In: bounded worker utility modes, Qt process ownership, download progress/cancel, explicit translation test input/result, contract tests and relevant docs.
- Out: launcher/standalone EXE policy, audio callback, inference, caption timing and translation fallback changes.
- Base: feat/standalone-qt-settings at 628b704468f53214a5ae927eb319ddd1e639980f (PR #65).

## Contract map
- Qt explicit download -> inherited private child pipes -> worker utility mode -> existing downloader thread, destination lock, SHA-256 and atomic publication.
- Qt typed text + selected languages -> private child stdin (no text in argv/files/logs) -> existing translation API -> bounded stdout result -> secondary Qt dialog.
- Qt remains UI/settings owner; worker owns network. Utility children never initialize Whisper or connect to the playback worker. Pipes are inherited handles, not publicly addressable IPC endpoints.
- Existing rollback marker protects active model while download is pending/failed; remove only after verified success. No new model-command is sent to the playback worker.
- Invariants: playback isolation, lifecycle, explicit states, identity, privacy/network.

## Failure semantics
- Missing/unlaunchable worker, invalid input, destination lock failure, provider/network failure: fail the requested UI operation with a visible error; playback/settings process survive.
- Download retry/verification/publication: unchanged existing downloader policy.
- Abort or parent pipe EOF: worker aborts and joins its downloader, releases destination lock and exits with cancellation status.
- Translation: existing provider tiers/deadline; nonzero exit is failure, never an apparently successful empty translation.
- Bound all stdin/stdout buffers; reject identity/text overflow and unsupported languages before network.

## Lifecycle impact
Playback START/STOP/pause/resume/seek/media swap/EOF/respawn are unchanged and do not own these operations.
Settings owns one download child and one test child. Closing settings closes the download control pipe and waits asynchronously for cleanup before final close; translation has its existing bounded deadline. No UI-thread blocking wait. New requests remain disabled until child termination. Closing the result dialog does not leave dangling callbacks.

## Identity / metrics / hot path
Use only fixed installed/build-relative worker candidates (prefer CPU), never PATH/shell. Model IDs come from existing catalog. Download progress comes only from worker snapshots; initial IDLE is not completion. Translation text is UTF-8 bounded to the existing API limit and is not persisted. No realtime code changes.

## Tests first
- Process seam: utility accepts no playback IPC/model, rejects invalid model/languages/oversized text without network; deterministic translator output and failure survive stdin/stdout boundary.
- Download seam with injected downloader: progress, completion, failed start, cancellation and parent EOF; initial IDLE must not signal success.
- Qt seam: input/current language selection, result dialog and failure handling, cancellation/close, worker discovery; use headless checks where available.
- Existing model downloader lock/hash/abort tests and translation fallback/deadline tests remain authoritative.

## Implementation
1. Add worker utility dispatch before normal configuration parsing and a small C17 service adapter reusing existing APIs.
2. Replace new download file commands with QProcess child lifecycle/progress and preserve rollback behavior. Do not use legacy playback progress as the UI operation state; the old plugin command consumer remains unchanged.
3. Add translation text input/Test button and secondary dialog; pass text privately to worker and display result/errors as plain text.
4. Update canonical contracts and user instructions, format and verify.

## Verification
Run new red tests before implementation, then targeted/new tests, full configure/build/CTest and memory gate when available; record environment limits. Review changed producers/consumers and git diff before committing. Open PR targeting the base branch with enhancement/risk labels; no Codex review request.

## Plan review
Read back against AGENTS.md, invariants, settings file rollback consumer, downloader API and translation API. Preserve worker network ownership rather than introducing Qt HTTP. Use direct inherited pipes for the new private child boundary. Keep unrelated launcher behavior untouched. Explicitly distinguish initial downloader IDLE, cancelled, failed and verified DONE.

## Verification evidence
- Wrote this plan and read it back before code changes. Initial service test failed because the service adapter did not exist; initial Qt seam guard failed because no independent download dispatch existed.
- Production C adapter through deterministic child pipes: all named checks pass, including cancellation, broken result pipe, initial IDLE, failure, UTF-8, and input rejection.
- Existing frontend contract and new independent-service source guard pass. Added a real headless Qt -> QProcess -> C adapter integration test for CI, including translation/result dialog, verified/failed download, cancellation and asynchronous close.
- Native debug configure/build and changed-file clang-format/git diff checks pass. Qt is unavailable locally; CMake explicitly omits that target. Dependency installation was blocked by container permission restrictions.
- Full local CTest: 53 tests, 12 failures/not-run in IPC/lifecycle executables; direct AF_UNIX SOCK_SEQPACKET probe returns EPERM. See CI for unrestricted IPC/Qt validation. No playback implementation was changed.
- Existing memcheck attempted; Valgrind is unavailable locally. CI already supplies Qt and Valgrind. Expanded only the PR base filter to include this stack's `feat/standalone-qt-settings` base so its existing checks can run.
- Launcher/standalone EXE policy remains inherited from PR #65. Windows UI/installed-worker discovery needs manual acceptance.
- CI runs 35637774501 and 35638421527 built Qt and passed all 55 CTest entries (one model-gated skip). Valgrind passed behavioral checks but reported Qt 6.4.2's first QProcess startup probe (`waitid` with a NULL result pointer on deliberately invalid fd INT_MAX). Checked the exact Qt v6.4.2 source at e3e40c44d3f998a433a6a1080297c5f28e9a768f (`forkfd_linux.c`, `detect_clone_pidfd_support`); added an exact-version, exact-parameter/stack suppression, not a project-code or broad Qt suppression. Detailed failed Valgrind reports now print in CI; the strict gate remains in place.
