# Original Xbox app: headless startup API chain (sprint 2026-10-07)

The sprint stage is `~/xodus-runs/xbox-service-principal-20261007-001` on the
experimental Mac. Each run was the genuine `Microsoft.GamingApp`
`XboxPcApp.exe` (2609.1001.16.0), started through packaged activation by
`run-sp-loaddll.sh`. Every slice below followed the same loop:

1. Take the first real failure on the original path.
2. Search upstream Wine, wine-staging (`2395d933`, 2026-10-06), Proton
   `experimental_10.0` and ReactOS.
3. If upstream had the code, reuse it. Otherwise measure native Windows 11
   with the `audit-*.c` probe and implement against those measurements. The
   native machine is a Parallels Windows 11 ARM64 VM, build 10.0.26300.9457,
   running the x64 probes under emulation. The genuine x64 DLLs that were
   read statically or exercised (`kernel.appcore`, `twinui.appcore`,
   `Windows.UI.Xaml` and others) are the 10.0.26100.9278 builds from the
   Microsoft symbol server. Code comments that say "26100" refer to those
   binaries. Behaviour was measured on the 26300 VM.
4. Run the same probe under Wine and compare the output.
5. Retry the original app immediately.

## Files

- `wine-xbox-original-app-sprint.patch`: the cumulative source delta for the
  stage, 71 files. `make-sprint-patch.py` generates it: each file is diffed
  from its earliest `.pre-*` (or `.base`) backup, and new files are diffed
  against `/dev/null`.
- `built-dll-sha256.txt`: hashes of the built PE DLLs and `.so` files, the
  genuine `Windows.UI.Xaml.dll`, `CoreMessaging.dll`, `MrmCoreR.dll`,
  `bcp47mrm.dll` and `threadpoolwinrt.dll` that were exercised, and the patch
  (last line). Paths are relative to the stage. The `evidence-sp46/`
  directory name is historical: the patch and hashes in it were regenerated
  after the sp47 roapi fix and the wineboot comment change.
- `patch-*.py`, `combase-ordinals.py`: patch scripts for the slices that
  were scripted. Each keeps `.pre-<slice>` backups. Other slices
  (recvattr, wob server side, ndr3 headers, gitw, pkgid, json, avs, hdi,
  ctm, gspo, state, resctx, mss, psm, tpwex) were direct source edits with
  `.pre-<slice>` backups. **The cumulative patch is the authoritative
  record.** See "Patch application order" for the order and for how far
  re-running the scripts is safe.
- `probe-manifest.txt`, `run-probes-native.ps1`, `run-probes-wine.sh`,
  `compare-probe-pairs.py`: run the same 71 probe invocations on native
  Windows and on the stage, then diff them (see "Paired parity").
- `probe-outputs/`: native and Wine probe outputs for each slice.
  `probe-outputs/paired-20261007/` holds the paired rerun and
  `parity-report.md`. `probe-outputs/activation-20261007/` holds the
  activation experiment (see "Activation boundary").
- `audit-*.c`: parity probes. Run the same binary on native Windows and on
  Wine.
- `framework-view-activation.reg`: mirrors the native WinRT registration of
  `Windows.UI.Xaml.FrameworkView`.
- `delayimp.py`, `callers.py`, `iatcalls.py`: read-only PE helpers. They find
  delay-import and IAT call sites in the genuine callers, so arguments can be
  read from the real call.

## Patch application order

The backup timestamps give the order:

1. Phase 1 `.base` (principal and service token);
2. `overlap` (`patch-write-mask.py`, `server/token.c`), then `openif`
   (`patch-openif.py`). Both scripts reproduce the original direct edits.
3. `recvattr`, `sendattr` (`patch-sendattr.py`), `wob` (`patch-wob-a.py`),
   `cmiocp` (`patch-cmiocp.py`);
4. the later slices, in the order of the tables below.

