# Xbox original app on Wine: inventory ledger

Every change, shortcut, and copied file used to run the original Xbox PC app
(Microsoft.GamingApp 2609.1001.16.0, `XboxPcApp.exe`) under Wine on the Mac.
Owner decision (2026-10-09 07:54): experimental shortcuts on the rendering /
composition-partner path are allowed if each one is listed here as
`EXPERIMENT-STUB`. Authentication, accounts, tokens, identity and service results
are never fabricated.

Classifications:

- `UPSTREAMABLE`: Wine source change against measured native semantics; in
  `wine-xbox-original-app-sprint.patch`.
- `REAL-IMPLEMENTATION`: lane-built implementation (not a stub) outside Wine source.
- `NATIVE-COPY`: unmodified Microsoft binary copied from the Windows 11 host
  (x64, 26100.x / 26300 host). Licensing: evaluation only, not redistributable.
- `EXPERIMENT-STUB`: success-shaped or state-holding placeholder on the rendering
  path (owner-approved shortcut). Must be replaced before any claim beyond "experiment".
- `DIAGNOSTIC`: logging/measurement only; never part of a candidate.

Paths: `$S` = `~/xodus-runs/xbox-service-principal-20261007-001` on `xodus-mac`;
`$P` = `$S/prefix/drive_c`; package root
`~/xodus-runs/xbox-app-crossover-20261005-001/official-package-code/Microsoft.GamingApp`.

Revert (generic): Wine source - restore `.pre-<slice>` baseline beside each file and
rebuild the DLL; native copy - delete the file from `$P/windows/system32` (Wine
falls back to builtin or class-not-registered); registry - delete the row(s).

## Wine source changes (UPSTREAMABLE, all in the cumulative patch)

| id | what | where | status | run |
|----|------|-------|--------|-----|
| W-01 | user32 GetProcessUIContextInformation (desktop context) | dlls/user32 | candidate | br118 |
| W-02 | user32 GetResizeDCompositionSynchronizationObject (NULL, FALSE) | dlls/user32, win32u | candidate | br119 |
| W-03 | dxgi WaitForVBlank waits to next refresh boundary | dlls/dxgi/output.c | candidate | br123 |
| W-04 | combase RoGetMatchingRestrictedErrorInfo, IsErrorPropagationEnabled, RoTransformError(W), RoFailFastWithErrorContext | dlls/combase | candidate | br114, br116, br136 |
| W-05 | combase IContextCallback::ContextCallback apartment-aware | dlls/combase/apartment.c | candidate | br138 |
| W-06 | windows.applicationmodel IPackage3 / PackageStatus | dlls/windows.applicationmodel | candidate | br128 |
| W-07 | windows.web IJsonValueStatics2, JsonObject IMap/IIterable/GetView | dlls/windows.web | candidate | br129, br135 |
| W-08 | ntdll RtlQueryPackageClaims from token identity attributes | dlls/ntdll | candidate | br131 |
| W-09 | ApplicationData folders, ApplicationDataContainer/LocalSettings (stateful PropertySet) | dlls/windows.storage.applicationdata | candidate | br132, br133 |
| W-10 | msvcp/msvcrt exception_ptr rethrow copies object | dlls/msvcp90, dlls/msvcrt | candidate | br134 |
| W-11 | StorageFolder.CreateFolderAsync | dlls/windows.storage | candidate | br137 |
| W-12 | shcore #270 SHCreateMemoryStreamOnSharedBuffer + IAccessPrivateBuffer | dlls/shcore | candidate | br139, br140 |
| W-13 | bcp47langs GetApplicationLanguages, LanguageListAsMuiForm, GetUserLanguages, Bcp47GetNlsForm/FromLcid/FromHkl | dlls/bcp47langs | candidate | br142-br144 |
| W-14 | dwrite IDWritePrivateTextAnalyzer::GetContentReadingDirection | dlls/dwrite/analyzer.c (`.pre-privanalyzer`) | candidate | br159 |
| W-15 | ntdll RtlQueryWnfStateData + WNF display state 0x41c61629a3bc1035 (measured empty) | dlls/ntdll/rtl.c (`.pre-qwnf`, `.pre-wnfdisp`) | candidate | br162-br167 |
| W-16 | shcore #244 | dlls/shcore (`.pre-sfw`) | candidate | br164 |
| W-17 | shcore #251/#253 window monitor change listeners (native CWindowTracker semantics) | dlls/shcore/main.c, shcore.spec (`.pre-wmcl`) | candidate | br168 |
| W-18 | windows.staterepositorycore (SRCacheManager read path), kernelbase package APIs, ALPC/server/services slices from earlier lanes | see patch file list | candidate | br17-br107 |

Full file list: `wine-xbox-original-app-sprint.patch` (113 files + configure.ac).

## Native copies in the prefix (NATIVE-COPY, from the Windows 11 host)

sha256 (first 16 hex) of the file as installed in `$P/windows/system32`.

