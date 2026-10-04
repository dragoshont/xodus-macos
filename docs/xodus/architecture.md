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

## Private native consent diagnostics

The management Account worker uses its inherited anonymous channel, not the
runtime socket. Failed consent stages are a closed static enum in the bounded
private handoff; the parent maps them to existing `AUTH_INVALID` with optional
three-string `details` (`category: nativeConsentFailure`, `stage`, `reason`).
The fourteen agreed pairs, including the original ten, are documented in the
management implementation ledger.
No raw exception, provider response, page content or credential is serialized.
The consumer uses only validated closed pairs and locally authored copy;
absent/malformed/unknown/extra-key details are stage unavailable.

A failed worker exit does not discard a validated failure handoff, but cannot
promote a successful session. Missing/invalid/unobserved outcomes remain stage
unavailable. Cancellation, expiry, proof, active-flow and already-started
Keychain commit semantics are preserved. The coordinator reports separate
continuity source-review closure for the diagnostic delta; it is not a fix or
precise explanation for an earlier unobserved failure and does not itself
authorize an account retry or a new deployment.

`devicePreparation/providerProofInvalid` represents the existing
`InvalidBrokerProof` content/proof rejection sites, not necessarily a signature
failure. Provider signing, nonce, verification, decryption and decoding errors
instead remain `providerRequestFailed`. Device authentication reuses the shared
checked selector for a bare response or exactly-one response collection, then
retains the same expiry, legacy/STS/cipher and 4096-byte version-four proof
checks before persistence. Empty/multiple/fault/undecrypted bodies are refused.
The collection compatibility correction requires its own continuity review;
it does not establish which rejection site caused the observed live failure.
Four explicitly agreed additive reasons distinguish registration fields,
response shape, checked token conversion and token structure. Each is a typed
static enum at its original rejection site, still using the same three-string
details and `AUTH_INVALID`. The older generic `providerProofInvalid` remains
valid. No raw provider value or new diagnostic field is carried; final native
test execution is separately gated by the host-resource blocker in the ledger.

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
no-argument service still uses its default credential profile and platform
runtime socket; the native launcher must not attach to or start that mode.

### Isolated native broker

The explicit `xodus-service --management-socket <absolute-private-Unix-path>`
entrypoint uses only the isolated launcher credential profile. It never falls
back to `/tmp/xodus.sock`, the ordinary CLI/service profile, plaintext keyring or
device provisioning. The existing parent directory must be local, owned by the
effective user, exactly mode 0700 and reachable without symlinks. Paths must be
normalized UTF-8 and at most 103 bytes. An existing entry is refused, never
removed/replaced. The created socket is mode 0600; only same-UID peers are
accepted. Parent identity/ownership/mode and exact socket identity/mode/link
count are rechecked after creation and before accepting peers. Cleanup uses the
retained directory descriptor and the exact original socket device/inode,
leaving any replacement unchanged.

The paired client configuration is `XODUS_RUNTIME_SOCKET`, containing **only**
that raw absolute Unix path. The broker receives its path explicitly in argv;
it does not read this environment variable or discover a global service.
No credentials enter argv, environment, management stdout, diagnostics or logs.
The isolated mode initializes no tracing subscriber even when logging variables
request trace output. Merely starting it or pinging does not read credentials,
provision a device, start consent or start a game.

The same XML wire uses Ping 1/2 and MSA 3/4. MSA requests have `ClientId` (exactly
16 ASCII hexadecimal characters), `AllowUi` and `MsaFullTrust`; `MSAFullTrust` is
also accepted for existing clients. Unknown fields fail. `AllowUi` is permission,
not a request to open a window: the broker always attempts silent issuance and
returns an explicit Rust consent-required error for an upstream user fault.
The wire has no invented error ABI: invalid/failed requests close without a
success-shaped ticket response.
The MSA payload itself is at most 4096 bytes and must be one flat
`MSATokenRequest` with only those known children. Unknown roots, nested fields,
attributes, duplicates/duplicate aliases and DTD/entity payloads are refused
before any credential getter; the general 65,535-byte frame bound is unchanged.

