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

`auth.begin` has `{"accountScope":"default"}` params. It accepts only signed-out
state and spawns one owned native WKWebView worker using the existing
`xodus::auth::start_new_session` Microsoft/XboxLive flow. The library validates
OAuth state; the navigation callback accepts only the exact HTTPS
`login.live.com/oauth20_desktop.srf` origin/path. No arbitrary scope, pasted token
or callback URL is accepted over management transport.

The result is authData with optional `flow`:
`{"flowID":"opaque-uuid","state":"pending","error":null}`. The client explicitly
polls `auth.status` and may send `auth.cancel` with that flowID. Terminal flow
states are completed/cancelled/failed. Starting consent is not signed-in success.
No automatic consent window or Keychain-approval clicking is performed.

Complete nonempty unexpired XAL sessions are atomically committed by the parent to one native
Keychain entry (`management-xal-user`) through the existing TokenBackend, paired
with a nonsecret flowID. They are **not** the legacy SOAP/Passport credential
used by the CLI's packagespc and licensing providers. `credentialPresent` is
cached credential presence/expiry, not fresh server authorization; it never
sets entitlementAuthorized true. Package and consumer inventory audiences are
not inferred from XboxLive sign-in.

The child has null stdout/stderr and no initialized tracing/log subscriber.
Its stdin is an inherited anonymous private Unix socket, not the management
stdin pipe; only a bounded length-prefixed consent result travels over it.
The child never initializes or writes any credential store. The parent alone
checks the still-pending matching flow and commits the complete Keychain entry.
It never writes plaintext token files, tokens to argv, a public credential pipe,
or callback fragments to logs. Only a nonsecret flow UUID is passed in argv.
Closing the window records cancellation. A 10-minute timeout kills the owned
worker. Cancellation waits for that child to terminate before acknowledging;
if the parent's matching Keychain commit already completed, the result
reports completed rather than pretending to undo it. No late child callback can
commit after the cancellation acknowledgment. Foreign/stale flow IDs are errors.
EOF, broken output and logout terminate/wait the owned child. Logout invalidates
the pending flow before removal, removes user/management-XAL entries, invalidates
all process-local cached XSTS audiences, and preserves device identity/license.
Keychain failure is an error, not signedOut. Live account consent remains a
manual release validation gate; automated checks use memory-only fake proofs.

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
