# Launcher management implementation ledger

This branch implements a real native management process, not a full consumer
Xbox launcher release. Requirement IDs follow public app foundation
`4e9c963085d20943fd0ef1c452c67ab40a52ac99`. App/native UI and final adversarial
review remain independently owned. No automatic merge or game launch.

Publication: branch `dragoshont-xodus-launcher-management`, checked broker source
`97fcbbbe8c024c453555014c8d12a1fbec6202a6`, bounded authenticated JSON source
`4a22b4b14bc24b99b69f7154384da9f6b8211f00` and the additive profile-fencing
delta described below. App-native draft PR creation failed
exactly `GitHub repository dragoshont/xodus-macos was not found`, despite a
successful push and matching remote SHA. No PR URL exists, and no CLI/MCP
fallback was used. Public comparison:
<https://github.com/dragoshont/xodus-macos/compare/dragoshont-xodus-launcher-management?expand=1>.

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
  The existing CLI helpers now propagate missing-credential, exchange, HTTP and
  malformed-response failures instead of panicking. They still are not a
  management provider: the management-profile branches now reconcile one
  complete credential snapshot around provider calls, but authoritative
  authorization/manifest checks and transactional queue
  integration remain necessary.
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

Current retained source-review status: coordinator reports **R01-R19 closed**.
Same retained report 39 closes R19 at exact
`924824ff63938307425e7f7cb87252e3512f48f2`, finding no significant issues in
the reviewed correction. Report 38 found no additional issue in the parent's
0d5 consumer/runner. These are scoped source-review closures, not live account,
production runtime-pair or complete-product approval.
The original retained reviewer was cancelled; no report 40/41 disposition
exists. The coordinator reports that one continuity reviewer
`3babca63-7752-4b60-8301-2cdbb6b847e6` found no significant issues in the
combined diagnostic producer `e60481fc918b4cc2cc599a5db2aba17a04738872` versus
232a and final app consumer 360e versus f680. This separate source-only closure
does not approve the newer device-response wrapper correction below, which
requires another immutable handoff to that same continuity reviewer.
Subsequently, the coordinator reported that the same continuity reviewer
found no significant issues in `bace09c1be95ff35864b8c8593b974b2aeee934f`
versus e604 and the app's da0 consumer versus 360e. The owner's fresh-target
native validation passed 45 selected checks; the earlier ENFILE/metadata
failures below remain historical failures, not successes. Reviewed CLI 342
was separately built and sealed after explicit authorization. None of those
closures or checks approves the newer XML lexical/refinement delta below.
The backend's SOAP nonce delta remains accepted by review 34. Historical
parent-owned R17 discussion below describes its then-open state, not a current
finding. Coordinator
reports that the same retained reviewer's reports 28/29 close the parent's R15
fresh-host fixture at the reported abbreviated pin `b380` and R14 public Windows
runner lifecycle residual at exact
`67adac4d17fa2e26ff610ff9fc0947d9387f6d40`. These are independently owned public
component changes, not edits to this backend's then-current core source
`5805d89fc281367c0bd9161dd8ac86f627576122` or the GUI da548/C95 pins.
Backend R12 remains closed by review 25 at exact 5805 versus
`2d27032471a441934c15d85da78a38414e2adce7`.

