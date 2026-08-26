# Step 21a: Real-Time Subtitle Translation Research

**Status:** Research report only; Step 20 is intentionally skipped. No provider credential,
API key, shared secret, or implementation is added by this report.

**Date:** 2026-08-26

## Executive finding

PotPlayer does not appear to provide an unlimited, provider-funded Google Cloud quota for all
users. The supplied PotPlayer file `SubtitleTranslate - google.as` provides direct evidence of
the mechanism used by this translator script: it uses a user-supplied Google Cloud API key when
present, then walks through several unauthenticated Google web/legacy endpoints when the key is
absent or the keyed request fails. That explains the apparently free, no-account experience, but
it is not a documented Google API and is not a reliable production dependency.

The defensible VLC choices are therefore:

1. User-owned credentials for an official provider API (recommended first implementation).
2. A project-operated relay with authentication, quotas, abuse controls, and an explicit privacy
   amendment (possible, but it is not free to operate).
3. A user-owned local or self-hosted translation endpoint (private, but without a managed SLA).

The undocumented PotPlayer-like fallback chain can be investigated as an isolated compatibility
experiment, but should not be the default or the basis of the product promise.

## Evidence about PotPlayer

The supplied screenshot shows a provider-oriented subtitle translation configuration: Google is
one of the possible services, with source/target language and account-related settings. The newly
inspected local script clarifies that the apparent account field is actually an API-key field;
the password value is ignored. The screenshot alone could not establish this.

The strongest implementation evidence is now the local PotPlayer script
`SubtitleTranslate - google.as` supplied in the working tree. Its host-interface comments and
functions match a PotPlayer subtitle-translation provider, including a per-line
`Translate(Text, SrcLang, DstLang)` callback. It shows the following request order:

1. If `ServerLogin(User, Pass)` received a value, the script stores `User` as `api_key` and
   calls the documented Google Cloud Translation Basic/v2 endpoint. `Pass` is never used.
2. If that call is unavailable or returns no parseable translation, it calls Google's internal
   `translate.google.com/_/TranslateWebserverUi/data/batchexecute` endpoint using an `f.req`
   POST body, RPC ID `MkEWBc`, and a hardcoded 2022 web-client build parameter. The script itself
   labels this path `open api(for free)`.
3. If the RPC response cannot be parsed, it calls
   `translate.googleapis.com/translate_a/single?client=gtx`, an undocumented legacy endpoint.
4. If that also fails, it requests the mobile Google Translate page and extracts
   `<div class="result-container">` from the HTML.

The script also prepends an empty source-language option for automatic detection and exposes a
large source/target language table. This is consistent with the screenshot and with PotPlayer
requesting translation one subtitle line at a time.

