# Original Xbox app: headless startup API chain (sprint 2026-10-07)

The sprint stage is `~/xodus-runs/xbox-service-principal-20261007-001` on the
experimental Mac. Each run was the genuine `Microsoft.GamingApp`
`XboxPcApp.exe` (2609.1001.16.0), started through packaged activation by
`run-sp-loaddll.sh`. Every slice below followed the same loop:

1. Take the first real failure on the original path.
2. Search upstream Wine, wine-staging (`2395d933`, 2026-10-06), Proton
   `experimental_10.0` and ReactOS.
3. If upstream had the code, reuse it. Otherwise measure native Windows 11
   ARM 26100 (x64 emulation) with the `audit-*.c` probe and implement against
   those measurements.
4. Run the same probe under Wine and compare the output.
5. Retry the original app immediately.

## Files

- `wine-xbox-original-app-sprint.patch`: the cumulative source delta for the
  stage, 37 files. Each file is diffed from its earliest `.pre-*` backup.
- `built-dll-sha256.txt`: hashes of the built PE DLLs, the patch, and the
  genuine `Windows.UI.Xaml.dll` and `CoreMessaging.dll` that were exercised.
- `patch-*.py`, `combase-ordinals.py`: idempotent patch scripts, one per
  slice. Each keeps `.pre-<slice>` backups.
- `audit-*.c`: parity probes. Run the same binary on native Windows and on
  Wine.
- `framework-view-activation.reg`: mirrors the native WinRT registration of
  `Windows.UI.Xaml.FrameworkView`.
- `delayimp.py`, `callers.py`, `iatcalls.py`: read-only PE helpers. They find
  delay-import and IAT call sites in the genuine callers, so arguments can be
  read from the real call.

## Reached APIs, in original-app order

| Run | Reached failure | Caller | Upstream status | Resolution | Parity |
|---|---|---|---|---|---|
| sp1–12 | registrar principal, ALPC attributes, WOB/context, IOCP, `CoGetStdMarshalEx`, `NdrStubCall3`, combase ordinals | CoreMessaging, combase | absent or partial | implemented against native probes; see the Phase 1 and 2 notes in `docs/macos-bringup/xbox-original-app-milestones-plan.md` | probe-matched |
| sp13–16 | combase design mode (#90/#157), process events (#86/87/88/110/111/133) | Windows.UI.Xaml, twinapi.appcore | absent | native measurements, non-ASTA paths | `audit-designmode`, `audit-process-events` |
| sp17 | `GetQueueStatusReadonly` (user32 #2541 / win32u) | CoreMessaging | stub only | `patch-gqsro.py`, syscall plus wow64 thunk | 12/12 lines identical |
| sp17 | `Windows.UI.Xaml.FrameworkView` not registered | CoreApplication | registry data | `framework-view-activation.reg` | native registry mirror |
| sp18 | `IsOneCoreTransformMode` | Windows.UI.Xaml (delay import) | commented out in user32, stub in win32u | `patch-onecore.py`: FALSE, last error unchanged | `audit-onecore` |
| sp19 | `iertutil.PrivateCoInternetCombineIUri` | Windows.UI.Xaml | Wine master `949c1ed5` (2026-04-01) | narrow backport into urlmon, forwarded from iertutil (`patch-combine.py`) | 14/14 cases, `audit-combine` |
| sp20 | `uxtheme.GetUserColorPreference` | Windows.UI.Xaml | absent (master, staging, Proton, ReactOS) | `patch-gucp.py` | `audit-gucp` modes 0–8 |
| sp21 | `uxtheme.GetColorFromPreference` | Windows.UI.Xaml `18035c3b4` (types 1–7, 0xd2) | absent; master has only semi-stub ordinals 95/96/98/100/104 (2026-07-15) | `patch-gcfp.py` | `audit-gcfp` modes 0–4 |
| sp22 | `ntdll.RtlGetAppContainerNamedObjectPath` | CoreMessaging `18006f62b` `(NULL, NULL, FALSE, &path)`; a failure is thrown as an HRESULT | absent | `patch-acnop.py` | `audit-acnop` modes 0–3 |
| sp23 | DXGI factory fails (`0x887a0004`), then `RaiseFailFastException` | render thread | Wine design: no adapter without a display driver | **stop: display boundary** | — |

sp23 hits no unimplemented API. The only failure is wined3d/DXGI. It can't
create a GL context because the harness disables `winemac.drv`, so the run is
headless. On native, a DXGI factory always exists, and Xaml can fall back to
WARP. The next rung needs Phase 3 (`winemac.drv` on the Mac foreground), which
needs the owner's permission.

## Measured semantics and recorded deviations

- **`GetColorFromPreference(pref, type, ignore_hc, hc_mode)`**
  - Types 1–7 are the accent palette, from light3 to dark3.
    - Source: the first process-wide read of the real-hive
      `Explorer\Accent\AccentPalette`. It must be a 32-byte `REG_BINARY` of
      R,G,B,A entries; alpha is ignored and forced to `ff`.
    - Otherwise the fixed default `99EBFF … 001A68` is used, whatever
      `AccentColorMenu` says.
    - Later registry writes, and forced `GetUserColorPreference`, do not
      refresh it.
  - Types 0, 8 and 0xd2 are constant. `pref` is not read for these types.
  - Native has 226 types (0–0xe1). 125 of them derive from `pref` through
    transforms that were not measured. Those types, and every other
    unimplemented type, log a FIXME and return `0xffff00ff`, which native
    returns for unknown types. **This is a deviation.**
  - High contrast is not modelled. Native showed no effect from `ignore_hc`
    or `hc_mode` while high contrast was off.
- **`RtlGetAppContainerNamedObjectPath`**
  - NULL out pointer: `STATUS_INVALID_PARAMETER`.
  - Both a token and a SID: `STATUS_INVALID_PARAMETER_MIX`.
  - Token errors are returned with the output left untouched.
  - A token that is not an AppContainer gives success with an empty string.
  - The SID must have authority 15, RID 2, and 8 or 12 RIDs; otherwise
    `STATUS_NOT_APPCONTAINER`.
  - The path is `[\Sessions\<PEB session>\]AppContainerNamedObjects\<parent SID>[\<4 child RIDs>]`,
    allocated from the process heap with `MaximumLength = Length + 2`.
  - Wine queries `TokenType` first. Its `TokenIsAppContainer` and
    `TokenAppContainerSid` are semi-stubs that do not validate the handle.
  - Wine has no AppContainer tokens. The token branch is therefore always the
    empty-success path.
- **`GetUserColorPreference`**: native sets the last error to 0 on its first
  call. Wine does not replicate this; it is incidental.
- **`PrivateCoInternetCombineIUri`**: upstream lives in iertutil after its IUri
  move. Here it lives in urlmon and is forwarded. `dwReserved` is ignored, as
  on native.
- **`EnableOneCoreTransformMode`**: not implemented and never reached.
- **No ASTA in Wine**: the process-events and design-mode slices implement
  native's non-ASTA paths only.

## Status

These are candidates for an experimental runtime. Nothing is upstreamed,
pushed or signed. Passing probes and getting further along the headless chain
are **not** app milestones: XBOX-APP-STARTUP remains UNTESTED until a real
original window renders.
