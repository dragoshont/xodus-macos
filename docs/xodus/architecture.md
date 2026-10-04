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
not establish a PE build, a supported Wine/shim/service version pair or live
authorization. The backend's separate scoped broker remains required.