The parent reports exact remote verification, 11 native checks, four actual
public delayed-Windows outcomes across five launches, exact native NTDLL absence,
and independent hosted run
[37172888303](https://github.com/dragoshont/xodus-macos/actions/runs/37172888303)
succeeding at 67ad. Those are parent/CI execution evidence, not reviewer reruns
or this worker's execution. The reviewer did not rerun owner, runtime or CI
checks, including this backend's 173 native checks. Closure is source/regression
review only, not full account, installation, Play or licensed-runtime
certification.

Original complete-owned inventory, authorization, immutable planning, managed
installation/update/recovery/removal and licensed gameplay gates remain open.
Successful user-mediated Store issuance remains unproven. The latest
app-owner-reported retry is described below; it is not this worker's execution.
No new provider or default-service initiation is implied or performed.
Historical milestone evidence and then-pending reviews below do not override
this status; genuinely new source deltas still require an immutable handoff to
the parent-owned continuity reviewer.

### XML base64 lexical compatibility and four structure subsites

The sole app owner reports a later, single user-directed attempt with reviewed
bace/da0 and a separately signed CLI 342 failed with
`AUTH_INVALID/devicePreparation/tokenStructureInvalid`, before an observed
native sign-in window. That identifies the converted-token structural guard,
not which condition failed. Its live cause, the earlier collection-wrapper
causality, and the first generic attempt's cause remain unestablished.

Public source contained an independently reproducible XML lexical bug:
RFC 4648-only decoding rejected valid whitespace in XML `CipherValue` and
`BinarySecret`. [XML Schema Part 2, section 3.2.16](https://www.w3.org/TR/xmlschema-2/#base64Binary)
allows exactly XML SP/TAB/CR/LF within `base64Binary`, while preserving the
standard alphabet, padding and padding-bit restrictions. Two native baseline
regressions failed on reviewed bace plus test-only additions, exit 101:
full SOAP parsing into the actual memory-only save route returned
`InvalidBrokerProofAt(TokenStructure)`; a real synthetic AES-encrypted
header/body successfully decrypted but the resulting legacy proof failed the
same predicate. An additional baseline ordering independently exercised
wrapped `BinarySecret` rather than failing first on wrapped `CipherValue`.
Canonical counterparts passed. These fixtures are synthetic, not issued
accounts or successful provider tickets; the live proof was never inspected.

The shared crate-private XML decoder removes only those four characters
from its local decoding buffer, never the original XML or signature input.
It checks the original encoded input against the unchanged 1 MiB SOAP
response ceiling before normalization/allocation. The 64 KiB serialized
credential bound, 4096-byte/version-four device secret, exact STS keys,
expiry, signature verification and nonce-ID/reference guards remain enforced.
Both device/user exchange routes reuse the same checked HMAC-secret parser;
XML nonce and encrypted-header/body consumers use the same lexical decoder.
Non-XML base64, digests, signature algorithms, endpoints, client IDs, licensing
and authorization are unchanged.

The coordinator and sole app owner explicitly agreed **before implementation**
to four more static `devicePreparation` reasons. All previous fourteen,
including `tokenStructureInvalid`, remain valid; the current total is eighteen.
`AUTH_INVALID`, `nativeConsentFailure` and the exact three-string details
shape remain unchanged; no raw length, version, key, XML, HTTP or identity
is reported.

| reason | existing structural rejection group |
|---|---|
| `tokenKindInvalid` | Checked conversion produced a non-legacy token |
| `tokenAudienceInvalid` | Copied or parsed STS key name does not match |
| `tokenCipherInvalid` | Serialized credential exceeds its bound, cannot parse, or has missing/invalid encoded ciphertext |
| `tokenSecretInvalid` | Secret is missing, invalid XML base64, or not the supported 4096-byte/version-four layout |

The shared legacy predicate retains its existing expiry-syntax guard;
conversion already checks future expiry before these issuer-side groups.
Their authored copy does not infer a cryptographic failure or provider rejection.
The older generic structural enum/pair remains readable for older producers.
The private-channel matrix and extra-secret-field rejection include all
eighteen; failure-exit, cancellation, profile and commit fencing are unchanged.

Regression coverage includes unchanged canonical bytes, valid wrapped
XML/secret admission in both response forms, actual shared HMAC derivation,
signed encrypted envelopes (including rejection of changed signed ciphertext),
AES/nonce guards, strict padding/alphabet and NBSP/VT/FF rejection, original
encoded bounds and preserved memory destinations for malformed proofs.
This new source requires the same continuity review. It does not authorize an artifact, provider request or
sign-in retry, and is not evidence of live authentication success.

Owner validation for this delta passed **54 native checks**: seven memory
device, seven response/nonce/signature, six AES/reference, six shared-response/
HMAC, three XML lexical/bound, five private-worker and twenty auth-lifecycle
tests. Core and management strict clippy, CLI clippy with only the two
pre-existing `collapsible_if`/`too_many_arguments` allowances, native CLI/
management check, touched Rust formatting and the unchanged C95 corpus passed.
All 190 public LF files matched the staged source before and after code validation;
the publication-only documentation status update followed without Rust changes.
An initial CRLF validation-script transport failure was corrected and its
accidental owned build directory/empty file removed; it is not a passing run.
The signed synthetic fixture initially lacked namespace declarations; those
fixture-construction failures were corrected before its verification,
tamper-rejection and final 54 checks passed. The contract checker reused the
existing owned schema environment after the default Python lacked jsonschema.
No production artifact, service, account endpoint, credential read, native
window or consent action was used for these checks.

### Earlier agreed static device-proof rejection sites

After the wrapper correction, the coordinator and sole app owner explicitly
agreed to four additive static pairs **before implementation**. The existing
ten remain valid, including `providerProofInvalid` for an older producer.
Details still contain exactly three strings, use category
`nativeConsentFailure`, and are emitted only with `AUTH_INVALID`. The new pairs
all have stage `devicePreparation`:

| reason | exact existing rejection site |
|---|---|
| `registrationProofInvalid` | Decoded device-registration failure or missing required identity/license fields |
| `tokenResponseInvalid` | The checked selector rejects an empty/multiple/fault/unexpected device-token body |
| `tokenProofInvalid` | Checked response conversion rejects token content, audience, type or expiry |
| `tokenStructureInvalid` | The converted token fails legacy/STS/cipher/4096-byte-version-four structure checks |

At that publication, `DeviceProofFailure` was a four-value static enum. The added
`DeviceCredentialError::InvalidBrokerProofAt` carries only that enum; its
Display text remains the same generic proof error. The older
`InvalidBrokerProof` variant and private `ConsentFailure::DeviceResponse`
remain readable and map to the old generic pair. The worker's exhaustive
typed mapper sends the four new private enum values through the unchanged
anonymous bounded handoff, and the parent maps them to the agreed pairs.
There are no new fields, raw reasons, response values, URLs, status codes,
identities, keys, credentials or exceptions. Signing/nonce/decryption failures
remain the existing provider-request category rather than being relabelled
as one of these content/proof sites. This does not identify the live cause.

The registration field checks, response selector, token conversion and proof
predicate are unchanged; only their existing failures become distinguishable.
Two additional memory-only tests check exact registration/token conversion/
structure categories, the unchanged generic Display text and secret-marked
synthetic input rejection. The worker's mapping test now round-trips all eight
typed device errors over its actual private socket writer. The existing parent
round-trip matrix now covers all fourteen pairs, and malformed private-payload
checks include an extra secret-marked field on each new variant and a public
reason incorrectly used as a private enum. Existing cancellation, expiry,
failed-exit, active-flow, profile and commit regressions are retained.

**Historical pre-fresh-target blocker (subsequently resolved):** the shared native host
reported `ENFILE`/errno 23, "Too many open files in system", followed by linker
SIGBUS; this also blocked a one-job, serial-test retry. A separate native CLI
check encountered missing cached dependency metadata. These are recorded
validation blockers, not source findings or successful test evidence. No
unfiltered account-provisioning tests, global file-limit changes, other-process
descriptor inspection or process kills were attempted. Owned bounded compiler
commands exited; no backend helper/service remains running.

Core-library and management strict clippy, touched Rust format checks and the
unchanged C95 contract corpus passed on the final source without test linking.
CLI lint/check and the six device, five worker and twenty lifecycle test runs
remain pending native-host recovery or an explicitly authorized safe native
CI path. The wrapper-only **19** passing checks below predate this refinement
and must not be attributed to the final fourteen-pair source.

At that publication, this was source-only pending the same continuity reviewer and successful
bounded native validation. No new artifact, engine replacement, provider call
or sign-in retry is performed or authorized by this publication. Separate
review closure and explicit artifact authorization remain required.

### Device-authentication single-response compatibility

After source closure and explicit artifact authorization, this worker built
the exact public LF e604 CLI with the existing native, offline, locked,
default-feature profile. All 190 source-archive files matched their Git blobs
and the native mirror before and after compilation. The additive artifact is
`artifacts/xodus-cli-native-consent-v1-47fab28009f9c6c5`, SHA256
`47fab28009f9c6c52294095e5684bfce3f86f45e50ddc98655d2b22150f02c8d`,
79,687,480 bytes, arm64 Mach-O, mode 0500, linker ad-hoc only with no app signing.
Its adjacent mode-0400 provenance JSON has SHA256
`02c9710e56713dedfc80b144dad4f86d5e34903d6a5201e9778836c6b2a4f762`.
Only inert help and an owned private temporary-state anonymous `hello` were
executed: C95, 25 operations, 17 supported, 8 gated, one valid result, exit 0,
no stderr, no runtime fingerprint. Old 57d/9e85 artifacts remain unchanged.
The existing 104 diagnostic checks were not rerun for this artifact.

The sole app owner reports a separate signed copy
`c50a98b3e0afe4ba6a9b883e6f09ef194b481817eea94f5db190fb2123aceaa8`
with final consumer 360e/C95, nine native read-only checks, and one newly
user-authorized native sign-in entry. It failed with the closed tuple
`AUTH_INVALID / devicePreparation / providerProofInvalid` before a successful
account flow was established. This is app-owner evidence, not backend or
reviewer execution. The earlier generic attempt's cause remains unknown.

The exact tuple maps to `DeviceCredentialError::InvalidBrokerProof`, not a
specific cryptographic exception. Its existing rejection sites are:

- decoded device-registration failure or missing identity/license fields;
- an unexpected device-authentication response body;
- failed checked token conversion, including absent proof, type or expiry;
- a non-legacy token or invalid STS/key/cipher/4096-byte-version-four structure.

Network, HTTP, XML decoding, request signing/canonicalization, response
signature, nonce and envelope decryption errors propagated by the provider
instead map to `BrokerFailure / providerRequestFailed`. The observed tuple
cannot distinguish the four proof-rejection sites, establish an AppID,
certificate or consent cause, or prove an underlying signature failure.

Source inspection found one concrete false rejection: device authentication
accepted only a bare `RequestSecurityTokenResponse`, while the existing
checked shared selector already accepts either a bare response or an
**exactly-one** response collection. The production device path now reuses
that selector after the unchanged provider verification/decryption route.
Checked conversion, future expiry, legacy type, STS key, cipher decoding,
4096-byte proof and version-four checks still run before memory persistence.
Zero/multiple responses, faults and undecrypted bodies remain invalid. No
signature, nonce, encryption, endpoint, client ID or credential profile policy
changes; the agreed ten diagnostic pairs and C95 bytes are unchanged.

The unchanged selection/proof checks were first extracted into the production
save helper without changing behavior. A native regression decoded complete
synthetic SOAP for both forms: the bare proof passed, then the identical proof
in a single-response collection failed with `InvalidBrokerProof`; the other
three device tests passed. With shared selection, all four device tests pass.
Fourteen malformed proof cases in both forms and empty/multiple/fault/
undecrypted bodies preserve the prior destination in an explicitly managed
memory backend. Synthetic proof data never enters an account endpoint,
successful runtime reply, native Keychain or real profile.

**19 scoped native checks passed:** four device-memory, four full-envelope
AES/unique-and-duplicate-nonce, six AES/reference, four existing checked shared
response/compact-proof tests, and the unchanged CLI typed failure mapper.
Core-library strict clippy, CLI/management native compile checks and touched
Rust formatting also pass. Unfiltered core tests were not run because the
existing `test_get_xbox_live_dev_token` performs real device provisioning.
Tested LF SHA256: device source
`57b0ece01c1faa42d315862cc490fc0d783d950eb6fb8eabf8ae233aadc642e3`;
shared API source
`9fb045053d40e2c2ae74c10df2267fd90b602f806431bbec7bcac5e92aacbd67`.

This is a verified parser compatibility fix, **not proof that the live
incident used a collection wrapper**. The new correction is source-only
pending the same parent-owned continuity reviewer. No newer production CLI
artifact is authorized or sealed, no account/provider/credential/Keychain
action or retry was executed by this worker, and the failed app is left under
its sole owner's control. The subsequently agreed additive site distinctions are recorded separately
above; they do not change this wrapper-only milestone's evidence.

### Static private consent failure diagnostics

The coordinator and sole app owner report one user-directed native Account
entry using reviewed 2a47/57d, a separate signed copy, and C95. The app showed
its authored generic failed-flow label, but the cached wire error was not
safely observable. **The actual failure cause remains unknown.** No
retrospective credential/page/memory/log inspection, new status request or
retry was performed by this worker. This correction makes a future authorized
failure diagnosable; it does not claim to fix the old authentication cause.

Source traced two concrete diagnostic losses: the worker reduced all
non-cancellation errors to a bare private `Failed`, and the parent poller could
discard a failure handoff when it observed a nonzero worker exit. An isolated
native regression with an empty managed memory backend and an owned `/bin/sleep`
child reproduces the latter on the original source: the handoff is lost before
reconciliation. No auth worker, native account or provider request is run.

The worker now maps errors to the closed `ConsentFailure` enum and sends only
`{"outcome":"failedAt","failure":"<enum>"}` on its existing anonymous private
channel. It never serializes an exception, Debug output, HTTP/XML response,
username, device identity or credential. The existing 256 KiB private frame
bound, complete session proof, anonymous-channel checks and same-executable
worker launch remain unchanged. The original bare `Failed` remains readable
but means stage unavailable.

The parent maps these static failures to the existing `AUTH_INVALID` error and
optional `flow.error.details`, without changing C95 bytes or public error codes.
The exact optional details shape was explicitly agreed with the sole app owner:

```json
{"category":"nativeConsentFailure","stage":"devicePreparation","reason":"providerRequestFailed"}
```

At the original e604 publication, exactly three string keys, the fixed category,
and one of these ten pairs were agreed with the app's typed consumer. The four
first additive pairs above retained all ten and extended the then-current
allowlist to fourteen; the later structure refinement extends it to eighteen:

| stage | reason |
|---|---|
| `privateBootstrap` | `bootstrapInvalid` |
| `clientInitialization` | `clientUnavailable` |
| `devicePreparation` | `credentialStorageUnavailable` |
| `devicePreparation` | `storedCredentialInvalid` |
| `devicePreparation` | `providerRequestFailed` |
| `devicePreparation` | `providerProofInvalid` |
| `deviceProof` | `proofUnavailable` |
| `nativeSignIn` | `pipelineFailed` |
| `storeProof` | `proofInvalid` |
| `stageUnavailable` | `workerOutcomeUnavailable` |

`nativeSignIn/pipelineFailed` deliberately means native UI **or** token-exchange
pipeline failure, not an observed window, endpoint or precise HTTP cause.
Device preparation categories distinguish existing typed storage/credential/
request/proof errors, not individual unobserved provider endpoints. Parent
profile/proof/commit errors retain their existing meanings and absent details;
no worker stage is invented for them.

The consumer accepts diagnostic details only with `AUTH_INVALID`. Missing,
malformed, unknown, mismatched or extra-key details (including a secret-marked
extra key) are discarded and rendered as locally authored stage unavailable.
It never renders arbitrary server messages/details and never automates retry,
logout or profile deletion. The public schema still allows an optional details
object; this closed diagnostic allowlist is an agreed interpretation, not a
schema alteration.

The poller now retains the receiver after an observed worker failure so its
validated failure outcome can be consumed. An observed failed exit cannot
promote a successful session; missing/invalid/unobserved/crashed outcomes
produce explicit `stageUnavailable`, not a fabricated reason. Cancellation
remains `AUTH_CANCELLED`; the existing deadline overrides diagnostic failures
as `AUTH_EXPIRED`. Active-flow, duplicate-begin, cancellation, completed proof,
already-started commit, profile reconciliation and OS mutation semantics remain
unchanged. No secrets move to management stdout or worker diagnostics.

**104 scoped native checks passed**: 29 CLI and 75 management tests (42 library,
8 adapter, 11 staging, 12 state/transport and 2 wire). Eight new tests cover all
ten failure categories over real anonymous private channels, preservation after
failed exit, rejection of success from an observed failed exit, malformed/
unknown/secret-marked private payloads, injected secret-marked worker errors,
typed device error mapping, cancellation/store-proof classification and
duplicate-begin/cancel/already-started commit gates. Existing success, foreign
flow, expiry, storage and mutation regressions remain included.
The two secret-injection regressions also ran with `--nocapture`; captured
stdout/stderr contained none of the injected secret sentinel, XML marker or
ticket bytes. These are isolated payload/output checks, not live provider logs.

```sh
cargo test --offline --locked -q -p xodus-cli -p xodus-management \
  --features xodus-management/live
cargo clippy --offline --locked -q -p xodus-management --features live \
  --no-deps -- -D warnings
cargo clippy --offline --locked -q -p xodus-cli --no-deps -- \
  -D warnings -A clippy::collapsible_if -A clippy::too_many_arguments
cargo check --offline --locked -q -p xodus-cli -p xodus-management \
  --features xodus-management/live
../schema-venv/bin/python tools/check_management_contract.py
```

CLI lint retains only the two previously documented baseline allowances; this
is not a clean strict whole-CLI lint claim. Management strict lint and native
compilation/test build checks passed. The unchanged contract corpus passed.
Tested LF source SHA256 values:
adapter `e3ca748f68e965f3eff6d328702cd906f35db070a39f5c0f6e621f37b026b550`;
worker `1744621b103125ee0f316980cb9fd3ea6df24ed813b532aa07d2e2033b2cef90`.

At its original publication this delta was **source only**, pending retained
review. That reviewer was subsequently cancelled without a report 40/41
disposition; the separate continuity closure and authorized e604 artifact are
recorded above. At original publication no new production CLI binary was
sealed, published, deployed or executed; the held
app/engine pair and historical artifacts remain unchanged. No live auth,
credential read, account/device RPC, runtime service, consent prompt or
package/game action was initiated. A diagnostic engine artifact and any further
human retry require separate reviewed provenance and coordinator approval.

### R19 optimization-safe developer fixture smoke

Retained report 38 reproduced that Python optimization removed all `assert`
checks in the 014d smoke tool. After a production-shaped refusal of the initial
gated probe, `python -O` or `PYTHONOPTIMIZE=1` could continue to the **no-argument**
probe, which would start the unchanged production service's native credential/
device path if the wrong executable had been selected. The Rust fixture gate
and the parent's hash-pinned, always-gated runner are unaffected.

`tools/smoke_empty_runtime.py` now uses unconditional `require` checks that raise
explicit static `RuntimeError` messages. Every pass/fail assertion was replaced,
including binary/root validation, fixture recognition, refusal exit/stdout/
stderr/state, framing/ping, valid MSA zero reply, process liveness, socket mode,
SIGINT exit/cancellation, cleanup and final output checks. A mismatched first
gated refusal always stops before any subsequent probe; optimization cannot
remove this check. The production broker was **never executed**.

The original committed `tools/test_smoke_empty_runtime.py` regression runs the
actual tool with **all process invocations mocked**. Its nine-case matrix covers
production-shaped stderr, wrong exit status and unexpected stdout under normal
Python, `-O` and `PYTHONOPTIMIZE=1`. Each refusal must leave exactly one initial
gated probe, no no-argument call, and no `Popen` attempt. A separate AST check
rejects any `assert` statement in the smoke tool. Both tests are themselves
optimization-safe `unittest` checks.

Before editing the tool, the regression reproduced the exact 014d negative on
Windows and native macOS: normal mode stopped, but both optimized modes recorded
the no-argument call and later startup attempt (mocked, never real). The old
suite failed with six optimized matrix failures plus the AST assertion check.
The corrected suite passes on both hosts when the suite itself runs in normal,
`-O` and `PYTHONOPTIMIZE=1` modes. Reproduction uses the original published script,
not a copied parser or simulated assertion expression.

The corrected real tool also passed five native fixture-only process smokes:
one per interpreter mode, then two optimized replays from the exact published
LF tool/regression sources, against the **unchanged immutable** 014d Rust binary
`empty-management-broker-gated-v1-9e854df1e44042c0`, SHA256
`9e854df1e44042c09067fc2d63ff6e4beeddeb73350c0329059cda28f2d13fb1`.
Hash checked before and after, no rebuild/replacement. Gated refusal, valid
MSA zero reply, ping before/after, exact payload-free output and bounded
SIGINT/socket cleanup all remain enforced under optimization. Existing Rust
tests/clippy/build are unchanged and were not rerun for this Python-only fix.

```sh
python3 tools/test_smoke_empty_runtime.py
python3 -O tools/test_smoke_empty_runtime.py
PYTHONOPTIMIZE=1 python3 tools/test_smoke_empty_runtime.py
python3 -O tools/smoke_empty_runtime.py --fixture-binary "$PINNED_FIXTURE" --root "$TMPDIR"
PYTHONOPTIMIZE=1 python3 tools/smoke_empty_runtime.py --fixture-binary "$PINNED_FIXTURE" --root "$TMPDIR"
```

Corrected LF tool SHA256:
`9d79b40d7515a478221a41331a6fe7efe5532bc248f5be560f6b4340a4e45f52`;
new regression LF SHA256:
`0c41fa0b13e828ebf2fab5facbe4629dde295f5a5e546388295955ed5184b1f3`.
This is only a tool/test/docs correction, not a schema, backend, account,
credential/profile, broker binary, app-engine or runtime change. The coordinator
reports **R19 CLOSED** by the same retained report 39 at exact
`924824ff63938307425e7f7cb87252e3512f48f2`, with no significant issues in the
reviewed correction. The five actual native fixture-only smokes and 136-file
public-LF byte comparison remain this worker's execution evidence, not the
parent's or reviewer's reruns.

Historical immutable 014d source archives remain unchanged and contain the
**superseded unsafe smoke script**. Never execute that historical script,
including under optimized Python. Use only the corrected tool pinned to exact
924824f and its LF digest above; the original 014d/9e85 Rust fixture binary
remains unchanged. Source-review closure neither authorizes a production
profile/service nor replaces any app engine.

Separately, the parent reports additional pure-mock suites passing on native
macOS (18 checks per `-O`/environment-optimization run) and Windows (11 passing,
7 Unix skips per mode), without broker, Wine or account execution. These are
parent-executed checks, not this worker's or the reviewer's execution.

Separately, the coordinator attributes three signed-out public Windows
gaming-COM interactions to parent source
`0d5c610731d168a4c3faa854c01f349ba8b80349` with the final mandatory-gated
014d/9e85 fixture, including the immutable archive. Reported outcomes were
generic E_FAIL, one completion/null handle, PID-bound same-connection ping
before/after, empty-memory banner plus one generic failure, graceful native
SIGINT exit zero/socket cleanup and exact selected ntdll inspection. The parent
reports hosted run
[37185608767](https://github.com/dragoshont/xodus-macos/actions/runs/37185608767)
succeeded. These are **parent/CI evidence**, not this worker's execution or
reviewer reruns; superseded e386/f0d was never parent-executed. No successful
tickets, live account/production profile, wire AuthenticationRequired,
gameplay or complete launcher/review approval is claimed.

### Developer-only empty-account broker fixture

`crates/xodus-service/examples/empty_management_broker.rs` is a standalone Cargo
example, not a deployed service mode or a selectable production profile. It
constructs `TokenManager::with_management_backend(Arc::new(MemoryBackend::default()))`
and calls the unchanged public `isolated::serve` library entrypoint. No
production source, backend, flags, dependency, frozen schema, wire or capability
is changed. The existing native/no-plaintext guard still applies: this fixture
requires macOS without `xodus/key-chain-file`.

The only accepted invocation is:

```text
<empty-management-broker-fixture> --empty-memory-fixture --management-socket <absolute-private-Unix-path>
```

The one lab gate is mandatory and ordered; it is never added to production.
There is no default path/profile or help selector. No arguments, ungated
`--management-socket`, the superseded `--fixture-socket` spelling, misplaced
gate, missing/extra arguments and non-UTF-8 paths fail explicitly.
The unchanged production parser rejects this three-argument fixture invocation
in its exact-two-argument guard, before path conversion, the native-feature
check, `init_secrets`, Keychain manager construction or any service start.
The example's source-order regression checks that actual production guard and
its early `exit(1)` precede initialization. Production is **not executed** by
this test. The smoke first probes the gated missing-path refusal and checks the
distinctive fixture-only usage error before attempting any no-argument test;
a mistakenly selected production executable would reject that first probe,
then fail the unconditional fixture error check without reaching no-argument
fallback, including under Python optimization after the R19 correction above.
Existing endpoint admission validates the selected
normalized absolute UTF-8 path, 103-byte limit, preexisting owned local 0700
parent with no symlinks, absent socket name, same-UID peers, 0600 socket,
eight-connection limit and descriptor/identity-preserving cleanup.
SIGINT cancels the existing broker and joins its owned connections; the
example's signal task is then aborted/joined. It never starts the default or
published production broker.

**Account isolation is by construction, not an empty directory masquerading as
a production credential profile.** Neither native store initialization nor any
Keychain backend or user credential is used. The persistent and ephemeral
backends are empty process-local memory; no profile import, credential seed,
prompt, persistent state or credential write exists. A valid MSA request does
perform one **in-memory absence lookup**. Its missing bundle returns before the
first device/account HTTP call or credential mutation. The unchanged production
route constructs its usual no-redirect HTTP client, but this fixture never
issues an account/device HTTP request or a ticket.

The existing real service regression
`rps::tests::signed_out_management_context_never_falls_back_to_default_credentials`
establishes the internal `RpsError::AuthenticationRequired` result. The actual
XML/frame path deliberately exposes **no authentication-error frame or
HRESULT**: a valid MSA request closes with zero reply bytes and only the existing
generic `Private runtime peer request failed` stderr diagnostic. Stdout remains
empty. A consumer may assert missing response/closure, not an authentication
category on the wire.

Native verification executed **27 service-library checks plus 4 example
argument/source-order checks**, strict service-library/example clippy and the
example build:

```sh
cargo test --offline --locked -q -p xodus-service --lib \
  --example empty_management_broker
cargo clippy --offline --locked -q -p xodus-service --lib \
  --example empty_management_broker --no-deps -- -D warnings
cargo build --offline --locked -q -p xodus-service \
  --example empty_management_broker
python3 tools/smoke_empty_runtime.py \
  --fixture-binary "$CARGO_TARGET_DIR/debug/examples/empty_management_broker" \
  --root "$TMPDIR"
```

The owned native process smoke executes only this example with
`--empty-memory-fixture --management-socket`, including with trace logging
variables. Six gated-fixture runs (initial, three repetitions, sealed-artifact
run and canonical-LF smoke replay) prove missing-gate/no-argument/old-selector
refusal, real ping before and after a valid MSA refusal, zero reply bytes, empty
stdout, exact payload-free stderr, cancellation of a pending partial header by
SIGINT within the five-second test bound, and no remaining socket/state files.
The exact valid XML request is:

```xml
<MSATokenRequest><ClientId>0011223344556677</ClientId><AllowUi>false</AllowUi><MsaFullTrust>true</MsaFullTrust></MSATokenRequest>
```

Wire framing is the existing little-endian `u32 0x58445358`, `u16` message type
and `u16` UTF-8 payload length. Ping 1/2 and request type 3 are unchanged; no
success type 4 is returned for this fixture. Existing per-request budgets,
cancellation, admission and descriptor cleanup are reused, not duplicated.

Additive unsigned native developer artifact (mode 0500):
`artifacts/empty-management-broker-gated-v1-9e854df1e44042c0`, SHA256
`9e854df1e44042c09067fc2d63ff6e4beeddeb73350c0329059cda28f2d13fb1`.
The unchanged native example LF SHA256 remains
`74fda34f8e573bc26cd2bba9486092e23fbf5438988123b64ab9ba80d70a184e`;
the original 014d smoke SHA256 was
`e256444441768e2d77c6064cee9a7a3862d474d63ea8f777456fec6445c14264`,
superseded only by the optimization-safe R19 tool correction above.
This user-directed mandatory-gate correction supersedes the earlier e386688
fixture invocation/artifact; retained prior artifacts are not the current
handoff pin. The native mirror remains aligned to the exact public LF sources,
and Git source archives disable Windows checkout newline conversion explicitly.
The immutable source commit is supplied in the publication handoff. This
fixture delta received separate retained reports 38/39, including the R19
correction closure above; it does not inherit review 34's approval.
The parent owns real public Windows gaming-COM
consumer adaptation and its actual fixture-pair run. No successful fake/real
RPS, Store sign-in, actual credential/license proof, runtime/game execution,
production deployment, GUI da548/C95 swap or full-journey gate closure is
claimed or performed.

### Checked SOAP derived-key identities

Continued tracing of the existing public RPS/device/user SOAP response path
found that collecting derived-key tokens into a HashMap silently overwrote
duplicate untrusted IDs, choosing the nonce by XML order. Empty unused IDs were
also accepted. The production envelope verification/decryption path now
rejects empty or duplicate derived-key IDs before using any nonce, returning
the existing static `InvalidEncryptedPayload` error without IDs, nonces,
decrypted content or tickets in diagnostics. Unique ID lookup/order and the
existing signature policy remain unchanged; this does not require a new
response signature or claim broader signature-policy certification.

Four native regressions build synthetic AES256-CBC encrypted PP headers and
token-response bodies, decode the complete response XML into the production
Envelope model, then call the actual verification/decryption path. They prove
unique IDs preserve both decrypted header/body in either order, identical
duplicate IDs fail, conflicting duplicates fail in both orders, and an empty
unused ID fails rather than being ignored. The synthetic body is marked
`fixture-not-a-ticket`; it is never sent as a successful runtime RPS result or
used for account HTTP. Restoring the old production HashMap collection with
the new tests reproduces one valid pass and three negative failures because
malformed envelopes still decrypt successfully. The corrected mapping passes
all four.

**177 scoped native Rust checks** pass: 54 core management, 26 CLI,
70 management and 27 service. Core/management/service-library strict clippy,
native CLI/service builds, unchanged contract corpus and anonymous management/
private malformed-before-credential broker process smokes pass. These are this
worker's native synthetic/read-only checks, not reviewer reruns, live ticket
issuance, actual audience/authorization proof or paired gameplay.

Additive immutable unsigned developer artifacts:

- `artifacts/xodus-cli-soap-nonces-v1-57d4500d6de922d6`, SHA256
  `57d4500d6de922d644071ff6749b662c56d75f5f4646da2a884a363169373c0b`.
- `artifacts/xodus-service-soap-nonces-v1-8e15fe61881e2006`, SHA256
  `8e15fe61881e200661330243bdb55a810b62d98707711c5d293b5a281523185b`.

Coordinator reports the same retained reviewer's review 34 at exact
`2a47eafc930603773583ce4c1d6be89a2f0ccd60` versus
`99fe345ad8725cfb8d05455b5171b900ddf1681f` returned **"No significant issues
found in the reviewed changes."** This separate source review, not inherited
R01-R15 approval, did not execute the owner's 177 native checks or any account
flow. No parent async/runtime code, GUI da548/C95, deployed broker, protocol
capability, credential profile, consent or provider configuration is changed.

### Parent public gaming API consumer milestone

Coordinator reports the parent published and remote-verified public
`runtime/public-async` source
`f7d8d4eec2eb4aaab96230df29c76c6de2b060ad`, replacing the 23 async stubs in
the exact 44d shim using pinned Microsoft MIT core 7ead73f. Reported evidence
includes four gaming COM checks, three XUserAddAsync/Result malformed/expired/
missing RPS checks, three cached-loader refusals, 20 native source guards and
20 shared ownership checks. The synthetic peer checks ClientId
`0011223344556677`, AllowUi false and MsaFullTrust true; no successful fake RPS
issuance, account HTTP, private profile or Store sign-in is claimed. Parent
reports public core initialization now requests RPS before default HTTP, with
coupled user/provider cleanup ownership corrected.

Parent-owned public artifact pins in `artifacts/public-shim-async-v1` are core
SHA256 `36b74899540eca304a2e51b107aa49cef3379b1c0f934e57b908657a304f1fca`
and gaming DLL SHA256
`7a062d7a837dc5354ba351304ee19246f8f1803f220c30eb4c4373cea5d92f79`.
Coordinator reports exact-head hosted cross-build
[37181433411](https://github.com/dragoshont/xodus-macos/actions/runs/37181433411)
and overlay
[37181433395](https://github.com/dragoshont/xodus-macos/actions/runs/37181433395)
passed. These are attributed parent/CI evidence, not execution by this worker.
Same retained review 33 subsequently opened **R17 MEDIUM** in the parent's
standalone direct-core check: a queue termination callback and CloseHandle do
not synchronize worker return, so FreeLibrary can race code still executing.
The production bridge retains its core module and is unaffected. Parent owns
the correction, retaining helper modules until process exit and rerunning its
scoped checks; no backend action, engine/broker swap or closure is inferred.
This worker does not duplicate the parent runtime/helper changes.
Later pairing must use
public source/pins, explicit-only XODUS_RUNTIME_SOCKET and the 40-second client
RPS bound, never the private/default runtime or a duplicate global service.
No approved app engine/broker replacement, live Store/RPS proof or runtime pair
certification follows from this milestone.

### R12 private download staging at creation

Retained Astra review 19 confirmed that the default `tempfile::tempdir_in`
creation mode is 0777 before umask: umask 0002 produces a group-writable 0775
directory. A same-group peer could replace a named staging file while the
downloader counted/synchronized its original open file and later renamed the
unchecked pathname. Download staging now uses the locked tempfile 3.27.0
`Builder::permissions(Permissions::from_mode(0o700))` at directory creation.
Named files retain tempfile's 0600 creation mode. There is no later chmod,
global umask change or replacement of the transfer/commit algorithm.

The isolated native regression runs its own exact test binary through child
shells with inherited umasks 0002 and 0000; the parent test process's umask is
never changed. A control default tempdir proves the permissive umask is real.
While a synthetic loopback transfer is paused after its first bytes, the test
checks the actual staging directory is 0700 and its sole named file is 0600,
retains an open handle and records the original device/inode, then releases the
rest of the response. The committed destination is a regular nonsymlink file
with exactly that original device/inode, 0600 mode and the same complete bytes
read through the retained handle; the previous destination remains unchanged
until commit and staging is cleaned afterward. Existing failed/truncated/
oversized transfer and partial-body cancellation preservation cases also run
inside each permissive-umask child.

With the new regression and old production creation call, the native test
fails at actual mode 0775 versus required 0700. Restoring the explicit creation
mode passes, including five additional selected repetitions. **173 scoped
native Rust checks** pass: 50 core management, 26 CLI, 70 management and
27 service. Core/management/service-library strict clippy, native builds,
unchanged wire corpus and anonymous management/private malformed-before-
credential broker process smokes pass. CLI clippy passes with only the two
previously reproduced baseline lint categories allowed; no strict whole-CLI
pass is claimed.

Additive immutable unsigned CLI:
`artifacts/xodus-cli-private-staging-r12-v1-76238c81398c1cc9`, SHA256
`76238c81398c1cc9f401a9ca9ced71232473bc756b7b6f40fcada4daf194a454`.
The existing 8065 broker remains unchanged. These are developer artifacts,
not an approved GUI replacement or real package/account/runtime proof.

Coordinator reports retained reviews 16/17 approved b16 profile fencing and
77 SOAP references, source-only; review 21 named the fresh-snapshot pending-IO
race R13, and review 22 closed R13 at 2d270 with no other significant issue.
Coordinator reports the same retained reviewer's review 25 **closed R12** at
exact `5805d89fc281367c0bd9161dd8ac86f627576122` versus
`2d27032471a441934c15d85da78a38414e2adce7`. Creation-time 0700/0600 and the
committed permissive-umask, original-inode/content and preservation regressions
were sufficient, with no significant residual issue. The reviewer did not
rerun the owner's 173 native checks. Coordinator also independently verified
the backend branch's exact 5805 remote SHA with `git ls-remote`.
That review closed R01-R13 for source review only; the subsequent parent
R14/R15 closures and current global status are recorded above.
No additional reviewer is spawned. GUI da548/C95, capabilities and all real
authorization/immutable-manifest/installed-lifecycle/runtime gates remain
unchanged; no accounts, signed packages or games are executed.

### Pending credential mutation fence

The initial shared epoch rejected stamps created before a mutation, but a fresh
snapshot could start after the epoch increment while a blocking backend write/
delete still retained the previous valid bundle. It could then capture that
new epoch and verify the old bundle before the mutation finished. Management
clones now share an active-mutation counter in addition to the epoch. A RAII
guard surrounds management Store commits, logout/removal and user-token
replacement, invalidates the epoch on entry/exit, and keeps fresh snapshots and
verification refused until every nested/overlapping mutation completes.
Failure/unwind releases the counter without reviving any previous stamp.
No blocking lock is held across credential IO; ordinary CLI mutation/cache
behavior is unchanged.

Five additional native regressions pause the actual synthetic backend write/
delete while the old Store bundle still exists, check snapshot/stamp refusal,
then release and verify the resulting state. Cases cover commit, logout with
device retention, an overlapping writer completing while another remains
paused, backend failure, and an owned thread unwind. Channels are bounded and
cannot leave a failed test waiting indefinitely on a blocking worker.
**172 scoped native Rust checks** pass: 50 core management, 25 CLI,
70 management and 27 service. Core/management/service-library strict clippy,
native builds, unchanged contract corpus and actual anonymous management/
malformed-before-credential private broker process smokes pass.

Additive immutable unsigned artifacts:

- `artifacts/xodus-cli-active-mutation-v1-0288a6e55e55ba48`, SHA256
  `0288a6e55e55ba48553b4c05081e497aeee6dbe08bdf5c13b0d0d482b2b1e431`.
- `artifacts/xodus-service-active-mutation-v1-806543487763031b`, SHA256
  `806543487763031bf15c022ae5475dbe9f53c4dd30940c0bfbfe9c604885f23e`.

This protects clones sharing one manager, not independently constructed
managers or other processes, and cannot undo an already-sent RPC. External
profile contents still require the full-bundle recheck. No protocol/capability,
GUI da548/C95, actual account/authorization, immutable-manifest/lifecycle or
runtime release gate changes. It is an independent correction after d8d610a.
Coordinator reports retained review 22 closed the separately named R13 at
2d270, source-only, with no other significant issue. This does not close the
independent R12 staging finding, subsequently closed in review 25 as recorded
above, or approve real credential/runtime execution.

### Explicit CLI transfer failures and staged byte-count commit

The package helper now validates declared transfer-file metadata before the CLI
picker can display it: one safe file-name component without path/control
characters, a nonnegative size, a present parseable HTTP(S) CDN root, no URL
credentials/fragment, and no concatenation-induced authority change. Both
download and streaming reuse the checked source instead of unwrapping the first
CDN root. Existing HTTP/HTTPS/query formats are preserved; these legacy helpers
are not a management host/redirect policy or proof of hash semantics.

The bare CLI downloader previously truncated its destination, wrote HTTP-error
bodies without checking status, and could finish successfully with an incorrect
byte count. It now requires a complete HTTP 200 response and the exact declared
and streamed byte count. It writes to an existing-library private temporary
directory/file beside the destination, flushes/synchronizes the complete file,
then atomically replaces that one destination. Error/caller-abort cleanup leaves
the prior destination unchanged. Existing symlink/nonregular destinations are
refused before the request. Streaming open failures now return CLI failure
instead of panicking. Errors omit provider URLs/query credentials/body contents.
Valid CLI flags, selection and final ContentID output remain unchanged.

This is a per-file legacy CLI transfer correction, not full package
authorization, cryptographic file verification, a multi-file install transaction,
an installed registry, crash-recovery journal or management install capability.
Existing whole-file hash interpretation/extraction/runtime release gates remain.
The caller's legacy redirect policy is preserved; a future management provider
still must use the owned no-redirect client.

**167 scoped native Rust checks** pass: 45 core management, 25 CLI,
70 management and 27 service. Nine new actual synthetic metadata/owned-loopback/
private-file regressions cover valid URLs, pre-picker malformed metadata,
missing CDN/unsafe paths/negative sizes, HTTP-error/partial/redirect status,
declared/actual overflow/truncation, exact-byte commit, symlink refusal,
partial-body caller abort/cleanup and a real streaming CLI failure exit.
The cancellation check passes five extra repetitions. Core/management/service
clippy remain clean with warnings denied; native builds, unchanged contract
corpus and anonymous management/private broker process smokes pass.

Strict whole-CLI clippy exposes three existing errors, independently reproduced
at exact published `77d190ca7cde509e79cf62516c1cbfb2bef3e7fb`: one unrelated
`extract_eappx.rs` collapsible-if and two existing streaming argument-count
warnings. They are not changed. CLI clippy passes with only those two lint
categories allowed and every other warning denied; this is not reported as an
unqualified strict whole-CLI lint pass.

Additive immutable unsigned CLI:
`artifacts/xodus-cli-download-errors-v1-57a17bf538c61f5e`, SHA256
`57a17bf538c61f5ef946b2487125ff43a8baf5a4a7a4b310d72246d180562625`.
The broker remains the unchanged 7c53 artifact. No real credentials/account
requests, signed package URLs, consent, license issuance or games are exercised.
Protocol C95, capabilities and approved GUI da548 remain unchanged. This
independent delta received the separate review 19 R12 finding; the correction
and review 25 closure are recorded above. No earlier approval was inherited.

### Checked encrypted SOAP references

A continued trace of the real device/user SOAP response path found that
encrypted PP headers and bodies still passed untrusted `KeyInfo` through a
panicking signature conversion, then sliced its reference URI at byte one.
Missing references, empty values and non-ASCII byte boundaries could panic;
an arbitrary first character could alias a valid derived-key ID. The decrypt
helper now requires a present security-token reference and a nonempty local
`#fragment`, with an exact known nonce lookup. Missing/invalid metadata returns
the existing static `InvalidEncryptedPayload`/`MissingNonce` errors; it does
not use a panic-catching wrapper or change successful cryptography.

Four native regressions construct public synthetic AES256-CBC ciphertext,
decode the actual `EncryptedData` XML model and call the production decrypt
helper. They cover preserved valid fragment/plaintext output, absent key
reference, empty/bare-fragment/Unicode/nonfragment/external references, and an
unknown local nonce. They do not rely solely on a URI parser or mocked result.
**158 scoped native Rust checks** pass: 45 core management, 16 CLI,
70 management and 27 service. Core/service-library clippy with warnings denied,
native CLI/service builds, the unchanged contract corpus and actual anonymous
management/private malformed-before-credential broker process smokes pass.
No real account/license/ticket issuance or gameplay occurs.

Additive immutable unsigned developer artifacts:

- `artifacts/xodus-cli-soap-reference-v1-2456c27ffa6d116a`, SHA256
  `2456c27ffa6d116ac823707d219f3dd5d7d3892426d8dad3dabf78b1b03e67d4`.
- `artifacts/xodus-service-soap-reference-v1-7c53004909df2d24`, SHA256
  `7c53004909df2d24cd091564977d186ce530ae6fd5929955f2ce3f1751caa679`.

This is a separate correction after published profile-fencing source
`b16ea66ba975d47139d559fbbc1b8112ca48f52a`; both new deltas require the retained
reviewer's independent closure. No protocol/capability, approved GUI pin,
provider authorization or runtime gate changes.

### Atomic management provider profile snapshots

Core `TokenManager::management_store_snapshot` reads one complete, valid
isolated Store bundle and returns an opaque full-bundle stamp. It checks the
shared clone mutation epoch before/after the read; verification checks both
the epoch and the full order-independent bundle, including account, device,
license, expiry and flow fields. Identical recommits and logout through the
same manager's clones invalidate outstanding stamps. Out-of-process changes
are detected by bundle comparison; the in-memory epoch is not a cross-process
transaction or cancellation mechanism. Stamps deliberately implement neither
`Debug` nor serialization.

The RPS broker now reuses this core fence rather than a separate fingerprint
implementation. Package/license helpers using a management profile capture
the account, user/device proof and device license together, never mixing
separate getters or falling back to ordinary CLI keys. Started management
credential IO runs off-actor with one two-second acquisition/result deadline
and four permits retained until the underlying blocking work actually ends,
including timeout or caller abort. Missing native Keychain configuration fails.
Ordinary CLI profiles retain their previous getters and late device-license
lookup.

Package credentials are rechecked after the complete Xbox exchange and before
returning the checked package. Licensing rechecks after device exchange, after
user exchange, after the license request and before returning the derived
key/license. These are explicit boundary checks, not a claim that every inner
Xbox authentication hop is independently fenced or that a remote request can
be undone after logout. No management endpoint starts an install or license
request, and no new protocol capability is enabled.

**154 scoped native Rust checks** pass: 41 core management, 16 CLI,
70 management and 27 service checks. Nine added synthetic regressions cover
read-only snapshots, identical recommit/logout, raw full-field mutation,
mutation during the actual backend read, ordinary-profile/legacy-key refusal,
captured username/device license, and real blocking IO permit retention after
deadline/caller abort. Blocking test peers themselves have bounded waits.
Core, management and service-library clippy pass with warnings denied; native
CLI/service builds pass. The unchanged 79-positive/20-negative/four-edge and
six-each failed-page/query corpus passes. Actual anonymous management
detail/refresh/replay/reconnect and private malformed-before-credential/ping/
cleanup process checks pass. An alternate-feature process refuses plaintext
before credential/socket/state initialization; the corresponding management
configuration test passes. No real credentials, consent, entitlement, ticket
issuance or gameplay are exercised.

Additive immutable unsigned developer artifacts, not app-engine replacements:

- `artifacts/xodus-cli-profile-fence-v1-1a13f28c6a8f82a9`, SHA256
  `1a13f28c6a8f82a92b1791a9caac3413f928d94d07d19fd7a8a1db297554bbab`.
- `artifacts/xodus-service-profile-fence-v1-93db2a3bee059bb6`, SHA256
  `93db2a3bee059bb6a8bde118da75858b4cb188f712435414d4b106c388f49494`.

The coordinator reports the same retained reviewer's turn 15 approved the
preceding 4a bounded-response delta, source/assertions only without rerunning
its owner's 145 checks. R01-R11 remain closed. This new profile-fencing delta
still requires separate review. The human Account-entry ask returned user
unavailable/work autonomously: no sign-in began and no real Store/Keychain/
license proof exists. Approved GUI da548/C95 remains unchanged. Actual inventory,
authorized immutable manifests, adoption, install/update/recovery and certified
runtime pairing remain active gates, not satisfied by these synthetic checks.

### Bounded authenticated JSON responses

The existing package, Xbox user/XSTS authentication and licensing content/token
HTTP paths now share one streaming JSON reader. Xbox authentication responses
are capped at **1 MiB**; package and licensing responses at **4 MiB**. Declared
oversize is rejected before body accumulation, and actual chunks are checked
even without a content length. One **30-second total deadline** covers response
headers and body; dropping a caller cancels its async request. Typed failures
contain only static messages, byte limits and HTTP status, never URLs, tickets,
headers or raw provider/JSON error content.

Success-only callers explicitly require a successful HTTP status. Licensing
content explicitly classifies the status/body pair so its existing structured
entitlement-denial result is preserved; an HTTP-error body shaped like success
cannot succeed. Empty license-token results fail. Public authenticated helpers
now return `ProviderResponseError` rather than raw reqwest errors. The reader
inherits the supplied client's redirect policy: future management integration
must use the existing owned no-redirect client, not a default legacy client.
No new endpoint, management capability, global client policy or implicit
credential/profile import is added.

Ten native owned-loopback regressions cover exact declared/chunked byte
boundaries, declared/actual oversize, non-JSON HTTP errors, malformed/truncated
data, incompatible typed JSON, structured denial preservation, header/body
deadlines, invalid budgets and caller abort. The synthetic peers disable
proxies and terminate before returning; none contact a real account/provider.
The cancellation case accepts only EOF or connection reset as proof of closure
and passed five additional repetitions.

With the R11 correction included, **145 scoped Rust checks** pass: 36 core,
12 CLI, 70 management and 27 service. Core/service-library clippy with warnings
denied and native CLI/service builds pass. The unchanged 79-positive,
20-negative, four-edge and six-each failed-page/query contract corpus passes.
Actual anonymous management and malformed-before-credential private broker
process checks pass without consent, ticket issuance or gameplay. Exact
transferred source mtimes are advanced after snapshot restoration to invalidate
Cargo's timestamp-based dependency cache; no dependencies or broad target
directories are cleaned.

Immutable unsigned developer CLI:
`artifacts/xodus-cli-provider-bounds-v1-6c62b6fd74bdaa2a`, SHA256
`6c62b6fd74bdaa2ad8920fdd048f88a0c22208d4c7fd126a460be2fbb3375c51`.
The earlier f1d5 provider artifact predates R11 and is not a current deployment
candidate. All app-approved da548/C95 and broker 8d pins remain unchanged.
This independent delta requires the same retained review and does not establish
consumer inventory, live authorization, immutable manifests, adoption or
runtime compatibility.

### R11 checked device-key derivation

The same retained reviewer found that a present 4096-byte encrypted-device-key
block still reached version/key-equality assertions after the missing-key fix.
The equality assertion could print both key arrays. `derive_device_key` now
returns `Result<DeviceKey, DeviceKeyDerivationError>` with static, non-secret
size/version/ciphertext errors instead of assertions. The sole production
caller, `get_license`, propagates that result. There is no panic-catching or
assertion wrapper retaining the old unsafe path.

Four native **decode then derive** regressions cover a full-size unsupported
version, corrupted version-four ciphertext, a valid synthetic version-four
block with unchanged output, and inconsistent internal size. Error `Display`
and `Debug` contain only static strings/variant names, never key material.
The tests generate their own synthetic ciphertext; no actual account/license
data or private worker artifacts are read.

This narrow correction was validated against exact published f7 source with
only these two source files overlaid, excluding the pending provider-JSON
changes. **135 scoped Rust checks** pass: 26 core management, 12 CLI,
70 management and 27 service checks. Core clippy with warnings denied,
CLI/service builds, the unchanged contract corpus and anonymous management
process detail/refresh/replay/reconnect/framing checks pass. Immutable unsigned
developer CLI: `artifacts/xodus-cli-device-key-r11-v1-173b2f4327bb643c`,
SHA256 `173b2f4327bb643c3f4547ab29ce5099a940c48ebc513a91cca6db82fb89d3cb`.
Coordinator reports the same retained reviewer's turn 14 **closed R11** at c976
versus f7, finding the payload-free typed derivation/CLI propagation and four
decode/derive regressions meet the finding with no significant issue. The JSON
working delta was excluded. This is not live license, authorized installation,
an app-engine replacement or runtime certification.

### Unconnected package/license provider error propagation

The legacy CLI package and license paths now fail explicitly when credentials
are unavailable, a SOAP result is empty/ambiguous/faulted, a ticket has the wrong
audience/type/expiry, or a license has missing/ambiguous keys, invalid base64,
UTF-8 or XML. Package HTTP status and requested content identity are checked;
`PackageFound: false` cannot become a successful result. Caller-visible errors
do not echo credential values or arbitrary upstream entitlement descriptions.
The Xbox exchange uses the actual stored username rather than `"USERNAME"`.
Missing/ambiguous XSTS user claims, invalid header fields and expired/empty
tickets fail instead of panicking or constructing a usable header.

CLI flags, interactive selection and successful header/license formats are
unchanged. Rust embedders must pass the username to `api::xbox::run` and handle
its `Result<XstsResponse, XboxAuthError>`; `get_xsts_auth_header` also returns a
`Result`. Shared checked compact-ticket helpers require exactly the one response
requested by these single-audience callers. The broker's distinct two-response
RPS flow is unchanged.

Native verification passes **131 scoped Rust checks**: 22 core management
checks, 12 CLI checks, 70 management checks and 27 service checks. These include
14 new synthetic regressions, with valid compact/delegation/header/XML shape
preservation separate from rejection cases. Core and service-library clippy
pass with warnings denied; CLI/service builds pass. Both plaintext-fallback
refusals pass, including an actual alternate-feature service process that exits
before any socket, state or credential-store initialization. The native
management process passes anonymous detail/refresh/replay/reconnect and schema
framing checks; the private broker process passes malformed-before-credentials,
ping and owned cleanup checks. Neither signs in, issues account tickets or
launches a runtime/game.

The unchanged wire corpus passes **79 positive / 20 negative / 4 evidence-edge**
fixtures and six each failed-discovery/failed-query detail rejects, preserving
all nine foundation definitions. Schema C95, all capabilities and the app's
approved da548 engine stay unchanged. New immutable unsigned developer CLI:
`artifacts/xodus-cli-package-errors-v1-2eb9a16d1155706c`, SHA256
`2eb9a16d1155706ca16d04c9f51aecb7c8ff0924711b323e8564da3cb88f7cce`.
This delta still needs the same retained reviewer; neither synthetic provider
tests nor the anonymous process smoke prove authorized installation.

The coordinator reports the app's R10 lifecycle race closed at f967 in retained
turn 13 and is preparing human Account-entry handoff without consent automation
or an engine swap. Actual Store/Keychain/license proof remains unexecuted. That
gate does not waive
complete consumer inventory, package authorization/hash/expanded-manifest
evidence, verified adoption or a certified public native runtime pairing.

### Complete-profile, flat XML and live endpoint identity fences

The follow-up broker snapshots and rechecks the entire same-profile credential
bundle, not only flow/ticket strings. Memory-backed regressions prove the actual
stored username is consumed, one-bundle reads remain byte-identical/no writes,
username/device-license/expiry/flow changes fail, and logout never falls back to
retained device/default user credentials. Deadline **and caller abort** tests
prove blocking IO retains its permit until the owned OS work completes.

Parent/socket identity, ownership, mode and link count are rechecked after
creation and before accepting peers. Renamed/replacement directory tests prove
failure and cleanup only through the original retained descriptor; replacements
are preserved. Strict bounded flat MSA XML rejects unknown roots, nested fields,
attributes, duplicate aliases and DTD/entities before any credential getter.
The actual native broker process smoke now includes those unknown-root/nested
requests, continued ping usability, empty stdout and owned socket cleanup.

All **27 service checks**, service-library clippy, build, private process smoke
and plaintext-fallback refusal pass. The unchanged 90 management/CLI/core checks
bring the scoped count to **117**; the other management fallback/corpus/public
process evidence is unchanged. New immutable unsigned service:
`artifacts/xodus-service-rps-context-v1-8d94aa3221da5789`, SHA256
`8d94aa3221da57892d834d9ebe22d918c000d7797ec68ed68fa683db403e2735`.
CLI 0a/da548 and schema C95 are unchanged. Coordinator reports the retained
reviewer's turn 10 found no significant issue in 97fc versus e729
**source/isolated tests only**, without independently rerunning the 117 checks
or approving live issuance/pairing. The newer package/license delta above needs
its own turn with that same reviewer.

The approved da548 native consent/device-preparation chain does not call the
newly guarded `exchange_device_token`: it uses `ensure_device_credentials` /
`authenticate_device` and NativeTokenBroker `exchange_user_token`. Therefore
that RPS consumer guard alone does not force a GUI engine swap before the
coordinator's actual human consent readiness gate. No real account/package/
runtime request was performed by these regressions.

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