The archived community
[PotPlayer Subtitle Translate GoogleFix source](https://raw.githubusercontent.com/veritas501/Potplayer-Subtitle-Translate-GoogleFix/refs/heads/master/SubtitleTranslate%20-%20googleFix.as)
independently corroborates this design:

- It uses the same keyed API plus undocumented fallback approach.

The local artifact makes the mechanism high-confidence for that installed script, but it still
does not prove that every PotPlayer release ships the same file or that the current proprietary
application has no additional service layer. The fallback endpoints can change or reject
automated traffic without notice. Google documents official authentication and API access in its
[Cloud Translation authentication guide](https://docs.cloud.google.com/translate/docs/authentication)
and does not document the web RPC, `translate_a/single`, or mobile-page scraping as general-purpose
product APIs.

## Official provider options

| Option | User access model | Free allowance / cost signal | Realtime fit | Main concern |
| --- | --- | --- | --- | --- |
| Google Cloud Translation Basic/v2 | User supplies a key for their own Google Cloud project, or a relay owns the credential | First 500,000 NMT characters/month are credited; usage beyond that is billed. Project setup and billing are required. [Pricing](https://cloud.google.com/products/translate/pricing), [setup](https://docs.cloud.google.com/translate/docs/setup) | Synchronous text translation; broad language coverage; source language may be omitted for detection. [v2 API](https://docs.cloud.google.com/translate/docs/reference/rest/v2/translate) | A shared key cannot be safely shipped in VLC. Advanced/v3 uses stronger Google Cloud credentials; v3 does not use API keys. |
| Azure Translator | User supplies a subscription key plus region, or uses a user-owned Azure identity/resource | F0 has a 2 million character/month free pricing allowance; service limits separately document quota and latency behavior. [Pricing](https://azure.microsoft.com/en-us/pricing/details/translator/), [limits](https://learn.microsoft.com/en-us/azure/ai-services/translator/service-limits) | Good fit for short asynchronous subtitle cues; documented typical latency for small requests is 150–300 ms | Azure resource creation and key/region management are still required. Verify the user's current regional quota. |
| DeepL API | User supplies a `DeepL-Auth-Key`, or a relay owns the credential | The Free API usage documentation states 500,000 characters/month; account and plan availability can change. [Usage limits](https://developers.deepl.com/docs/resources/usage-limits) | Multiple texts can be sent in one request; good quality is attractive for subtitle text | DeepL explicitly says not to expose keys in publicly distributed client code. [Authentication](https://developers.deepl.com/docs/getting-started/auth) |
| Amazon Translate | User-owned AWS credentials or a relay; direct long-lived keys are unsuitable for a desktop binary | 2 million characters/month for the first 12 months, then billed. [Pricing](https://aws.amazon.com/translate/pricing/) | Synchronous requests with documented input and throttling limits | IAM and request signing are heavier for desktop BYO credentials; AWS data handling must be disclosed. |
| LibreTranslate / compatible endpoint | User supplies an endpoint, optionally with its own key; self-hosting needs no cloud provider key | Software can be self-hosted, but hosting and maintenance are not free | Local-network latency can be good; public endpoint behavior varies | Smaller language coverage and no common managed-service SLA. [Supported languages](https://docs.libretranslate.com/guides/supported_languages/) |

No managed provider found in this research offers unlimited free translation to arbitrary VLC
users without either user credentials or an operator paying for and controlling the quota.

## Recommended product direction

For a reliable cloud provider while keeping the project free of embedded secrets, start with a
BYO-credential design. Google Cloud Basic/v2 is the closest match to the PotPlayer request and
has a documented per-project free allowance. Azure is a strong alternative when low-latency
behavior and documented no-trace handling are the priority. DeepL can be added after its current
Free-plan availability and privacy terms are confirmed for the target release.

Do not make an undocumented Google web-RPC fallback the default. If compatibility testing is
useful, isolate it behind an explicit experimental provider flag, with no guarantee of availability
and no automatic fallback from an official provider.

A shared project relay is a separate product, not a credential-storage shortcut. It would need:

- authenticated users or device enrollment;
- per-user quotas and rate limiting;
- abuse, billing, and provider-outage controls;
- a service endpoint, operational budget, and incident response;
- a clear disclosure that finalized transcript text leaves the device;
- terms/privacy review for the selected provider and jurisdiction.

## Fit with the existing VLC-Whisper architecture

The translation path must remain separate from transcription and playback safety:

```text
Whisper final SEGMENT
        |
        v
bounded translation queue -> worker translation thread -> provider HTTPS API
        |                                      |
        +------------ bounded result queue ----+
                                               |
                              worker main loop / sole IPC writer
                                               |
                                 translated segment -> plugin presenter
```

Required invariants:

- Whisper remains local; only finalized transcript text is eligible for translation.
- No PCM, media, or partial hypothesis is sent to a provider.
- HTTP must not run in Lua, the VLC audio callback, the plugin sender, or the worker IPC
  reader. A dedicated worker translation thread must own blocking provider calls.
- The worker main loop remains the sole IPC writer, so STOP, PAUSE, session changes, and
  caption results cannot be stranded behind a network request.
- Translation results reuse the original `session_id`, `segment_id`, `start_pts_us`, and
  `end_pts_us`. Network latency must never rewrite media timing.
- Source captions remain immutable and visible when translation is disabled or fails.
- A separate translation SPU channel is preferable to revising an already-rendered source cue.
- Pause, seek, media swap, stop, and worker death clear or invalidate queued translations using
  the session epoch. Playback and local transcription continue when cloud translation fails.
- Lua remains command-only: it writes bounded configuration and returns immediately. It does not
  poll, wait, download, or perform HTTP.

The current product and ADR documents allow only explicit worker model downloads as network
egress. Translation is a materially broader cloud boundary and must not be implemented under
ADR-023 by implication. Shipping it requires a reviewed ADR-024 and corresponding updates to
`docs/product.md`, `docs/architecture.md`, `docs/api-contracts.md`, the privacy disclosure, and
the test strategy.

## Credential and privacy rules

Never place a project-owned provider key in the repository, installer, VLC configuration, Lua
state, worker command line, logs, or unauthenticated IPC. The report intentionally contains no
credential examples or secrets.

For BYO credentials, the eventual implementation should use the operating system's credential
store (Windows Credential Manager/DPAPI and the platform equivalent on Linux), passing only a
credential reference or securely scoped secret to the worker. Development tests may use an
environment variable, but production configuration should not expose keys through process
arguments or `vlcrc`.

Translation must be disabled by default. Enabling it must be an explicit user action and must
make the data flow visible. When disabled, there must be zero translation network traffic. Do
not add a persistent local translation cache: transcript persistence is outside the current
privacy boundary. Provider retention and training policies must be disclosed per provider and
plan; local deletion cannot control provider-side retention.

## Failure and latency policy for a future implementation

The request budget should be measured from final segment emission, with a target of at most one
second for a live cue. A late result may be dropped rather than shown stale. Use small requests or
bounded micro-batches, stable request IDs, bounded retries with backoff, and provider-specific
rate limits.

| Failure | Required behavior |
| --- | --- |
| Translation disabled | No cloud request; local source captions are unchanged |
| Missing credential or authentication failure | Keep source caption; expose recoverable status |
| Timeout, quota, rate limit, malformed response | Drop only translated text; never stop playback/transcription |
| Translation queue full | Drop translation work; preserve the source segment |
| Stale session or expired cue | Discard the result |
| Pause, seek, media swap, stop, or worker death | Cancel/ignore translation work and clear its display channel |
| Provider unavailable | Continue local transcription and playback |

## Proposed Step 21a decision record (pending approval)

This report recommends the following scope for ADR-024, but does not accept the ADR:

1. Translation is opt-in and disabled by default.
2. First provider: one official API using BYO credentials; Google Cloud Basic/v2 is the
   closest initial candidate, with Azure as the strongest latency/privacy alternative.
3. The worker owns provider HTTP on a dedicated translation thread.
4. The protocol carries an optional translated-segment message keyed to the original segment;
   source captions are always the fallback.
5. Credentials use an OS secret store; no shared project key is distributed.
6. No disk cache, no automatic provider fallback, and no live cloud calls in CI.
7. A hosted relay and undocumented web-RPC compatibility provider remain separate future
   decisions.

### Open decisions

- Select Google or Azure for the first official adapter.
- Decide whether the UI displays source plus translation or translation only.
- Define supported target languages and source-language auto-detection behavior.
- Choose OS credential-store integration and a user-facing credential setup flow.
- Set the exact live-cue timeout, batch size, and in-memory queue limits.
- Complete the privacy, provider terms, and release-documentation review.

## Repository note

The Step 21a roadmap entry previously linked to
`docs/plans/gui_translation_research_plan.md`, but that file is not present in this checkout.
This report is the concrete Step 21a research artifact; the roadmap link is updated to point here.

## Sources consulted

- Supplied local PotPlayer provider script: `SubtitleTranslate - google.as` (inspected in the
  working tree; intentionally not copied into the repository or committed)
- [PotPlayer GoogleFix implementation](https://raw.githubusercontent.com/veritas501/Potplayer-Subtitle-Translate-GoogleFix/refs/heads/master/SubtitleTranslate%20-%20googleFix.as)
- [PotPlayer GoogleFix repository](https://github.com/veritas501/Potplayer-Subtitle-Translate-GoogleFix)
- [Unofficial PotPlayer translation setup discussion](https://potplayer.org/jiqiao/707.html)
- [Google Cloud Translation authentication](https://docs.cloud.google.com/translate/docs/authentication)
- [Google Cloud Translation setup](https://docs.cloud.google.com/translate/docs/setup)
- [Google Cloud Translation v2 reference](https://docs.cloud.google.com/translate/docs/reference/rest/v2/translate)
- [Google Cloud Translation pricing](https://cloud.google.com/products/translate/pricing)
- [DeepL authentication](https://developers.deepl.com/docs/getting-started/auth)
- [DeepL usage limits](https://developers.deepl.com/docs/resources/usage-limits)
- [DeepL privacy](https://www.deepl.com/en/privacy)
- [Azure Translator service limits](https://learn.microsoft.com/en-us/azure/ai-services/translator/service-limits)
- [Azure Translator pricing](https://azure.microsoft.com/en-us/pricing/details/translator/)
- [Amazon Translate pricing](https://aws.amazon.com/translate/pricing/)
- [LibreTranslate installation](https://docs.libretranslate.com/guides/installation/)
