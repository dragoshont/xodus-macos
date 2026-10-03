# Management protocol 1.0 (scoped implementation)

License: GPL-3.0-only, like the fork and public app foundation.

Canonical schema: `docs/contracts/management-v1.schema.json`, ID
`urn:xodus:management:1.0`. Generated deterministically by
`python tools/generate_management_contract.py`. The immutable baseline is the
app foundation at `4e9c963085d20943fd0ef1c452c67ab40a52ac99`;
`foundation-v1.schema.json` has SHA256
`f354bcce990e161d822f6db61d62b922e196ada6541e53ecf9f8577bc299b54c`.
Envelope property names and all productEvidence definitions are preserved.
The implementation fills previously unspecified operation payloads/results,
adds negotiated `auth.logout`, and adds explicit validation/state error codes.
The subsequent optional `authData.flow` extension was explicitly agreed by the
coordinator and native app owner before consumer integration.

## Transport

`xodus-cli manage --protocol 1 --state-dir <absolute-private-directory>` (the
shipping bundle may rename the executable to `xodus`). UTF-8 JSONL on stdin and
stdout, LF framing, maximum 1 MiB per frame. No interactive prompts, credentials,
raw upstream errors, signed download URLs, callback fragments or human progress
on stdout. Hello must be first.
Only exact 1.0 is accepted; a future minor requires a new negotiated schema.
Unknown envelope/parameter fields fail closed. Each valid, distinct request ID
receives one terminal result. Request IDs are unique per connection, not durable
idempotency keys. A repeated request ID is an error and must never be used to
submit a mutation again. Malformed frames get sanitized errors with a
transport-reserved `invalid-N` ID when no valid requestID can be extracted.

Capabilities are objects (`command`, `supported`, nullable `audience`, nullable
`reason`), not the foundation's illustrative string array. This explicitly
communicates audience and release gates. `requiredCapabilities` in hello fails
if any capability is unavailable. Runtime fingerprint is null, never a pretend
runtime/version. Account scope is only `default`, without an account identifier.

## Operations and result definitions

| Command | Result `$defs` / availability |
| --- | --- |
| hello | helloData |
| auth.status / auth.logout | authData, native macOS Keychain only |
| auth.begin / auth.cancel | authData with optional flow, owned native WKWebView consent worker |
| inventory.snapshot | gated: ACCESS_UNKNOWN; no catalog/history ownership substitution |
| product.detail | productData, anonymous public product-ID lookup or explicit cached lookup |
| catalog.search | searchData, **observedPublicProducts** corpus only, partial catalog coverage |
| catalog.discover | discoveryData, one bounded public PC GamePass page; not ownership or whole-store text search |
| catalog.query | queryData, real bounded anonymous Microsoft Store game search, resolved live PC metadata and explicit per-ID failures |
| jobs.enqueue | jobData for catalogRefresh; install variant gated |
| jobs.cancel / jobs.retry | jobData; expectedRevision required |
| jobs.pause / jobs.resume | gated: no pause guarantee |
| jobs.snapshot | jobsData, authoritative set at watermark |
| events.replay | replayData, ordered durable events |
| installed.snapshot | installedData, managementRegistryOnly; no legacy-folder discovery |
| install.plan | gated: no authorized complete package plan provider |
| game.launch | gated: no signed/certified paired runtime |
| game.update / game.rollback / game.remove | gated: no management-installed version to mutate |
| diagnostics.export | diagnosticsData, bounded preview object only; no export file or upload |

All commands have strict parameter schemas, including gated commands. Gated
commands **cannot** return successful plan/launch/update/remove objects in this
version. No install plan or install-job success schema is promised before there
is an implementation. `installationRecord` is the future registry entry type,
not a claim that existing CLI installations have been imported.

Catalog search is local matching by title/productID over public records fetched
by this adapter, restricted to requested market/language and PC candidates.
Empty query lists this limited corpus. It never searches an owned library or
the global Microsoft catalog. Cursor pins cache revision and query scope;
cache changes invalidate it explicitly. Cached metadata cannot authorize a job.
`productID` is the public Store ID, `editionID` is the returned SKU ID. Missing
SKU IDs are errors, never fabricated editions. Content IDs are not package IDs.
Entitlement and compatibility remain unknown. Installability is unknown for a
public PC candidate and blocked when the returned edition has no PC package;
neither is downloadable without a separate authorized package source.

