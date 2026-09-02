# Qt Settings Process vs Worker-Owned Model Downloader

## Question

If the standalone Qt settings process becomes the production model/settings frontend, should VLC-Whisper move the model-download HTTP implementation out of `vlc-whisper-worker` and into that Qt process?

## Current downloader

The current worker implementation does much more than issue an HTTP GET:

- Linux forks/execs `curl` and polls the `.part` file size while retaining abort ownership of the child.
- Windows uses WinHTTP directly and streams the response into the `.part` file.
- Both platforms limit oversized transfers, retry once, delete failed/aborted partial files, verify the model's pinned SHA-256, and atomically rename a verified `.part` file to its final filename.
- A per-model lock prevents two downloaders from mutating the same destination concurrently.
- Worker lifetime currently owns progress and abort semantics, and plugin/worker IPC exposes those states to the Lua UI.

That security/integrity behavior must not be lost merely because another HTTP API is easier to use.

## What Qt improves

A production Qt frontend could replace both the Linux `curl` subprocess and Windows WinHTTP transport with one asynchronous implementation based on `QNetworkAccessManager`/`QNetworkReply`.

Useful Qt primitives map directly onto the existing downloader responsibilities:

| Current responsibility | Qt-side equivalent |
| --- | --- |
| HTTP GET | `QNetworkAccessManager::get()` |
| Incremental body reads | `QNetworkReply::readyRead()` |
| UI progress | `QNetworkReply::downloadProgress()` |
| Abort | `QNetworkReply::abort()` |
| SHA-256 | `QCryptographicHash::Sha256` |
| Cross-process destination lock | `QLockFile` (only if every model writer adopts the same lock convention) |
| Partial file | streamed `QFile` at `<model>.part` |
| Final install | verify first, then same-directory rename/replace policy |

This would substantially reduce platform-specific downloader code and make progress/cancellation natural UI events instead of states mirrored through VLC config and IPC.

On Windows, Qt can use its native Schannel TLS backend. A production package can therefore ship Qt Network plus the Schannel plugin and rely on Windows' TLS implementation rather than requiring the user to install OpenSSL.

## Advantages of moving downloads to the settings process

1. **Models can be provisioned while VLC is closed.** This is the strongest product advantage. The user can install/select models before the audio filter or worker exists.
2. **One cross-platform HTTP implementation.** The current WinHTTP/curl split disappears from application-authored code.
3. **Direct progress UI.** The settings process receives progress, errors, redirects, and cancellation events directly.
4. **Smaller worker responsibility.** The inference process can return to local model verification/loading, transcription, VAD, and translation rather than also acting as a package manager.
5. **No Linux runtime dependence on an external `curl` executable for model provisioning.** Qt Network becomes the transport dependency already bundled with the settings app.
6. **Cleaner user intent boundary.** Network activity happens in the process whose visible UI the user explicitly used to request a download.

## Disadvantages and architectural risks

1. **Closing the settings window can terminate the downloader.** If the GUI owns the transfer, either downloads are cancelled when it exits or the process must deliberately remain alive until the requested transfer finishes. A hidden tray/background application would be a poor fit for the current simple VLC style.
2. **VLC can request/use models while the GUI is absent.** If runtime behavior still needs on-demand provisioning from VLC without showing settings, worker ownership is more robust.
3. **Two model writers are unacceptable.** During migration, worker and Qt downloaders must not both be enabled unless they share exactly the same cross-process locking and install protocol. Qt `QLockFile` only serializes cooperating users of that same lock format; it does not automatically interoperate with the worker's current `flock`/exclusive-handle lock implementation.
4. **Integrity logic must migrate with the HTTP logic.** Size bounds, pinned manifest URL/filename, SHA-256, retry policy, `.part` cleanup, atomic install, path safety, and logging are part of the downloader's security contract.
5. **Runtime status needs a new path.** After a Qt-owned download finishes while VLC is running, the plugin/worker need a deterministic way to discover/activate the new model. The future settings IPC or settings-file generation mechanism should own that notification.
6. **Qt Network increases the frontend deployment surface.** It adds `Qt6Network` and a TLS backend/plugin. This is still bundleable by the installer, but it is more runtime content than a Widgets/Core-only settings app.

## Recommendation

**Do not move the downloader as part of the initial Qt settings migration.** First land the out-of-process settings ownership and plugin synchronization contract. Keep the already-hardened worker downloader during that transition.

After the Qt frontend is established, a second migration is likely worthwhile **if model management is defined as a settings-application responsibility even when VLC is closed**. At that point, move the *entire model provisioning transaction* to Qt, not just HTTP:

```text
Qt settings app
  -> acquire model-install lock
  -> GET manifest-pinned URL asynchronously
  -> stream to <model>.part
  -> enforce expected-size ceiling
  -> SHA-256 verify
  -> atomic final install
  -> release lock
  -> persist/select model
  -> notify running VLC-Whisper plugin if present
```

The worker should still verify a model before loading it. That is a trust-boundary check, not merely downloader functionality, and should remain even when Qt has already verified the downloaded file.

### Preferred final ownership

- **Qt settings/model manager:** explicit user-initiated provisioning and deletion, download progress/cancel UX, first SHA-256 verification, settings persistence.
- **Worker:** verify-before-load, inference, VAD, translation; no model HTTP transport.
- **Plugin:** no network access; consume settings and trigger/observe model activation.

This produces a cleaner final architecture, but only after download lifetime, locking compatibility, and plugin notification are solved. Until then, retaining the worker downloader is safer than duplicating it.
