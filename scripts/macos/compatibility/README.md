# Original Xbox PC app compatibility experiments

These components target Microsoft's original Xbox PC application and its
Windows runtime dependencies. They are not a replacement launcher UI and do
not establish that the Xbox app is usable in CrossOver.

## Upstream-first prerequisite procedure

Every reached Wine gap follows the mandatory procedure in root `AGENTS.md`:
resolve the actual API, inspect current upstream implementation and tests at a
recorded commit, narrowly reuse/backport it if it supplies the required behavior,
otherwise implement the measured missing behavior, then rebuild and retry the
original app. An export, stub or false-success return does not count as support.

For `CoRegisterServerShutdownDelay`, upstream commit
[`eba89375a0515957701928faac0f5007ef638b04`](https://github.com/wine-mirror/wine/commit/eba89375a0515957701928faac0f5007ef638b04)
(October 5, 2026) contains neither its
[`combase` export](https://github.com/wine-mirror/wine/blob/eba89375a0515957701928faac0f5007ef638b04/dlls/combase/combase.spec)
nor its
[`implementation`](https://github.com/wine-mirror/wine/blob/eba89375a0515957701928faac0f5007ef638b04/dlls/combase/combase.c).
The decision for this prerequisite is therefore native-behavior implementation,
not an upstream-version upgrade.

The separately requested WineOffice search identified
[`ttv20/wine4office`](https://github.com/ttv20/wine4office).
Direct inspection of
[`roapi.c` at `1cc720a0e3dde8d66634beefafe2835e46220fd1`](https://github.com/ttv20/wine4office/blob/1cc720a0e3dde8d66634beefafe2835e46220fd1/dlls/combase/roapi.c)
confirms that its server-class enumeration and activation-factory registration
still contain the same success placeholders, not working implementations.
This rules out those bodies as backports, not every unrelated Office patch.
[`Rosentti/wine-uwp`](https://github.com/Rosentti/wine-uwp/blob/888416f1f8c2c4aefab46d71b525b501bcbdb5f2/README-uwp.md)
also documents Xbox-app experiments, but no reusable implementation of the
currently blocking package-scoped catalog/broker APIs was found in its inspected
source. No fork runtime has been substituted or installed.

Wine4Office's
[`appxdeploymentclient/msix.c` at the same revision](https://github.com/ttv20/wine4office/blob/1cc720a0e3dde8d66634beefafe2835e46220fd1/dlls/appxdeploymentclient/msix.c)
does contain substantial signature-chain, publisher and signed-digest/payload
verification code. This is a possible later package-integrity reference, not a
server-class catalog implementation: no relevant manifest-to-server-class
registration helper was found in the inspected deployment files. It has not
been adopted or validated in this runtime, and remains deferred.

The attribute-map reuse check also identified WineHQ
[merge request !8895](https://gitlab.winehq.org/wine/wine/-/merge_requests/8895),
head `e89fdb920392c88154938482b33e95089b65717d`. Its generic map source has real
lookup, ownership and iteration logic, but the API reports this MR closed and
unmerged with a failed pipeline. Its inspected source still has unsupported
IInspectable metadata methods and a no-op `Split`. It is an evaluation reference,
not a verified backport or proof that current Wine supplies this complete
interface. Closure alone does not prove there is no newer implementation.

## Package claims

`package-claims.c` and `package-claims.h` implement the
`RtlQueryPackageClaims` ABI using actual filtered token security attributes.
`package-claims-test.c` compares the implementation with the native Windows
function and tests buffer/error behavior.

The package-claim structure contains two 32-bit fields, `Flags` and `Origin`.
String-buffer sizes are byte counts. Package identity is read from the queried
token, not selected from an executable name or hardcoded to the Xbox package.
Missing token-provider support remains an explicit error; it is not a signed-in
user, a game entitlement, or evidence that a package was installed.

The first native Windows regression covered x86 and x64: 826 differential
comparisons and 56 fixture assertions passed against packaged Xbox,
unpackaged, invalid-handle and buffer-boundary cases. On the tested CrossOver
26.3 runtime, `NtQuerySecurityAttributesToken` is missing, so the provider
reports `STATUS_NOT_SUPPORTED`. The higher-level API implementation alone
cannot supply the missing token model.

### Coherent token-runtime experiment

`wine-cw26-token-runtime.patch` adds token security-attribute support to a
matched CodeWeavers Wine 11.0 source/server/NTDLL build. Its real-runtime
controls passed 21 assertions, including token duplication, filtered queries,
invalid-handle/access errors, empty attributes and genuine event waits.
This experimental build is x64-only; it is not a replacement WoW64 engine.

An independent coordinator test booted this engine against a separate
copy-on-write snapshot of the Xbox-only experimental prefix. The unchanged
managed claims probe then returned `STATUS_NOT_FOUND` for its genuinely
unpackaged token and `STATUS_INVALID_HANDLE` for an invalid handle, rather than
the stock CrossOver runtime's missing-provider `STATUS_NOT_SUPPORTED`.
This verifies the token-query prerequisite, not Xbox package activation.

Use a separate prefix and the matched Wine loader/server. Do not mix this
engine with CrossOver Unix libraries, builtin DLL paths or native core
overrides. Keep NTDLL builtin; redirect only `RtlQueryPackageClaims` to the
tested helper, leaving `NtQuerySecurityAttributesToken` bound to the actual
patched NTDLL. Never assign package identity merely from an executable name.

The genuine x64 `XboxPcApp.exe` still exits with a `0xc0000409` fail-fast under
this engine. The actual traced path is `XboxPcApp+0x5c5a22` through
`XAML+0x6a4246` to the claims provider and real token query. XAML receives
`STATUS_NOT_FOUND` for the genuinely unpackaged token, takes its negative-status
branch and raises the fail-fast exception. No invoked WNF-stub exception
preceded it. Real catalog-backed package registration and activation are
required; assigning a hardcoded package claim would conceal this prerequisite.
No Xbox application rendering, sign-in or library success has been observed
from this runtime experiment.

The separate full-trust `XboxPcAppCE.exe` entry does not bypass Windows app
activation. Its initial zero exit still accompanied a missing
`Windows.System.BrokeredLauncher` class. Registering that class's actual
Windows DLL/activation/threading mapping in a separate CE-only prefix advanced
the genuine client to an explicitly invoked, unsupported
`UMgrQueryUserContext` call. A Microsoft-signed `usermgrcli.dll` and an
ABI-preserving forwarder, resolved from the actual Windows API-set mapping,
then reached that function's real local-RPC client. The measured next failure
is RPC exception `0x6f7` (`RPC_X_BAD_STUB_DATA`) during NDR32 procedure 10's
first argument sizing (format byte `0x3c`). This is not an observed RPC endpoint
failure, a valid user-session result, or evidence that the Xbox library opened.
The exact descriptor is `3c 05 08 00 00 00`: `FC_SYSTEM_HANDLE`,
`sh_token`, `TOKEN_QUERY`. See `package-manager-rpc-system-handle-notes.md` for
the genuine DLL's descriptor RVAs, actual stock-CrossOver trace, matched Wine
dispatcher gap, and required local token-duplication/ownership semantics.

`wine-rpc-token-handle.patch` now provides a bounded experimental,
Wine-to-Wine, input-only ncalrpc implementation for that descriptor. Real
distinct-process controls verify receiver-side token duplication with query-only
access and call-lifetime cleanup, not a scalar-handle shortcut. Native Windows
controls establish the broker's actual null-token argument as valid. With this
patch the original broker passes the `0x6f7` failure and reaches the next real
boundary: its UserManager RPC endpoint is not registered (`0x6d9`).

### Explicit local package activation

`package-activation-catalog.py` and `wine-package-activation.patch` implement a
prefix-local developer registration and suspended-child activation seam.
The registrar checks the original manifests, dependency closure, all 1,310
file receipts and signed BlockMap digests. Signature mathematics and file
binding are verified; signer-chain trust and original archive-wide signature
coverage are **not** claimed. Registration therefore records
`DeveloperUnsigned`, not Store installation or licensing.

The server supplies identity only from that catalog to a caller-owned,
suspended process whose image and code hashes match. Independent controls
passed 21 checks covering mismatched/unregistered images, invalid handles,
relabel rejection, duplicated-token identity and relaunch; the launcher stays
unpackaged and no AppContainer, privilege or capability grants are fabricated.

The recovered activation build and RPC patch were combined in a fresh coherent
runtime at `~/xodus-runs/cw-xbox-integrated-20261006`, with a separate prefix
under `~/xodus-runs/xbox-app-integrated-runtime-20261006-001`. The original Xbox
UI passes its earlier package-identity fail-fast and reaches genuine
FrameworkViewSource/CoreApplication providers. An initial relay trace reported
`combase.CoGetSystemSecurityPermissions`, but subsequent original-PE inspection
proved that this was the wrong function selected by an ordinal collision.
The controller's
zero exit verifies its activation checks, **not** the child app: the child
exits `0x80000100`, and the Xbox library still has not opened.

`com-system-permissions.c` now implements the export's registry-backed branch,
returning only bounded, validated self-relative descriptors from the actual
machine COM policy. It preserves invalid-argument outputs, uses `LocalAlloc` /
`LocalFree`, honors explicit restriction-policy precedence, and fails explicitly
when no authoritative descriptor or resolver default exists. It does **not**
invent permissive defaults, import the Windows PC's policy, or implement a COM
security resolver. Its isolated fixtures pass on actual Windows and Wine.

The original CoreApplication DLL imports **ordinal 122**, not the named
security API. Genuine Windows ordinal-122 calls return the same apartment
identifier as `RoGetApartmentIdentifier`; they do not return a security
descriptor. The invalid-selector result was therefore a misleading consequence
of Wine's different export numbering, not evidence of a real security-policy
request. No argument-flipping or permissive descriptor workaround was applied.

`combase-apartment-ordinal.def` redirects verified ordinals 122 and 120 through
individual single-import lab bridges, preserving their IAT addresses. Windows
ordinal 120 is the target of the named `RoRegisterForApartmentShutdown` wrapper.
The patched
`RoGetApartmentIdentifier` returns the existing real Wine apartment's identity,
not the old `0xdeadbeef` stub. Controls verify stable MTA identity and a distinct
STA identity.

`wine-com-lifecycle.patch` replaces fake shutdown cookies with actual
registration records, agile-callback validation, reference ownership,
unsubscription and notification on real apartment destruction. The same
fixture on Windows and Wine verifies callback delivery, cancellation and
non-agile rejection. This also clears the mismatched-ordinal null write in
`StdMarshalImpl_QueryInterface`.

Absent/unknown activation-factory cleanup now follows the measured native
void-API behavior. Actual out-of-process activation-factory registration remains
unsupported and returns `E_NOTIMPL` explicitly instead of the previous false
`S_OK`; it does not advertise a functioning EXE-server registration.

The original UI advances to genuine `RMCLIENT.HamConnectForExtendedExecution`.
The signed Windows resource-manager client clears that missing entry and
reaches `RPCRT4.RpcBindingCreateW`. `rpcbindingcreate-minimal.patch` supplies
the measured local V1 constructor, actual QOS inquiry and the observed
NONCAUSAL/default-timeout options without an association rewrite. These
controls pass after integration into the working catalog-backed runtime;
the isolated worker's failed catalog query is not an app launch result.

The next apparent `CLSIDFromOle1Class` failure is another ordinal collision.
Matching Microsoft public symbols identify Windows ordinal 69 as
`HRESULT CoRegisterServerShutdownDelay(HANDLE hStopEvent, DWORD dwMilliseconds)`.
`com-server-shutdown-delay.c` and its ordinal-only export implement only the
native invalid-pair checks and cleanup when no registration exists.
Real delay registration returns `E_NOTIMPL` explicitly; no timer, event
signaling or COM server lifetime capability is advertised.

After a single-import ordinal-69 redirection, the registered original Xbox
executable creates the XAML, CoreApplication, PropertySet and actual requested
Vector interfaces without the previous unsupported-export abort. The next
observed failure was `Application.Start` returning `0x80070057`, with the native
CoreApplication error parameter `serverName`. The native parser requires the
last command-line argument to start with `-ServerName:`. The genuine Windows
Xbox process supplies this argument; the original direct-launch control did not.
`package-activation-launch.c` now accepts that separately observed server name
without changing its catalog, image-hash or token-activation checks. The actual
retry passes this argument error and now reaches `0x80004001` (`E_NOTIMPL`).
The first return is now traced to the explicit shutdown-delay boundary:
the native CoreApplication requests `CoRegisterServerShutdownDelay` with a real
event and a 2,000-ms delay, before `RoRegisterActivationFactories` is called.
Native controls establish that this event is tied to the actual COM server
reference count, not an immediate registration timer: references suppress
delivery, release to zero starts the delay, reacquisition cancels it, and
explicit cancellation suppresses delivery. The legacy cleanup-only C provider
deliberately does not implement that lifetime behavior.
`rpcbindingcreate-shutdown-delay.patch` now implements the real behavior in
Wine's existing COM reference-count machinery. The integrated ordinal bridge
forwards to that named Wine export, rather than the legacy C provider.
Independent parent-runtime controls cover fresh-process delayed delivery,
cancellation, reacquisition, terminal rejection and replacement/ownership.
The original Xbox retry passes this prerequisite but still fails fast with
`0xc0000409`. No window or library success is claimed.

That subsequent producer is now resolved: the inherited
`RoGetServerActivatableClasses` placeholder returned an empty class list, so
`RoRegisterActivationFactories` rejected zero classes. The real ordered list
was recovered from the installed matching package's native `ActivationStore.dat`,
including all four class-to-server backlinks, executable and application ID.
Only activation/code metadata was exported; no account state or security policy
was copied. `rpcbindingcreate-server-catalog.c` queries the explicitly staged
metadata only for the real registered token and matching executable.
Its clean parent-runtime integration passes allocation/lifetime controls and
the original app now requests factory registration with four actual classes.
The reproducible parent source delta is `wine-server-class-catalog.patch`,
with the reviewed helper `rpcbindingcreate-server-catalog.c` beside `roapi.c`;
worker diagnostic patches and complete engines are not part of that integration.
The subsequent registration work below supersedes that earlier
`RoRegisterActivationFactories` `E_NOTIMPL` boundary.

The reached `RoGetActivatableClassRegistration` metadata/attribute path is also
integrated as `wine-class-registration-metadata.patch`, with the two reviewed
helpers beside `roapi.c`. Its owning immutable view reuses the already staged
genuine `xbox_wintypes_base.dll` PropertySet and PropertyValue implementations;
no draft MR collection implementation was vendored. Independent parent controls
cover both real entry points, boxing, QI, iteration and lifetime. A temporary
diagnostic inside the legitimately activated original process resolves metadata
and invokes all four genuine factory callbacks successfully, then restores the
normal bridge. This is not out-of-process factory delivery or registration.
Unreached secondary metadata interfaces/getters remain explicitly unsupported.

The generated IInspectable/IActivationFactory proxies reuse WineHQ
[MR !8911](https://gitlab.winehq.org/wine/wine/-/merge_requests/8911) and existing
SDK/Wine IDLs and HSTRING wire routines. Only the two interface mappings to the
native-measured existing PSFactoryBuffer CLSID were added. The parent also
integrated `rpcbindingcreate-nullstub-fix.patch`: real stubs can now be constructed
without an initial server, then connected/disconnected normally. Native Windows
and the parent runtime agree on those controls, including the existing normal
IClassFactory case; the unrelated final stub-reference diagnostic count differs
and is not claimed as parity.

The isolated paired test now delivers a genuine factory through existing ROT
and COM proxies to a separate client, which executes `GetIids`; real revocation
invalidates the held class-factory proxy and subsequent lookup. This is a working
private Wine-to-Wine transport seam, not native Windows broker-wire equivalence.

`wine-winrt-registration.c` now wires that seam into actual registration,
revocation and package/image-eligible factory lookup. The original activated
Xbox process registers all four real classes with an owning cookie. In-process
activation stays first, cancellation closes whole-batch admission, and owned
references preserve registration lifetime. Destruction takes and clears the
apartment-shutdown cookie under the same lock as shutdown notification, then
unsubscribes outside that lock. The candidate builds and reaches the next real
startup prerequisite; paired public-route, STA, collision, rollback and race
qualification remain pending. This is trusted isolated-prefix interop, not a
native Windows broker or an adversarial package-security boundary.

### Reached ALPC attribute prerequisite

`wine-alpc-attributes.c` narrowly backports the real attribute-buffer helpers
from Wine commit `eba89375a0515957701928faac0f5007ef638b04`,
[`dlls/ntdll/alpc.c`](https://github.com/wine-mirror/wine/blob/eba89375a0515957701928faac0f5007ef638b04/dlls/ntdll/alpc.c).
The original LGPL-2.1-or-later attribution is retained. Stage the source as
`dlls/ntdll/alpc.c` and its compatibility layout header beside it, then apply
`wine-alpc-attributes.patch` to the matched older source and rebuild its NTDLL.
Do not replace the loader, Unix NTDLL or wineserver with unrelated binaries.

Native Windows confirms the required-size output is eight-byte `SIZE_T`.
`alpc-attributes-test.c` checks all 128 known-attribute combinations, sizes,
short buffers, untouched payload/tail bytes and lookup offsets. Native Windows,
the isolated child runtime and the integrated parent each pass 66,561 checks.
Use `--native` only on Windows: for an allocated unknown bit, native lookup
returns the end of known attributes whereas upstream returns NULL. The
backport intentionally preserves upstream behavior; the reached Xbox call uses
the known security attribute.

The genuine native CoreDispatcher/CoreMessaging providers now execute these
helpers, then reach missing `ntdll.NtAlpcConnectPortEx`. The independently timed
parent retry exits `0x80000100` after 7,185 ms; the launcher passes its 13 identity
checks, which is not app success. No greater-than-ten-second lifetime, Xbox
window, sign-in or library is established. These helpers are not ALPC port,
message, handle-transfer or broker support. The next port API needs its own
upstream-first decision; never manufacture a successful connection.

That decision is now bounded and measured. A temporary observer in the genuine
original process identifies `\BaseNamedObjects\CoreMessagingRegistrar` as the
connection target without intercepting the call or changing its result; the
normal bridge is restored and hash-checked afterward. Wine master provides
`NtAlpcCreatePort` object creation, but its connect/accept/message operations are
unsupported and `NtAlpcConnectPortEx` is absent. No usable connection or
registrar implementation was verified in the checked public sources; this is
not a claim of exhaustive fork coverage. ReactOS legacy LPC is not an
ALPC/security-compatible substitute.

`wine-alpc-connect-unsupported.c` and its patch expose an explicitly unsupported
eleven-argument entry point, following Wine's `STATUS_NOT_IMPLEMENTED` pattern.
It logs the missing capability, leaves outputs untouched, creates no handles
and does not bypass server security requirements. This is **not a connection
implementation**. Its 164 controls pass, and the attribute controls remain
green. The actual native caller rejects that status and fail-fasts
`0xe0464645` after 7,007 ms: there is no functioning optional fallback.

An alternate provider check also found no working reached queue export in the
pinned Wine master or WineOffice source: `GetDispatcherQueueForCurrentThread`
remains a spec stub, and the controller's `get_DispatcherQueue` is `E_NOTIMPL`.
The allocated controller and its asynchronous shutdown object are not an
implemented dispatcher. Wine-UWP's inspected controller entry returns
`E_NOTIMPL`. Do not swap providers on the strength of a nonnull controller.
Real ALPC connection/message/security semantics and the required registrar
behavior need a separately grounded implementation or a working reference;
more success-shaped export shims cannot establish an Xbox window.

### Grounded transient-object security lookup

The private `sechost.QueryTransientObjectSecurityDescriptor` boundary is now
resolved against the matching Windows implementation, not a guessed signature:
`NTSTATUS (ULONG type, LPCWSTR name, PSECURITY_DESCRIPTOR *out)`.
Type 8 selects an ALPC object. The implementation reads the configured
`SecurityDescriptor` value under
`HKLM\Software\Microsoft\SecurityManager\TransientObjects`, using the native
encoded object-name convention. Binary values produce an owning process-heap
copy; ordinary SDDL uses the existing converter and an owning copy.
`FreeTransientObjectSecurityDescriptor` releases that allocation and accepts
NULL. Failure leaves the caller's output untouched. The native initial
16-byte value probe rejects small values that fit it; this unusual boundary
is intentionally preserved.

`wine-transient-object-security.c` and its scoped sechost patch implement this
behavior. The matching native binary/public-PDB identity, disassembly and
read-only controls ground the contract. The same compiled negative controls
pass on native Windows; 29 isolated Wine fixture checks cover errors, small
binary boundaries, independent allocation, binary copying, ordinary SDDL and
cleanup. Fixtures never write native Windows policy. Domain-qualified strings
remain explicitly unsupported rather than inventing machine/domain identity.

The real service retry now invokes the implementation with the actual name
`WM_RegistrarServer` and returns `STATUS_OBJECT_NAME_NOT_FOUND`: its required
installation-owned registry entry is absent in the experimental prefix.
Native Windows has the entry and its query succeeds; only existence/type and
success/validity were checked, with no policy bytes exported or imported.
CoreMessaging still fails startup and the Xbox process waits for the service;
its greater-than-ten-second lifetime is **not responsive app startup**.
The next prerequisite is properly sourced installation configuration, not
another unknown API ABI. No permissive descriptor, fabricated RUNNING state,
Xbox window, sign-in or library is supplied by this slice.

The missing registrar value has subsequently been located in the matching
Microsoft-OneCore-CoreMessaging `10.0.26100.9444` **component installation
manifest**, rather than copied from live Windows registry policy. Decoding the
DCM/PA30 manifest requires its servicing-stack dictionary (resource 614/name 1
in `wcp.dll`); an empty MSDelta source is insufficient. The staged genuine DLL
matches the manifest version and verified component hash.

`extract-coremessaging-install-default.py` snapshots its inputs, selects only
the declared registrar default and rejects mismatched versions, duplicate
entries, malformed ACE/SID extents and machine/domain-account identities.
Its focused rejection tests are in
`test-extract-coremessaging-install-default.py`. This is an unchanged portable
installer asset, not an invented permissive descriptor.

`stage-coremessaging-install-default.c` permits only that pinned asset and
fixed registry value in the experimental Wine runtime, refuses existing
values and reads back the exact bytes. The one-shot Python provisioning wrapper
binds the identified prefix, holds an exclusive lock, quiesces it before
publication and records intent/result receipts. The owner explicitly confirmed
the exact target on 2026-10-07; enrollment through installed tooling and the
trusted observer completed before the one-shot publication. The unchanged
264-byte value was read back exactly. The genuine service lookup now succeeds.
The subsequent `NtAlpcCreatePort` request with flags `0x70000` was rejected by
the earlier scoped transport. A frozen three-file flags/QoS candidate now
allows the genuine registrar to report RUNNING. The original Xbox process
still exits after 6.559 seconds: its real extended connection returns
`STATUS_SERVER_SID_MISMATCH`, not transport success or application startup.

### Reached restricted service-principal prerequisite

The matching component installation manifest declares
`CoreMessagingRegistrar`, `NT AUTHORITY\LocalService`, shared-process hosting
and `sidType="restricted"`. Its unchanged original client requirement names
the deterministic service SID, independently reproduced by native
`RtlCreateServiceSid`. Read-only native service configuration reports SID type
3. Wine SCM's inherited process token does not establish this contract.

`restricted-token-access-control.c` creates and closes only owned token handles;
it does not create services, change policy or add a normal service identity.
Its synthetic descriptors establish independent normal/restricted access
checks, preservation through token duplication and write-only restriction
behavior. Compile with `x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -Werror
restricted-token-access-control.c -ladvapi32 -o restricted-token-access-control.exe`.
The same executable passes 11 native assertions. In the matched experimental
Wine runtime, `CreateRestrictedToken` returns success but
`TokenRestrictedSids` query fails; the control correctly reports failure.
Native protected-service process inspection was denied with error 5, without
elevation. These owned-token controls are not genuine service-token proof.

Inspected token bodies at Wine
[`59416cf5`](https://github.com/wine-mirror/wine/blob/59416cf58482d97371d17207ddbfd4d6cc22b347/server/token.c)
and Proton
[`dc26e618`](https://github.com/ValveSoftware/wine/blob/dc26e61847081a1b5cb0733dc30feba6ee575482/server/token.c)
do not store restricting SIDs. The staging tree at
[`2395d933`](https://github.com/wine-staging/wine-staging/tree/2395d93338d6b75d44c4c68fec38213f2398a5a9)
has no matching named token-restriction patch. ReactOS
[`e38a44d1`](https://github.com/reactos/reactos/blob/e38a44d1b4910d059659509b67e3fb6befc1fa09/ntoskrnl/se/tokencls.c)
contains real restricted-SID query logic, but that GPL kernel code is a
behavior/reference source, not a verified Wine backport. No complete faithful
SCM restricted-principal implementation has been verified in this bounded
search. This is limited coverage, not proof of universal absence.

The next implementation must preserve the configured account, genuine
service/logon identity, normal/restricted lists and real access enforcement
before launching the service with its intended primary token. A SID-only
injection, type-3 downgrade, descriptor omission or permission rewrite would
conceal the actual defect. The original client must then be retried unchanged;
the installer, DeveloperUnsigned identity and helper controls do not prove
an Xbox window, Microsoft sign-in or the account-backed library.

The earlier bounded negative source search is now superseded by an actual
upstream-author reference: Zhiyi Zhang's public WineHQ branch
[`bug-23698-react-native-alpc-part4`](https://gitlab.winehq.org/zhiyi/wine/-/tree/c415dc732ce0a79b91391ba353251c00efc56dc8)
contains real connect, accept, message and disconnect code, not just mainline
stubs. Its thirteen-commit ALPC-only closure is being backported into a separate
matched x64 engine, retaining the LGPL-2.1-or-later attribution. The first
byte/context candidate passes real paired connection, request/reply, caller-PID
and disconnect controls. It rejects unqualified security/token/handle attributes;
the original author suite failed an unsupported accept-flag case and timed out,
so that suite is not claimed green.

The next isolated candidate implements the reached extended connection's
server-security requirement against the actual server-process token. Native
Windows controls establish required access bit 1, unchanged outputs and no
request delivery on `STATUS_SERVER_SID_MISMATCH`, and real client/server identity
separation using a locally restricted client token. They also establish that
the original security-attribute shape requires genuine message-bound sender
impersonation, not simply forwarding a thread handle. Independent security,
ownership and lifetime gates remain required before adoption; these reference
backports do not yet establish original Xbox startup, a window or a library.

`wine-current-package-family.patch` also implements the actually invoked
`GetCurrentPackageFamilyName` from the registered token's `WIN://SYSAPPID`
family value. Native Windows and Wine agree on invalid arguments, untouched
unpackaged outputs and character-count buffer sizing. A probe inside the
activated original Xbox child returns the same family and required length as
the native Xbox process; no identity is selected from the executable name.
This repairs a measured identity API gap, not the missing server argument.
These retries disable the window driver:
they establish prerequisite progress, not application rendering or a working
library. Full RPC transport and platform-service requirements remain unfinished.

## Narrow import integration

`redirect-pe-import.py` redirects a single imported function to an experimental
compatibility DLL. It splits the original module's import descriptor around
that function while preserving every existing IAT slot address, including
ordinal imports.

Example using separately obtained, user-owned laboratory files:

```text
python redirect-pe-import.py original.dll experimental.dll \
  --module ntdll.dll --symbol RtlQueryPackageClaims \
  --replacement package-claims.dll
```

An ordinal target uses `--symbol "#122"`. Ordinals are retained rather than
converted to guessed names; the replacement provider must export the same
ordinal with the verified Windows ABI. Tests cover both pointer widths,
first/middle/last ordinal slots and invalid/missing ordinal rejection.

The tool refuses to overwrite the source or an existing destination. It clears
the modified copy's Authenticode and bound-import directory references; the
modified copy is **not vendor-signed**. Keep the original signed DLL and verify
its hash independently. Never apply this tool to the globally installed
CrossOver application or another working bottle as an implicit side effect.

Run the structural tests with:

```text
python test-redirect-pe-import.py
```

Tests cover both pointer widths, first/middle/last import positions, unchanged
IAT addresses and missing-target rejection. The actual experimental native
XAML image was independently checked: one of 435 imports changed provider,
and all original IAT addresses remained intact.

## Remaining application requirements

`stage-winrt-package-classes.py` prepares in-process activation mappings from
the original application/framework manifests and captured file hashes.
It does not mark a package installed or create token identity. Providers
supplied by another verified package are resolved only when the file hash is
unambiguous. Conflicting class declarations are reported and excluded rather
than arbitrarily registered globally. The current Xbox dependency set contains
both WinUI generations, so proper package-context resolution remains necessary.

Run its hash/conflict/dependency tests with:

```text
python test-stage-winrt-package-classes.py
```

An Xbox installer window is not an installed Xbox app, and successful package
metadata parsing is not completed package deployment. The original installer
also requires real PackageManager/PackageVolume behavior, including
`IPackageManager3`. Native app startup additionally depends on package
activation, token attributes and other Windows runtime services.

The experimental PackageManager provider now supports the default-volume
query and a typed, verified-absent catalog result; it does not implement package
installation. The original installer renders with bottle-local WPF software
rendering, but its Install action reaches a genuine
`Windows.ApplicationModel.Store.Preview.InstallControl.AppInstallManager`
dependency and reports an error. Native Store-client class-factory resolution
is not object activation or a functioning Store installation service.
See `package-manager-notes.md` and `package-manager-store-notes.md` for measured
interfaces, binary provenance, current activation blockers and limitations.

The narrow x64 state-location bridge in `package-manager-state-notes.md`
implements the measured, five-argument, byte-count registry-location contract
for the actual disabled-state-separation mode. It does not create registry
state or invent redirected paths. Independent controls confirm the genuine
Store client now completes its two state-location queries, then fails object
activation with `0x80040154` at the real out-of-process
`Windows.Internal.InstallService.Control.InstallServiceControl` dependency.
This is not a working InstallService service or a successful app installation.

No component here substitutes downloaded game editions, fabricates
authentication or licensing, or treats a zero exit code as library/gameplay
success.