## Native account consent

`auth.begin` has `{"accountScope":"default"}` params. Here `default` means the
single **isolated launcher profile**, not the ordinary CLI/service account.
The existing native Keychain TokenBackend is reused with service
`Xodus Management Service`; normal CLI/service `Xodus Service` entries are never
read, imported, copied, overwritten or logged out by management.
It accepts only signed-out state and spawns one owned native WKWebView worker,
reusing the actual CLI NativeTokenBroker/SOAP provider (`InlineLogin.srf`,
client ID `000000004424da1f`, XboxLive broker scope and Passport.NET/tb request).
Required legacy ticket identity is the response's `http://Passport.NET/STS`
KeyName, not the request's `/tb` address. No arbitrary scope, pasted token or
callback URL is accepted over management transport.

The result is authData with optional `flow`:
`{"flowID":"opaque-uuid","state":"pending","error":null}`. The client explicitly
polls `auth.status` and may send `auth.cancel` with that flowID. Terminal flow
states are completed/cancelled/failed. Starting consent is not signed-in success.
No automatic consent window or Keychain-approval clicking is performed.

Read-only `auth.status` explicitly disables native Security interaction and
runs outside the public actor with a two-second deadline. A blocked/unapproved
read returns `AUTH_INVALID` with `details.category:credentialStoreUnavailable`,
not a permission dialog, signed-out fallback or an empty account. Late read
results are discarded; they cannot modify credentials. Explicit preparation,
parent commit and logout also run outside the actor, so public discovery/jobs
remain dispatchable while the user responds to an intentionally initiated
Keychain prompt. The ordinary CLI process's interaction policy is unchanged.

Complete validated user/SOAP/device proof is committed by the parent in one
native Keychain entry (`management-store-user`), paired with the matching flowID.
Existing package/license TokenManager getters consume this complete bundle,
without a conversion from XboxLive XAL tokens or a partial user-record write.
Validation requires nonempty user/device fields, structured Passport encrypted
tickets, valid lifetimes, and the device's exact bounded version-4 HMAC proof.
`credentialPresent` is cached proof presence/expiry (the earlier user/device
expiry), not fresh endpoint authorization; entitlementAuthorized stays false.
Package, PC purchasing-account identity and consumer inventory authorization
are not inferred. A PC Microsoft Store buyer may differ from the Xbox player.

The child has null stdout/stderr and no initialized tracing/log subscriber.
Its stdin is an inherited anonymous private Unix socket, not the management
stdin pipe. A bounded length-prefixed bootstrap carries only this profile's
existing device material, then a bounded result returns complete proof.
The child uses an in-memory TokenBackend only; it never initializes or writes
native credential storage. Device provisioning/reauthentication occurs only
inside explicitly initiated sign-in, never hello/status. Missing device material
can be provisioned; corrupt, denied or inconsistent reads are explicit failures,
not permission to replace a device. Broker bodies and signing-state/RSA inputs
are bounded/checked, with typed propagated errors rather than panics.
The parent alone
checks the still-pending matching flow and commits the complete Keychain entry.
It never writes plaintext token files, tokens to argv, a public credential pipe,
or callback fragments to logs. Only a nonsecret flow UUID is passed in argv.
The management webview is nonpersistent/incognito, rejects untrusted navigation
and IPC origins, and preserves one private WK cookie context across bounded
inline-consent continuations. Ordinary CLI cookie behavior is retained.
Closing the window records cancellation. A 10-minute timeout kills the owned
worker. Cancellation waits for that child to terminate before acknowledging;
if the parent's matching Keychain commit already completed, the result
reports completed rather than pretending to undo it. Once an atomic parent OS
commit/disconnect has started, cancel/logout return `INVALID_TRANSITION` until
its outcome is reconciled, rather than falsely acknowledging cancellation of an
uncancellable native storage call. Flow remains pending while that explicit
prompt is active. Transport loss during a started mutation reports uncertain
account outcome and requires status reconciliation, never confirmed cancellation.
No late child callback can
commit after the cancellation acknowledgment. Foreign/stale flow IDs are errors.
EOF, broken output and logout terminate/wait the owned child. Logout invalidates
the pending flow before removal, removes this profile's user entries, invalidates
all process-local cached XSTS audiences, and preserves its device identity/license.
Before removing the combined bundle, logout retains its device material in
separate same-profile entries; retention failure leaves the user bundle intact
and reports an error. No unrelated Keychain service is touched.
Keychain failure is an error, not signedOut. Live account consent remains a
manual release validation gate; automated checks use memory-only fake proofs.

