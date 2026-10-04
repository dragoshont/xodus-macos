# Launcher management implementation ledger

This branch implements a real native management process, not a full consumer
Xbox launcher release. Requirement IDs follow public app foundation
`4e9c963085d20943fd0ef1c452c67ab40a52ac99`. App/native UI and final adversarial
review remain independently owned. No automatic merge or game launch.

| Requirement | Implemented surface / evidence | Remaining release gate |
| --- | --- | --- |
| AUTH-01 | Existing NativeTokenBroker/SOAP provider, required Passport.NET/STS ticket, isolated launcher Keychain profile, memory-only checked device preparation, bounded private bootstrap/result, parent-only atomic user/device proof commit and cancellation/deadline tests | Real user-mediated sign-in and Keychain approval, issued store proof and live package authorization not yet executed |
| AUTH-02 | Same-profile user-only disconnect with device retention, no default CLI/service account access/import/logout, shared-clone XSTS invalidation, storage-error propagation and flowID correlation | Real multi-account/Keychain failure/manual refresh validation; no complete consumer library inferred |
| LIB-01/02/03 | inventory.snapshot fails ACCESS_UNKNOWN with source/completeness; anonymous metadata never gains ownership | Consumer audience/authorization, paging, never-played PC purchase coverage, expiry and refresh evidence |
| FIND-01 | Real anonymous Microsoft Store game text query with resolved PC evidence, bounded cursors/per-ID failures; separate observed cache search and official PC GamePass discovery | Complete owned-library search/coverage and real account authorization remain active requirements; public search is not ownership |
| ID-01 | Returned product and SKU IDs preserved, absent/mismatched IDs error, content IDs never become package IDs | Authorized edition/package/version/architecture/language resolution |
| DETAIL-01 | Independent unknown access/downloadability/compatibility; real explicit selected-folder marker inspection with partial observed metadata, unchanged files/state and no invented retail identities | Authorized entitlement/download source, full file verification and registry-backed live install/adoption state |
| COMPAT-01 | No verified/experimental compatibility fabricated, runtimeFingerprint null | Exact signed/distributable runtime/OS evidence and opt-in experimental policy |
| PLAN-01 | Local manifest size overflow, free-space allocation recheck and SHA256 validation implemented/tested; wire plan remains error-only | Authenticated complete MSIXVC file/hash semantics, expanded/staging/rollback estimates, immutable authorized plan |
| QUEUE-01/02 | Actual public-catalog refresh jobs: atomic ordered state/events, snapshots/replay, idempotency, revisions, cancellation, bounded retries, interrupted recovery | Install/download/extract jobs are not connected; local transaction tests are not live package queue proof |
| PLAY-01 | game.launch returns RUNTIME_MISMATCH without spawning game/runtime; separate explicit private native RPS broker with checked real SOAP handler, owned socket/memory regressions and no default/global fallback | Real Store/RPS issuance, public native shim client and certified exact paired runtime, license/entitlement policy, real supervised process outcomes |
| UPDATE-01 | Local file-set verify/atomic promote, retained version re-verification/rollback, interrupted journal recovery with save separation | Authorized package provider, complete extracted manifests and real install/update integration |
| REMOVE-01 | UUID-owned uncommitted staging disposal refuses escapes/unexpected files and leaves saves | User-confirmed live installation removal and separate save policy; wire removal remains gated |
| SETTINGS-01/PRIV-01 | Typed bounded diagnostics counts, no accounts/paths/tokens/URLs, no log subscriber in management/helper, plaintext fallback refused | Independent final public-source/privacy review and manual export/UI validation |
| OFFLINE-01 | Explicit cached public detail/search and durable job/management snapshots | No offline access authorization or runtime launch is advertised |
| RELEASE-01 | Public versioned schema, deterministic sanitized fixtures, native debug build under isolated tooling | Signed/notarized distribution, licenses/dependency audit, runtime/ownership proof and final coordinator review |

## Source-based boundaries, not blanket runtime excuses

- `api/displaycatalog.rs` provides public product-ID lookup. The new bounded
  variant limits response size/time and refuses redirects. It does not discover
  a global full-text catalog or a complete owned library.
- The initial published consent engine used `auth.rs::start_new_session` XAL.
  The store-capable bridge now reuses `commands/login.rs` NativeTokenBroker and
  SOAP ticket issuance directly, not a fabricated XAL-to-store conversion.
  Its isolated launcher profile supplies the exact legacy device/user credential
  getters required by `package.rs` (`http://update.xboxlive.com`) and `license.rs`
  (`www.microsoft.com`). Those endpoint authorizations and checked package
  provider integration still need live proof; possession of a Passport ticket
  is not a successful package/license call or complete purchasing-account library.
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

### Agreed public query contract freeze