Re-running the scripts was checked on 2026-10-07. Every `patch-*.py` was run
twice against a copy of the final stage source (`dlls`, `server`, `include`,
`programs`). Fourteen scripts recognised their edits as already applied and
changed no file. `patch-sendattr.py` is the exception: it is idempotent
only before `patch-wob-a.py`. On the final tree it stops with "expected 1
got 0" for a work-on-behalf anchor that wob-a later rewrote. It exits
before writing anything, and the copy was left unchanged.

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
WARP. The sprint then used a headless D3D11 device through MoltenVK
(`Graphics=null` set for the run, then deleted). The GUI runs (`gui*`) needed
`winemac.drv` and waited until the Exodus lane released the foreground.

| Run | Reached failure | Resolution | Parity |
|---|---|---|---|
| sp24 | dxgi private IID `{f898b024…}` (`Set/GetInProcessGPUPriority`) | implemented in dxgi | 45/45 lines, `native-gpuprio` |
| sp25 | UISettings QI | upstream `3e86631f` backport | `native-uisettings6` (subset) |
| sp27 | marshal IID `49a07732` | proxy/stub registered | `native-iid-49a07732` |
| sp28 / gui2 | alive about 60 s | — | — |
| sp29 | SRW self-deadlock | GuidHelper registered | — |
| sp30–31 | `Package.Current.Id` | implemented | `native-state-ident*` |
| sp32 | `JsonObject` | `windows.web` backport at master `59416cf5` | `native-json-parity` |
| sp33 | `ApplicationViewScaling` | implemented | `native-avs` |
| sp34 | msctf `HasDeferredInputForCoreDispatcher` | implemented | `native-hdi` |
| sp35 | combase #95 `CoSignalPendingGitRegistrationWaits` | implemented | `native-gitw` |
| sp36 | IID proxy/stub and DispatcherQueue registry | registered | `native-reglook-sp36` |
| sp37 | `GetStringValueForManifestField` | genuine MrmCoreR | `native-mrmreg` |
| sp38 | `CheckTokenMembershipEx` | implemented | `native-ctm` |
| sp39 | `GetStagedPackageOrigin` | implemented | `native-gspo` |
| sp40 | bcp47mrm | genuine bcp47mrm | `native-apiset-mrm` |
| sp41 | package state APIs | implemented | `native-state` |
| sp42 / gui3 | `GetCurrentPackageApplicationResourcesContext` | implemented | `native-resctx*` |
| sp43 | `RtlIsMultiSessionSku` | implemented | `native-mss` |
| sp44 | `PsmQueryBackgroundActivationType` | implemented | 6/6 calls, `native-psm` (export DLL differs) |
| sp45 | CLSIDs `{02844640}` PlmSuspendControl, `{96c7a5ef}` CSignalableNotifier | registered; genuine threadpoolwinrt | — |
| sp46 | `TpSetWaitEx` | implemented | 8/8, `native-tpwaitex` |
| sp46 / gui4 | **no missing API**: the app stays alive about 70 s, then the launcher ends it (exit 92) | — | — |

In gui4 no CoreWindow exists and no app window is visible. XAML renders only
into the hidden wined3d DXGI device window. The earlier sp36 "window
rendered" claim is **withdrawn**. Original-app symbols are consistent with
this hypothesis: the app never receives a launch activation, so it never
creates its CoreWindow. That is **unproven** until a change to activation
input changes the observed outcome. `NdrDllGetClassObject {dbce7e40}` still
warns and is unresolved.

## Paired parity (2026-10-07)

The 71 invocations in `probe-manifest.txt` were run on the same day:

- native: the Parallels Windows 11 VM, in the interactive user session
  (`prlctl exec --current-user`);
- Wine: the stage, through `run-probes-wine.sh`.

`compare-probe-pairs.py` normalises pointer values and exit codes. **28/71
outputs match.** The Wine outputs equal the earlier committed `wine-post-*`
outputs, so the rerun shows no regression. The per-slice parity column above
therefore covers only the subset of each API that the app reached; it is
**not** full parity. The diffs fall into three groups:

- **Environment or identity:**
  - session 2 vs 1;
  - user accent colours;
  - thread and handle values;
  - Wine's placeholder user SID `S-1-5-21-0-0-0-513` and `GA` access in
    `filter-token-sd`;
  - `gucp_1/2`: both sides fault on NULL;
  - `gcfp_2`: Wine timed out enumerating the colour-name table (native: 1227
    names).
- **Wine semantic gaps on paths the app did not reach:**
  - WOB: thread information class 44 returns `STATUS_NOT_IMPLEMENTED`;
    native returns a 16-byte ticket.
  - ALPC:
    - connect attribute `0x80000` returns `c000000d`;
    - message attribute sequence and size are zero;
    - context port on `CLIENT_REPLY` differs.
    - Port basic information: native handle 2 / pointer ~65537 / attr 1;
      Wine 1 / 2 / 0.
  - CoreMessaging queue status: native `0x00400040`, Wine 0.
  - `CoGetStdMarshalEx`: `SMEXF_HANDLER` returns `E_NOTIMPL`; the outer-QI
    order and refcounts (+1 on Wine) differ.
  - NDR3 proxy: native has `vtbl[3]/[6]` in combase with `CountRefs` 0; Wine
    has them in rpcrt4.
  - UISettings6: `MessageDuration` returns `E_NOTIMPL` (native returns 5);
    the identity comparison differs.
  - `CoInternetCombineIUri`: an `ms-appx` relative URI, or `res://`, returns
    `8007000e` on native but succeeds on Wine.
  - onecore: native `byname=0`, Wine 1.
- **Matched:** gpuprio and the other 26 invocations listed in
  `parity-report.md`.

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
- **CoreMessaging drain**: `NtUserDrainThreadCoreMessagingCompletions2` does
  not queue message-arrival packets to the port for registered windows. It
  logs a one-time FIXME.
- **Per-thread CoreMessaging IOCP**: not closed at thread exit.
- **ALPC `msg->id` validation**:
  - a sender on a connection port needs a valid connection-message entry,
    otherwise it gets `STATUS_ACCESS_DENIED`;
  - a sender on a communication port needs a source that matches the
    destination port, otherwise it gets `STATUS_REPLY_MESSAGE_MISMATCH`.
- **Phase 1 principal**:
  - Only LocalService services with `ServiceSidType` 1 or 3 get the new
    token. Other services keep Wine's existing token; this is pre-existing
    behaviour.
  - If token creation fails, the service start fails; there is no silent
    fallback.
  - The token object carries a security descriptor set by the SCM.
  - The port creator receives `ALPC_PORT_ALL_ACCESS` (native measured
    `0x001f0001`).
- **`OBJ_OPENIF` and the write mask**: follow `native-tokensd-openif` and
  `native-write-overlap`.
- **`PsmQueryBackgroundActivationType`**
  - Written from a static reading of genuine `kernel.appcore`
    10.0.26100.9278, then measured on 2026-10-07 with `audit-psm.c`.
  - All six calls match native: the pseudo process token twice, a real
    process token, a token without `TOKEN_QUERY` (`c0000022`), NULL
    (`c0000008`), and an invalid handle (`c0000008`). With no `WIN://BGKD`
    attribute, both return type 1.
  - The case where the attribute is present was not measured. Neither
    token carries it, and no test creates one.
  - Native exports the function only from `kernel.appcore.dll`. Wine has no
    builtin kernel.appcore, so it is exported from `kernelbase.dll`.
- **`RtlIsMultiSessionSku`**: wineboot sets `DbgMultiSessionSku` for every
  workstation product type. This rests on one measured edition: the VM's
  Windows 11 client reports `SharedDataFlags 0x98e`. Other client editions
  were not measured.
- **Design-mode token query** (`roapi.c`): if the size query fails with
  anything other than `STATUS_NOT_FOUND` or `STATUS_BUFFER_TOO_SMALL`, that
  error is now returned as an HRESULT; a reported size smaller than the
  header gives `E_UNEXPECTED`. Fixed after R4 review. combase was rebuilt,
  and `audit-designmode`, `audit-process-events` and the original app (sp47)
  gave the same output as before the fix.
