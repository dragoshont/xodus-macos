# Experimental PackageManager default-volume provider

## Verified boundary (2026-10-06)

This is a real WinRT COM provider for the original Microsoft Xbox installer's
observed prerequisite. It is **not an AppX deployment backend**. It does not
install/register packages, report fabricated installed packages, provide Store
identity, or change account/game data. No installer GUI was launched or operated
while building/testing this provider.

The unchanged `XboxDeploymentPrerequisiteProbe.exe` initially activated Wine's
PackageManager but failed the genuine WinMD projection's `IPackageManager3` cast.
After the scoped activation change it printed:

```text
ACTUAL_PACKAGE_MANAGER_ACTIVATED
ACTUAL_DEFAULT_PACKAGE_VOLUME=Windows.Management.Deployment.PackageVolume
```

Both native x64/x86 tests and native-.NET/genuine-WinMD x64/x86 property probes
passed. The native tests cover QI/controlling-IUnknown identity, interface lists,
null outputs, unsupported methods, name lookup including embedded NUL rejection,
completed async result/callback/close behavior, capacity agreement with Win32,
and 1,000 independent activation/volume lifetimes. `DllCanUnloadNow` returns
`S_OK` only after all objects are released.

## ABI provenance and files

* `package-manager-abi.h`: exact separate IInspectable vtables for
  IPackageManager 1/2/3 and IPackageVolume 1/2, plus uint64 async ABI.
* `package-manager-provider.cpp`: provider implementation and WinRT exports.
* `package-manager-test.cpp`: native console tests; loads an explicitly supplied DLL.
* `package-volume-probe.cs`: reflection through genuine Microsoft WinMD and native
  .NET, not a replacement managed PackageManager implementation.
* `package-manager-inventory.h`: read-only, fail-closed fresh-prefix inventory checks.
* `package-manager-catalog-probe.cs`: genuine WinMD user/family package enumeration.

Deployment ABI source:
<https://github.com/microsoft/windows-rs/blob/42503b90ef4486c7878b227bbeee6cf75c008fe3/crates/libs/windows/src/Windows/Management/Deployment/mod.rs>

Exact source SHA256:
`36137250cf31dab59a6f714f6e61c0e8dbe5b430c2b59d3cfdb0622113163274`.

Async ABI source:
<https://github.com/microsoft/windows-rs/blob/0.58.0/crates/libs/windows/src/Windows/Foundation/mod.rs>

Exact source SHA256:
`f2a0e2d241a18580198a2cb920bce166c74b6e1265bddd47eae4d56c66351a77`.

Collection ABI source:
<https://github.com/microsoft/windows-rs/blob/0.58.0/crates/libs/windows/src/Windows/Foundation/Collections/mod.rs>

Exact source SHA256:
`9cc5c9f3ca019b194fad0841dd14246e7544f036d98bf2119e1208acb8e595d5`.

`IAsyncOperation<UInt64>` IID is the standard WinRT parameterized IID,
`2a70d630-0767-5f0a-a1c2-deb08126e26e`, computed from
`pinterface({9fc2b0bb-e446-44e2-aa61-9cab8f636af2};u8)`.
The existing parent-owned parameterized-IID provider independently resolved
this same IID during the successful genuine-metadata probe.

## Implemented behavior

* PackageManager activation, interfaces 1/2/3, proper COM ownership.
* `GetDefaultPackageVolume`: a separately owned volume for the experimental
  bottle's actual C: root; filesystem errors propagate.
* `FindPackageVolumeByName`: resolves only that volume's actual Win32 name;
  unknown names return `ERROR_NOT_FOUND`, empty names `E_INVALIDARG`.
* Volume offline state from `GetFileAttributesW`, system-volume state from
  `GetWindowsDirectoryW`/`GetVolumePathNameW`, volume name from
  `GetVolumeNameForVolumeMountPointW`, and hard-link capability from
  `GetVolumeInformationW`. Name omits the Win32 trailing slash and MountPoint
  returns `C:`, matching read-only observation on the user's real Windows PC.
* `GetAvailableSpaceAsync`: queries `GetDiskFreeSpaceExW` on the bottle's actual
  C: filesystem; returns a real completed `IAsyncOperation<UInt64>` and IAsyncInfo.
  It supports completion delegates and explicit close without performing deployment.
* `IsAppxInstallSupported` and `IsFullTrustPackageSupported` are **false**.
  `PackageStorePath` returns `HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)` with a null
  HSTRING because no real durable package store exists.

Observed bottle properties: `MountPoint=C:`, offline false, system volume true,
hard-link flag false. Volume name was Wine's
`\\?\Volume{00000000-0000-0000-0000-000000000043}`; this is **Wine's drive
metadata**, not a claimed APFS UUID. Capacity varied with real host writes,
approximately 180.3 GB free, and agreed with direct Win32 queries. No capacity,
UUID, package catalog, or package store is hardcoded.

