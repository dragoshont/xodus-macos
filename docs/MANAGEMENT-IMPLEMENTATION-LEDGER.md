# Launcher management implementation ledger

This branch implements a real native management process, not a full consumer
Xbox launcher release. Requirement IDs follow public app foundation
`4e9c963085d20943fd0ef1c452c67ab40a52ac99`. App/native UI and final adversarial
review remain independently owned. No automatic merge or game launch.

| Requirement | Implemented surface / evidence | Remaining release gate |
| --- | --- | --- |
| AUTH-01 | Existing XAL Microsoft/XboxLive flow, exact native WK callback, inherited private socket with bounded proof handoff, parent-only atomic Keychain commit, flow polling and owned-child cancel/EOF/logout/deadline tests | Real approved account consent, Keychain approval, successful XboxLive account result not executed unattended |
| AUTH-02 | User-only disconnect, device preservation, shared-clone XSTS cache invalidation, storage-error propagation, flowID correlation | Real multi-account/Keychain failure/manual session refresh validation; no package credential conversion inferred |
| LIB-01/02/03 | inventory.snapshot fails ACCESS_UNKNOWN with source/completeness; anonymous metadata never gains ownership | Consumer audience/authorization, paging, never-played PC purchase coverage, expiry and refresh evidence |
| FIND-01 | Persistent observedPublicProducts cache search plus live, user-paged official PC GamePass discovery with explicit per-ID failures, source/freshness, locale resolution and revision-bound cursors | Full-text whole-store query and genuine owned library search remain active requirements; discovery is not a substitute |
| ID-01 | Returned product and SKU IDs preserved, absent/mismatched IDs error, content IDs never become package IDs | Authorized edition/package/version/architecture/language resolution |
| DETAIL-01 | Independent unknown access, unknown/blocked downloadability, unknown compatibility, management-only notInstalled facets | Authorized entitlement/download source and registry-backed live install state |
| COMPAT-01 | No verified/experimental compatibility fabricated, runtimeFingerprint null | Exact signed/distributable runtime/OS evidence and opt-in experimental policy |
| PLAN-01 | Local manifest size overflow, free-space allocation recheck and SHA256 validation implemented/tested; wire plan remains error-only | Authenticated complete MSIXVC file/hash semantics, expanded/staging/rollback estimates, immutable authorized plan |
| QUEUE-01/02 | Actual public-catalog refresh jobs: atomic ordered state/events, snapshots/replay, idempotency, revisions, cancellation, bounded retries, interrupted recovery | Install/download/extract jobs are not connected; local transaction tests are not live package queue proof |
| PLAY-01 | game.launch returns RUNTIME_MISMATCH without spawning game/runtime | Certified exact paired runtime, license/entitlement policy, real supervised process outcomes |
| UPDATE-01 | Local file-set verify/atomic promote, retained version re-verification/rollback, interrupted journal recovery with save separation | Authorized package provider, complete extracted manifests and real install/update integration |
| REMOVE-01 | UUID-owned uncommitted staging disposal refuses escapes/unexpected files and leaves saves | User-confirmed live installation removal and separate save policy; wire removal remains gated |
| SETTINGS-01/PRIV-01 | Typed bounded diagnostics counts, no accounts/paths/tokens/URLs, no log subscriber in management/helper, plaintext fallback refused | Independent final public-source/privacy review and manual export/UI validation |
| OFFLINE-01 | Explicit cached public detail/search and durable job/management snapshots | No offline access authorization or runtime launch is advertised |
| RELEASE-01 | Public versioned schema, deterministic sanitized fixtures, native debug build under isolated tooling | Signed/notarized distribution, licenses/dependency audit, runtime/ownership proof and final coordinator review |

## Source-based boundaries, not blanket runtime excuses

- `api/displaycatalog.rs` provides public product-ID lookup. The new bounded
  variant limits response size/time and refuses redirects. It does not discover
  a global full-text catalog or a complete owned library.
- `auth.rs::start_new_session` returns a XAL TokenStore scoped to XboxLive. The
  new native callback deliberately reuses that flow/state validation. Existing
  `package.rs::get_packages` instead requires legacy device/user SOAP tokens,
  gets XSTS for `http://update.xboxlive.com`, and unwraps several failure paths.
  `license.rs` uses `www.microsoft.com`, device license keys and consumer content
  licensing. No source-proven XAL-to-legacy/broker conversion exists.