- **`CoBeginProcessEvents`**: zeroes 0x60 bytes of the context
  unconditionally, as measured on native's non-ASTA path. No NULL check
  was added, because native behaviour for NULL was not measured.
- **ALPC reply-context lookup**: the message-context lookup uses the
  caller's `msg->id` for every send, not only for
  `ALPC_MSGFLG_REPLY_MESSAGE`. This is a recorded deviation (R4 LOW); it was
  not narrowed without a native measurement.
- **Work-on-behalf ticket**: a ticket is the thread ID plus its creation
  time, so a thread can adopt the ticket of any live thread, including
  another process's. Native validates tickets in the kernel. This is a
  recorded deviation (R4 LOW).

## Activation boundary (2026-10-07, bounded experiment)

The experiment's observations and stops were declared before it ran (see
`probe-outputs/activation-20261007/predeclare.txt`). It tests the documented
route only: `IApplicationActivationManager::ActivateApplication`.

- **Upstream:** Wine master `59416cf` has only the `shobjidl.idl` interface.
  wine-staging, Proton and ReactOS have no implementation.
- **Native registration** (VM 26300.9457):
  - `CLSID_ApplicationActivationManager {45BA127D…}` is in-proc
    `twinui.appcore.dll`.
  - Genuine x64 `twinui.appcore` 26100.9278 (sha256 `5cb48ef8…`, with
    public PDB) implements it as `ApplicationActivationManagerProxy`.
  - `ActivateApplication` calls
    `CoCreateInstanceEx({6C3EE638…}, CLSCTX_LOCAL_SERVER)`, the
    "Activation Manager Shim".
  - That class has no `LocalServer32`. Its implementation,
    `activationmanager.dll`, is loaded by `sihost.exe`, the Shell
    Infrastructure Host, which registers the class at runtime.
- **`audit-aam.c` create mode:**
  - Native: both objects are created (`hr=0`).
  - Wine: both return `REGDB_E_CLASSNOTREG`.
  - With the genuine `twinui.appcore` registered temporarily (removed
    afterwards), the proxy still can't load: its import `Windows.Storage.dll`
    fails. The inner class remains unregistered, and there is no server,
    service or surrogate for it.
- **Result:** stop S1 hit. The documented route needs a shell-host component
  (`sihost`/activation manager). The missing-DLL rungs were not chased (S2).
  Processes peaked at about 850 (S3), and the experiment stayed within its
  2-hour budget (S4).
- **Decision: NO-GO for the documented-interface route in this sprint.**
  The hypothesis that launch activation is missing is still **unproven**: no
  activation input reached the app.
- **Options for the owner:** stop here, or approve a separate lane for a
  shell-host activation manager that uses genuine package identity.

## Owner-approved continuation: phases A and B (2026-10-07 ~21:00–21:50)

**Phase A: does no activation mean no window?** Result: **supported, not
proven.** The probe is `audit-plainlaunch.c`. Outputs are in
`probe-outputs/activation-20261007/phaseA-*`.

All rows were measured on the native arm64 VM (build 26300.9457). Each launch
started from a state with no `XboxPcApp` process. Only exact PIDs were stopped.

| Native launch | Process | Windows |
|---|---|---|
| Normal activation (`shell:AppsFolder\…!Microsoft.Xbox.AppL`, 21:28) | PID 11772, `XboxPcApp -ServerName:Microsoft.Xbox.AppL.AppXhg7…mca`. Its parent is svchost 564, which hosts BrokerInfrastructure, DcomLaunch, Power and SystemEventsBroker. | Owns a visible `Windows.UI.Core.CoreWindow` inside `ApplicationFrameWindow "XBOX"` (`phaseA-native-activation-owner.txt`) |
| Plain `CreateProcess` from an unpackaged caller (21:30) | Child PID 11224, `GetPackageFullName` 15700 (no package). Alive for 30 s, then terminated by exact PID. | 0 top-level windows at every 1 s sample (`phaseA-native-plainlaunch-rerun.txt`) |
| Plain launch with package identity (control 1) | No `XboxPcApp` process after 25 s | None (`phaseA-native-launch-control.txt`) |