## Public PC discovery extension

All-failure `PACKAGE_UNAVAILABLE` error details use `$defs.failedDiscoveryData`:
the same strict source, scope, provenance and cursor fields as successful
discovery, but no products and at least one bounded typed failure. Successful
`discoveryData` still requires a nonempty product page. Error details are
validated in the correlated discovery-command context, not accepted as arbitrary
safe payloads merely because they arrived inside an error envelope.

## Public Microsoft Store query extension

`catalog.query` is explicitly agreed with the native consumer. Params:
`{query,market,language,limit:1..16,cursor:null|string}`. Query is 1..256 Unicode
characters, no controls or all-whitespace input; its result echoes the exact
requested wire string, without implicit trimming. The UI can trim before sending.
Existing `catalog.search` remains the observed-cache operation.

`queryData` returns corpus `publicMicrosoftStoreSearch`, partial completeness,
source `MicrosoftStoreEdge:v9.0/searchResults`, live freshness/checkedAt, exact
query, bounded products/failures and nextCursor. Products require live resolved
Windows.Desktop/pcCatalogCandidate evidence; a console-only source card is not a
PC result. Requested market/language remain unchanged and `resolvedLanguage`
exposes neutral-locale resolution. Visibility/PC metadata are not ownership,
authorized download, subscription or Mac compatibility.

A real zero-source-match page is an empty success without failures. Otherwise
all failed metadata resolutions return typed `PACKAGE_UNAVAILABLE` details
validated separately against `failedQueryData` (zero products, 1..16 failures).
Failed items remain visible. No error-details blob becomes implicitly trusted.
Sanitized fixtures include mixed failures, neutral locale, actual-zero shape,
all-failure details, deadline/cancellation and console-only schema rejection.

The verified production source begins at anonymous HTTPS
`storeedgefd.dsx.mp.microsoft.com/v9.0/pages/searchResults` with market, locale,
deviceFamily `windows.desktop`, query and mediaType `games`. Its server-provided
next URI can use the same host `/v9.0/search`, with an opaque server cursor.
`productFamilies=games` is optional: the actual server omits it on later
continuations; any supplied value must still be `games`. Media type, exact query,
market/language, device family and `facets=false` remain mandatory and validated.
No redirects, credentials, cookies, CardActions or arbitrary URLs
are executed. A bounded `q1-` opaque cursor binds exact query/market/language,
validated source URI, unconsumed source-page offset and observed ID revision;
an 8-item client page cannot discard the source page's remaining 12 cards.
Cursor is public metadata, not a credential. Invalid/changed scope is rejected.

The implementation budget is one source page (1 MiB/10 seconds), at most four
metadata workers (8 seconds each), 30 seconds whole page, 896 KiB output, and
at most 16 attempted IDs. EOF cancels pending work; no per-query wire cancellation
is promised. The real public provider is connected and advertised independently
from cache-only search and PC GamePass discovery. Unsupported alternate providers
retain a false capability with typed actionable failure. Fixtures are not live proof.

`catalog.discover` is an agreed additive 1.0 extension. Parameters are
`{"market":"US","language":"en-US","limit":8,"cursor":null}`; limit is 1..16.
Its result contains `corpus:"pcGamePassDiscovery"`, `completeness:"partial"`,
`source:"MicrosoftGamePassSigls:v3"`, `checkedAt`, `freshness:"live"`,
`corpusRevision` (lowercase SHA256), `products` (existing productRecord),
`failures:[{"productID":...,"error":WireError}]`, and `nextCursor`.
Every attempted ID appears in products or failures; metadata failure is never
silently omitted or replaced with cached/fabricated identity. If every lookup
fails, the request returns PACKAGE_UNAVAILABLE with this same page shape in
`error.details`, not a successful empty page.

Cursor `d1-<revision>-<nextOffset>` pins ordered feed IDs, corpus, market and
language. Feed/scope changes yield REVISION_CONFLICT; restart without a cursor.
Malformed/out-of-range cursors yield INVALID_REQUEST. The first-page feed is
not a promise of whole Xbox Store or owned-library completeness.