- `PackageFile` contains FileSize/FileHash/KeyBlob/CDN paths, but this fork has no
  FileHash algorithm/encoding validation or authoritative expanded-file space
  calculation. No signed URL/key blob enters management results/state.
  An anonymous live DisplayCatalog lookup of public product `9NBLGGH2JHXJ`
  exposed PC MSIXVC candidate metadata, but its Version was `"0"`, Hash and
  HashAlgorithm were empty, and MaxInstallSizeInBytes was zero while the download
  bound was nonzero. These are not authoritative immutable version/digest or
  expanded-space estimates. BCP47 `en-us` casing is normalized by comparison.
  Public package IDs remain candidates under unknown installability.
- Existing streaming extraction writes/truncates active paths directly and
  reuses identical whole-file hashes/redownloads changed files, not block deltas.
  It has no failure-safe ownership registry, atomic version promotion, safe
  removed-file reconciliation or exact runtime pairing. Management never calls
  that path. Its explicit license/key-count/space failures now return CLI
  failure instead of unconditional success.
- The local staged-file primitive is real file IO with durable journals and
  verified promotion, but remains independent from consumer package providers.
  No fixture manifest, authorization, installation or compatibility enters the
  live adapter. There is no live install success endpoint in protocol 1.0.

## Reproducible native verification

Tooling root: `/Users/dragoshont/xodus-app-tooling/launcher-management-20a5b11d`.
Source, target, schema virtualenv and test temp roots all stay in this scope.
Existing Rust 1.98, CLT macOS 27, `/opt/homebrew/bin/protoc` and cmake are reused.
Compilation is capped at two jobs. No Xcode installation/security/service or
private-runtime changes. No real consent, game restart, DLL copying or tokens
in files/logs/fixtures.

```sh
export PATH="/opt/homebrew/bin:$PATH"
export PROTOC=/opt/homebrew/bin/protoc
export CARGO_BUILD_JOBS=2
export CARGO_TARGET_DIR="$HOME/xodus-app-tooling/launcher-management-20a5b11d/target"
export TMPDIR="$HOME/xodus-app-tooling/launcher-management-20a5b11d/tmp"
cargo test -q -p xodus-management --features live -p xodus-cli
cargo test -q -p xodus --lib management
cargo clippy -q -p xodus-management --features live --no-deps -- -D warnings
cargo test -q -p xodus-management --features live,xodus/key-chain-file --lib \
  native_capabilities_refuse_plaintext_fallback_configuration
cargo build -q -p xodus-cli
../schema-venv/bin/python tools/check_management_contract.py
../schema-venv/bin/python tools/smoke_management.py \
  --binary "$CARGO_TARGET_DIR/debug/xodus-cli" --root "$TMPDIR"
```

Native checks cover strict framing/limit/truncation/duplicates, schema decoding,
capability gates, revision/idempotency/cancellation/replay/restart, Keychain
memory facades, exact callback origin, CLI false-success regressions, bounded
path/hash/space checks, interrupted verified/promoted/committed transactions,
rollback integrity and preservation of separate saves/unrelated files.
JSON Schema fixture checks are explicitly separate from live public API checks.
Never hash CRLF worktree bytes as if they were the Git blob.

## Verified results

The isolated arm64 native run passed **48 Rust checks** (6 CLI/callback/private-channel,
8 owned-auth/configuration/handoff lifecycle, 7 adapter, 9 local staging/recovery, 11 framing/state,
2 frozen wire, 5 core token/Keychain-memory checks). The schema validator passed
**68 positive, 10 negative and 4 evidence-edge fixtures**, preserving all nine
foundation evidence definitions exactly.
Management clippy passed with warnings denied. An additional alternate-feature
run confirmed that `key-chain-file` disables all native auth capabilities and
refuses the management token manager before any plaintext backend initialization.

The actual native executable passed a separate anonymous public API/process
smoke check: hello, live product detail, a real catalogRefresh job, durable
snapshot, enqueue idempotency, ordered replay, limited cached search, explicit
inventory/runtime gates, management-only installed snapshot, sanitized
diagnostics and process reconnect with the same persisted session/job.
Every emitted frame was validated against the canonical schema while
XODUS_LOG/RUST_LOG were set to trace; there was no stdout/stderr contamination.
No account consent, Keychain approvals, package downloads, ownership API or game
launch was executed. Source-based/manual release gates in the table remain open.