The handler reuses real NativeTokenBroker/SOAP device/user exchange functions.
It uses the actual stored username, checks the requested audience, token kind,
nonempty bounded ticket, expiry and complete user/device result instead of
positional collection assumptions, swallowed failures or panics. The response
has all four required fields: `Token`, `Expiry`, `DeviceRps`, `DeviceExpiry`
(UTC seconds). Management credentials are read noninteractively and rechecked
for the same complete live bundle before returning tickets, including user,
device, license and expiry fields, without depending on HashMap serialization
order. The broker never writes or
rotates the launcher bundle; native consent owns its atomic writes. Ordinary
legacy refreshed-STS persistence errors are now propagated.
The shared core `ManagementProfileStamp` also fences the manager's clone
mutation epoch, so identical recommits/logout invalidate prior snapshots.
Neither stamp contents nor raw credential-store errors are exposed.

Limits: eight concurrent connections; ten-second magic/payload reads;
two-second response writes; two-second credential IO result deadlines with four
permits held until already-started OS reads finish; thirty-second ticket exchange
including final profile reconciliation. Cancellation closes owned requests and
connections. The native process smoke verifies ping, refusal of malformed MSA
requests **before** credential reads, a subsequent usable connection and exact
owned-socket cleanup. Other tests use unnamed sockets and memory-only profiles.
`tools/smoke_runtime.py` is not live RPS issuance or runtime evidence.

The separate developer example `empty_management_broker` wraps the **same**
`isolated::serve`/management XML route with an empty managed `MemoryBackend`.
It accepts only `--empty-memory-fixture --management-socket
<absolute-private-Unix-path>`; no arguments or a missing/misplaced gate fail,
and it never initializes native credential storage. The production parser
rejects these three arguments before credential initialization; the lab gate
exists only in the example. A source-order regression checks this guard without
executing production. The smoke confirms distinctive gated refusal first, so
it never tests no-argument fallback on an unidentified executable.
All smoke pass/fail checks use unconditional guards, including this first
recognition check; `python -O` and `PYTHONOPTIMIZE=1` cannot disable them.
`tools/test_smoke_empty_runtime.py` runs the actual tool with mocked processes
and proves production-shaped refusal stops before no-argument/startup calls
in normal and optimized interpreters, without executing production.
It performs an in-memory absence lookup, not a Keychain/user-credential
read. Missing credentials return before device/account HTTP or mutation.
The real internal result is AuthenticationRequired, but the wire remains
zero reply bytes/connection closure, not an invented error frame or HRESULT.
`tools/smoke_empty_runtime.py` verifies ping before/after this valid-request
refusal and bounded SIGINT cancellation/owned cleanup. This native macOS-only,
no-plaintext Cargo example is an additive fail-only developer artifact, **not**
a new shipped memory mode, successful fake RPS, deployed broker replacement or
public runtime pair certification.

The public shim/core gaming API consumer is independently owned. Coordinator
reports the public `runtime/public-async` milestone
`f7d8d4eec2eb4aaab96230df29c76c6de2b060ad` wires the gaming COM and
XUserAddAsync/Result APIs to the explicit private RPS client before default HTTP.
Its malformed/expired/missing-RPS and loader/ownership checks are parent evidence,
not a successful fake RPS response or execution by this Rust worker. Same
retained review 33 opened parent-owned R17 in the standalone direct-core check:
queue termination notification and CloseHandle do not prove worker return
before FreeLibrary. The production bridge retains its core module and is
unaffected; the parent owns helper retention through process exit and validation.
No Rust backend or engine/broker replacement follows. The explicit-only
XODUS_RUNTIME_SOCKET/40-second client bound is unchanged, and no deployed broker
or approved GUI engine is swapped.
No management launch capability is enabled: live Store issuance, actual
user/device audience evidence, an exact public native shim/Wine/service pairing,
compatibility and signed/distributable runtime certification remain necessary.

## Package/license provider boundary

The existing CLI package/license helpers are not management installation APIs.
They now propagate credential, exchange, HTTP and malformed-payload failures
without panics or printing upstream credential/entitlement payloads. Package
responses must affirm `PackageFound` and match the requested content UUID.
Single-audience SOAP callers require one matching, future, nonempty compact
ticket; a fault or empty/ambiguous collection fails. License responses require
one decodable key and nonempty license data. These checks do not establish
ownership, file-hash semantics, an expanded manifest or runtime compatibility.

`api::xbox::run` takes the actual account username and returns
`Result<XstsResponse, XboxAuthError>`; `get_xsts_auth_header` likewise returns a
`Result`. Missing/ambiguous user claims, invalid header fields and expired/empty
tickets fail explicitly. CLI arguments, selection and valid wire formats are
unchanged. Management does not call these legacy helpers or their direct-write
download/extraction paths. A future authorized provider must combine the
implemented isolated profile reconciliation and bounded authenticated metadata
with complete identity and
manifest validation, and the durable verified staging/commit lifecycle before
any install capability can be advertised.

