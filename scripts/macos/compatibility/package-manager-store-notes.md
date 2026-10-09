# Original Xbox installer: real Store install-manager boundary

## Current verified failure

The parent/user's genuine installer now renders and remains alive after the
default-volume and fresh-inventory fixes. After the user accepted the license
and clicked Install, profile `installer-native-gui-catalog-provider/stderr.log`
recorded:

```text
375: Class L"Windows.ApplicationModel.Store.Preview.InstallControl.AppInstallManager" not found in registry
376: RoGetActivationFactory Failed to find library for L"Windows.ApplicationModel.Store.Preview.InstallControl.AppInstallManager"
```

The displayed `0x80131522` is a managed type/activation failure, not evidence of a
successful Store request. No GUI, input, screenshot, restart, package deployment,
service change, or class-mapping change was performed by this worker.

## Actual metadata and installer call chain

The genuine metadata already supplied by the parent resolves the class from
`Windows.ApplicationModel`, **not** `Windows.Management`:

```text
Windows.ApplicationModel.Store.Preview.InstallControl.AppInstallManager,
Windows.ApplicationModel, Version=255.255.255.255, Culture=neutral,
PublicKeyToken=null, ContentType=WindowsRuntime
```

`Microsoft-Windows.ApplicationModel.winmd` SHA256:
`25c17722258acd8f3370612d6c34c6464578741f28bacbfa2e7fe56c1d1536f2`.

The read-only `package-manager-store-inspect.cs` console probe dumps the real
metadata interface GUIDs and methods in metadata-token/vtable order, without
activating any Store class. Relevant exact interfaces:

| Interface | IID | Observed required operation / zero-based COM slot |
| --- | --- | --- |
| IAppInstallManager | `9353e170-8441-4b45-bd72-7c2fa925beee` | get_AppInstallItems: 6; GetIsAppAllowedToInstallAsync: 24 |
| IAppInstallManager3 | `95b24b17-e96a-4d0e-84e1-c8cb417a0178` | MoveToFrontOfDownloadQueue: 13 |
| IAppInstallManager5 | `3cd7be4c-1be9-4f7f-b675-aa1d64a529b2` | get_AppInstallItemsWithGroupSupport: 6 |
| IAppInstallManager6 | `c9e7d408-f27a-4471-b2f4-e76efcbebcca` | StartProductInstallAsync with AppInstallOptions: 10 |

`package-manager-store-abi.h` contains the exact v1/v6 native ABI for a genuine
client control, with opaque object/async pointers where the control does not
inspect or fabricate those objects. Interfaces are separate IInspectable
contracts. This is **not a Store compatibility provider**.

`package-manager-store-static-test.py` reads CLI metadata and method bodies
without loading/invoking the installer. It verifies the exact approved installer
SHA256
`ba30ea16cd8fbd9209d40ae193206ad00f042d100524cf310982c33369325ca2`
and found 24 real Store call instructions, including:

* `XboxInstaller.Helpers.AppInstallManagerShim..ctor`: AppInstallManager constructor.
* `GetIsAppAllowedToInstallAsync`: the v1 Store policy/product eligibility query.
* `GetPendingInstall`: both actual Store queue getters.
* `MoveToFrontOfDownloadQueue`: real Store queue prioritization.
* The `<StartProductInstallAsync>d__7.MoveNext` state machine constructs
  `AppInstallOptions`, sets completed-install notification mode and TargetVolume,
  then invokes the **v6 five-argument StartProductInstallAsync overload**.
* AppInstallItem status subscription/cancellation/restart and actual
  AppInstallStatus bytes, progress, error, install-state and ready-for-launch getters.

The v6 install call takes four strings plus real AppInstallOptions, returning
`IAsyncOperation<IVectorView<AppInstallItem>>`. It is a Store product
acquisition/download/deployment operation, **not** a local file URI passed to
`PackageManager.AddPackageAsync`.

## Real backend prerequisites; no fake implementation