| id | file | sha256 | reason / run |
|----|------|--------|-----------|
| N-01 | Windows.UI.Xaml.dll | 4b69724693faf293 | system XAML (br139+) |
| N-02 | Windows.UI.Xaml.Resources.{Common,th,rs1-rs5,19h1,21h1,win81,win8rtm}.dll | see `$P` | resource-only, br141 |
| N-03 | Windows.UI.dll | 0b8b1a5342cf4600 | CoreWindow / WindowServerFactory, br120 |
| N-04 | CoreUIComponents.dll | 6927df3deaf036b4 | CoreUI window factory / navigation |
| N-05 | CoreMessaging.dll | 88c7b4d6ee26f769 | CoreMessaging |
| N-06 | twinapi.appcore.dll | d360b4926165bc68 | CoreApplicationView |
| N-07 | InputHost.dll | 4e84bb64485a6be5 | XAML input |
| N-08 | WindowManagementAPI.dll | 222d3f1cd858bb1d | window management |
| N-09 | dataexchange.dll (26100.9278) | 6b979beb361e0e00 | br117 (`dataexchange=n`) |
| N-10 | Windows.Web.Http.dll | beba2c3b048170d2 | HttpClient, br130 |
| N-11 | Windows.Graphics.dll | 5db92f87796f9a1d | DisplayInformation, br161 |
| N-12 | icu.dll / icuuc.dll / icuin.dll (72.1.0.4) | 7e28c779 / 59d134dd / 585026af | hermes.dll dependency, br169 |
| N-13 | MrmCoreR.dll, bcp47mrm.dll | 6186f981 / c8a840f9 | MRT resources |
| N-14 | threadpoolwinrt.dll, rmclient.dll, execmodelproxy.dll, ExecModelClient.dll, windows.shell.servicehostbuilder.dll, OneCoreUAPCommonProxyStub.dll | see `$P` | execution model / proxies |
| N-15 | Windows.System.Launcher.dll, Windows.System.Diagnostics.dll, Windows.Devices.Radios.dll, gameplatformservices.dll | see `$P` | class activations (br145-br153) |
| N-16 | api-ms-win-core-winrt-roparameterizediid-l1-1-0.dll | e6f28b53b07c81f3 | parameterized IID |

Not part of this lane (present in the prefix base; provenance to verify, not used
by the app path as far as measured): `mscoree.dll`, `*_clr0400.dll`, 1032-byte
`atidxx64.dll`/`nvapi64.dll`/`nvngx.dll` placeholders.

## Lane-built helper DLLs (REAL-IMPLEMENTATION, outside Wine source)

`package-claims.dll`, `package-manager-provider.dll`, `xbox-ui-settings.dll`,
`xbox_wintypes_base.dll`, `xboxftps.dll`, `wintypes.dll` (lane shim),
`combase-apartment-ordinal.dll`, `combase-server-shutdown-delay.dll`: built by
earlier lanes from repo sources under `scripts/macos/compatibility/`; sha256 in `$P`.
Activator + shellhost-broker (shell-host lane) are lane programs.

## Registry (REAL-IMPLEMENTATION from measured native rows)

| id | what | file |
|----|------|------|
| R-01 | WinRT activation rows (system classes, measured DllPath/ActivationType/Threading/TrustLevel) | `$S/r164.reg`, `rows162.reg`, `wgraphics.reg`, `http-rows.reg`, `wintypes-winrt-classes.txt` |
| R-02 | package class catalog (442) + WinUI 2.8 / WindowsAppRuntime framework rows | `$S/fw-Microsoft.UI.Xaml.2.8.reg`, `fw-Microsoft.WindowsAppRuntime.2.reg` |
| R-03 | LocalServer32 for manifest com:ExeServer CLSIDs (XboxPcAppFT, GameInterop) | br126 |
| R-04 | CLSID/proxy-stub rows (twinapi.appcore, threadpoolwinrt, CoreUI factory proxy, execmodel) | `$S/run-br-relay.sh` lines 18-35 |
| R-05 | `HKCU\Software\Wine\Drivers Graphics=null` (headless runs only) | `$S/run-br-relay.sh` line 14 |

## Composition / rendering path

| id | what | where | sha256 | class | status |
|----|------|-------|--------|-------|--------|
| C-01 | staging dcomp with builtin marker zeroed (composition owner, from Wine staging patch series) | `$P/windows/system32/dcomp.dll`, `dcomp=n,b` | cd3f0a57d60efd1d | EXPERIMENT (patched Wine build, not native) | installed |
| C-02 | Compositor factory POC (partner, 2a8ed1b) | `C:\wincomp_poc.dll` + `ActivatableClassId\Windows.UI.Composition.Compositor` row | 3d8392a3573f7c0f | EXPERIMENT-STUB | installed (br171 state) |
| C-03 | logging factory v2 | `C:\wincomp_poc.dll` | c6fae4729ce4a164 | DIAGNOSTIC | removed |
| C-04 | partner skeletons v1-v5 (wincomp_diag*.dll) | `C:\wincomp_poc.dll` during brD1-brD5 | 269302b9, 9cb83307, 2702d803, a504b5c0, ce925936 | DIAGNOSTIC (v5 getters return defaults: EXPERIMENT-STUB) | removed after each run |

## Diagnostics (DIAGNOSTIC, all reverted)

br121-br122, br124-br125, br144, br154-br155, br157, br158 (xaml byte patch), ntdll
exception.c `.pre-hrdiag`, combase roapi.c `.pre-errdiag`, marshal.c `.pre-psdiag`.
All reverted before br171.

## Change log

Each new entry is appended below as the experiment proceeds (id, run, sha256, class).

| id | run | what | sha256 | class | status |
|----|-----|------|--------|-------|--------|
| E-01 | brE1 | wincomp_exp6.dll (composition owner d98c21a, get_Properties benign S_OK PropertySet) | aceab546bd390ab0 | EXPERIMENT-STUB | run once; removed (prefix back to br171) |
| E-02 | brE2 | wincomp_exp7.dll (ICompositor Create* benign any-QI objects, INV-06) | 383256090e192965 | EXPERIMENT-STUB | run once; removed |