One user-requested page has a **30-second total budget**, including a 10-second
feed timeout. Each metadata lookup has an 8-second budget; at most four metadata
lookups run within a page, and the live provider shares four HTTP permits across
catalog operations. Remaining/failed lookups are explicit NETWORK_UNAVAILABLE
failures. Feed size is capped at 512 KiB/2048 IDs; successful page metadata at
896 KiB, with explicit LIMIT_EXCEEDED failures rather than oversized stdout.
Discovery is asynchronous and cannot hold the management actor during network
IO; auth/status and snapshots remain dispatchable.

Successful records atomically seed the existing observed-products search cache.
Discovery retains at most 512 cached records, evicting the oldest prior public
metadata when necessary (never jobs, installations, account or saves); this
advances cacheRevision and invalidates old cache-search cursors. No background
full-feed walk, artwork download, ownership or subscription promotion occurs.

productRecord's optional `resolvedLanguage` records the returned source tag.
`language` remains the requested query/cache scope. Mapping prefers an exact
BCP47 case-insensitive match, then a same-base neutral language (for example,
en for en-US); unrelated/regional alternatives are rejected, not hidden.

## Persistence, snapshots, replay, cancellation

One private state directory, one process lock, one versioned atomic durable
state file holds the session ID, watermark, jobs, idempotency keys, observed
catalog and bounded ordered events. A session ID survives restart. No tokens or
account identity are written to this directory. Wrong version, malformed state,
symlink or lock collision is an explicit error, never an empty-state reset.

Catalog refresh: queued -> running -> completed/failed/cancelled. Terminal
states never revive due to late results. Retry is allowed only for retryable
failures, maximum three attempts, same job ID with a higher revision.
Cancellation of queued/running work aborts the network future and commits the
terminal cancelled state before acknowledging; completed work cannot be
cancelled retrospectively. Repeated cancellation of cancelled work is
idempotent. Startup marks interrupted queued/running work as recoverable failed,
without automatically touching the network or credentials. Enqueue idempotency
key reuse with the same canonical params returns the same job; changed params
return IDEMPOTENCY_CONFLICT.

Every job change and its event/watermark are committed together before emission.
Sequences start at 1. Keep at most 4096 events and 256 jobs; jobs are not silently
evicted. Replay after an unknown session, too-old sequence or future watermark
returns EVENTS_EXPIRED; take a jobs.snapshot then replay strictly above its
watermark. Replay pagination returns hasMore and the current watermark; when
hasMore is true continue from the last returned event sequence, **not** watermark.
Duplicates can arise across reconnect/replay; deduplicate by sessionID/sequence.
Live event delivery is supplementary; a missed event is recoverable via replay.
EOF or broken stdout is not evidence of pending-job completion.

## Isolated local transaction primitive (not a live install provider)

`crates/xodus-management/src/staging.rs` implements real bounded file IO,
SHA256/size verification, durable prepare/verify/promote journals, atomic
registry promotion, retained version rollback and explicit interrupted-work
recovery. Saves are separate from staged/versioned directories. Disposal is
limited to the exact UUID-owned uncommitted directory and refuses symlinks,
hardlinks or unexpected files. Paths are bounded ASCII relative manifest paths;
Unicode/normalization and encrypted package extraction are not guessed.

This component is **not wired to successful management install/update/remove
operations**. It accepts caller-supplied local manifests with explicit SHA256;
it cannot establish ownership, license policy, the upstream FileHash algorithm,
complete expanded-file manifests or runtime pairing. Filesystem tests run on
synthetic bytes in an isolated test root. They are evidence of local transaction
correctness, not evidence of a working Xbox package installation or gameplay.
The management registry remains empty by construction until an authorized
complete package adapter is implemented; legacy folders are never imported.

## Sanitized fixtures

`docs/contracts/fixtures/management-v1/positive.json` and `positive.jsonl` cover
every request, every implemented success data shape, errors and a cancelled
job event. `negative.json` contains named schema-negative frames.
`evidence-edge.json` distinguishes purchased/unavailable, expired subscription,
installed/revoked and experimental evidence. All identifiers and dates are
synthetic. Fixture-only facets never enter live catalog or authorization state.
JSON Schema validates frame shape; Rust tests separately cover temporal replay,
revision, persistence and state transitions. Success interpretation is
request-correlated: the app must decode the result shape for its command.
