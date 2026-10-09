# Narrow x64 registry state-location bridge

This replaces the failed whole-native-KernelBase diagnostic route. It implements
only justified `GetPersistedRegistryLocationW` behavior, without loading native
KernelBase in Wine, creating registry state, fabricating installed packages, or
providing Store authentication.

## Exact contract and actual activation inputs

The genuine Windows KernelBase export, RVA `0xe6eb0`, has **five** arguments:

```c
DWORD WINAPI GetPersistedRegistryLocationW(
    LPCWSTR SourceID, LPCWSTR DefaultPath, LPWSTR TargetPath,
    DWORD BufferLengthInBytes, LPDWORD BufferLengthOutBytes);
```

This was established before implementation by the exact export disassembly and
a native Windows control. It wraps the
[documented RtlGetPersistedStateLocation](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntddk/nf-ntddk-rtlgetpersistedstatelocation)
with CustomValue NULL and StateLocationType 0 (registry), then converts NTSTATUS
to a Win32 error. No seven-argument Win32 ABI was guessed.

The native control patches only the IAT of InstallService **in its own Windows
console process**, observes the real call, and delegates unchanged to the
genuine Windows implementation. No system DLL on disk or other process is
modified; no Store/install API beyond factory/object activation is invoked.

The actual AppInstallManager activation produced two calls, both at caller RVA
`0x48d54`, each with a 256-byte buffer and size-out pointer:

| Actual SourceID | Caller-supplied DefaultPath | Genuine output bytes |
| --- | --- | --- |
| InstallServiceMutableState | `SOFTWARE\Microsoft\Windows\CurrentVersion\InstallService\State` | 126 |
| WindowsStoreWindowsUpdate | `SOFTWARE\Microsoft\Windows\CurrentVersion\WindowsStore\WindowsUpdate` | 138 |

Both genuine calls returned 0 and the exact supplied default. Genuine Windows
factory **and object activation** returned S_OK in that read-only control.
No install, purchase, download, entitlement, or account action was requested.

## Measured state and supported behavior

The genuine `ntdll.RtlIsStateSeparationEnabled` predicate was disassembled and
checked against native Windows behavior:

* x64 `PEB.SharedData`, offset `0x90` (also declared in public Wine winternl.h).
* For a populated silo scope: its first DWORD and state byte at `+0x1d`.
* Otherwise: KUSER_SHARED_DATA flags at `0x7ffe02f0`, bit 10.

The provider reads only its **actual current process/platform state**, with
NtQueryInformationProcess and VirtualQuery bounds/protection checks. It does
not force flags or treat an unreadable scope as disabled. Invalid silo boolean
values fail closed.

Native Windows: shared flags `0x0000098e`, no silo scope, state disabled; the
genuine ntdll selector independently returned 0. Original experimental Wine:
shared flags `0x00000000`, no silo scope, state disabled. This is measured
platform state, not inferred from missing exports or assumed from a registry
path.

**Only disabled state separation is implemented.** In that measured mode the
genuine API returns the caller's default, independently of SourceID, including
null SourceID. The bridge copies that caller-provided UTF-16 default; the
production implementation contains **no hardcoded target registry path**.

Enabled/unavailable state separation returns `ERROR_NOT_SUPPORTED` (50) with
outputs untouched. Resolving an enabled silo's real NT redirection map is not
implemented or guessed. A returned location does not assert that a registry key,
package catalog, Store service, or installed app exists.

Native Windows contract measurements, reproduced by unit tests:

* Lengths are **bytes**, including the terminating UTF-16 NUL.
* Insufficient/odd-too-small capacity returns `ERROR_MORE_DATA` (234), reports
  required bytes when requested, and leaves the target untouched.
* Size-out is optional; null target with insufficient capacity is a valid size
  query.
* Missing DefaultPath returns `ERROR_FILE_NOT_FOUND` (2), with outputs unchanged.
* Empty default succeeds with a two-byte terminator.
* LastError is preserved; status is returned directly.
* Native Windows faults for null target with sufficient capacity. The bridge
  deliberately returns `ERROR_INVALID_PARAMETER` (87) instead: a documented
  safety improvement for invalid input, not claimed native success.

## Five owned implementation/control files

* `package-manager-state.h`: exact API and internal typed context contract.
* `package-manager-state.c`: actual selector and bounded disabled-state behavior.
* `package-manager-state-test.c`: buffer boundaries, null/default/UTF-16 cases,
  unsupported contexts, guards and LastError tests.
* `package-manager-state-contract-control.cpp`: genuine Windows IAT observation,
  native API cases, and state-selector comparison. No GUI or installation.
* This document.

The provider exports **only GetPersistedRegistryLocationW**. Its internal
test resolver/context helpers are not exported as bypasses.

## Build and verification

In the existing Mac stage:

```bash
export TMPDIR="$PWD"
cc=/opt/homebrew/bin/x86_64-w64-mingw32-gcc
cxx=/opt/homebrew/bin/x86_64-w64-mingw32-g++
"$cc" -std=c11 -Wall -Wextra -Werror -O2 -static -shared \
  package-manager-state.c -o package-manager-state-provider-x64.dll
"$cc" -std=c11 -Wall -Wextra -Werror -O2 -static \
  package-manager-state-test.c package-manager-state.c -o package-manager-state-test-x64.exe
"$cc" -std=c11 -Wall -Wextra -Werror -O2 -c \
  package-manager-state.c -o package-manager-state-control.o
"$cxx" -std=c++11 -Wall -Wextra -Werror -Wno-cast-function-type -O2 -static -municode \
  package-manager-state-contract-control.cpp package-manager-state-control.o \
  -o package-manager-state-contract-control-x64.exe -lole32 -lruntimeobject -luuid
```

The tests pass on both actual Windows and the original experimental CrossOver
engine (`PACKAGE_MANAGER_STATE_TEST_PASS failures=0`). Controls:

```powershell
# Assigned Windows scratch directory; real native Windows execution:
& "$scratch\package-manager-state-contract-control-x64.exe" --capture C:\Windows\System32\InstallService.dll
& "$scratch\package-manager-state-contract-control-x64.exe" --selector
& "$scratch\package-manager-state-contract-control-x64.exe" --case `
  InstallServiceMutableState 'SOFTWARE\Microsoft\Windows\CurrentVersion\InstallService\State' `
  0 null sizeout
```

`@null` and `@empty` sentinel arguments must be quoted in PowerShell, otherwise
PowerShell treats them as splats. Each native negative case is an independent
console process, with error dialogs disabled; invalid-pointer faults were
recorded without a GUI.

Wine executions use the same original experimental bottle and
`winemac.drv=` headless override as previous controls, not the parent's new
coherent engine or APFS clone.

## Genuine activation retest and exact next boundary

The isolated profile is:
`/Users/dragoshont/xodus-runs/xbox-app-crossover-20261005-001/package-manager-installservice-state-provider/x64`.

It contains the existing single-import-derived InstallService lab DLL
(`0f52538e773c95642ed3b692c5785c2f18df8dd8a7447f42730d44021dee3d37`),
the tested state provider named `package-manager-state-helper.dll`, the native
console control, and previously verified UMPDC/rmclient/HAM forwarding artifacts.
There is **no native KernelBase** in this profile.

The original single-import redirection receipt is unchanged: same IAT slot,
only GetPersistedRegistryLocationW routed to the new helper. The derived client
is explicitly **not vendor-signed**; the original signed client remains intact.
No class mapping, bottle system DLL, or registry location was replaced.

With the narrow provider, genuine AppInstallManager:

```text
STORE_NATIVE_FACTORY_HRESULT=0x00000000
GetPersistedRegistryLocationW context=0 result=0
GetPersistedRegistryLocationW context=0 result=0
STORE_NATIVE_ACTIVATION_HRESULT=0x80040154
```

The original GetPersistedRegistryLocationW abort is gone. This new failure is a
real missing **out-of-process WinRT service**, not another state-helper failure:

* Actual attempted class:
  `Windows.Internal.InstallService.Control.InstallServiceControl`.
* Genuine Windows class registration: `ActivationType=1`, `Server=InstallService`.
* Genuine Windows server registration: `ServerType=2`, `ServiceName=InstallService`.
* The bottle has no InstallService service registration, as measured earlier.

There is also an observed missing OneSettingsPayload class warning; no claim is
made that it caused the final error. The exact reached InstallServiceControl
dependency was identified by the new activation log and read-only Windows
hosting metadata, without registering it or fabricating its RPC behavior.

A genuine InstallService service host/WinRT out-of-process activation path is
the next required backend. This state helper does not implement that service,
Store queue, downloads, registrations, authentication, or entitlement.

Evidence retained in the assigned scratch/Mac stage:
`package-manager-state-native-capture.log`,
`package-manager-state-native-contract-matrix.log`,
`package-manager-state-native-empty-default.log`,
`package-manager-state-callsite-disassembly.log`,
`package-manager-state-callsite-literals.log`,
`package-manager-state-platform-disassembly.log`,
`package-manager-state-windows-selector.log`,
`package-manager-state-wine-selector.log`,
`package-manager-state-windows-test.log`,
`package-manager-state-wine-test.log`,
`package-manager-state-provider-activation-x64.log`.

No original installer/Xbox GUI launch, input, account copying, class-map change,
commit, runtime-source modification, or CPU lease was part of this work.

Verified state-provider SHA256:
`17172e2eb646fd630d7f6df7d127dbf0995ff765fa908e8dc80fef84962dda71`.
