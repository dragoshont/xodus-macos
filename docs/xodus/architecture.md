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

### Native launcher sign-in integration

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
No successful Microsoft sign-in, Keychain commit, entitlement, package
authorization or gameplay is established;
credentials and consent remain user-controlled.
