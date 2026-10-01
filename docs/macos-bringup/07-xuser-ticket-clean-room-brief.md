# XUser RPS ticket bridge clean-room brief

Status: human implementation required

This document defines the first remaining XUser/GDK semantic boundary for the
Hogwarts Legacy Game Pass launch. It intentionally does not provide an
implementation. The surrounding transport, service protocol, ownership, and
validation requirements are derived from existing public Xodus source.

## Proven boundary

The private runtime now completes all of the following:

1. Initializes XGameRuntime.
2. Creates the serialized work and completion task queues.
3. Runs `XUserAddAsync(AddDefaultUserSilently)`.
4. Generates and exports the ECDSA proof key.
5. Performs TLS requests through Wine Schannel.
6. Downloads and parses the default Xbox endpoint and signature-policy
   document.

The next call is the explicit public stub:

```c
static HRESULT get_rps_tickets(
    BOOLEAN allowUi,
    char **userTicket,
    char **deviceTicket
);
```

Public source:

```text
xodus-gaming/xgameruntime
branch: origin/xuser
commit: 44d97de
file: xuser.c
```

No implementation was found in upstream branches, the Xodus Wine branch, or
indexed public GitHub forks.

## Existing public service contract

The native Xodus service already exposes the required data through its XML IPC
path.

Socket:

```text
/tmp/xodus.sock
mode 0600
same-user AF_UNIX stream
```

Frame:

```text
u32 little-endian magic: 0x58445358
u16 little-endian message type
u16 little-endian payload length
payload bytes
```

Message types:

```text
MSA_TOKEN_REQUEST  = 3
MSA_TOKEN_RESPONSE = 4
```

Request XML model:

```rust
pub struct MSATokenRequest {
    pub client_id: String,
    pub allow_ui: bool,
    pub msa_full_trust: bool,
}
```

Response XML model:

```rust
pub struct MSATokenResponse {
    pub token: String,
    pub expiry: i64,
    pub device_rps: String,
    pub device_expiry: i64,
}
```

Public source locations:

```text
crates/xodus/proto/xodus/common.proto
crates/xodus/src/models/xgameruntime/xuser.rs
crates/xodus-service/src/connection/xml.rs
crates/xodus-service/src/connection/mod.rs
```

The service obtains both values through the existing Keychain-backed
`TokenManager`, device-token exchange, and user-token exchange. The Wine-side
runtime must not duplicate those authentication flows.

## Required runtime behavior

A human clean-room implementation should:

1. Use the existing AF_UNIX IPC layer and service framing.
2. Build an `MSATokenRequest` with:
   - `client_id` from the already parsed `MSAAppId`;
   - `allow_ui` from `XUserAddOptions`;
   - `msa_full_trust` from the already parsed `MSAFullTrust` setting.
3. Send message type `MSA_TOKEN_REQUEST`.
4. Require response type `MSA_TOKEN_RESPONSE`.
5. Parse the response XML.
6. Return independently allocated, NUL-terminated UTF-8 strings:
   - `*userTicket` from `MSATokenResponse.token`;
   - `*deviceTicket` from `MSATokenResponse.device_rps`.
7. Return an explicit failing `HRESULT` for:
   - socket unavailable;
   - malformed frame or unexpected response type;
   - empty required ticket;
   - XML parse failure;
   - allocation failure;
   - user interaction required while `allow_ui == FALSE`.

The caller currently releases both returned strings with `free()`. Ownership
must match that contract.

## Security requirements

- Never log request or response XML.
- Never log either ticket, token length, account identifier, proof key, or
  authorization header.
- Preserve socket mode `0600` and same-user peer assumptions.
- Do not write tickets to disk.
- Clear temporary response buffers before freeing when practical.
- Keep Keychain access in the native Aqua LaunchAgent service; do not move
  secrets into SSH-launched processes.
- Reject frames larger than the protocol's `u16` payload size.
- Treat a response with an empty `device_rps` as a failure unless public
  service semantics explicitly document otherwise.

## Validation plan

Validation must use synthetic or redacted evidence:

1. Unit-test frame encoding and response-type validation with placeholder XML.
2. Unit-test malformed, truncated, oversized, and empty-ticket responses.
3. Test socket-unavailable behavior.
4. Test `allow_ui == FALSE` without displaying UI.
5. Run the existing service in the Aqua namespace.
6. Confirm the runtime advances beyond `get_rps_tickets` with only
   `+xgameruntime,+gdkc` enabled.
7. Confirm logs contain no ticket/token material.
8. Stop at the next unimplemented GDK semantic boundary and update
   `06-experiment-ledger.md`.

## Non-goals

- Reimplementing Microsoft authentication in Wine.
- Reading Keychain records from Wine.
- Hardcoding account, device, title, or endpoint credentials.
- Tracing proprietary Microsoft runtime behavior.
- Guessing error mappings or UI behavior not established by public source.