The coordinator and app consumer agreed additive `catalog.query`, `queryData`
and strict `failedQueryData`. Public source verification covers anonymous
Microsoft Store Edge v9 initial array responses and same-host `/v9.0/search`
object continuations, 20 cards each, with 40 distinct IDs across two actual
source pages. Card visibility/actions never grant ownership or install access.
The canonical contract now preserves exact query text and source-page offsets,
separate PC product metadata and visible failures. A true source-zero response
can succeed empty; an attempted all-failure response cannot.

This commit freezes types/schema/fixtures only; its negotiated query capability
remains false and returns `CAPABILITY_MISSING`. The native freeze run passed
51 management Rust checks and **77 positive / 15 negative / 4 evidence-edge**
fixtures, plus six failed-discovery and six failed-query detail rejection checks.
An additional direct request-validation check covers exact spaces, whitespace,
controls, page limits, malformed cursors and mandatory cursor presence. Real
query implementation/validation follows independently; fixtures are not live
proof. Schema committed LF SHA256:
`655e1ed31772b35a8526ef5a0986557e7f6de689d5c4925ccde7041bc33b5f29`.

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

### Checked RPS handler and explicit isolated native broker

The Rust service now has a reusable checked handler and caller-owned
`route_management` context, plus the explicit
`--management-socket <absolute-private-Unix-path>` process entrypoint.
It uses only the native launcher profile, an existing local owned 0700 parent,
one 0600 socket, same-UID peers and descriptor/inode-bounded cleanup. No default
socket/profile, symlink, existing-entry replacement, provisioning, plaintext
fallback or implicit consent is permitted. No-argument legacy service is
unchanged and was not started. Client config is agreed as nonsecret raw absolute
`XODUS_RUNTIME_SOCKET`, without global fallback.

Real SOAP device/user issuance is wired with the actual username, strict audience,
type/expiry/size checks, complete user+device tickets and checked ordered or
reordered refreshed-STS parsing. Empty/device-failed/fault/missing/duplicate
responses fail rather than panic or return empty tickets. Native profile is
rechecked before returning, without native credential mutations. Exact audience
echo/cardinality assumptions still require live issuance proof; tests are not
successful endpoint authorization. Existing generic device exchange now rejects
an empty/unexpected response collection without indexing/panicking.

Native scoped run: **113 Rust checks** (70 management, 7 CLI, 13 core,
23 service), plus separate management and service plaintext-fallback refusal
checks, management and service-library clippy with warnings denied, both native
builds, unchanged 79/20/4 fixtures and six/six detail rejection checks. Service
binary-wide clippy exposes the pre-existing unchanged `utils.rs` needless-return
warning; the changed library passes. Actual owned private process checks verify
framing/ping, invalid MSA syntax before any credential getter, continued usability,
empty public stdout and socket cleanup. Actual public detail/jobs/replay/reconnect/
discovery/query and synthetic read-only selected-folder checks also pass.
No valid MSA/account ticket request, human consent, external/private game folder,
entitled package, public/private runtime or gameplay was executed.

Immutable unsigned artifacts under the same owned tooling root:
`artifacts/xodus-service-rps-v1-718973decb45201c`, SHA256
`718973decb45201c8a4a924650432c2d1f3b375e2b574c41f82ffe29edf7c3b6`;
`artifacts/xodus-cli-rps-v1-0a09062e2e132183`, SHA256
`0a09062e2e1321833d28b59ef5116620a336cc794eec85992a6d3614753e77f8`.
Schema remains c95c3fab; no protocol command or launch capability changed.
The coordinator reports retained reviewer closure of R01-R07 and approval of
the inspection primitive/adapter **source/regression only**. This new RPS/broker
delta still requires the same retained reviewer; runtime/client/account proof
and all product release gates remain active.

### Connected native read-only selected-folder inspection

`installed.inspect` now dispatches off the actor and returns the agreed exact
shape/path. Five-second result deadline and four owned read permits bound work;
timed-out reads hold their permit until native IO finishes, with no late result,
registry mutation or file writes. Five isolated primitive/deadline checks pass.
The actual native JSONL process inspects a synthetic marker only under its own
private test root, proves the exact 196-byte digest/observed version, unchanged
marker and management.json bytes, typed missing/alias/malformed errors and a
working subsequent request. Registry snapshot remains empty/management-only:
marker observation never registers a game or fabricates ownership/launchability.

