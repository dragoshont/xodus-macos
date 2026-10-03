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

## Transport

`xodus-cli manage --protocol 1 --state-dir <absolute-private-directory>` (the
shipping bundle may rename the executable to `xodus`). UTF-8 JSONL on stdin and
stdout, LF framing, maximum 1 MiB per frame. No interactive prompts, credentials,
raw upstream errors, URLs or human progress on stdout. Hello must be first.
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
| auth.begin / auth.cancel | gated: approved cancellable broker not established |
| inventory.snapshot | gated: ACCESS_UNKNOWN; no catalog/history ownership substitution |
| product.detail | productData, anonymous public product-ID lookup or explicit cached lookup |
| catalog.search | searchData, **observedPublicProducts** corpus only, partial catalog coverage |
| jobs.enqueue | jobData for catalogRefresh; install variant gated |
| jobs.cancel / jobs.retry | jobData; expectedRevision required |
| jobs.pause / jobs.resume | gated: no pause guarantee |
| jobs.snapshot | jobsData, authoritative set at watermark |
| events.replay | replayData, ordered durable events |
| installed.snapshot | installedData, managementRegistryOnly; no legacy-folder discovery |
| install.plan | gated: no authorized verified package/atomic install engine |
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
All entitlement/installability/compatibility evidence remains unknown unless a
separate authorized/certified source exists; none exists in this scoped version.

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
