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

The public shim's `get_rps_tickets` remains a separate consumer integration.
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