Native management checks total 70; the separate unchanged CLI/core suites bring
the scoped count to 89. Clippy and 79/20/4 fixtures plus six/six failed-detail checks
remain clean. Real public query/discovery/job/replay/reconnect checks still pass.
The separate plaintext-fallback refusal check also passes. Immutable unsigned
developer engine:
`artifacts/xodus-cli-inspection-v1-da548dd5abe4c32d`, SHA256
`da548dd5abe4c32dc17035817d1a809a31c8eb19e615f26dad079a245cf72178`.
This replaces 35f0 for native app integration and includes R05/R06/R07 corrections.
It is not a certified runtime or signed/distributable release.
No external/private/user game folder, credential/consent, entitled package or
runtime was accessed. The frozen inspection schema remains
`c95c3fabdf114f89329d2361e76421e7b47be4c113f56c2381e64d437e44f749`.
Inspection primitive and adapter deltas require retained reviewer closure.

### Query corrections XODUS-R06 / XODUS-R07 and inspection contract freeze

R06 now bypasses cache persistence for genuine successful source-zero query pages.
An actual `serve`/completion regression returns empty success for both initial
and terminal-continuation cases, checks byte-identical management.json and unchanged
cache revision, then succeeds on another request through the **same connection**.
R07 compares decoded server cursors after strict scope validation; optional-family,
parameter-order and equivalent-percent-encoding changes cannot disguise repeats.
Independent variants are rejected before emitting another client cursor; a new
cursor with legitimate omitted family is accepted. Retained reviewer closure
remains necessary; no random/fuzzy live query is called source-zero evidence.

The native consumer agreed strict `installed.inspect`/inspectionData. The bounded
primitive is implemented with descriptor-relative no-follow regular reads,
identity rechecks and exactly 196 metadata bytes, excluding signatures/key material.
Header GUIDs/observed version do not establish retail identities, verified files,
ownership or runtime compatibility; capability remains false during the standalone
freeze. Four isolated primitive checks pass; no external/user/private-runtime
folder or credentials were inspected. libc 0.2.189 is already locked/cached;
the manifest adds only its direct management dependency edge.
Native management checks total 69; fixtures are now 79 positive / 20 negative /
4 evidence-edge, plus six failed-discovery and six failed-query detail rejects.
Canonical committed-LF schema SHA256:
`c95c3fabdf114f89329d2361e76421e7b47be4c113f56c2381e64d437e44f749`.

Combined verification passed **88 scoped native Rust checks**, separate
plaintext-fallback refusal, management clippy with warnings denied and the real
public detail/job/replay/reconnect/discovery/four-page-query smoke. Corrected
unsigned developer engine:
`artifacts/xodus-cli-query-review-v1-35f0d3d4271b4cee`, SHA256
`35f0d3d4271b4ceecb29ab2f767cd0d702a1ac48cd936383a962119165a382fe`.
Read-only public contract bundle: `artifacts/contracts-inspection-c95c3fab`.
All artifacts remain under the isolated launcher-management tooling root.
This pin supersedes 58f5 for the confirmed query defects; no live inspection
adapter, account consent, authorization or game/runtime action is claimed.

### Real public Microsoft Store query

`catalog.query` is now backed by the anonymous production Microsoft Store Edge
v9 source, not cached title filtering or fixture DTOs. Initial array and
subsequent object pages are parsed explicitly; source `Index=-1` and card
`TypeTag=app` do not become invented game/platform identities. Server next URIs
may omit `productFamilies` on later pages; that known field is optional and
constrained to `games` when present. All other host/HTTPS/path/query/locale/
device/media/facets/cursor constraints remain checked. No redirects or remote
actions execute.

The shared bounded DisplayCatalog resolver now serves discovery and query.
Query requires real Windows.Desktop package-candidate evidence; console-only,
failed, timed-out or response-budget-exceeding metadata stays visible per ID.
All-failed attempted pages are typed errors, not empty successes; genuine
source-zero behavior is covered separately by a fixture/unit test, not claimed
as observed for arbitrary nonsense terms. The Store can return fuzzy suggestions.

The actual native process check preserved source positions **8/8/4/8** across
two source pages: **28 distinct attempted IDs, 7 resolved live PC records and
21 explicit failures**. Every stdout frame validated against the unchanged
frozen schema; foreign and malformed cursors were rejected. Real product lookup,
catalog-refresh job/idempotency/replay/reconnect and two-page PC discovery also
passed. No account consent, entitlement call, package download or game executed.
The final combined native run comprises 82 scoped Rust checks plus the separate
plaintext-fallback refusal check, clippy with warnings denied, and 77 positive /
15 negative / 4 evidence-edge fixtures plus six failed-discovery and six
failed-query detail rejection checks. Retained reviewer delta closure is required.

Unsigned developer engine, preserved read/execute-only:
`/Users/dragoshont/xodus-app-tooling/launcher-management-20a5b11d/artifacts/xodus-cli-query-v1-58f5b80f253d8ee1`.
SHA256 `58f5b80f253d8ee199dc193d3a31cbd1571b641ce309f81bf0430910a6982e83`.
It includes the R05 capacity correction and still advertises no certified runtime.
Schema bundle `artifacts/contracts-query-9d024ae` contains read-only LF artifacts
and SHA256SUMS; schema SHA256 is unchanged
`655e1ed31772b35a8526ef5a0986557e7f6de689d5c4925ccde7041bc33b5f29`.