Management-profile package/license helper branches capture the complete valid
Store account, user/device proof and device license in one snapshot. The snapshot
is rechecked after authentication/provider calls and before returning a result.
Package rechecks surround the complete Xbox exchange, not each internal hop.
An epoch shared by manager clones rejects same-process mutations, including an
identical recommit; full bundle comparison detects external changed contents.
This is not a cross-process transaction or a guarantee that an already-sent
remote request can be cancelled by logout. Default CLI profiles preserve their
existing separate getters/late device-license lookup. Management credential IO
is off-actor, with a two-second total acquisition/result budget and four permits
held through completion of any started OS work after timeout/caller abort.
There is no ordinary-profile or plaintext fallback.

Manager clones also share an active-mutation counter. Management Store commit,
logout/removal and user-token replacement hold a RAII fence across their
blocking backend work, including nested/overlapping operations. Fresh snapshots
and stamp verification fail while any mutation remains active, even when the
backend still contains the previous valid bundle. Entry/exit epoch changes
invalidate outstanding stamps; error/unwind releases the counter without
reviving old stamps. No blocking lock spans OS IO. This fence is local to
shared clones, not independently constructed managers or processes.

Existing authenticated JSON calls now share streaming byte bounds and a
30-second total headers/body deadline: 1 MiB for Xbox authentication and 4 MiB
for package/licensing data. Typed errors reveal only static categories, limits
and HTTP status. Success callers require a successful status; licensing content
explicitly preserves structured denial while rejecting success-shaped HTTP
errors. Async caller cancellation closes its owned partial-body request.
The reader inherits the caller's redirect policy, so a management provider must
use its owned no-redirect client. Bounded response handling alone is not
authorization, full manifest validation or an installed-state transaction.

Encrypted device-key derivation is likewise fallible:
`derive_device_key` returns `Result<DeviceKey, DeviceKeyDerivationError>` and
rejects inconsistent size, unsupported version and corrupt ciphertext using
static non-secret errors. The license caller propagates the result; no assertion
or panic diagnostic can print the compared keys. Valid synthetic version-four
decode/derive tests preserve the previous output without implying entitlement.

Encrypted SOAP PP/body decryption checks its untrusted key metadata directly
instead of using a panicking model conversion. It requires a present reference,
a nonempty local `#fragment` and an exact derived-key nonce lookup, never byte
slicing a possibly empty or Unicode URI. Malformed metadata returns the existing
static `InvalidEncryptedPayload`/`MissingNonce` errors. Actual synthetic XML
decode/decrypt regressions preserve valid AES256-CBC output and reject missing,
nonfragment/external and unknown nonce references without account access.
The envelope nonce map likewise rejects empty or duplicate derived-key IDs
before verification/decryption, instead of silently selecting the last XML
occurrence. It returns the existing static `InvalidEncryptedPayload` error;
valid unique ID order and signature policy are unchanged. Full synthetic
Envelope XML/AES tests exercise both PP header and body decryption and actual
ambiguity rejection, without generating a usable runtime RPS result.

Legacy package transfer files are checked before interactive presentation:
single safe names, nonnegative sizes and present HTTP(S) CDN sources without
credentials/fragments or concatenation-induced authority changes. The download
and streaming commands share this validation. A streaming-open error returns
CLI failure. Bare downloads require HTTP 200 and exact declared/actual bytes,
write into private same-filesystem temporary staging, flush/synchronize and
atomically replace one regular destination only on success. Failed/cancelled
transfers retain the prior file and remove their staging; symlink destinations
are refused. This is not cryptographic hash verification, a whole-package
transaction or a crash-recovery journal, and is never called by management.
The temporary directory explicitly requests mode 0700 through the tempfile
builder at creation, rather than relying on the default 0777-before-umask
directory mode. Named staged files retain their 0600 creation mode. There is
no post-creation chmod or process-global umask change. Isolated child-process
0002/0000-umask tests check the actual paused transfer's directory/file modes
and that commit preserves the original staged file's device/inode and bytes,
with failed-transfer/cancellation preservation exercised under the same masks.
The legacy caller redirect policy/valid HTTP and HTTPS source formats remain;
future management downloads need the owned no-redirect client and all existing
authorization/manifest/registry/runtime gates.