Except for the bounded current-user/family inventory query described below,
remaining methods on manager/volume interfaces are explicit `E_NOTIMPL`
(null output; `E_POINTER` for null result pointers). In particular:
Add/Update/Stage/Register/Remove/Cleanup/MovePackage, package catalog queries,
FindPackageVolumes, volume creation/removal/online/offline/default mutation, and
package state/status mutations. Later interface versions are `E_NOINTERFACE`.
Unsupported calls log the method name, not URI/identity/account information.
There is no sealed-source registration implementation.

The next real installer action belongs to the parent. This resolves its
observed v3/default-volume cast failure, **not installation**. The cheapest
honest deployment follow-on requires a durable package store/catalog,
manifest/signature/dependency validation, transaction/rollback, and actual
registration/activation semantics. Returning an async success over copied
files or advertising a nonexistent WindowsApps path would misrepresent that
missing backend and is deliberately not implemented.

## Build and headless tests

The seven source/documentation files are in `scripts\macos\compatibility`.
All Mac build products and evidence are in:
`/Users/dragoshont/xodus-runs/xbox-app-crossover-20261005-001`.
Exact evidence/binaries/registry backups are also retained in the assigned
Windows session scratch directory:
`C:\Users\dragoshont\.copilot\session-state\4a8b01bb-a77c-4773-9d3b-41eebd5fa602\files\package-deployment`.

Run in the Mac stage after copying the C++/header files there:

```bash
export TMPDIR="$PWD"
for arch in x86_64 i686; do
  case "$arch" in
    x86_64) tag=x64; extra= ;;
    i686) tag=x86; extra=-Wl,--kill-at ;;
  esac
  /opt/homebrew/bin/$arch-w64-mingw32-g++ -std=c++11 -Wall -Wextra -Werror \
    -O2 -static -shared package-manager-provider.cpp \
    -o package-manager-provider-$tag.dll $extra -lole32 -lruntimeobject -luuid -ladvapi32
  /opt/homebrew/bin/$arch-w64-mingw32-g++ -std=c++11 -Wall -Wextra -Werror \
    -Wno-cast-function-type -O2 -static package-manager-test.cpp \
    -o package-manager-test-$tag.exe -lole32 -lruntimeobject -luuid -ladvapi32
done
win_stage="Z:${PWD//\//\\}"
run() {
  /Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin/wine \
    --bottle XboxAppExperimental20261005-001 --no-gui \
    --dll 'winemac.drv=;mscoree=n;wintypes=n;windows.ui=b;api-ms-win-core-winrt-roparameterizediid-l1-1-0=n' \
    --debugmsg '-all,err+all,warn+combase' "$@"
}
for tag in x64 x86; do
  run "$win_stage\package-manager-test-$tag.exe" \
    "$win_stage\package-manager-provider-$tag.dll"
done
run 'C:\windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe' /nologo \
  /platform:x64 "/out:$win_stage\package-volume-probe-x64.exe" \
  "$win_stage\package-volume-probe.cs"
run 'C:\windows\Microsoft.NET\Framework\v4.0.30319\csc.exe' /nologo \
  /platform:x86 "/out:$win_stage\package-volume-probe-x86.exe" \
  "$win_stage\package-volume-probe.cs"
run "$win_stage\XboxDeploymentPrerequisiteProbe.exe"
run "$win_stage\package-volume-probe-x64.exe"
run "$win_stage\package-volume-probe-x86.exe"
run 'C:\windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe' /nologo \
  /platform:x64 "/out:$win_stage\package-manager-catalog-probe-x64.exe" \
  "$win_stage\package-manager-catalog-probe.cs"
run 'C:\windows\Microsoft.NET\Framework\v4.0.30319\csc.exe' /nologo \
  /platform:x86 "/out:$win_stage\package-manager-catalog-probe-x86.exe" \
  "$win_stage\package-manager-catalog-probe.cs"
run "$win_stage\package-manager-catalog-probe-x64.exe"
run "$win_stage\package-manager-catalog-probe-x86.exe"
```

The probes report `PACKAGE_MANAGER_TEST_PASS failures=0` and
`PACKAGE_VOLUME_METADATA_PROBE_PASS`. The inherited metadata resolver emits a
nonfatal lookup failure for the compatibility async implementation class;
genuine `IAsyncOperation<UInt64>.GetResults` still succeeds with the verified IID.
The inherited `winebth` driver error is unrelated to these passing tests.

## Persistent activation change and rollback

Only in `XboxAppExperimental20261005-001`, installed:

* x64 DLL: `C:\windows\system32\package-manager-provider.dll`.
* x86 DLL: `C:\windows\syswow64\package-manager-provider.dll`.

