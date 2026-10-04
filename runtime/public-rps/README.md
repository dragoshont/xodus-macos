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
bytes are never logged. Caller-owned streams are closed on every outcome and
are marked non-inheritable so an executed child does not retain the credential
channel.

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
This checks the real Windows branch and API declarations. These standalone
object checks do not link a full PE DLL or execute Wine; macOS XML headers are
not a delivered PE XML library.

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
pair. Version pairing and actual licensed operation remain separate
requirements.

## Full public PE build probe

A complete PE32+ x86-64 shim DLL was separately compiled and linked on macOS
from public Wine `eab69739f15180b96797645ccade44c1c4414980`, the development
shim pin above, and this repository's client/adapter at
`8375d0d586ec9c903c7408542eaed65c3caf0d33`. Wine's default shim gitlink is
`64aebcabb8c66121eae25d3bf0ace4b582ebb0da`; this probe deliberately overrides
it and is not an upstream-supported composition.

After preparing an owned disposable Wine checkout, checking out the exact
development shim in `dlls/xgameruntime`, and applying the guarded adapter:

```sh
cd /absolute/owned/public-wine
sh autogen.sh
mkdir -p /absolute/owned/public-wine-build
cd /absolute/owned/public-wine-build
/absolute/owned/public-wine/configure \
  --disable-tests --without-x --without-wayland --without-vulkan \
  --without-gstreamer --enable-archs=x86_64
make -j2 dlls/xgameruntime/x86_64-windows/xgameruntime.dll
```

The observed host was aarch64 macOS, using existing Autoconf 2.73, Bison 3.8.2
and `x86_64-w64-mingw32-gcc` 16.2.0. Bootstrap must use the public `autogen.sh`,
not just Autoconf: it also generates required Wine headers. No packages were
installed for this probe.

The actual link command contains both `xodus_rps.o` and `xuser.o`, the Winsock
import library, and Wine's **static PE** `libs/xml2/.../libxml2.a`. This is not a
DLL linked against macOS XML headers or an unimplemented stub. The resulting
imports include `WSASocketW`, `GetEnvironmentVariableW` and `GetTickCount64`.
Other imported DLLs are bcrypt, combase, kernel32, ntdll, shlwapi, ucrtbase,
winhttp, wininet and ws2_32.

That individual build's DLL SHA-256 is
`105532cbf0741ac4038abeef9b1b58cc3c79dd368a4d0d5886b764027e319259`.
This identifies one observed artifact, not a reproducible-build claim or a
runtime certificate. That individual PE build did not execute a Wine loader,
credential exchange or game.
The host loader's architecture, its dependencies and prefix, service/profile
pairing, Winsock execution and licensed gameplay still need separate proof
before this candidate can be offered as a playable runtime.

A separate [public macOS host probe](../public-macos/README.md) now builds
the complete configured x64 macOS candidate, including genuine Mach-O
loader/server/ntdll/Mac-driver/secur32 components and 613 PE DLL files, with
reviewed original platform guards. Its explicit optional-feature omissions and
artifact hashes are documented there. It is not a supported runtime pair or
licensed-operation proof.

## Actual Windows/Wine component checks

`windows_smoke.c` is a small original Windows console executable linked to the
unchanged client and Wine's static **PE** XML library. `check_windows.py` runs
it through explicitly selected public loader/server binaries in a new private
temporary prefix, with a private HOME and synthetic Unix peer only. It does
not load the gaming shim or contact a Store, Xbox, license, or Xodus broker
endpoint. It must not be pointed at private runtime candidates.

The selected owned root must be non-aliased, current-user-owned and not
group/world writable; all three selected binaries must be regular,
non-aliased files inside it. Temporary directories are owner-only, the
synthetic socket is mode 0600, and the endpoint must be short ASCII. Each
Windows child has a bounded wait. Before bootstrap and each check, a bounded
socket connection verifies Darwin's peer PID against the recorded foreground
server PID; process creation or mere socket-file existence is not readiness.
The same identity is checked after each result.

Cleanup sends Wine's client-killing `SIGINT` to the recorded PID and waits for
its successful graceful exit; `SIGTERM` only exits the server and is not sufficient.
Once any Windows client has been attempted, an already-dead controller or a
nonzero shutdown result retains the private directory with an explicit error.
An absent listener is not evidence that its clients have ended.
Cleanup also reconciles any unexpected listening
replacement on this prefix's exact socket. A replacement is stopped only after
its actual executable path matches the explicitly selected public server.
Darwin process-exit notification must confirm the replacement ended successfully.
Unknown ownership, inaccessible identity or unsuccessful reconciliation raises
an error and retains the private prefix instead of deleting live state. Wine's
server socket is derived from this new prefix's device/inode under
`/tmp/.wine-UID`, not from `TMPDIR`; no other prefix's socket is selected.
Logs use owned temporary files, not pipe EOF waits that can hang when Wine's
boot children inherit stdout.

For an already prepared complete public macOS build, run the following in
its configured build directory. Paths below identify only owned public
source/build outputs. `$tools` contains tools built from that public source,
and `$client` contains this directory's source files.