### Confirmed adversarial finding XODUS-R05

Registry preparation previously lacked the loader's aggregate 256-installation
and 4 MiB serialized-byte limits. Preparation now checks the exact prospective
registry before creating a journal/staging root; commit checks it again before
promotion or replacing registry-selected active state. The authoritative registry
writer (including rollback/open) enforces the same shared limits. Durable JSON
writes cannot exceed their shared loader byte budget.

All **11 native staging regressions** passed, including 256 real tiny installation
commits, clean rejection of both preparation and a previously prepared 257th
authoritative commit, byte-identical prior registry and successful reopen.
Exact prospective 4 MiB commits/reopens; 4 MiB plus one byte fails before
promotion, leaves the verified staged transaction recoverable, and preserves the
previous active files and separate saves. Test-only sized registry fixtures never
enter the live management adapter. Retained final reviewer closure remains required.

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

## Isolated store-capable native sign-in bridge

The coordinator and app consumer explicitly agreed launcher-profile isolation:
`Xodus Management Service` reuses the existing native TokenBackend implementation,
while ordinary `Xodus Service` credentials and CLI browser-cookie behavior are
unchanged. No implicit CLI account import or logout occurs. The helper uses the
existing NativeTokenBroker/SOAP issuance provider with an in-memory backend and a
private device bootstrap. The parent commits one complete validated user/device
bundle only for its still-active flow. Logout first retains same-profile device
material, fails without deleting the bundle if retention fails, then disconnects
the user and invalidates shared-clone XSTS caches.

Checked device preparation distinguishes genuine missing identity from corrupt,
denied or inconsistent storage; it never provisions after a read failure.
Malformed broker status/body/proof, signing-state versions, license lengths and
RSA blob lengths/inversion now propagate explicit errors rather than panic or
continue with false success. SOAP HTTP faults remain typed fault responses (for
real inline-consent continuation); non-success credential responses cannot
become issued proof. The management WK context is nonpersistent and retained
across bounded inline continuations; its IPC origin comes from the actual WK
message frame URL in pinned Wry 0.56.1, not an untrusted payload field.

The final isolated native run passed **69 Rust checks** (7 CLI, 20 management
library, 8 adapter, 9 staging, 11 state/framing, 2 wire and 12 core credential/
device/license checks) plus the alternate-feature plaintext-refusal check.
Management clippy passed with warnings denied. CLI clippy passed with only the
existing `collapsible_if` and `too_many_arguments` lint classes allowed
(`extract_eappx.rs` and legacy streaming signatures); service compilation passed,
but broad service clippy still finds its unchanged `utils.rs` needless return.
No unrelated style changes were made.
The 71/11/4 fixture checks, six strict failed-discovery detail negative checks,
and actual public detail/job/replay/reconnect/two-page discovery process checks
all passed against the newly built executable.

Preserved unsigned developer store-auth executable:
`artifacts/xodus-cli-store-auth-v1-889f5a0a36d15289`
SHA256 `889f5a0a36d15289576f61807a3dfb19f15df78cfbb70261b29fc3aae161ea40`.
The shared wire schema remains LF SHA256
`2ede71d5171cf4dc1659fedfc99187a90d904d9264119a22ee9f94064baef3d2`.
**No actual sign-in, Keychain approval or account/package authorization was
executed.** Real issuance and user-mediated consent remain manual validation
gates, not proof supplied by sanitized fixtures. Broader live text search,
selected-install inspection, complete consumer inventory, managed package
lifecycle, runtime pairing and coordinator-owned final review remain active.

The app's actual old-engine GUI startup exposed a native Keychain read waiting
for interaction while the SSH context denied it. The replacement management
backend explicitly suppresses Security interaction for reads; process policy is
restored and default CLI policy is untouched. The already locked
security-framework 3.7.0 dependency is now declared directly for its checked
native interaction guard; no new installed library/version was needed.
`auth.status` has a two-second off-actor deadline with typed sanitized
`credentialStoreUnavailable` failure, while metadata and jobs stay dispatchable.
Explicit preparation/commit/logout use parent-owned account work outside the
actor; a started atomic mutation rejects cancel/logout until reconciliation,
rather than acknowledging a cancellation while the native call could still
commit. EOF during a started mutation is an uncertain-account error, not
confirmed cancellation. Tests exercise slow denied reads, actual process-policy
restoration (without reading credentials), public work during a delayed fake
parent commit and false-cancellation rejection. The replacement GUI and actual
user-mediated sign-in still require app-owner/manual validation.
