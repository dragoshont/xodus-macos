# Xodus Architecture


## Components

![Xodus architecture diagram](/assets/Docs/architecture_diagram.png)

- [Xodus Service](https://github.com/xodus-gaming/xodus) - Process ran in the Linux host environment that communicates with xgameruntime via IPC, handling the processing of complex runtime API calls that involve communication with the XBOX Servers or host-side UI. 
- [xgamertuntime](https://github.com/xodus-gaming/xgameruntime) - Open source implementation of `xgameruntime.dll`, a Windows component required to execute XBOX PC games. 
- [Wine](https://github.com/xodus-gaming/wine) - Xodus fork of Wine that is patched to include the open source `xgameruntime.dll`.


## Requirements

- Games downloaded through Xodus retain executable in encrypted form - following in Windows footsteps
- Users shouldn't need to login multiple times - auth confirmations are fine
- Steam games using Microsoft services should be able to use Xodus login
- Users want to use their launcher of preference, not another launcher. Integration should be simple

## Runtime transport boundary

The service's XML framing uses little-endian `u32` magic `0x58445358`,
`u16` message type, `u16` payload length, then exactly that many payload bytes.
Payloads larger than 65,535 bytes are rejected rather than truncated. Only Ping
and MSA token requests are accepted as incoming request types. Unsupported
protobuf frames, unknown types or magic, and failed XML requests close the
connection without a success-shaped response. Cancellation interrupts pending
header, payload, and response operations. Request bodies are not logged.

Transport regression tests use unnamed local socket pairs and memory-backed
token storage; they do not start the service or access accounts.
Run them on a supported Unix host with `cargo test --locked -p xodus-service`.
This boundary is not a version handshake or gameplay certification. The legacy
service still uses its default credential profile and platform runtime socket;
the native launcher must not attach to or start it as an isolated management
service. A scoped credential/endpoint integration and a validated public
Wine/shim/service pairing remain necessary.

The original client in `runtime/public-rps` targets the public development
shim's RPS stub using an explicitly inherited private `XODUS_RUNTIME_SOCKET`,
not a global default. It checks both tickets/expiries, bounded private framing
and one overall IO deadline, and suppresses response-parser credential dumps.
Its pinned-source applicator also removes directly coupled public shim
JSON/HTTP-body/header debug dumps. See that directory's README for isolated
native checks, exact public source provenance and licensing. This client does
not itself establish a supported Wine/shim/service version pair or live
authorization. The backend's separate scoped broker remains required.

The separate `runtime/public-macos` probe now compiles the complete configured
public Wine candidate with an actual x64 macOS loader, server, native Mac
driver, Schannel dependency and patched x64 PE shim. It uses exact public source
pins and checked original platform overlays, not a private runtime. Vulkan,
GStreamer, the Linux DRM AMD extension and the fork's optional duplicate legacy
loader are explicitly excluded. The default Wine shim gitlink is deliberately
overridden by the development shim, so this is not an upstream-supported pair.

Compilation and offline dependency checks do not prove Windows-branch RPS
execution, prefix isolation, graphics, TLS peer validation or licensed gameplay.
No real credentials or game were used for this build. Its output hashes and
limitations are recorded in the probe README; it must not enable the app's
Play capability or be distributed as a certified runtime.

A separate original Windows console check now actually executes the client
through that public loader/server in a new private prefix against an owned
synthetic Unix peer. Four checks cover the real DOS/ACP-to-Unix socket mapping,
fragmented successful framing/output ownership, malformed and expired responses,
and a bounded timeout. This is Windows-branch component evidence, not a gaming
shim/broker account exchange, supported version handshake, graphics/TLS-peer
validation or licensed gameplay. See `runtime/public-rps/README.md`; genuine
Store consent, account authorization and the complete runtime pair remain gates.

A separate original PE HTTPS client now exercises actual WinHTTP/Crypt32,
Schannel and owned x64 GnuTLS against an owned loopback fixture in that same
reviewed private-prefix lifecycle. It rejects an unknown CA and wrong hostname,
and accepts only an explicitly trusted fixture with HTTP 200 and exact body.
The exact-source public overlay preserves WinHTTP's specific certificate errors;
it does not disable validation. Dependency lookup is explicit and child-local.
The fixture uses only private Wine registry trust, not native trust-store writes,
and its single worker has verified bounded shutdown for stalled handshakes and
headers. This is scoped TLS component evidence, not production endpoint coverage,
account authorization, runtime pairing, graphics or gameplay certification.

The public graphics component subsequently executes both legacy and
forward-compatible core WGL contexts in the graphical login session, rejecting
SSH sessions without actual graphics access before launching Wine. Each uses
its own hidden window and allocated 32x32 RGBA8 offscreen framebuffer, with
actual clear/readback matching all four channels within one byte. The
eleven-source guarded overlay fixes initialization queries and buffer
restoration; no GL errors are cleared to claim success. The hidden window's
native default framebuffer is unavailable, so this proves only owned offscreen
rendering, not presentation or gameplay. A subsequent explicit nonactivating
window probe now verifies an owned default RGBA8 back buffer, clear/readback
and successful buffer-swap API in both contexts. Guest activation checks and
metadata-only native foreground-PID snapshots reject observed changes. These
snapshots are not continuous activation monitoring, and buffer submission is
not compositor capture or licensed gameplay. Existing actual RPS/TLS checks still
pass, and the owned-prefix lifecycle remains shared. This does not enable
Play or establish account entitlement, package authorization, runtime pairing
or a certified distribution.

The separate `runtime/public-async` candidate replaces the public shim's 23
XAsync/XTaskQueue stubs with the pinned MIT Microsoft async core, preserving the
COM layout and existing time-sensitive methods. Four actual gaming-COM cases
and three real user-add malformed/expired/missing RPS failures now execute,
rather than exercising only the standalone client. The tightly coupled user
initializer obtains RPS before default endpoint HTTP and reconciles failed and
unconsumed user allocations. This is controlled failure/component evidence, not
successful account authorization or a production service/runtime pair. Its
source, licensing, checks and limitations are in that directory's README; the
approved native app engine and Play gate are unchanged.

A subsequent developer-only integration uses the actual Rust broker library
with an explicitly empty in-memory account and the real public Windows
gaming-COM user-add consumer. The parent checker's mandatory fixture argv and
exact binary pin prevent accidental production-Keychain broker selection. Before/after ping is
bound to the recorded native PID; the actual failed user add has `E_FAIL`, one
completion and no handle, followed by graceful socket cleanup. Peer closure
does not encode an authentication category. This proves signed-out component
interoperation, not successful Store RPS, credential-profile pairing, entitlement
or licensed gameplay; it changes no approved app engine or Play capability.
Retained report 38 found no additional issue in that consumer. Report 39 closed
R19 in the backend's separate fixture smoke at
`924824ff63938307425e7f7cb87252e3512f48f2`: unconditional checks and actual-script
mocked optimization regressions prevent an unsafe ungated production probe.
The Rust fixture and approved app engine are unchanged; the old sealed source
archive's standalone smoke is superseded and must not be run.
All R01-R19 findings are closed only for their reviewed component scopes. See
`runtime/public-async/README.md` for the exact source, hosted evidence, archive
pins and unchanged account/gameplay gates.

### Product runtime selection

The user requires source-level support for four selectable runtime options:
Apple Game Porting Toolkit 3, Toolkit 4, a legitimate existing CrossOver
installation, and standalone/source-built Wine. Toolkit 4 is the current
game-trial default, not a restriction on the product's provider API.

Execution engine, source/provenance and version must be modeled independently
from graphics renderer/backend and version. Standalone Wine 11 with Apple
D3DMetal 4 is a composition; selecting a Toolkit generation must not silently
assert that the engine is an older bundled Wine version. Provider settings and
management responses must preserve older consumers through an explicit
backward-compatible capability contract.

The approved integration boundary is a separate pure `runtime-plan` CLI
entry, not a public C95 management operation. It accepts bounded, strict
configuration JSON on stdin and returns a generation plan as JSON on stdout,
before credential-manager or logging initialization. Invalid input must fail
with static nonzero errors. Planning must not invoke a provider, runtime,
filesystem discovery or GUI. The backend boundary is published separately at
`9ef0f298481fb48840734b538e0f6d22e1c98ff3`, tree
`8b2f7abb54f91e347afe013eee18c93873b111a5`, atop the unchanged authentication
source pin. Native selection and plan consumption are implemented separately
at final consumer `43d0d190517a545f856b123749bdec10a885ced1`, tree
`af09fa41333f50f06188094684e03e6fb4b34d6e`. The retained review grants scoped
runtime-consumer source closure from authentication-corrected consumer
`6196399e7390eca854a5c50a1a4356287041c565` through this final pin against the
frozen planning producer. This closes the installation-copy and Quit-cleanup
findings, not runtime execution or deployment.

The parent reviewed the seven-file planning delta, including startup before
account/log initialization, explicit stdin EOF, the 16,384-byte input limit,
strict configuration parsing and output-failure handling. No additional
high-confidence source blocker was found. The owner reports 12 selected native
checks (six provider, four stream and two argument checks), 46 schema checks
and scoped lint/check/C95 validation. The parent independently verified all
200 sealed archive files against all 200 immutable Git blobs, with no disk
extraction. Archive SHA-256 is
`8f1e55a6872e6ca92767b40841a295943a6d09f4042f140004efbdceddafa2f8`;
the unchanged certificate is
`46eff7f37b9c45a861010fe63263de8a7c1adb39b2a719e9c8a184c9889b1da4`.
The canonical schema SHA-256 is
`90c094e4585af059b5ebcfc3201260362e88aa642b50ec0388a03427260d55e9`,
and fixtures SHA-256 is
`76a0d791c99a6c77e12581a4361dbe6e37a0dbd29f2e556ef292bbbc360bc40c`.
These backend source checks do not themselves qualify the consumer integration,
an installed runtime or a game.

The consumer wires live and fixture native Settings to all four presets,
initially unconfigured, with independent declared engine/graphics metadata and
explicit required JSON nulls. Its live action invokes the separate bounded
planning client; fixture execution is disabled. Returned configuration,
canonical identity hash, generation/path agreement, fixed unknown evidence
and nonlaunchability are checked. Nonzero output is discarded; an older engine
without the entry reports planning unavailable rather than falling back to
authentication or a provider. Installation copy now says "Installation not
inspected", not that an already installed runtime is absent.

The parent independently verified exact consumer `7477d4` normal push
[hosted run 37211343959](https://github.com/dragoshont/xodus-macos-app/actions/runs/37211343959)
with Xcode 27.0 build `27A266a`, SDK 27.0 and macOS 27.0: 15 core,
352 management, 60 preview, 144 mock-session and 87 private-host checks passed
with zero failures, plus SVG reproducibility. These are source-only synthetic
checks, not an installed runtime or actual game execution.

The first local 658-check stage was assembled from Windows working-tree bytes;
its manifest compared LF-normalized text rather than the raw compiler inputs.
The parent's bounded raw-byte check found 39 of 56 inputs differed from Git:
38 Swift/package files with CRLF and one artwork-provenance JSON file.
Binary artwork remains binary, not subject to text newline normalization.
That stage is retained as local intermediate evidence, not literal Git-byte
qualification. The owner subsequently built a new isolated canonical-Git
stage and reran 658 checks for `7477d4`. The parent independently verified all
56 raw compiler inputs against that immutable Git pin, stable before/after,
without normalization. This qualifies the historical stage, not the later
Quit correction.

The retained review found that ordinary Quit awaited management disconnect but
could leave an independently owned planning task running. Final correction
`43d0d1` adds the shared `ApplicationTerminationCoordinator` to the normal
`PreviewDelegate` termination path, with owners attached in both main and
Settings scenes. Shutdown fences planning edits/spawns and new management
connects, cancels and joins the actual planning task through owned-child cleanup
and reap, reconciles retained planning-client ownership, and concurrently awaits
management disconnect. Quit succeeds only after both closures succeed. Failure
refuses termination, surfaces the error and restores interactions after both
attempts finish, preserving ownership for reconciliation and retry.

The new checks use that same coordinator with actual neutral children,
including zero-management Quit with a SIGTERM-ignoring planner,
concurrent/repeated shutdown, cancellation, unavailable planning and retained
management-child refusal/retry. Retained review turn 16 closes the Quit finding
with high confidence and finds no significant tightly coupled regression.
Authentication, host binding, native header and public C95 policies are unchanged.

The final isolated native stage was assembled from immutable Git, not a
Windows working-tree tar. The parent independently verified all 58 raw
`Package.swift`/`Sources`/`Tests` inputs against final `43d0d1`, with stable
owned regular files, no text normalization and unchanged binary resources.
The owner reports SDK 27, Swift 6.4, arm64 Release and 668 passing checks.
The parent separately verified the stable Release artifacts without executing
them: launcher SHA-256
`5505dd7e6cab234198eee16a1e612859c23a05b5ed01bb2c94c5a799ffcb7186`
(4,543,040 bytes) and host
`063d0fe5aafc34c71e71110e22ff8180f8be0f38fb60affaa7fbe3c505021b6d`
(537,408 bytes), both owned regular files with mode `0755`.

The parent also independently verified final `43d0d1` normal push
[hosted run 37212933441](https://github.com/dragoshont/xodus-macos-app/actions/runs/37212933441):
Xcode 27.0 build `27A266a`, SDK 27.0 and macOS 27.0 passed 15 core,
352 management, 60 preview, 154 native-session and 87 private-host checks,
668 total with zero failures, plus SVG reproducibility. This is fresh
exact-correction evidence, not inherited `7477d4` qualification. Native-session
children are neutral mocks and private-host WebKit is detached and synthetic.
No live provider, runtime, game, authentication, visual-conformance or
deployment approval is implied; the deployed pair and desktop holds remain
unchanged.

Each engine/backend generation needs an isolated prefix. Existing bottles and
saves must not be reused, migrated or deleted silently when a selection changes.
Configuration, installation, device preflight and game-specific verification
are distinct states. Neither discovery nor a successful trial of one game
establishes universal compatibility or authorizes another title.
Declared engine/renderer hashes are configuration, not observed installation
identities. Pure planning leaves installation, device preflight and per-game
assessment unknown and reports `launchable=false`; it is not execution support.

CrossOver remains user-installed and licensed; its binaries must not be copied
or its licensing bypassed. Supporting Apple Toolkit 3 and 4 does not establish
a blanket commercial redistribution license. This provider follow-up is
source-only and separate from the reviewed native authentication source
freeze; no private runtime inspection, live game probe or app GUI action is
authorized by this product requirement.

### Native launcher sign-in integration

**October 6, 2026 persistence baseline:** app
[`5ac830c`](https://github.com/dragoshont/xodus-macos-app/commit/5ac830c8620c1f293a0cc0389b9ae41cfd00b1c6)
is paired with backend
[`397dd02`](https://github.com/dragoshont/xodus-macos/commit/397dd0249c81414dae3d75b3562f696378fd6b11).
The engine restores upstream Xbox-first exchange ordering, with Passport
requested after continuation. The app owner observed a saved Microsoft
session after user-assisted sign-in and confirmed it remained saved after
a normal app restart. Authenticated provider use remains unverified because
the subsequent installed `2b4f962`/`c42e21a` pair returned
`credentialUnavailable` on its sole live `auth.verify`. Saved-session status
must not substitute for authenticated provider use.
Validated failed-exchange inputs may be retained only in launcher Keychain
for at most five minutes, bounded by their original device-auth/device
expiry. Success clears them; retries do not renew their expiry. Expiry
cleanup runs while the engine is alive or on the next access, not while
the engine is stopped. Accidentally closing the browser exposed a stuck
pending-state bug; cancelling the attempt and restarting recovered it,
and the subsequent `5ac830c` cancellation/retry fix is installed. Hidden AppKit
direct-close/perform-close and explicit-retry regressions passed; installation
preserved the saved session without another credential prompt.

**Engine-update access boundary:** subsequent Mac inspection established that
the old and new CLIs have different per-build ad-hoc signing identifiers and
designated requirements, with no Team ID or available signing identity at that stage.
The old Keychain item remains present, but the new engine cannot read it under
the no-prompt policy. This is not evidence that the credential was deleted or
that its provider rejected it. The status-observation loop was stopped;
the additionally proposed direct status request was never started.
One human access/sign-in approval is needed for the current engine. Durable
rebuild reuse requires fixed app/CLI identifiers under one stable signing
identity whose private key remains in OS Keychain, and comparison of designated
requirements across two different builds. Provisioning and private-key/access
approvals remain human-gated; no export of an existing private key or saved
credential, broad ACL change or permission bypass is authorized.

**October 6 CLI signing setup:** after user approval, a standard local signer
was imported with a permanent, nonextractable, signing-only private key and
traditional signing access limited to `/usr/bin/codesign`. Matching identity
enumeration independently confirms `Xodus Local Code Signing`; its self-signed
trust status is not treated as a missing identity. Temporary newly generated
key, PKCS12 and passphrase files were removed, without claiming secure erasure
on APFS. Public certificate and non-secret receipts are retained.
App signing policy
[`b3973e67`](https://github.com/dragoshont/xodus-macos-app/commit/b3973e67dbbed9e01dcd01db08934fd8758eca19)
passed targeted checks and exact-source CI
[37424750870](https://github.com/dragoshont/xodus-macos-app/actions/runs/37424750870).
It accepts the pinned matching local identity without requiring global trust,
while retaining fixed identifiers, certificate-bound requirements and strict
signature checks.

The first actual signing attempt failed with `errSecInternalComponent` in the
unlocked Aqua user session. A single no-prompt inspection of the exact imported
identity confirmed that its partition ACL contains only the importer's ad-hoc
identity: Apple signing-tool partitions are absent despite the matching
traditional signing ACL. The user subsequently completed the visible Terminal
permission command for the two explicitly approved Xodus signing keys: the
imported signer and the unused certificate-less provisioning key. No password
was captured by the agent, and no global certificate trust was changed.
The cause-directed signing retry succeeded.

Both independently built `c42e21a` engines now pass strict signature checks.
Their actual designated requirements were independently inspected and match:
`io.github.dragoshont.xodus.cli` and certificate leaf
`6c5f1cd832a2b2842686245b4def219bb8b465b2`, without a build-specific `cdhash`.
Matching app `b3973e67` is installed with the first build, and the second-build
package is staged after independent hash, source, sealed-input, resource and
signature checks. This establishes stable signing across two real builds,
not credential persistence or provider authorization.

The supervisor reported a user-directed fresh start clearing the saved Xodus
management session and four private CLI credential items; that deletion was
not performed or independently inspected by this coordinator. The working
signer remains present. The installed app's old credential-store error had kept
sign-in disabled until a fresh signed-out result. After restart, a genuine
Microsoft helper window was observed and left untouched for human sign-in.
The staged read-only check and second-build replacement must wait for that
flow to finish; no new browser sign-in is permitted during the rebuild-reuse
check. The private game runtime may need its own login again later.

**Reference persistence and native permission parity:** the stable-signed
`1759a61`/`397dd02` reference subsequently reported `credentialPresent` after
fresh Microsoft sign-in and again after a new-process restart. The saved item
and profile were preserved when switching to stable-signed `c42e21a`;
that engine's single `auth.status` returned `credentialStoreUnavailable`,
so no authenticated provider request was sent. Identical designated
requirements did not establish cross-engine access.
Apple's independent partition check explains why this can happen:
[client partition selection](https://github.com/apple-oss-distributions/Security/blob/db15acbe6a7f257a859ad9a3bb86097bfe0679d9/securityd/src/clientid.cpp)
uses a `cdhash` for signed code outside the validated Apple signing chains,
and [ACL validation](https://github.com/apple-oss-distributions/Security/blob/db15acbe6a7f257a859ad9a3bb86097bfe0679d9/securityd/src/acls.cpp)
checks that partition separately from the normal trusted-code requirement.
The actual saved item's partition was not inspected or edited.

Installed app `f30f1b1855c279cfe2c1c3391c5042f53cf76df7` and engine
`d00a8b97501a2ce1045d579e62568c5feb017ca8` restore upstream-compatible
foreground native permission reads without a new command or schema.
Foreground `auth.status` has a 120-second off-actor permission budget and a
130-second client budget. Its owned read permit remains held until the actual
native read returns, even after timeout, preventing overlapping account work.
The UI invalidates stale status and fences duplicate actions; denial is not
converted to signed-out success. Active sign-in polling, unattended cleanup
and the deliberate bounded `auth.verify` reads remain noninteractive.
Status publication reconciles the current worker/flow rather than a pre-read
snapshot, and the client makes one final reconciliation at its polling deadline.
Native qualification reported 88 backend checks, 352 app native checks and
453 management checks passing; exact app CI
[37459156452](https://github.com/dragoshont/xodus-macos-app/actions/runs/37459156452)
also passed. Package hashes, source bindings, bounded proof and strict deep
signatures were independently verified before installation.
The reference rollback and saved login remain preserved.

After installation, the automated Account action timed out and the native
observer reported `accessibilityNotTrusted`; neither result proves that a
Keychain read or permission prompt was dispatched. No prompt was approved by
the agent, and no overlapping read or provider call followed. The user was
directed to open Account once and handle any native permission prompt.
At that stage, updated-engine saved status and authenticated Xbox access were
unverified. A local self-signed identity is not a guarantee of prompt-free
updates.

**October 6, 15:20 live authentication closure:** after the user handled the
native permission step, one typed `auth.status` on the admitted installed
`f30f1b18`/`d00a8b97` pair returned `credentialPresent`, with no pending flow
or failure. Immediately afterward, one `auth.verify` for the publicly derived
Halo content ID `513710f5-ab8e-4d7c-9ed5-d0ba94dcfb33` returned exact
`{"verified":true}`. The fixed, non-secret result binds package receipt
`413bd87af89770aac3dd970f9306f35d059db586380c952daf8211e353052255`
and signed CLI
`9933bd8651d0df22bf1b1779da8fc89d10962493368882699ca97b2994dd22f2`
(21,694,112 bytes), the same installed binary and management profile.
The coordinator independently read that result. No browser, new Microsoft
sign-in, logout, credential deletion, copying or reset was performed.
The app owner confirmed Xodus reopened with one owned engine and no helper.
The working reference rollback remains preserved.

This establishes saved-session access by the current updated engine and a
real authenticated package-provider read. It does not establish ownership,
license acquisition, installation, gameplay or unattended access by a future
engine hash. The normal native permission flow, rather than identical
self-signed requirements alone, resolved the observed read barrier.

**Supported Microsoft sign-in research:** Microsoft documents MSAL for Swift/
Objective-C macOS clients, with `ASWebAuthenticationSession` as the default
system authentication browser on macOS 10.15+:
[browser guidance](https://learn.microsoft.com/en-us/entra/msal/objc/customize-webviews),
[installation and redirect/cache configuration](https://learn.microsoft.com/en-us/entra/msal/objc/install-and-configure-msal),
and [authorization code with PKCE](https://learn.microsoft.com/en-us/entra/identity-platform/v2-oauth2-auth-code-flow).
App registration must support
[personal Microsoft accounts](https://learn.microsoft.com/en-us/entra/identity-platform/v2-supported-account-types).
These sources establish ordinary Microsoft account authentication, not
compatibility with the inherited Passport/Store credential exchange.
[Store authentication](https://learn.microsoft.com/en-us/gaming/gdk/docs/store/commerce/service-to-service/xstore-authenticating-your-service)
requires additional service-specific credentials/configuration and distinguishes
the purchasing account from the Xbox player account on PC. No MSAL dependency,
authentication rewrite or invented Store entitlement capability was introduced
by this research.

**Installed UI cleanup:** app `b82ac96`, tree
`393e71b9d7805781d105933559125bc9d0bcff94`, passed the scoped native source
review and exact-source shipping CI
[37389117999](https://github.com/dragoshont/xodus-macos-app/actions/runs/37389117999).
The controlled package's binary and receipt hashes and deep strict signature
were independently checked before installation. The signed `c42e21a` CLI
remained byte-for-byte identical to the previous installed copy. The owned
window is visible and responsive; no account or provider actions were taken.
Screen Recording permission blocks screenshots, and preflight stopped before
the keyboard/accessibility round. Source and CI acceptance do not certify
actual visual, VoiceOver, keyboard or resize conformance.

**October 6 real-data visual cleanup:** app
[`a8df179`](https://github.com/dragoshont/xodus-macos-app/commit/a8df179a37ec5c6629104f5045dbf287fde55efd),
tree `575e70388269ecbb69af57c0e6ddfe226b573a79`, passed focused native checks
(64 preview and 354 native), direct source review, and exact-source shipping CI
[37469198866](https://github.com/dragoshont/xodus-macos-app/actions/runs/37469198866).
The controlled package's source, inventory hashes and strict deep signature
were independently checked before installation. The actual packaged engine
remains byte-for-byte the working signed `d00a8b97` CLI; no credential, signer
or backend changes were needed for the UI update.

Two bounded render rounds used real `LiveSession` data: Library's genuinely
unavailable state, current public Halo search results including a partial-result
warning, real product detail and a freshly confirmed saved account. A
development-only exporter binds to the compiler-admitted installed pair and
renders the app's own NSView hierarchy; shipping builds reject its flag.
There are no fixture titles, fabricated owned/installed games or account IDs
in the exported evidence, and no repeated authenticated-provider probe.
The parent visually reviewed all four before and after images.
The single correction batch removes repeated Library/Discover copy and moves
folder diagnostics into secondary details. Product is actually 600 x 300 pt
and Account 560 x 280 pt, reduced from 620 pt-high sheets, with visible native
footers and no observed clipping in these renders. The latest app is open
with one owned engine and no auth helper; the working `f30` and reference
`397` rollbacks remain preserved.

Final own-view PNGs are under
`/Users/dragoshont/xodus-app-tooling/app-live-visual-round-7ytgw4eo/after/`:
`live-library.png`, `live-discover-search.png`, `live-product.png`, and
`live-account-signed-in.png`. These qualify the observed native content/layout,
not the window-server compositor, live Glass or accessibility behavior.
The current native-toolbar authority supersedes the Figma mockup's earlier
custom chrome and fictional hero; neither was reintroduced to fake parity.
Full owned inventory, installed-game scanning and gameplay remain unavailable.

**Owned/installed capability boundary:** the current backend has no complete
consumer Xbox PC-owned inventory enumerator. Partner-configured Store queries
and current-app add-on collections are not substitutes for that capability.
`installed.snapshot` currently emits a constant empty vector with
`managementRegistryOnly` scope; durable state has no installation map or
writers. It is not evidence that the Mac has no installed games.
`installed.inspect` performs bounded no-follow structural reads of a selected
folder marker, not a Mac-wide scan or entitlement/playability check. Its
partial/external/unregistered/unverified result must not be promoted to
installed or launchable. Durable observations, if added, must remain distinct
from verified installation records.

**Earlier October 6 Store artwork and recent-history slice:** app
[`fb39072`](https://github.com/dragoshont/xodus-macos-app/commit/fb39072cd9e0301c308c1b91bfd5ea157bf5e51d),
tree `47cf0c73161e501024388321736f8fd273733b39`, binds producer
[`680593de`](https://github.com/dragoshont/xodus-macos/commit/680593de3d32390fe2105780b9a21b09fd302337),
tree `4b6fda27f9cf8aa546dfff6693b4270eabfee230`, and generated public schema
`6945db01df88eeaf80f495f6b9d7240e270a879851c2c830fc5548ba20d32192`.
Source reviews, focused native tests, exact-source app CI
[37477681227](https://github.com/dragoshont/xodus-macos-app/actions/runs/37477681227),
sealed Release provenance and final package hashes/signatures qualified the
pair before installation. The working `a8` rollback and saved profile are
preserved; no credential reset or new Microsoft login was performed.

DisplayCatalog's selected localized `Images` now map BoxArt, Poster and
SuperHeroArt into a bounded typed artwork array. Only normalized HTTPS URLs
on the verified Store image host/path are exposed. The native, memory-only
loader rejects redirects, cookies and authentication and bounds requests to
10 seconds, 8 MiB and 16,777,216 decoded pixels. Metadata absence/rejection is
distinct from fetch/decode failure. Old cached catalog records explicitly
migrate to empty artwork with `notQueried`.

One actual anonymous Halo catalog query in an isolated public-metadata profile
returned two products with available artwork. Actual HTTP/decode preload of
two covers and the first hero succeeded, with zero failures. Both real native
own-view PNGs were retrieved and visually reviewed:
`/Users/dragoshont/xodus-app-tooling/app-art-history-fb39072/public-art-confirmation/images/live-discover-search.png`
and `live-product.png` in the same directory. They show Halo Infinite/Gears 5
covers and the Halo hero/box art, without fixtures or a personal account read.
No further public-art probe or polish round was needed.

The separate `library.recent` operation uses the existing saved-proof exchange
to request a fresh Xbox XSTS token for `http://xboxlive.com`, then a bounded
TitleHub GET with contract version 2 and an explicit item limit. Private XUID
claims remain inside the provider; public results carry game titles, reported
platforms, optional last-played dates and artwork, never guessed Store mapping
or ownership. The source-backed
[OpenXbox client](https://github.com/OpenXbox/xbox-webapi-python/blob/master/xbox/webapi/api/provider/titlehub/__init__.py)
describes this as recently played history. It is a partial window, not a
complete owned-game inventory or proof of PC installation.
History stays in memory and is cleared before every explicit Account refresh,
on failure/profile uncertainty, sign-out and disconnect. The read-only
publication path compares the complete stored profile, then checks generation,
shared epoch, expiry and deadline. Successful full-profile revalidation is
the read linearization point for independent writers; a later external write
does not retroactively invalidate that read.

The first live personal exporter stopped because saved status was not confirmed;
its generic guard did not retain a fixed cause. Subsequent bounded captures
reported `credentialStoreUnavailable` before dispatching history. Those failed
attempts produced no personal PNGs and did not establish signed-out state,
expired credentials or a provider rejection.

**Current saved-auth and history result:** the independently admitted
`4e204270` app / `2b31d199` engine explicitly enables requested foreground
Keychain interaction under the existing mutex and checks restoration of the
prior setting. Background and verification reads remain noninteractive;
credential writes are unchanged. This installed pair returned current
`credentialPresent`, then 20 actual TitleHub titles, live/partial. Library and
saved Account own-view PNGs were retrieved and visually reviewed. No Microsoft
relogin, credential reset or inferred ownership was involved.

All 20 history image references were rejected by the initial URL policy, so
no personal images were fetched and Library artwork remains unverified.
The qualified `c1073100` producer correction accepts HTTP metadata only for
the exact Store origin and existing single-ASCII-asset grammar, emitting HTTPS
for the same asset. Ports, credentials, query/fragment, wrapper URLs and other
hosts remain rejected. Its sealed native Release is checked; matching app
packaging and actual post-correction Library artwork are still pending.
The [full delivery ledger](delivery-ledger.md) tracks that acceptance gap and
all nine Figma flows, game authorization, installation, update and play.

The chronology below records earlier source and runtime observations,
not the current persistence result.

The Store sign-in route currently uses inherited public Microsoft client ID
`000000004424da1f`, not a Microsoft application registration created for this
launcher. The active route is the existing native Windows TokenBroker-style
inline-login and SOAP exchange; the separate XAL OAuth configuration is not
its entrypoint. The macOS bundle identifier
`io.github.dragoshont.xodus.development` is independent. Neither the Microsoft
registration owner nor current provider acceptance is established by source,
and the absence of our own registration is not a demonstrated failure cause.
Registering a new client would not by itself grant Store-wide game entitlements:
the documented [Store collection query](https://learn.microsoft.com/en-us/windows/uwp/monetize/query-for-products)
requires a Store ID user key and service token, and scopes owned products to
apps associated with that service's Azure AD client ID.

Reviewed backend source `e60481fc918b4cc2cc599a5db2aba17a04738872` and app source
`360e8bf3a2eb7efc087ccce2ed4d2bf93fc16369` preserve bounded, static authentication
failure details without raw provider responses or credentials. The backend's
immutable unsigned native CLI has SHA256
`47fab28009f9c6c52294095e5684bfce3f86f45e50ddc98655d2b22150f02c8d`;
the app owner verified a separately signed embedded copy with SHA256
`c50a98b3e0afe4ba6a9b883e6f09ef194b481817eea94f5db190fb2123aceaa8`.
The C95 management schema is unchanged.

Following the user's explicit close-and-retry request, the app owner verified
the idle owned app and engine, quit them gracefully, verified the new pair,
and performed one native accessibility Sign In action. This new attempt
terminated at `AUTH_INVALID / devicePreparation / providerProofInvalid` before
the user-login stage. This reason covers incomplete device registration data,
unsupported response shape or an invalid converted token/proof; it does not
establish a cryptographic failure. The underlying cause and the cause of the
older unobserved failure remain unknown. The user's subsequent instruction
authorizes the owned-app click, fix, restart and retest loop, while preserving
active human/OS-commit flows. Cryptographic verification remains mandatory.
At that earlier stage, no successful Microsoft sign-in, Keychain commit, entitlement, package
authorization or gameplay is established;
credentials and consent remain user-controlled.

The subsequent scoped correction at backend
`bace09c1be95ff35864b8c8593b974b2aeee934f` reuses the existing checked single
response selector for both a bare device response and an exactly-one response
collection. Empty, multiple, fault and invalid-proof responses remain rejected;
cryptographic and nonce checks are unchanged. Four additive static reasons
distinguish registration, response shape, converted proof and token structure,
while retaining the older reason and exact three-key details format.

App source `da0adc00d337ba27c914f5e47ce3c2b11b8000de` consumes those fourteen
closed reason pairs, corrects the misleading cryptographic-failure copy, and
replaces the hand-composed floating navigation with native window toolbar,
segmented navigation and account controls. Routes, artwork and keyboard
shortcuts are preserved. The same continuity reviewer closed both exact source
deltas without significant findings. The backend owner passed 45 isolated
native checks in a fresh owned build target; exact app hosted run
[37191128180](https://github.com/dragoshont/xodus-macos-app/actions/runs/37191128180)
passed 353 checks and editable SVG verification. Earlier local build/test
failures are not relabeled as passing or assigned an unproved host cause.

The additive, immutable unsigned CLI built from the reviewed backend source has
SHA256 `34214ee29b3582a6d146ccd029991a70949514c58bcaa99f834db930d41e96ca`.
Source review, isolated checks and inert artifact readiness do not prove that
the response-wrapper bug caused the observed failure. The app owner subsequently
verified a separately signed embedded copy with SHA256
`5d6bb07936559ea44a0fd81ad96ed7e118bf6346d8d09a83f8212a543ee1329a`,
gracefully retired the idle older pair, and verified the fresh app/engine
relationship, native toolbar controls and read-only readiness.

The new user-authorized native Sign In action still failed before the login
window at `AUTH_INVALID / devicePreparation / tokenStructureInvalid`. The
locally authored failure title was visibly presented; no pending cancel control
or auth child remained. This narrows the refusal to the supported token/proof
structure guard, not a specific legacy-kind, STS-key, cipher/base64 or v4-secret
cause. No working Microsoft popup or authenticated Store session is established.
Account credentials and consent must still be supplied by the user once the
preparation blocker is resolved.

The backend owner's next isolated trace reproduced a separate XML lexical
false rejection: both new regressions fail against the exact reviewed baseline
when otherwise valid XML base64 values contain permitted space, tab, CR or LF.
Full SOAP decryption succeeds before the wrapped proof is rejected; the
canonical no-whitespace case passes. This establishes a source defect, not that
the actual provider response contained whitespace or caused the live failure.
Published backend source `2acb452a7ee66b2c9d3ad75ecf85e2be2f94fbc3` now applies a
shared strict, bounded XML-only decoder across proof, HMAC, nonce and AES
consumers. It normalizes only XML whitespace for binary decoding, bounds raw
input to 1 MiB, and preserves original signed XML, non-XML decoding policy,
token/proof bounds and existing cryptographic checks. The backend owner reports
54 selected native checks passing, including signed-envelope verification,
ciphertext-tamper rejection and lexical/bounds regressions, plus qualified
lint, build checks, formatting and the unchanged contract corpus. The public LF
source mirror matched before and after those checks.

Before producer implementation, the parent and sole app owner agreed four
additional static device-preparation reasons: `tokenKindInvalid`,
`tokenAudienceInvalid`, `tokenCipherInvalid` and `tokenSecretInvalid`. App source
`5ae30fd4bd4e17cb235857e5c41fba1627c317f7` retains all fourteen existing pairs,
including the coarse structure reason, and preserves the AUTH_INVALID-only,
exact three-key format and C95 schema. Its exact hosted run
[37193138334](https://github.com/dragoshont/xodus-macos-app/actions/runs/37193138334)
succeeded; the app owner reports 398 checks and editable SVG verification.
The parent independently verified the exact run SHA, successful job metadata
and four zero-failure log summaries totaling 398 checks.

The same continuity reviewer closed the combined exact producer and consumer
source deltas without significant issues. Independent source assertions
confirmed all eighteen matching pairs, unchanged dependencies and schema,
diagnostic privacy, original signature-verification input and retained session
mutation gates. The reviewer did not rerun the supplied native checks or certify
the live failure's cause.

The backend owner sealed the exact public LF source archive with SHA256
`9d1366907990d156d540c65be4ab7335ce5fae2ee1acc92d1e11bfeec9f583b0` and
native validation evidence with SHA256
`773767f6f288519033abf57802dedf57a1bf1343949e1c48d2eab9c03654fa01`.
All 190 archive files match the published source and native mirror; the eight
Rust files match the actual tested code. The evidence explicitly separates
earlier baseline/fixture/tool failures from the final passing checks.
The parent independently verified both sealed hashes, sizes, owner-only read
modes and ownership on the Mac, the 190-file archive without links, and the
evidence's exact source pin and native check totals.

Source closure is not a working-login certificate. The parent authorized an
additive immutable CLI build followed by separate app signing, fresh owned-pair
checks and an actual native retry under the user's existing authorization.
The backend owner sealed the new unsigned CLI with SHA256
`8a4b8d56aad18841326834e5ad57860072560f4739963323875ffa52d41a545b` and
provenance SHA256
`2d7b372e3df8030ade83952d35f73f6cae74cf61075082ddd9aabd08c62438a6`.
The parent independently verified its arm64 executable header, immutable
ownership/mode/size/hash and exact reviewed source/native-proof provenance.
Only inert help and anonymous HELLO were exercised by the builder; existing
capabilities and original artifacts were preserved.

The app owner then built and separately signed the new pair, preserving the
unsigned original. Its signed engine has SHA256
`125a05dd0a2a577cc8fc71e4d3eaf324b9c70b3d409a54fd5b0eeef17ad45cf4`;
the app executable has SHA256
`b168a78e42bffc40bbb8e65379a3b117734939b4c2b4926f6aac06746e199607`.
After fresh idle/noncommitting guards, the older pair was gracefully retired
without signals. The parent independently verified the new bundle signature,
both executable hashes and the specific app/engine process relationship.
The embedded engine's actual path is
`Contents/Resources/XodusEngine/xodus-cli`; the previously recorded MacOS
subdirectory must not be assumed.

One actual native Sign In action on this new pair still failed at
`AUTH_INVALID / devicePreparation / tokenCipherInvalid`. No pending progress,
cancel control, owned auth child or owned auth window remained. The failure is
from the new engine, not a stale old deployment. It narrows the refusal to
serialized token XML bounds, reparsing or encoded cipher structure, without
establishing the particular cause. The failed Account state is retained while
the backend owner traces those guards with isolated source/model fixtures.
There is still no working Microsoft login window or authenticated Store proof.

The backend owner's pinned comparison with inherited source
`08b06d38c993072043e51c854213e81e662165af` identifies a material distinction.
That original CLI stored serialized EncryptedData without the later token XML
bound, reparsing, cipher-encoding, expiry and device-layout admission checks;
its cached-device branch skipped validation and re-authentication when both
entries existed. At deployed source `2acb452a7ee66b2c9d3ad75ecf85e2be2f94fbc3`,
the legacy CLI and management worker share the stronger device checks and the
same login handler, client ID, native webview and SOAP exchange. The management
worker starts with its isolated owned namespace, not the original Xodus Service
profile. Thus the shared changes also affect current legacy CLI behavior;
they are not merely a different launcher login UI. A cached original CLI path
and a fresh managed attempt are not equivalent. The user's reported successful
original login has not been freshly reproduced, and this comparison does not
establish the live rejected condition. The final isolated comparison covers
32 SOAP metadata combinations in one parameterized synthetic test: checked
conversion and saved memory match the original converter field-for-field.
A separate artificial invalid-cipher fixture passes original serialization
but fails current admission without writing memory. This demonstrates the
stricter guard, not rejection of proven-valid provider data or a verified
acceptance defect. No original-profile credential reads or imports are
authorized.

The parent and sole app owner agreed three more static reasons before producer
implementation: `tokenXmlBoundInvalid`, `tokenXmlParseInvalid` and
`tokenCipherEncodingInvalid`. They retain all eighteen existing pairs and the
coarse cipher reason, without changing admission policy or the exact three-key,
AUTH_INVALID-only contract. Consumer source
`e5a573aaca94f4cee46f591f9f927fcd0df0f782` is published and frozen; its exact hosted
run [37196334163](https://github.com/dragoshont/xodus-macos-app/actions/runs/37196334163)
passed 440 checks and editable SVG verification. The parent independently
verified the exact SHA, successful job and four zero-failure suite summaries.
Final producer source `baf92bc204755a12d956dca657b7e45989ec6991` is published and
frozen. Its native evidence is explicitly a scoped composite: an initial 56
selected checks, followed by the final device group of ten after test-only
comparison additions, plus 47 unchanged checks. It is not a single 57-test
run or an unfiltered full-suite certificate. Production Rust remained
unchanged between those runs; core lint was rerun, with the other qualified
checks retained.

The parent independently verified the sealed source archive with SHA256
`165e869e9b16b4fa405f21b54bd73eb33522e3fcc2a24c326a95e8e574988f75` and native
evidence with SHA256
`fdcac5e40f780b451fa5f4cf696353a05d5a9a0a23bf2fd9c17cbd02b08a339c`,
including owner-only modes, stable regular-file identities and no archive
links or unsafe paths. A separate bounded archive manifest comparison matched
all 190 public source files exactly to the final committed Git blobs.

The continuity reviewer then closed the combined exact producer and consumer
deltas without significant issues. Independent source assertions confirmed
twenty-one matching pairs, identical canonical schema and unchanged
dependencies, admission conditions, cryptographic paths and authentication
lifecycle gates. This is source closure and diagnostic refinement, not an
authentication fix.

The subsequent additive immutable CLI has SHA256
`542e855cd2e2131b391f772590a16dfc8f22704243c9c0b4bcea419510599846`;
its provenance has SHA256
`88a9379699b945f8eef11b520a6d83417c22c852a2dfa324b39da3cf72c9a808`.
The parent independently verified both owned, stable regular files, exact
sizes and modes, the arm64 header, linker ad-hoc signature and reviewed
source/native/build provenance. Only inert help and anonymous HELLO were
exercised by the builder. The immutable native certificate's historical
pending-review wording is retained, with actual combined source closure
recorded separately in provenance.

After separate-copy signing and fresh owned/noncommitting transition guards,
the app owner gracefully retired the older pair without signals and performed
exactly one native Sign In action on the new pair. The signed engine has SHA256
`1d2d0847ed14f156f47a8671287b0833119972afbce9471104e657240a6abdaa`;
the app has SHA256
`95c22c56eede8164f48b976b0ebbf960c7182f04c62b0597b5308e109cef541e`.
The parent independently verified their hashes, arm64 headers, bundle signature
and specific owned app/engine process relationship. The actual attempt failed
at `AUTH_INVALID / devicePreparation / tokenCipherEncodingInvalid` before a
Microsoft window was observed. This identifies the added ticket-decoding
predicate, not the provider's actual alphabet, padding or token format.
The failed Account remains stable; the same engine must not be retried.

The backend owner's subsequent consumer trace identifies a source-contract
defect: the Passport request builder forwards the legacy device ticket as
opaque XML, either verbatim as a BinarySecurityToken or reparsed and re-emitted
as EncryptedData. It does not locally decrypt its CipherValue. HMAC derives
from the separately issued BinarySecret; SOAP-envelope ciphertext and nonces
have their own strict decoding and cryptographic verification paths.
The added independent Base64 requirement on the opaque ticket is therefore an
unsupported admission assumption at this boundary. The scoped compatibility
repair at `e7e61fa820b771099fd90516ccaca056f265966d` retains bounded, reparsed,
nonblank, unmodified ticket
validation and the separate proof, signature, HMAC, AES, nonce, scope, expiry,
namespace-isolation and atomic-commit safeguards. It intentionally corrects
shared legacy-CLI and management admission; it does not globally relax XML
decoding or establish the provider's actual ticket encoding.

Two native regressions fail against the exact preceding producer with
test-only additions: actual checked managed storage and a checked ticket
after signed-envelope verification and AES decryption. The correction passes
those paths and preserves the issuer ticket in both actual request builders;
signed-ciphertext tampering remains rejected. The backend owner passed 59
selected checks in one bounded validation run, plus the qualified lint,
build checks, formatting and unchanged contract corpus. A test-only outgoing
versus incoming signature-model inspection error was corrected separately;
production signature parsing was not changed.

The parent independently verified the new immutable source archive with SHA256
`40e2a6a5751313ed82f62d3f6b6e5674fd01362fecc09a683988714f91999aa4` and native
evidence with SHA256
`039d9a8fbfa912e24516f9ad34f265e02f61f6f5baea04187c78659b8a4a359a`,
including ownership, stable identities, owner-only modes and all 190 archive
files matching the final Git blobs exactly. The continuity reviewer closed
the exact correction without significant issues, confirming unchanged
cryptographic paths, authentication mutation gates and compatibility with
the unchanged twenty-one-reason consumer. The historical encoding reason
remains compatible but is no longer emitted by opaque-ticket admission.
The subsequent additive immutable CLI has SHA256
`304c249ae24fc187533865ebb3d61cd40cec812629c537692e009f54bec22349`;
its provenance has SHA256
`647094684c9f90d50d30a6cf911baa5d3c4d9bdeb50b056f67f8a4a79e272bb5`.
The parent independently verified both owned, stable regular files, their
sizes and modes, the arm64 header and exact reviewed source/native/build
provenance. The CLI-only build retained the native Keychain-backed feature
profile, with no plaintext fallback. Only inert help and anonymous HELLO were
exercised by the builder; old artifacts were preserved.

The app owner packaged the frozen consumer's native release stage and a
separately signed engine copy with SHA256
`4d04fd6c98478574f8b0413de3a2422cebf66dce9c72357629c38cf02e2dbf32`.
The app executable has SHA256
`97e423985d1e5193db2cd80b6f2aa9ce546cc167d91ec2337a4653f3dc0bfabe`.
After fresh idle/noncommitting guards, the older pair was gracefully retired
without signals and its bundle preserved. On the new pair, an authentication
flow was already active before the next agent Sign In guard; the guard refused
before pressing. The agent performed zero Sign In presses on this pair.
The activation source is unknown.

The app owner observed one native authentication window on the owned worker,
with the exact worker-to-engine-to-app process relationship and a pending
Cancel control. This establishes passage beyond the previous device-ticket
preparation refusal, not successful authentication or credential commit.
At 2026-10-04 11:55:45 UTC, the parent independently verified both executable
hashes, stable owned regular-file identities, arm64 headers, deep bundle
signature and the reported processes' ownership, executable paths and parent
relationships. That read-only process check did not inspect a window, provider
page or authentication status; process presence is not fresh window proof.

The user subsequently reported a face/fingerprint prompt. Whether this is a
Microsoft passkey screen, macOS authenticator dialog or frozen embedded view
has not been established. Human-controlled password/code/consent interaction
remains required. No automatic retry, authenticator approval, account-setting
change, provider-page capture or client-ID substitution is authorized by that
unclassified symptom. At that point, the active flow was preserved.

The user has since explicitly paused sign-in to test games and requested
source-only native Swift app updates. Shared Mac desktop ownership was released
for those tests. Authentication recovery, provider-window inspection, retry
and deployment remain paused; this handover does not establish successful
sign-in or credential commit. The user's subsequent anonymous launcher preview
and single Apple Games reference-window screenshot are narrow UI exceptions,
not permission to resume authentication or inspect other game windows.

### macOS toolkit audit: source and deployed-file evidence

The initial audit was scoped to authentication first, then modern macOS UI.
Before the later source-only UI request, the sole app owner completed a
source-only audit of frozen consumer
`e5a573aaca94f4cee46f591f9f927fcd0df0f782` and read-only metadata from the exact
deployed app/engine pair above. No UI source changes, build, deployment,
foreground action, accessibility navigation, authentication query or capture
were performed for this audit.

The main app genuinely uses SwiftUI and AppKit, not UIKit or Electron.
The app owner verified Mach-O SDK 27.0, minimum macOS 14.0, direct AppKit and
SwiftUI framework linkage, and no declared external Swift package dependencies.
The absent SDK fields in the hand-written plist do not establish an old SDK;
the binary supplies that evidence. The separate Rust-hosted login webview is
not the main launcher's UI toolkit.

The toolbar already uses standard toolbar items, segmented navigation,
Account controls and unified toolbar styling. Available-on-26 paths use actual
system `glassEffect`, `glassProminent` and toolbar spacers, with earlier-system
fallbacks. These are positive source findings, not proof of current compositor
appearance. The audit identified four P2 review items:

| Surface | Pinned source evidence | Bounded correction candidate |
| --- | --- | --- |
| Window bridge | `PreviewWindow.swift:7-12` overrides titlebar/content geometry, background and opacity after creation. This does not prove Liquid Glass is disabled. | Prefer scene, stock toolbar and safe-area ownership; retain only compatibility mutations demonstrated necessary by a safe visual comparison. Do not blindly make the window transparent or place content under traffic lights. |
| Search | `NativeSearchField.swift:16-26,39-41` removes the native bezel, search/cancel cells and semantic text appearance. `LiveRootView.swift:85-100` and `RootView.swift:168-190` independently compose replacement visuals. | Share one scoped native search surface; restore standard search/cancel controls and semantic appearance while preserving bindings, focus, disabled behavior and search placement. Actual contrast remains unverified. |
| Account sheet | `LiveAccountView.swift:17-25,70-110` combines a non-scrolling body, fixed decorative header/width and horizontal action footer. Longer states and smaller available heights present a layout risk, not observed clipping. | Use a bounded scrolling body and stable adaptive native footer; preserve authentication predicates, identifiers, keyboard cancellation and dismissal safeguards. |
| Startup activation | `XodusPreviewApp.swift:9-12` unconditionally requests `activate(ignoringOtherApps: true)`. No actual focus theft was observed. | Let LaunchServices/AppKit handle normal activation; isolate any necessary direct-executable development activation and use the current API. |

These recommendations follow Apple's
[Liquid Glass adoption guidance](https://developer.apple.com/documentation/technologyoverviews/adopting-liquid-glass),
[titlebar property semantics](https://developer.apple.com/documentation/appkit/nswindow/titlebarappearstransparent)
and [activation API guidance](https://developer.apple.com/documentation/appkit/nsapplication/activate(ignoringotherapps:)).
The parent independently retrieved the latter two official DocC descriptions.
Framework imports, accessibility roles and successful compilation alone are
not a native visual-conformance certificate. Live appearance, scroll-edge
behavior, contrast, transparency/motion preferences, resizing and
keyboard/VoiceOver focus remain unverified until the human flow is safe.
No mobile-specific conformance score or touch-target rules are applied to Mac.

Further review of the official
[macOS 27 release notes](https://developer.apple.com/documentation/macos-release-notes/macos-27-release-notes)
and [TabsPickerStyle](https://developer.apple.com/documentation/swiftui/tabspickerstyle)
establishes a specific adoption requirement: navigation pickers can use
`.pickerStyle(.tabs)` on macOS 27, with `.segmented` retained on earlier
supported systems. The new style distinguishes navigation from value selection
visually and announces options as tabs to VoiceOver. The source correction
must preserve route bindings, shortcuts, search placement and the macOS 14
deployment baseline. Native UI corrections are now frozen in source at
`ced5ff9778ae00538d3d286d49997d7ba2d67ab1`; no changed app has been deployed or
visually certified.
Using the actual API requires an SDK/Xcode 27 build toolchain, not a compiler
version proxy or runtime availability check alone. The existing native CI job
is authorized to select the standard `xcode-27` runner and assert its actual
Xcode, macOS SDK and OS versions; an older hosted result does not validate the
new API or source.

### Apple Games header: frozen native source

The user selected the actual Apple Games window as the visual reference and
requested a single reference screenshot before the next header correction.
The first fresh window-capture command failed without producing an image.
The parent instead inspected an existing actual Apple Games window capture,
clearly labeled as an earlier reference rather than a fresh screenshot.
It shows a centered native navigation/search group, separate trailing account
control and artwork-toned translucent titlebar without app-name text.

The in-window navigation toolbar should sit over the hero artwork, with native
translucency naturally taking on the artwork's color. This is not a request to
modify the global macOS menu bar, manually sample a tint or replace the system
toolbar with a custom overlay.

The same shared preview/live layout must keep functional native search with
the centered navigation group, existing navigation and keyboard shortcuts,
and catalog/library query scoping. No app-name/title text should be visible in
the window header; app, menu and Dock identity remain intact.
System window controls, dragging,
resizing, accessibility and safe-area spacing must be preserved, including
system contrast and Reduce Transparency behavior.

This explicitly supersedes the earlier preserve-header-layout scope, but does
not authorize an unrelated redesign. The requested shared live/fixture header
is implemented in the frozen consumer source, including grouped principal
navigation, stock compact search and hidden visible title. The preview shown
earlier is an unchanged work-in-progress stage, not deployment of this frozen
correction. Its current closure state is not inferred.

The targeted native capture remedy reported no existing screen-capture
permission and stopped before enumeration, capture or a permission request.
There is no fresh screenshot. No privacy-setting change, other-window capture
or preview closure is authorized by that failure. Source-only work must not
prolong the reference desktop slot; the user's preview-close decision is
preserved.

After reference inspection, the user explicitly returned desktop ownership to
game testing. The earlier preview-close gate was released without inferring
that the preview had closed. Further launcher/reference focus, input, capture
and window checks are paused. Source implementation and nonactivating
validation continue independently of the desktop slot.

The parent independently verified the consumer's published commit and tree
`549b6724e340b8f014593556fe5c0f11760af7ec`. The existing push
[hosted run 37206889438](https://github.com/dragoshont/xodus-macos-app/actions/runs/37206889438)
completed successfully on that exact source with Xcode 27.0 build `27A266a`,
macOS SDK 27.0 and macOS 27.0. Its logs contain 548 passing checks: 14 core,
267 management, 50 presentation, 144 mock native-session and 73 private-host
checks, plus reproducible SVG output. The latter use detached synthetic WebKit,
with no visible window or activation, not a Microsoft provider session.
Compact search policy, actual detached 32/220-point editor geometry and native
control behavior are covered; compositor tint and whole-toolbar live geometry
remain unverified. The initial paired review found two authentication-host
defects; the separate consumer correction and scoped re-review below close
those source blockers without granting live visual or operational approval.

### Swift-owned authentication host: source migration

The approved source migration replaces the managed Rust/Wry UI host with a
dedicated Swift AppKit/WKWebView helper. Rust retains device preparation,
cryptographic verification, Passport SOAP issuance and Store-session commit.
Issuer material stays on a bounded private worker/helper channel, outside
public management JSONL and app models. The new topology requires concurrent
parent-channel EOF monitoring and a cancellation fence through issuance,
helper closure and final handoff; the preceding worker did not monitor that
channel continuously during issuance. Parent/worker death, cancellation,
deadline and late-result behavior require neutral process regressions.

This is native UI ownership and failure-propagation work, not a proven repair
of the reported biometric stage. Read-only inspection of the exact signed
app and engine above found ad-hoc signatures, no TeamIdentifier and no embedded
associated-domain or browser public-key-credential entitlements.
[Apple's embedded passkey requirements](https://developer.apple.com/documentation/authenticationservices/supporting-passkeys)
cannot be supplied merely by translating Wry to Swift or adding a Microsoft
domain locally. The fork's separate Xbox Live OAuth/XSTS route has not been
shown equivalent to the required Passport Store proof; a system-browser URL
callback is not a substitute for the existing ServerData exchange.
The initial source migration was frozen at producer
`94353b5cc3196a2b73b89655855ec50c31d35b81` and consumer
`ced5ff9778ae00538d3d286d49997d7ba2d67ab1`. The parent independently verified the
producer commit and tree `96bf643d6ef58364546513dba340ecbc27d1466f`.
The producer's final canonical Git/LF native run reports 80 selected passing
checks, including three actual neutral process-chain cases, with lint/check,
format and public/private corpus checks. Earlier CRLF-input intermediate runs
are not this final exact-source qualification. It did not execute the actual
Swift helper or authenticate with a provider.

The parent independently checked the sealed source-only archive
`native-swift-host-source-94353b5.tar` (5,222,400 bytes, SHA-256
`40c39324cf9cd56e6299dc6becf074ae5d32c911175d07b82b19ef1ede982419`)
and adjacent certificate (27,259 bytes, SHA-256
`56c140a930730005f1374d3b978ecb9db8b0d8aa51726f99fb1c3d446f062171`).
Both were stable, single-link regular files owned by UID 501 with mode `0400`,
opened without following symlinks. This verifies the immutable artifacts'
integrity; all 195 archive files also match all 195 published Git blobs, without
disk extraction. Their contents were not changed, and artifact integrity alone
does not grant paired review approval.

The implementation retains the original remaining deadline, continuously
monitors the parent channel, and requires matching clean helper closure,
worker exit and EOF before parent completion. Nonblocking worker-exit polling
keeps cancellation and diagnostics dispatchable. The consumer's canonical
metadata-path equality repair preserves file identity and no-follow checks;
its close acknowledgement awaits the in-flight issuer-data writer and refuses
a masked write failure. Public C95/21 and the private channel schema are
unchanged. The new retained paired review uses these exact source deltas and
does not inherit the earlier opaque-ticket review's closure.

The review found two reproducible consumer blockers in the frozen `ced5`
baseline. `Sources/XodusAuthHost/LegacyBridge.swift` rejects the inherited
direct flat seven-string DA notification because it recognizes only a nested
`DAProperty` wrapper. Separately, continuation handling in
`Sources/XodusAuthHost/AuthHostMain.swift` can send readiness before the
outstanding DA writer finishes, causing a false private-channel protocol
failure. Both are corrected separately at consumer
`6196399e7390eca854a5c50a1a4356287041c565`, tree
`fc7b0d7e2225f5572fa10d96384d4ef1a8369deb`; producer `94353b5` remains unchanged.
Direct exact-seven-field notifications now use the existing strict decoder,
with malformed/type/extra-field rejection and wrapped/context behavior
preserved. Navigation readiness and close acknowledgement share an output
fence that awaits the writer and rejects its recorded failure. A deterministic
anonymous-channel test holds writer completion after full frame delivery,
reproduces the unfenced failure and confirms ordered readiness and failed-write
rejection; the scheduling hook is nil in production.

The retained review inspected the immutable six-file correction and closed
both original blockers with high confidence, finding no significant new
regression in that delta. Its disposition is limited to those corrections;
the separate runtime-planning source and future consumer wiring were excluded.
The owner reports a new native release with 562 passing checks. The parent also
independently verified the exact correction's normal push
[hosted run 37208819522](https://github.com/dragoshont/xodus-macos-app/actions/runs/37208819522):
Xcode 27.0 build `27A266a`, SDK 27.0, macOS 27.0, and 14 core, 267 management,
50 preview, 144 mock native-session and 87 private-host checks, all with zero
failures, plus reproducible SVG output. Detached synthetic WebKit remained
nonvisible and inactive; no provider request was involved. This is fresh
correction evidence, not inherited qualification from the baseline's 548 checks.

At that source-review checkpoint, new operational artifacts, deployment and
authentication attempts remained separately gated and paused. Passing source
fixtures and hosted compilation do not establish a live sign-in, passkey
capability or successful Store commit.

### Native login readiness follow-up

Windows-only source tracing confirms that producer `9ef0f2`, which contains
the unchanged authentication source `94353b5`, accepts the consumer's three
explicit native-helper flags. An absent binding fails authentication rather
than falling back to the managed Wry host. HELLO's platform/feature capability
advertisement is not helper validation or engine-source attestation. The
previous installed engine predates these helper flags and cannot activate
the reviewed Swift-host migration.

The consumer's packaged both-missing helper/receipt case formerly returned
an absent binding, allowing a connection to look sign-in-capable before the
backend rejected authentication. Additive correction
`6a2103cfd0e2d263918039122990ac9492ec17f3`, tree
`797e111744d180b345542380229d6fb9367e7e6b`, now rejects that packaged case
before engine launch and preserves unpackaged anonymous/development checks.
Helper validation failures receive an actionable helper-specific error;
failed preflight leaves no owned engine to retire. The six-file delta leaves
the authentication helper, DA/writer fences, Quit coordinator, C95, runtime
provider policies, packager and workflows unchanged.

Retained review turn 17 grants scoped source closure with no significant
issue. The parent independently verified the exact normal push
[hosted run 37226882643](https://github.com/dragoshont/xodus-macos-app/actions/runs/37226882643):
Xcode 27.0 build `27A266a`, SDK 27.0 and macOS 27.0 passed 15 core,
358 management, 60 preview, 154 native-session and 87 private-host checks,
674 total with zero failures, plus SVG reproducibility. The six new
helper-admission checks are fresh evidence, not inherited 668 qualification.
This is source/fixture evidence, not a new Mac Release artifact or live login.

The user authorized an isolated matching native engine build and a
controlled human login test on October 4, and the game-testing owner released
a bounded CPU/foreground slot. Source verification and staging completed,
but the backend confirmed that compilation never started: no new CLI,
release seal or provenance receipt was produced. Before packaging or launch,
the user changed priority to game testing and the full Mac slot was returned.
No app signing, packaging, launch or agent Sign In action occurred.
A future separately authorized controlled test may use
the independently qualified `43d0d1` app/helper artifacts with a freshly
verified helper receipt and matching new engine; it must not mix `6a2103c`
source claims into that older package. The old installed bundle is preserved.

The October 5 resume is bounded launcher UI/source work with focused neutral
checks and the existing isolated hosted CI. It does not renew the previous
Mac build/foreground lease or authorize account sign-in, Microsoft/MFA/consent
or Keychain prompts, deployment, or interaction with active human windows.

No public versioned engine-provenance loader exists yet. General admission of
arbitrary engine paths or developer overrides remains a product gate.
For the one controlled test, externally pinned unsigned-engine provenance,
exact source/build identities and separately verified signed engine/helper
copies are required; untrusted receipt claims, HELLO and a helper hash alone
are insufficient. The selected engine must be the verified bundled copy,
without an environment or remembered-path override. Agent Sign In presses
remain zero; the user handles any Microsoft or Keychain interaction.

### Freshness-aware native account presentation

Additive consumer `8412f2c813ee8e29da6d1f19a847a157f21d4115`, tree
`b8eaee185248ad47119cc8146c0e14af6d65baf0`, fixes a concrete presentation gap
over `6a2103c`. A failed account-status read already disabled mutations but
retained an earlier credential snapshot for safety. Some views still used
that snapshot to show a filled profile icon, saved-sign-in Library copy or
expired-sign-in removal advice. The shared `currentCredentialState`,
`accountSymbol`, label and explanation properties now require a current
status result and are consumed by the actual toolbar, Account, Settings and
Library views. Retained snapshots remain available for lifecycle safety,
without being presented as current account evidence.

HELLO alone now displays connected-but-unchecked account state. An
unconfirmed pending flow keeps its cancellation fences and displays an
unknown outcome. Confirmed pending copy conditionally refers to a Microsoft
window instead of asserting that one opened. Current saved credentials still
do not establish PC ownership, package access or permission to play.

Fourteen actual neutral `LiveSession` regressions and one live/fixture
presentation check cover saved-status failure and recovery, expired-status
failure, HELLO-only state, pending uncertainty and disconnection. The new
saved-profile child trace contains only the three requested `auth.status`
reads, not sign-in or logout mutations. The parent's direct bounded source
review found no significant tightly coupled issue. Independent raw Git checks
confirmed all 31 protected source/resource blobs and the session's mutation
predicate prefix and failure/authentication/lifecycle suffix were unchanged.
No shared Mac action, real account operation, deployment or human-window
interaction was performed for this UI slice.

The parent independently verified this exact source pin and normal push
[hosted run 37288191441](https://github.com/dragoshont/xodus-macos-app/actions/runs/37288191441):
Xcode 27.0 build `27A266a`, SDK 27.0 and macOS 27.0 passed 15 core,
358 management, 61 preview, 168 native-session and 87 private-host checks,
689 total with zero failures, plus SVG reproducibility. The logs confirm the
new saved-status failure/recovery, expired uncertainty, HELLO-only and pending
presentation cases executed using neutral children. This closes the bounded
source/UI slice, not live authentication, a new Mac package, owned-library
enumeration, installation, playability or live visual qualification.

### Production-hardening source and controlled release

The October 5 production review identified shipping fixture/test entry points,
synthetic live hero art, arbitrary engine selection, premature empty-search
copy, startup-query replacement and a fixture-transition planning hazard.
The coherent hardening batch and additive corrections culminated in app
`00ba51fd5e48f6298d27a56e75f491f6f697696f`, tree
`9869d8ac428bd3f9ed2fa7418579097614066036`. Earlier `f9e6f97` and
`5343b86` failed conditional-compilation checks; `24243a3` compiled but failed
the relocated neutral-child test harness. None qualifies release artifacts.

The coordinator independently verified exact normal push
[37314629896](https://github.com/dragoshont/xodus-macos-app/actions/runs/37314629896)
for `00ba51f`: hosted Xcode 27.0, build `27A266a`, SDK 27.0. Shipping Release
launcher/helper and shipping XCTest compile. The run passed 15 core,
358 management, 61 presentation, 176 neutral native-session and 87 private-host
checks, plus 23 shipping assertions in one XCTest method, 22 portable packaging
checks, five launcher and two helper forbidden-argument checks, fixture-resource
exclusion and SVG reproducibility. These are source/neutral gates, not a real
Microsoft login or gameplay test.

The final read-only `8412f2c..00ba51f` review found no high-confidence release
blocker within the frozen hardening requirements. Shipping physically excludes
fixture state, art, views and test resources; developer selection/injection
cannot choose its engine. Freshness-aware presentation remains in place.
Search loading is distinct from confirmed empty results, and generation fencing
prevents late startup seeding from replacing a user's query. The live Settings
fixture transition was removed. Protected helper authentication, DA/credential
writer and Quit implementation and C95 resource remain unchanged; management
adds pair revalidation before launch and authentication mutations.

Shipping admission uses only fixed bundle paths and compiled stage-generated
source/engine/helper pins. Repository defaults are nil and fail closed before
management or pure planning. Helper receipts must match the compiled app source
and helper identity; file-descriptor ownership, link, mode, size and hash checks
are repeated immediately before launch. Receipt claims and HELLO are not trust
authorities. This is controlled local operator pairing, not a Developer ID
distribution or anti-tampering certification.

The exact reviewed producer remains
`9ef0f298481fb48840734b538e0f6d22e1c98ff3`, tree
`8b2f7abb54f91e347afe013eee18c93873b111a5`. Its fresh sealed arm64 Release CLI
has SHA-256
`ca86296dfdab23c63c7ff7c5428f9e2feb2f9b4bf6757072a64dbe7695c34d06`,
21,315,008 bytes, mode `0500`, UID 501. Adjacent provenance has SHA-256
`d72edb61fb45b0c5f9e91343d4e68ee25e2345ea0df9a0eeb7b4f13ae9d36f6d`,
46,234 bytes, mode `0400`, UID 501. The coordinator freshly reverified both
using no-follow descriptors, single-link ownership and stable file identities,
without executing the engine. Provenance binds Release optimization 3,
debuginfo 0, disabled debug assertions, exact source/tree, jobs 1/incremental 0
and only the reviewed management `live` and Apple native-Keychain features.

The user authorized autonomous isolated packaging and rollback-preserving
redeployment using the existing app owner, not new workers or architecture.
There is no CPU lease or build-slot prerequisite; bounded builds may proceed
in parallel. Only foreground interaction and exact shared mutation targets
require exclusive coordination. The frozen app source must be archived from immutable Git bytes; uncommitted
Architrave adoption files are not package inputs. The hardened packager verifies
external unsigned engine/provenance pins, signs separate copied engine/helper
bytes, generates compiler pins, builds/signs the launcher and verifies final
resources, signatures, helper receipt and raw source inputs. No completed
package or deployment is claimed in this entry.

The rollback target is the existing public bundle at
`/Users/dragoshont/xodus-app-tooling/app-foundation/dist/Xodus.app`.
Historical launcher `97e42398...` and embedded engine `4d04fd6c...` identities
are not current closure evidence; recheck the bundle and app-owned process
safety before replacement. Preserve prior bundle bytes, user configuration,
Keychain, accounts, game prefixes and saves. Foreground is not released:
skip GUI launch rather than interacting with human game/auth windows.
Anonymous smokes must not invoke account-status reads, credential APIs or
authentication mutations, nor claim that the user's account is signed out.

The user's first-release product decision is a genuine, separately installed
official CrossOver dependency. This is a release blocker for the next shipping
candidate, not a reason to represent `00ba51f` as satisfying a later requirement.
The existing app owner will implement one additive prerequisite/default/
presentation slice and run fresh hosted CI before packaging its immutable
source. Any already-built `00ba51f` package remains a nondeployed candidate.
Read-only installation identity detection must distinguish absent/unverified
CrossOver from an observed official installation without executing it or
claiming a license. New/unset profiles prefer detected CrossOver; explicit
persisted alternatives retain their selection with experimental status and
explicit confirmation before use. Library, Account, Settings and runtime-plan
readiness must agree without promoting dependency presence into playability.
Other Wine/GPTK runtime tracks remain
experimental alternatives, not supported first-release choices. This does not
authorize redistribution of commercial binaries or imply CodeWeavers
endorsement, detected installation, licensing, game compatibility or launch
readiness. Provider plans still report unknown installation/preflight/game
verification and `launchable=false`. Human Store login, live accessible UI,
authoritative inventory and install/update/play gates remain open.

The user's subsequent specification-driven-development direction is captured
in [the first-release specification](release-v1-spec.md). It maps runtime,
authentication, actual backend data, shipping exclusion, CrossOver regressions
and deployment requirements to acceptance evidence. The reviewed production
adapter supports public catalog/query and catalog-check jobs, but does not
advertise authoritative inventory; its empty installed snapshot is scoped to
the management registry and its package/game lifecycle is unavailable.
Developing UI against the schema must not promote these unsupported surfaces.

### First real anonymous Store-query evidence

The existing backend owner ran exactly one anonymous `catalog.query` using
the independently pinned `9ef0f2` Release engine, after HELLO, in a fresh owned
temporary state directory. Query `Halo`, market `US`, language `en-US`, limit 5,
returned live public Store results at `2026-10-05T13:30:00Z` in 2.797 seconds.
The source is `MicrosoftStoreEdge:v9.0/searchResults`, corpus
`publicMicrosoftStoreSearch`, completeness `partial`.

One resolved PC candidate, `Halo Infinite` (`9PP5G1F0C2B6`), came from
`MicrosoftDisplayCatalog:v7.0` with resolved language `en` and editions
`0010`, `0011`, `0017`. Every edition retains unknown entitlement,
installability, compatibility and inventory. Its `notInstalled` field is
management-registry scoped, not proof about external folders or bottles.
Four returned IDs failed Windows.Desktop metadata qualification; this is not
proof of missing packages, licensing or macOS incompatibility. A continuation
cursor was present but not followed, so this is not complete Halo coverage.

The response passed schema/correlation checks; stdout was 7,522 bytes and
stderr was empty. The exact child received EOF, exited zero and was joined/
reaped with no trailing output. Temporary anonymous state was removed.
Engine and provenance identities remained unchanged before and after.
No account, credential-store, helper, provider, GUI or game action occurred.
The public evidence receipt is SHA-256
`ab5975ab8bb496a597464be6e90a9246aaa08b767888cf455a17a0786e196556`,
8,530 bytes, mode `0400`, UID 501; the coordinator independently verified its
no-follow single-link hash/size/stable identity. This closes a real anonymous
engine network-to-wire check, not shipping UI, ownership, login or playability.

### CrossOver-first implementation and fresh qualification

The additive first-release slice is app
`95af7c0e07d84ef85672d3d1ea184955f97536bf`, tree
`a407e75dc6b9ad7cb411d6ece87f3095518b1e1c`. It adds off-main-thread,
read-only detection at approved CrossOver app locations. A fixed Apple
Developer ID requirement binds identifier `com.codeweavers.CrossOver` and
publisher team `9C6B7X7Z8E`; canonical bounded metadata, ownership/mode and
stable file checks precede the installed observation. Short version and build
remain distinct. The coordinator independently matched the actual
user-approved installation to that requirement: version `26.3`, build
`26.3.0.39832`, without executing CrossOver or examining licensing/game state.

Only a new/unset profile defaults to verified installed CrossOver. Explicit
decoded, edited or cleared selections are preserved. Alternative providers
and custom graphics are Experimental and acknowledgement-gated; selection,
component or observed-identity changes invalidate acknowledgement. Library,
Account and Settings use the shared prerequisite view. Missing/unverified
CrossOver does not prevent public browsing or native account setup, and
verified installation does not promote license, entitlement or playability.
The real reviewed engine accepted the observed CrossOver version in a pure
configuration plan while retaining `notInspected`, `notPerformed`,
`notVerified` and `launchable=false`; no provider or prefix was created.

The independent focused `00ba51f..95af7c0` review found no high-confidence
release blocker. Authentication/management, packager and hosted workflow
surfaces are unchanged. Exact normal push
[37318275541](https://github.com/dragoshont/xodus-macos-app/actions/runs/37318275541)
passed on Xcode 27.0/`27A266a`, SDK 27: 15 core, 358 management, 61
presentation, 215 neutral native-session and 87 private-host checks, plus
27 shipping and 22 portable packaging checks, forbidden arguments/resources
and SVG reproducibility. The 39 new native and four shipping assertions are
fresh evidence, not inherited `00ba51f` qualification.

The bounded CrossOver regressions have also resumed: NMS Xbox 7.5 and Hogwarts
Xbox 1.0.16 were directly user-confirmed working on their preserved controls;
Hollow Xbox 1.5.12620 reached its main menu and quit normally with child absence
verified. One launch each, no runtime/prefix configuration, download or auth
mutation, and no general save/game certification. The game owner released
foreground but preserved the user's NMS/Hogwarts windows.

The new isolated packaging target uses immutable `95af7c0` only; adoption WIP
and `00ba51f` are not release inputs. A fresh read-only check confirmed all
26 known public prior-bundle/root-artifact hashes, sizes, modes and ownership
remain unchanged. However, the old installed launcher and embedded engine
are still running with unknown operation/authentication state. The user was
unavailable to quit them normally. Do not force termination, replace active
bundle files, duplicate the UI or interact with an unknown auth window.
Safe staged build/signature/pair validation and anonymous engine checks may
continue; deployment and genuine packaged sign-in remain external gates.

The first native `95af7c0` package attempt stopped after helper compilation
and separate copied helper/engine signing, before launcher compilation,
generated pins or final receipt. The Mac Python rejects
`Path.write_text(..., newline="\n")`. Failed owned stage and signed partial
copies are retained; no installed bundle or original sealed input changed.
Additive `bc429f1fd03bf564b789d550c74c36851b04a09b`, tree
`2d3ba45c2ae2850c5812b4ee32d85414b13a2bec`, switches pin generation to exact
UTF-8 bytes and exercises the actual neutral generator CLI, increasing portable
checks to 23. The focused correction changes no Swift/auth/admission/resource
or workflow code. However, exact run `37319757608` failed one of the existing
215 native-session assertions: shared short-exit failure must retain child
ownership and block fixture/Quit. It does not qualify this successor. Diagnose
the precise test/child observation rather than weakening cleanup or suppressing
failure; a new package needs complete fresh green qualification and a new stage.

### Final staged native package and signed-pair query

Final source is `9be674d28edaa79f376cc79592a09bedcf3cd6b7`, tree
`3c951506850fe534ecec4e6e628eb768ab0135b7`. Exact hosted
[37323168881](https://github.com/dragoshont/xodus-macos-app/actions/runs/37323168881)
passed Xcode 27.0/`27A266a`, SDK 27: 15 core, 358 management, 61 presentation,
218 neutral native-session and 87 private-host checks, plus 27 shipping and
29 POSIX portable packaging checks, all zero failures; shipping arguments,
resource exclusions and SVG also passed. The deterministic neutral lifecycle
fix releases the short observation only after all four real waiters join and
holds the test child until explicit release. Production budgets, ownership,
authentication, DA and writer behavior are unchanged.

The native SwiftPM bundle uses `Contents/Resources`. Verifier follow-ups
`1dcd7f1` and final `9be674d` accept exactly one complete known layout and reject
duplicates, mixed/missing schemas, wrong hashes and matching-hash links.
The generated Swift resource accessor searches the packaged app's resource URL.
Failed `95af7c0` and `2aaba41` stages remain preserved and unqualified; no partial
signed copy or receipt was substituted into the final package.

The final new stage is:

```text
/Users/dragoshont/xodus-app-tooling/app-shipping9be-controlled-xi7xdyfz/packages/Xodus-controlled-pair.xgzNsv/Xodus.app
```

| Final artifact | SHA-256 | Bytes |
| --- | --- | --- |
| Launcher | `123f8fd349faed03ce487cdee8d541f48efcddb45d1978c5a48d96710dc0c0fa` | 3,349,680 |
| Native helper | `b434836b9ebc3966626f2736ac3cee42ad2b1eb9568c22aecec15c4ad1cbc673` | 333,472 |
| Separately signed bundled CLI | `55b0738da218b1b0fe4e1b8f29c86b515e244f83956e4e831ed90de824a9dc16` | 21,191,200 |
| Package receipt | `e8654311a643b2ce39ce23354bc63412ed049dc3c041c0565044f15777fe92a3` | 12,288 |
| Generated compiler pins | `330687de54f85c7a04dce5cec0c4b5bacdfc00a036b2a30aad72fe73a7279a47` | 762 |

Executables are UID 501, regular, single-link, mode `0755`. Signed CLI identity
is distinct from the original sealed unsigned `ca86296...`; original CLI and
adjacent provenance retain their exact hashes, sizes and sealed modes.
The coordinator independently verified the bound receipt, all three signed
identities, 55 immutable raw Git inputs (only the generated pin template
overridden), exact generated Swift pin recipe, canonical helper receipt,
all 21 final package files, unchanged C95/runtime schema hashes, native resource
layout, absent fixture/private bundles and system deep signature. No app,
helper or engine was executed during that independent verification.

Five actual staged launcher arguments rejected with exit 64; the helper was
not executed. The first anonymous smoke correctly obtained public data but its
session harness erroneously asserted top-level market/language fields absent
from `CatalogQuery`. It did not preserve the raw result before assertion,
so it was not certified. The corrected session-only harness preserves result/
error/cleanup evidence before assertions, validates per-product scope and
unknown anonymous access, and passed one positive/eight negative offline cases.
One explicitly authorized corrected query then passed, with frozen source and
package unchanged; there was no network retry loop or pagination.

At `2026-10-05T14:30:28Z`, the signed pair's HELLO plus single Halo US/en-US
query returned live `Halo Infinite` (`9PP5G1F0C2B6`), editions `0010`, `0011`,
`0017`, four explicit Windows.Desktop metadata failures, partial
`publicMicrosoftStoreSearch` coverage and an unfollowed cursor. Entitlement,
installability, compatibility and inventory remain unknown; registry
`notInstalled` is not filesystem evidence. The coordinator read and validated
the retained raw successful frame. Evidence receipt SHA-256
`67fb1f28e3a051d9c712c33bb0d3cc22db48f026d9cac36ef07105e906e9b107`,
1,299 bytes, was independently verified through a stable no-follow owned FD.
The child received EOF, exited zero and was joined/reaped; stderr, auth methods,
helper/provider/game operations and forced cleanup were zero.

Final closure confirms package identities, original sealed inputs and all
26 protected prior-bundle/root-artifact files remain unchanged, with no new
pair processes remaining. The coordinator's executable-only check confirms
old launcher PID 76334 and embedded engine PID 76336 are still active. Their
operation/auth state was not inspected. The user was unavailable to quit them
normally, so no termination, adoption, bundle replacement, duplicate GUI,
native authentication or human-prompt handling occurred.

**Disposition:** source and staged controlled-local package are verified;
deployment is not performed and rollback was not needed. The old installed
bundle remains the rollback/current bundle. Safe old-app exit, actual new
launcher UI/accessibility observation and one human-assisted Store login remain
external gates. Authoritative ownership and game package/launch capabilities
are still unavailable in the reviewed adapter; this package does not certify
them or the reported biometric/passkey method.

### Repository-local canonical qualification references

Architrave 0.12.1 binds source and gate evidence to one repository. Root Run
`xodus-ui-production-20261005` revision 20 therefore cannot promote app
`9be674d` CI/package evidence into root acceptance. Root adoption has its own
fresh deterministic PASS; shipping, review, package and product remain UNTESTED.
The following references provide traceability only, not root acceptance.

The existing app owner established exactly one app-local qualification Run,
`app-9be-qualification-20261005`, in an isolated clean checkout of
`dragoshont/xodus-macos-app` at `9be674d28edaa79f376cc79592a09bedcf3cd6b7`,
tree `3c951506850fe534ecec4e6e628eb768ab0135b7`. Native-enabled local
configuration is ignored and separate from the reviewed source; the original
app branch, user changes and adoption stash are preserved.

The coordinator independently read its public native status: COMPLETED,
revision 13, event cursor 14, `APP-QUAL: PASS`, fresh source SHA-256
`bd93942a23e3bfd28259f18de0731088a024a4d4619a7cdafb59e30383dafebe`,
no active/stale workers, and its finished deterministic worker retained.
Supported executors observed exact app CI `37323168881` and reran 28 portable
checks on Windows against that source:

| App-local gate | Authenticated receipt SHA-256 |
| --- | --- |
| `gate-ci-5578e093537944d18bb9f2bd8df996db` | `fa1f7b62378177256166d46ab0d76145e679284a0d07f21fcd43a1dc940cd572` |
| `gate-test-5273d3d257c9403086330af498a40698` | `54ed327a35e765e62bc7d1962bdbe0c21420ed627115c991265450a8c8ebbf4d` |

This qualification covers deterministic app CI/build/test evidence only. It
does not certify deployment, runtime behavior, human login, owned inventory,
gameplay or complete production readiness. Recording these references changes
neither root canonical state nor the genuine shutdown/deployment/login holds.
