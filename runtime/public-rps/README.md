# Isolated public RPS client

This original GPL-3.0-only C client implements the existing private Xodus XML
transport for the public development shim's `get_rps_tickets` boundary. It does
not implement Store authentication, package authorization, a runtime version
handshake, or gameplay certification.

The producer must be the scoped management broker, **not** the legacy global
service. `XODUS_RUNTIME_SOCKET` is an explicitly inherited absolute UTF-8 Unix
socket path; it contains no credentials. Missing, malformed, overlong and legacy
default paths fail closed. Raw paths are bounded to 103 UTF-8 bytes, matching
the scoped macOS producer. There is no socket discovery or fallback. The
producer, not this path string, establishes the private directory, profile and
same-UID peer boundaries. The application must independently validate the
runtime/service pair before offering Play.

Requests use exact 16-ASCII-hex `ClientId`, `AllowUi` and `MsaFullTrust`.
`AllowUi` grants permission but does not make the broker open consent. Responses
must be MSA type 4 with all four fields: nonempty user/device tickets and future
UTC-second expiries. Both tickets must be printable, JSON-safe ASCII because
the public shim inserts them into its existing Xbox JSON requests. DTDs,
external entities, duplicate/unknown/nested fields, malformed XML and partial
frames fail without ticket outputs. Response parser diagnostics and credential
bytes are never logged. Caller-owned streams are closed on every outcome.

The 40-second **overall**, monotonic connect/write/read deadline covers the
producer's 30-second exchange budget without restarting for partial reads.
The frame length remains inclusive 65,535 bytes. Tickets returned on success
use `malloc` ownership, matching the public caller's existing `free` calls.

## Native isolated checks

On macOS with existing Command Line Tools:

```sh
sdk=$(xcrun --show-sdk-path)
xcrun clang -std=c11 -Wall -Wextra -Werror -pedantic \
  -isystem "$sdk/usr/include/libxml2" xodus_rps.c tests.c -lxml2 \
  -o /absolute/owned-output/rps-client-checks
/absolute/owned-output/rps-client-checks
```

Run from this source directory. These checks start only caller-owned synthetic
peers in a new private temporary directory. They exercise the actual client,
including fragmented/truncated/bounded frames, the exact request, both ticket
expiries, XML rejection and an overall stalled/trickle deadline. They neither
start Xodus services nor access credentials, external game folders or gameplay.
Native POSIX checks are not proof of the Winsock/Wine execution path.
The Winsock branch uses Wine's public Unix/DOS conversion exports, checks the
exact Unix roundtrip and refuses lossy ANSI-codepage conversion. Wine interprets
`sockaddr_un` names as DOS/ACP paths, so passing the raw Unix string directly
would target the wrong file. The launcher should create an ASCII private
broker path; unrepresentable paths fail explicitly rather than silently
connecting elsewhere.

Win64 client and exact adapter **COFF objects** can also be compiled with Clang
against headers from public Wine
`eab69739f15180b96797645ccade44c1c4414980` and the existing XML headers.
This checks the real Windows branch and API declarations. It does not link a
full PE DLL or execute Wine; macOS XML headers are not a delivered PE XML
library.

## Apply to the pinned public shim

`apply_to_public_shim.py` accepts an explicitly selected disposable checkout
of `xodus-gaming/xgameruntime` at
`44d97de1e084be61243323062ed49e307d28728c`. It verifies HEAD and the exact Git
blobs of `xuser.c`, `util.c` and `Makefile.in` before modifying anything.
Changed sources or existing client files are refused.

```sh
python3 apply_to_public_shim.py /absolute/owned/public-shim
python3 apply_to_public_shim.py /absolute/owned/public-shim --apply
```

The adapter reads the environment through UTF-16 and explicitly converts to
UTF-8, maps client failures to HRESULTs, replaces only the RPS stub, adds the
Winsock import and client source, and removes the directly coupled existing
JSON/HTTP-body/header debug dumps. Original upstream LGPL-2.1-or-later notices
are preserved. The original client is GPL-3.0-only; a combined distributed DLL
must comply with GPLv3 and the retained upstream notices, not be advertised as
an LGPL-only binary.

The pinned development shim is **not** the public Wine fork's default gitlink.
Applying the source patch or compiling the client is not a supported Wine/shim
pair. Full PE/DLL compilation, version pairing and actual licensed operation
remain separate requirements.