During the plain run, a separate `-ServerName` instance (PID 13956) appeared
8.5 s after the probe started. Its parent was svchost 564, not the probe. That
instance is **observed only**; what started it was not established.

On Wine, the launcher starts the same `-ServerName` server with package
identity. It is alive and has no window. This matches the native no-activation
rows in outcome only; the two contexts differ.

**Phase B: minimal activation manager.** Result: **predeclared stop (shell
host). Not built.** Details are in
`probe-outputs/activation-20261007/phaseB-native-activation-chain.txt`.

- The stop conditions (explorer, ApplicationFrameHost RPC, window band) came
  from the CTO directive. No separate Phase B predeclaration file exists.
- On the Xodus stage, the app publishes its activation factories through
  `RoRegisterActivationFactories`, including `Microsoft.Xbox.AppL`, the
  `Windows.Launch` class. That works only because the stage combase carries the
  Xodus `wine-winrt-registration.c` implementation; the logs show "Registered 4
  owning Wine-local WinRT factories". Upstream's version is a stub. Nothing
  calls these factories.
- The native chain, measured on the VM and read from the genuine
  `twinapi.appcore` 26100.9278 public PDB:
  1. `CoreApplication::RegisterActivatableApplication` creates
     `ActivatableApplicationRegistrar {DEA794E0…}`.
  2. Inferred, not traced: the registrar calls back
     `IActivatableApplication {92696C00…}::Activate`.
  3. `ActivateForeground` calls `GetWindowFactory`, which creates
     `ShellServiceHostBrokerProvider {3480A401…}` and from it
     `IApplicationActivationBroker`, then `ICoreWindowFactory`.
- Both classes are `RunAs Interactive User`, with no server binary. They are
  registered at runtime by the shell:
  - Registrar: only `sihost.exe` loads `ActivationManager.dll`, the one
    binary that holds the registrar CLSID. This is measured.
  - Broker: the CLSID is embedded in `sihost.exe`, `twinui.dll`,
    `twinui.appcore.dll`, `twinui.pcshell.dll` (explorer) and
    `Windows.Immersiveshell.ServiceProvider.dll`. Which process registers
    it was not measured.
- **Upstream:** Wine `59416cf` has `RoRegisterActivationFactories` as a
  FIXME stub and a stub `CoreApplication` factory. It has no
  `IActivatableApplication`, registrar or broker. There is nothing to
  backport.
- Calling the app's factory directly would skip the registrar. It would still
need the shell-hosted window broker to produce a window, which is the stop
  condition, so it was not built.
- **Phase C (first frame) is not reachable.** No GUI test ran, and the shared
  foreground was not used.
- The missing-activation explanation is still **unproven**: no activation
  input changed the Wine outcome. Going further needs an owner-approved
  shell-host lane, implementing both the registrar and the window broker
  against the measured contract.

## Status

These are candidates for an experimental runtime. Nothing is upstreamed,
pushed or signed. Passing probes and getting further along the headless chain
are **not** app milestones. XBOX-APP-STARTUP remains UNTESTED: the original
app stays alive about 70 s, but no window of its own appears.

Sprint budget: started 2026-10-07 13:39 +03:00, hard deadline 2026-10-08
13:39 +03:00.

Deterministic gates: `gates/checks.sh` was run on a clean clone of commit
`e19290e` on the Mac (cargo 1.98.0). Result: **PASS**. `cargo build
--workspace` succeeded and `cargo test --workspace` passed 42 tests with
0 failures. This gate covers the Rust workspace only. The Wine candidate is
checked with the hash manifest and the paired probes above, not by this gate.

A crash-loop incident happened on 2026-10-07: about 770 `winedbg --auto`
processes in 4 minutes reached the per-user process limit. Since then:

- every run goes through `guard.sh`;
- AeDebug is empty;
- process counts are checked before and after each run.