Published auth baseline (`b9cd60bf51cd4cb8e864318cd0c4316453b4b8df`)
**committed LF bytes** schema SHA256:
`b27dab79d05f985eb39ffbd638cab0dbd69e831fd37d29ace414f517229ba7f2`.
Both schema additions (auth.logout and optional authData.flow) were explicitly
agreed before integration. Fixtures are test-only; the native public smoke
uses the real anonymous catalog provider, not the fixture provider.

## Verified official discovery source (additive integration)

The current official page `https://www.xbox.com/en-US/xbox-game-pass/games`
references
`https://www.xbox.com/en-us/xbox-game-pass/games/js/xgpcatPopulate-2025.js`.
That script explicitly maps `allgamespc` to category
`609d944c-d395-4c0a-9ea4-e9f39b52c1ad`, `platformContext=pc`, and
`subscriptionContext=cfq7ttc0kgq8`. A bounded anonymous request to
`https://catalog.gamepass.com/sigls/v3?id=609d944c-d395-4c0a-9ea4-e9f39b52c1ad&language=en-us&market=US&platformContext=pc&subscriptionContext=cfq7ttc0kgq8`
returned HTTP 200, 12499 bytes, 557 array entries: a header with siglId/title/
description/requiresShuffling/imageUrl, followed by 556 `{id:productID}` entries.
No account credential, purchase, entitlement or active subscription was queried.
The adapter uses only category/IDs, not remote artwork or guessed account APIs.
This is curated PC GamePass public discovery; it does not close global text
search, complete owned inventory, or package authorization requirements.

The coordinator and sole app consumer explicitly agreed `catalog.discover`
request/result/cursor/failure fields before implementation. The published auth
binary remains separately preserved and byte-pinned while this extension is
validated. All unmet end-to-end requirements remain active, not waived.

The discovery/native account-status extension passed **56 native Rust checks**
(6 CLI, 15 library auth/discovery, 8 adapter, 9 staging, 11 state/framing,
2 wire, 5 core credential checks), clippy with warnings denied, and
**71 positive / 11 negative / 4 evidence-edge** schema fixtures. A real
two-page anonymous discovery process check resolved four distinct feed records,
validated every stdout frame, retained unknown entitlement and partial corpus
metadata, and passed the existing public catalog/job/replay/reconnect checks.
The 512-record cache limit/eviction and concurrent auth.status during pending
discovery are tested directly, not inferred from mocked DTO decoding.

Actual source responses for `9NPDN9R45JX4` and `9P8LR42PTRGJ` return `en` for
requested `en-US`. The explicitly agreed optional `resolvedLanguage` exposes
that same-base neutral resolution; unrelated/regional alternatives are rejected.
Malformed cached credential JSON is a typed invalid credential state; real
Keychain/IO denial remains explicit AUTH_INVALID/credentialStoreUnavailable.
No live consent, credential mutation, account inventory or game execution was
performed by these checks.

Canonical discovery schema LF SHA256:
`11d8c9932a2e104a31491d4ecd4922af90c77207bee1cee062d7e7b3c4288898`.
Preserved unsigned developer discovery executable SHA256:
`14dd06466a201ddb77fe7c2d6f9a57bbbb79788f03413989e5158d9053c864ad`.
This pin is an integration milestone, not completion of all user requirements.

The app consumer requested and agreed the additive `failedDiscoveryData`
definition for command-correlated all-failure error details. It preserves
nonempty success pages, requires zero products and 1..16 typed failures, and
retains all strict provenance/cursor fields. The existing sanitized
`fixture-discovery-all-failed` frame is unchanged. Native contract validation
passes 71 positive, 11 negative, 4 evidence-edge and six dedicated failed-page
negative checks (products, empty/excess failures, wrong/missing provenance and
secret-field injection).
Refined canonical LF schema SHA256:
`2ede71d5171cf4dc1659fedfc99187a90d904d9264119a22ee9f94064baef3d2`.
This schema-only refinement does not claim additional live account/install
capabilities, and the earlier developer engine remains byte-pinned.