Only the `DllPath` value of
`HKLM\Software\Microsoft\WindowsRuntime\ActivatableClassId\Windows.Management.Deployment.PackageManager`
in **both 64-bit and 32-bit registry views** changed, from
`C:\windows\system32\appxdeploymentclient.dll` to
`C:\windows\system32\package-manager-provider.dll`.
The 32-bit path uses normal WOW64 redirection to its matching x86 DLL.
No other activation classes or DLL overrides were modified.
The provider/mappings are deliberately **left installed** for the parent's next
original-installer test; they have not been restored.

Original class entries were exported before either mapping change to:
`package-manager-activation-before-x64.reg` and
`package-manager-activation-before-x86.reg`. Both contain only that class and
the original DllPath. Roll back using the same headless `run` helper:

```bash
run 'C:\windows\system32\reg.exe' import \
  "$win_stage\package-manager-activation-before-x64.reg"
run 'C:\windows\syswow64\reg.exe' import \
  "$win_stage\package-manager-activation-before-x86.reg"
```

Do not alter any game bottle. No original installer restarts, GUI interaction,
sign-in, installed Xbox package state, account data, or game data were part of
this verification.

## Next observed call: current-user/family inventory

The parent's original signed-installer profile
`installer-native-gui-volume-provider/stderr.log`, line 177, identified the
precise next failing operation:

```text
PackageManager compatibility: FindPackagesByUserSecurityIdPackageFamilyName unavailable (E_NOTIMPL); no durable deployment backend
```

It is the genuine metadata's public
`PackageManager.FindPackagesForUser(String, String)` overload. The follow-on
implements **only that manager method**, returning a correct
`IIterable<Windows.ApplicationModel.Package>`/`IIterator<Package>` snapshot
when read-only inspection proves the experimental prefix has no package
registration repositories. No package objects or installed-state records
are fabricated, and no empty repository is manufactured on disk.

Every query rechecks both registry views of:

* HKLM `Software\Microsoft\Windows\CurrentVersion\Appx`.
* HKLM `Software\Microsoft\Windows\CurrentVersion\AppModel\StateRepository`.
* HKCU `Software\Classes\Local Settings\Software\Microsoft\Windows\CurrentVersion\AppModel\Repository\Packages`.
* HKLM and HKCU `Software\Classes\ActivatableClasses\Package`.

It also checks configured ProgramData's `Microsoft\Windows\AppRepository` and
the system/x86 Program Files `WindowsApps` locations. All were genuinely absent
in this experimental bottle before implementation. A present repository,
even one not understood by this provider, returns `E_NOTIMPL` rather than
silently reporting no packages. Access/filesystem/configuration errors
propagate; missing/empty environment roots and non-absolute paths fail closed.
This is deliberately a **fresh-prefix-only reader**, not a general Windows
catalog or a durable deployment writer.

The empty user argument queries the current user; an explicit SID is accepted
only after comparison with the real current process token. Other users remain
`E_NOTIMPL`; malformed/embedded-NUL arguments fail. Token/SID values are never
logged. The family parameter is not hardcoded to Xbox: unknown family names
use the same actual empty-registration evidence.

Typed iterable IID:
`69ad6aa7-0c49-5f27-a5eb-ef4d59467b6d`.
Typed iterator IID:
`0217f069-025c-5ee6-a87f-e782e3b623ae`.
They derive from the standard generic signatures containing
`rc(Windows.ApplicationModel.Package;{163c792f-bd75-413c-bf23-b1fe7b95d825})`.
The parent-owned parameterized-IID provider independently resolved both
during genuine WinMD enumeration.

Native x64/x86 tests exercise collection lifetime, independent iterators,
null outputs, bounds, GetMany, invalid and non-current SIDs, and present
repository/path rejection without creating or altering any repository.
Genuine WinMD/.NET x64/x86 probes enumerate GamingApp, GamingServices, and an
unregistered arbitrary family using the actual projected method; each reports
zero registrations and `PACKAGE_MANAGER_CATALOG_METADATA_PROBE_PASS`.
The previous volume/property probes still pass.

The parent's subsequent original-installer test restored the live Microsoft UI;
the user's actual Install click then reached the separate Store broker
activation boundary. See `package-manager-store-notes.md` for the exact genuine
Store ABI, pinned-installer call chain, and read-only native-client control.
No Store success is represented by this PackageManager provider.

The update replaces **only this provider's two DLL files**; activation mappings
are unchanged. Previous working default-volume providers are retained as
`package-manager-provider-before-catalog-x64.dll` and
`package-manager-provider-before-catalog-x86.dll` in the Mac stage.
No installer was restarted by this worker. The parent owns the next GUI attempt
and any subsequently observed call; deployment remains unsupported.

Final DLL SHA256 pins:

```text
69026088bef024f1228aedd417bc16397d1dbb7d8a3946538a39d41dff677659  package-manager-provider-x64.dll
9105099d8eb210705bc390b4052cf9fb1c8122c614955e1d76e7547961065b71  package-manager-provider-x86.dll
```