```sh
tools=/absolute/owned/public-wine-tools
source=/absolute/owned/public-wine
client=/absolute/owned/public-rps
root=/absolute/owned/output

"$tools/tools/winegcc/winegcc" -b x86_64-w64-mingw32 \
  --wine-objdir . --winebuild "$tools/tools/winebuild/winebuild" \
  -std=c11 -Wall -Wextra -Werror -pedantic -Wno-cast-function-type \
  -D__WINE_PE_BUILD -DLIBXML_STATIC \
  -isystem "$source/include" -isystem "$source/include/msvcrt" \
  -isystem "$source/libs/xml2/include" -I"$client" \
  "$client/xodus_rps.c" "$client/windows_smoke.c" \
  libs/xml2/x86_64-windows/libxml2.a -lws2_32 -lkernel32 -lucrtbase \
  -o "$root/xodus-rps-windows-smoke.exe"

python3 "$client/check_windows.py" "$root" \
  "$root/public-wine-build-macos-x64/loader/wine" \
  "$root/public-wine-build-macos-x64/server/wineserver" \
  "$root/xodus-rps-windows-smoke.exe"
```

The GCC allowance is specific to Windows `GetProcAddress` function-pointer
casts; the Wine headers are marked as external system headers. Other source
warnings remain fatal.

The runner also accepts an explicit `--library-directory` for an owned x64
dependency prefix. It must be inside the selected root, with non-aliased,
current-user-owned, non-group/world-writable directory ancestors. Its path sets
only the child environment's `DYLD_FALLBACK_LIBRARY_PATH`; omitting it retains
the original behavior and does not discover or import another runtime.
Colons anywhere in the resolved path are refused before process launch:
dyld treats them as search-path separators, not literal directory characters.

The actual x64 macOS candidate at platform overlay
`a29baafbea2da6c310a847a3c7708ac8f3706ac1` executed all four checks:
fragmented successful response with exact synthetic outputs, malformed
response, expired tickets, and a stalled peer reaching the client's 1,500 ms
deadline. The peer checks the real request frame and all three fields; failures
must leave both ticket outputs NULL. Successful outputs are freed by the caller.
The observed PE executable SHA-256 is
`f4cc95d81e8c9c68bfe2c371a4b5c3bc47f958e906af81eba01759059650744d`.
The production C source and header retain SHA-256
`61d4f95934497f8c0b04874016bf9fd528f49a12e4123533ca975daf06475c4f`
and `74c4e09fc5e13f65ea78b854b2b1f66e046c64c5b5e5107f34d5eed9011adba8`.

This proves the actual Winsock DOS/ACP-to-Unix mapping and bounded client
transport in that candidate, not merely COFF compilation or the POSIX branch.
The initial transport runs did not prove complete background-client cleanup:
a command-name scan missed Wine's Windows service processes. A subsequent
check of the exact native ntdll mapping exposed them. Their individually
verified public-loader PIDs were cleaned up, and shutdown was corrected to use
Wine's client-killing `SIGINT`. The corrected runs below verify cleanup using
the native ntdll mapping as well as prefix/socket and executable-path checks.
No real credentials, native consent, broker issuance, shim account
exchange, full runtime/service handshake, package, or game was used. Those
authorization, pairing and licensed-gameplay requirements remain open.

### Server-ownership regressions

```sh
python3 -B test_windows_runner.py -v
```

Eleven Darwin-only checks use owned Python socket helpers, never Wine:
delayed PID-bound readiness, actual main-loop client ordering, bounded
startup timeout, exited server, wrong listening PID, recognized replacement
cleanup, unknown-replacement refusal and main-loop retention after cleanup
failure. The client-order regression fails on
the original runner at `b7e9e9f8c730483386d24d1bdfa1fa86fbfc3ac6` before
any Wine client is executed and passes with the readiness correction.
Hosted macOS CI runs the same ownership suite.

The current suite extends these eleven to twenty: three cases verify that
a trusted component callback runs only within the ready, owned lifecycle,
that callback exceptions fail, and that missing outcomes cannot report success;
six cases verify explicit owned dependency configuration and refusal of
outside, aliased, writable or colon-containing directories before any process
launches.
The default callback preserves the four original RPS cases. The public HTTPS
check reuses this same lifecycle, and two additional real native socket checks
verify that its peer stops even during a stalled handshake or partial headers.
See [the Windows HTTPS evidence](../public-macos/README.md#actual-windows-https-certificate-checks).

The unexpected-death regression starts a real owned helper holding a file in the
new prefix, kills its controller, and confirms there is no listening server.
The live helper and its prefix file remain intact when the runner refuses
cleanup. The original runner at
`b3805d699255b2641f6cf89c52e189ae76777c55` fails this exact regression by
removing that prefix. Separate checks reject abnormal shutdown statuses for both
the recorded server and a verified replacement instead of treating mere exit as
confirmation of client cleanup.

The corrected runner also completed the four actual Windows checks normally
and with the real public server's same-PID `exec` deliberately delayed by
600 ms. All five Windows launches, including bootstrap, waited for that
server's ready socket. No selected runtime processes or private prefixes
remained after either corrected run; `lsof` found no process retaining the exact
selected native ntdll mapping. This closes the reproduced startup-order defect;
retained adversarial review remains a separate source-level check.

The same retained adversarial reviewer closed R14 at
`67adac4d17fa2e26ff610ff9fc0947d9387f6d40` in review 29, after closing the
fresh-host fixture finding R15 at `b3805d699255b2641f6cf89c52e189ae76777c55`
in review 28. All confirmed findings R01-R15 are closed. The reviewer checked
source, syntax and required call arguments, not the Darwin/runtime executions.
Independent hosted run
[37172888303](https://github.com/dragoshont/xodus-macos/actions/runs/37172888303)
passed the native client and all eleven ownership regressions at the exact
R14 correction. These scoped closures do not replace Store consent, authorized
installation, runtime pairing, licensed gameplay or the final end-to-end review.