[Microsoft's public class documentation](https://learn.microsoft.com/en-us/uwp/api/windows.applicationmodel.store.preview.installcontrol.appinstallmanager?view=winrt-26100)
explicitly says access is protected by a private capability restricted to
Microsoft-developed apps and that Store-related services must be initialized
before product-install requests. The original installer is Microsoft's signed
code; this does not authorize the compatibility provider to manufacture the
private capability, a Store acquisition identity, purchases, licensed identity,
or successful install/status objects.

Read-only queries of the **Mac experimental bottle**, not the parent's Windows
lookup, found no service registrations for `InstallService`, `ClipSVC`,
`AppXSvc`, or `StateRepository`. These are measured absences, **not yet a
native-client RPC trace proving which particular service this build calls**.
The existing PackageManager provider also honestly reports that no durable
AppX package store/deployment backend exists.

The parent's verified handoff identified the real DLLPath as
`C:\Windows\System32\InstallService.dll`, ActivationType 0, Threading 0.
The Windows class lookup was deliberately not duplicated. Both the supplied
system32 client and its genuine SysWOW64 counterpart were checked with
Get-AuthenticodeSignature: Valid, Microsoft Windows signer, version
10.0.26100.8117. Only system code was copied; no accounts or service data.
Native controls and the exact later activation block are recorded below.
No new Store activation mapping was installed.

## Built, tested headless controls

`package-manager-store-control.cpp` initializes WinRT, loads an explicitly
supplied DLL, calls its genuine `DllGetActivationFactory`, activates its actual
object, and verifies IAppInstallManager v1/v6 QI. Optional `--read-queue` calls
only the native client's real queue getter. It never requests an install,
mutates settings/identity, changes registry mappings, or generates install items.

Both x64/x86 builds passed negative controls: the current PackageManager DLL
correctly rejects the Store class with `CLASS_E_CLASSNOTAVAILABLE` (`0x80040111`).
This is proof of a correct rejection, **not proof a native Store client works**.
The genuine WinMD dump and pinned-installer static-call test also passed.

In the existing Mac stage, build using the previously documented `run` helper:

```bash
export TMPDIR="$PWD"
for arch in x86_64 i686; do
  case "$arch" in x86_64) tag=x64;; i686) tag=x86;; esac
  /opt/homebrew/bin/$arch-w64-mingw32-g++ -std=c++11 -Wall -Wextra -Werror \
    -Wno-cast-function-type -O2 -static package-manager-store-control.cpp \
    -o package-manager-store-control-$tag.exe -lole32 -lruntimeobject -luuid
done
run 'C:\windows\Microsoft.NET\Framework\v4.0.30319\csc.exe' /nologo \
  /platform:x86 "/out:$win_stage\package-manager-store-inspect-x86.exe" \
  "$win_stage\package-manager-store-inspect.cs"
run "$win_stage\package-manager-store-inspect-x86.exe" metadata

# Direct diagnostic; do not register it into the live installer yet:
run "$win_stage\package-manager-store-control-x86.exe" \
  "$win_stage\package-manager-InstallService-x86.dll" --read-queue
```

Use the same `winemac.drv=` headless override from `package-manager-notes.md`.
Additional `warn+module`/`err+rpc` tracing can expose the real native failure;
no missing function or service should be replaced with a claimed success.
A real install operation remains outside this read-only control.

## Genuine native-client results after the handoff

Two distinct bounded controls were performed in both architectures:

1. Unmodified signed InstallService alone: `LoadLibrary` fails with error 126.
   Actual missing dependency modules were UMPDC and
   `api-ms-win-ham-apphistory-l1-1-0.dll`.
2. Restore the exact genuine dependencies **only in isolated control directories**:
   signed UMPDC, signed rmclient, and a generated PE API-set forwarder. Loading
   succeeds and the genuine `DllGetActivationFactory` returns **S_OK**. However
   `ActivateInstance` actually calls the missing
   `api-ms-win-stateseparation-helpers-l1-1-0.dll.GetPersistedRegistryLocationW`
   and Wine aborts in both architectures before any manager object/queue result
   can be returned.

These second controls live in the existing Mac stage's
`package-manager-installservice-control/x64` and `/x86`, not in any bottle
system directory. They use per-process native DLL selection only. Nothing was
copied over Wine core DLLs and the live original installer was not restarted.

The HAM API-set forwarder is **not a fake implementation**: Windows' genuine
signed `apisetschema.dll` maps that contract to `rmclient.dll`, and both signed
rmclient DLLs export its required `HamQueryPackageUsageInfo`. The forwarder
contains just this genuine route, no entry point/imports/function bodies:

```def
LIBRARY api-ms-win-ham-apphistory-l1-1-0.dll
EXPORTS
    HamQueryPackageUsageInfo=rmclient.HamQueryPackageUsageInfo
```

The `.def` and generated aliases are retained in the assigned scratch area.
Generate from the Mac stage with matching MinGW compilers:

```bash
/opt/homebrew/bin/x86_64-w64-mingw32-gcc -shared -nostdlib -Wl,--entry,0 \
  package-manager-ham-api-set.def \
  -o package-manager-installservice-control/x64/api-ms-win-ham-apphistory-l1-1-0.dll
/opt/homebrew/bin/i686-w64-mingw32-gcc -shared -nostdlib -Wl,--entry,0 \
  package-manager-ham-api-set.def \
  -o package-manager-installservice-control/x86/api-ms-win-ham-apphistory-l1-1-0.dll
```

The control uses the normal headless overrides plus
`umpdc=n;rmclient=n;api-ms-win-ham-apphistory-l1-1-0=n`, with working directory
and executable inside the matching isolated directory.

Exact restored-dependency x86 control (use x64 paths/names for that build):

```bash
cd /Users/dragoshont/xodus-runs/xbox-app-crossover-20261005-001/package-manager-installservice-control/x86
win_dir="Z:${PWD//\//\\}"
/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin/wine \
  --bottle XboxAppExperimental20261005-001 --no-gui \
  --dll 'winemac.drv=;mscoree=n;wintypes=n;windows.ui=b;api-ms-win-core-winrt-roparameterizediid-l1-1-0=n;umpdc=n;rmclient=n;api-ms-win-ham-apphistory-l1-1-0=n' \
  --debugmsg '-all,err+all,warn+module,warn+combase' \
  "$win_dir\package-manager-store-control-x86.exe" \
  "$win_dir\package-manager-InstallService-x86.dll" --read-queue
```

**Exact reached blocker:** `GetPersistedRegistryLocationW`, during genuine
AppInstallManager object activation. The actual Windows API-set schema has
`api-ms-win-stateseparation-helpers-l1-1-1 -> kernelbase.dll`; an arbitrary
default path or guessed private ABI is not an implementation of persisted state
separation. No such substitute was supplied.

Additional **unimplemented imports observed by the loader**, not claimed to have
been called yet, include:

* InstallService: NtQueryWnfStateData, RtlIsStateSeparationEnabled,
  RtlIsMultiUsersInSessionSku, RtlIsMultiSessionSku, RtlSetBit,
  GetPackageFullNameFromToken, SHTaskPoolQueueTask/SHTaskPoolAllowThreadReuse,
  and OOBEComplete.
* UMPDC: ZwAlpcConnectPort, ZwAlpcSendWaitReceivePort, ZwAlpcQueryInformation,
  ZwAlpcCancelMessage, ZwAlpcDisconnectPort, AlpcInitializeMessageAttribute,
  AlpcGetMessageAttribute, TpAllocAlpcCompletion/TpWaitForAlpcCompletion/
  TpReleaseAlpcCompletion, plus AVL/thread-pool operations.
* rmclient: PsmGetKeyFromToken/PsmIsValidKey/PsmIsDynamicKey/PsmGetAumidFromKey,
  WNF subscribe/query/unsubscribe, RtlQueryPackageClaims, and
  RtlQueryTokenHostIdAsUlong64.

Wine itself installs aborting unimplemented-import thunks for these missing
symbols; this worker did **not** add success stubs or claim those APIs work.
There is no successful activation, Store queue response, RPC transaction,
download, registration, entitlement, or application installation in this control.

Registering the current native client into the live installer would therefore
replace its catchable missing-class error with this measured native abort.
It was deliberately not done. A real kernelbase state-separation implementation
and real subsequent Store/NT runtime support are needed before that integration
is safe; copying more contracts or registering only the class is insufficient.

`package-manager-installservice-evidence-test.py` verifies all genuine DLL pins,
both API forwarders' exact non-stub export structure, the original missing-module
failure, and both genuine factory successes followed by the exact native
activation block. It passes without pretending that this is installation
success. Run with the existing scoped Python tools:

```powershell
python scripts\macos\compatibility\package-manager-installservice-evidence-test.py $scratch
```

Additional evidence:
`package-manager-installservice-native-x64.log` / `-x86.log`,
`package-manager-installservice-restored-x64.log` / `-x86.log`,
`package-manager-installservice-blockers.log`,
`package-manager-installservice-state-host.log`, and
`package-manager-installservice-evidence-test.log`.

## x64 continuation and snapshot-safe point

When the parent's coherent Wine 11 token runtime became ready, this worker had
**no outstanding bottle registry/provider-DLL write and no CPU lease**. The
parent may snapshot the original experimental prefix: its installed provider
hashes/mappings are still the previously recorded ones. No new engine was run
in this bottle, and the parent's Wine source/build was not modified.

A further x64-only experiment used an ABI-preserving forward to the actual
signed Windows KernelBase export, rather than guessing the private
GetPersistedRegistryLocationW prototype or returning a made-up registry path:

* Signed `C:\Windows\System32\KernelBase.dll` genuinely exports
  GetPersistedRegistryLocationW. SHA256:
  `becad014fb8efa8cb5e314931cca92778ad42c649b12a6909632cacd68af4f40`.
* A no-code PE forwarder routes that symbol to the separately named
  `package-manager-state-kernelbase-x64.dll`, avoiding any replacement of Wine's
  kernelbase DLL. Its `.def` is retained in the assigned scratch directory.
* Direct API-set-name selection did not change the original abort, so the
  existing read-only `redirect-pe-import.py` tool was used to redirect **only**
  this import in a new experimental InstallService copy, preserving every IAT
  slot. The utility itself was not modified.
* This derived lab DLL is explicitly **not vendor-signed**; the tool clears its
  Authenticode metadata and records both hashes in
  `package-manager-state-bridge-receipt.json`. Original signed DLLs remain intact.
  Derived DLL SHA256:
  `0f52538e773c95642ed3b692c5785c2f18df8dd8a7447f42730d44021dee3d37`.

The isolated direct bridge genuinely loads the native KernelBase component,
but its process initialization calls **ntdll.RtlCreateTagHeap**, which is
unimplemented in the original bottle's engine. Native KernelBase initialization
fails and the control reports `STORE_NATIVE_LOAD_ERROR=317`. The same native
component also imports unimplemented **RtlGetPersistedStateLocation**; that
second function was **not reached** because initialization failed first.

This is a different measured native block, not an implemented Store path.
No false heap tags, state-separation paths, WNF/ALPC services, purchases,
capabilities, installed packages, or success responses were supplied.
The native core DLL and bridge are confined to
`package-manager-installservice-state-control/x64` in the Mac stage; neither is
installed in the prefix. Original Microsoft installer UI/process input remains
parent/user-owned.

Validate this additional experiment together with the preceding factory
evidence:

```powershell
python scripts\macos\compatibility\package-manager-installservice-evidence-test.py `
  $scratch --state-bridge
```

It checks the native host pin, the exact PE forwarder, derived-copy provenance,
cleared signature metadata, unchanged IAT slot, and actual RtlCreateTagHeap
initialization failure. Logs are
`package-manager-installservice-state-native-x64.log` (API-set-name attempt) and
`package-manager-installservice-state-direct-x64.log` (explicit single-import
route). No control process remains running.

Further progress requires real persisted-state/NT runtime functionality, not
another metadata contract or a fabricated Store object. This worker has not
patched the separately owned token-runtime source or registered the failing
native client into the live bottle.

Genuine native-artifact SHA256 pins:

```text
5172d5d3ac77c56b60899ae468b154c80b77972302c1fafff99470483223c8c2  InstallService x64
e2e479ea1e033b1d23dbc015adf88df48695cc5e1c17d52713ba5e678f3a1169  InstallService x86
b3407a812c7f34ac6b5beefb2a78e8f2d7c4376f7c7012f880aab4fe07235e4e  UMPDC x64
52e5f333029039f3a7e5f28c673fbc25d5b2f2151dea6358802e668759d736a5  UMPDC x86
667884a4471c9da403368adc1f8acb38c7a22fd5e75ce35deb13a4a8263a49f4  rmclient x64
c8f143f406ebf36cab8ee79934aa74b01f1dd252415a757fa3e18fdd346be420  rmclient x86
391a2818fdef14f4a9b6e6ce2a7e7f01d04f943de9ef543ec683927cfe480889  apisetschema
```

The static test uses scoped `dnfile` 0.18.0 / `dncil` 1.0.2 dependencies, restored only after
their import failed, under the assigned Windows scratch directory's
`cli-metadata-tools`. Set PYTHONPATH there and run:

```powershell
python scripts\macos\compatibility\package-manager-store-static-test.py `
  "$scratch\package-manager-XboxInstaller-inspection.exe"
```

Evidence logs: `package-manager-store-metadata.log`,
`package-manager-store-static-test.log`, `package-manager-store-negative-x64.log`,
`package-manager-store-negative-x86.log`, `package-manager-store-services.log`.
Source and evidence remain in the
existing Mac stage/assigned Windows scratch. Current PackageManager DLLs and
their activation mappings are unchanged.

Native-control executable SHA256:

```text
ebdece3f0ee8c3af6d61ab54e7518c55763162cb6e00f53d359ae7035a352149  package-manager-store-control-x64.exe
97ca543c3708a7e75c07bc5ae1ae9695af4a22ce7604f921ca290388409f2049  package-manager-store-control-x86.exe
```
