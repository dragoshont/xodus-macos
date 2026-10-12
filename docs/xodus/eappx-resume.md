# EAppx / Cuphead Wine lane: resume handoff

Status as of 2026-10-12: **paused**. Nothing is authorized to run. Nothing in this
document authorizes a build, deployment, launch, or new interface implementation.

The long evidence ledger is [`eappx-feasibility.md`](eappx-feasibility.md). It is
complete through cycle 2407. Cycles 2408–2415 are summarized **only here**.

## 1. Loss risk (read first)

**This handoff is not resume-ready.** The most recent Wine source work exists only
on the reference Mac.

| Item | Where it lives | Remote copy? | Risk |
|---|---|---|---|
| EAppx CLI work (`extract-eappx` offline, `inspect` metadata, Appx activation parsing) | This repo, branch `dragoshont-celeste-eappx-support` (`d12fc6b`, `3c20792`, `af28314`, `fb45770`, `ba2a141`) | Yes, pushed to `origin` | Low |
| Wine patches through ~2407 | This repo, `scripts/macos/compatibility/*.patch` + `*-test.c` | Yes | Low. Check them against the Mac source before reuse. |
| 2408–2415 Wine edits: `dlls/ole32/git.c`, `dlls/windows.ui/corewindow.c`, `dlls/windows.ui/tests/corewindow.c`, `dlls/windows.ui/tests/Makefile.in` | Mac only: `$TOOLING/celeste/wine-source/` | **No.** The source tree is not a git repo, and no patch was exported. | **High.** Lost if the Mac or its disk is lost. |
| Owner/launcher scripts (`cuphead-ordinary2411-owner.py`, `-launch.py`, `cuphead-2415-aqua.py`) | Mac only: `$TOOLING/cuphead/` | **No** | **High.** Required for safe swap and restore. |
| Cycle receipts, logs, candidate DLL dirs | Mac only: `$TOOLING/cuphead/cuphead-owned-*` | No (on purpose; see §5) | Medium. The only proof of 2412–2415 results. |
| Existing `wine-corewindow-context-interop.patch` | Repo | Yes | Stale. It predates AUTOCREATE and the adapter fallback. |

`$TOOLING` means `~/xodus-app-tooling` on the reference Mac. Neither that
directory nor `celeste/wine-source` has a `.git` directory (checked 2026-10-12).

**First action when resuming:** export the four Mac-only Wine files as a diff
against a pristine CrossOver 26.3 source tree. Do the same for the three scripts.
Commit them under `scripts/macos/compatibility/` in a separate pass. Strip the
diagnostic-only parts (§4) or label them clearly.

### Mac-local source fingerprints (read 2026-10-12)

| File (relative to `$TOOLING`) | sha256 |
|---|---|
| `celeste/wine-source/dlls/ole32/git.c` | `6238967f6df9565d87ed1dc77277f047bbad62a881842c71e23d9202cc19552c` |
| `celeste/wine-source/dlls/windows.ui/corewindow.c` (2415 state) | `c451b89c26324073235a178c7bc30922babe36502332dff4d57384758d987019` |
| `celeste/wine-source/dlls/windows.ui/tests/corewindow.c` | `7dac685151e3ac7c963e29334b4a2e2c1efdffc86a7e32aa2e5b823aaece8f6e` |
| `celeste/wine-source/dlls/windows.ui/tests/Makefile.in` | `aedd604ad361ea3809694b8ee02d0562d835e9a6106368c9c86a9b82c90be7aa` |
| `cuphead/cuphead-ordinary2411-owner.py` | `adf3bba147fa610aed00978f43b9bb74ac226b869462dccec9f4278098c5e8fa` |
| `cuphead/cuphead-ordinary2411-launch.py` | `7e56f862d360b71916cd21c7891160857773eb9fc80082bd7effef4e30b56cd6` |
| `cuphead/cuphead-2415-aqua.py` | `6e1f5c9aeefc4a7bab72650491f02224f606a26bf5544f23a81224590cab8e14` |

**Inventory gap:** on 2026-10-12, `dlls/{windows.ui,ole32}/x86_64-windows/*.dll`
were not found under `celeste/wine-source`. The admitted DLL copies are in the
candidate directories (§3). Before rebuilding, find out where the build output
directory now lives. Do not assume the earlier target paths.

## 2. Cycle results 2412–2415 (observed)

Each cycle was one Aqua-session launch with pinned DLL hashes and an independent
restore check.

| Cycle | Change under test | Observed | Restored |
|---|---|---|---|
| 2412 | ViewWait release experiment (windows.ui `6c12b46c…`, ole32 `d459074e…`) | Three runs self-blocked, each restored cleanly: CoreApplication and MRM `DllPath` left dirty by an earlier crash, and an SSH run with no window driver. The 4th run, in Aqua, activated Cuphead: `ACTIVATION status=00000000`, alive >10 s, **windows 0/0**. The ole32 release consumer could not find `windows.ui`. | Yes |
| 2413 | ole32 `git.c`: `L"windows.ui"` → `L"windows.ui.dll"`, plus `GetLastError` logging | SetEvent on the pump handle succeeded and the pump released. CoCreate `{96C7A5EF}` returned S_OK (HandleSignalNotifierFactory; builtin threadpoolwinrt already serves it). No CreateView/SetWindow seen. Windows 0. | Yes |
| 2414 | windows.ui `XODUS_COREWINDOW_AUTOCREATE=1` | The first attempt failed the strict pre-gate (5 failures) because the flag was set globally. No game launched. | Yes (39/39) |
| 2414-game1 | Same flag, scoped to package capture only | Strict pre-gate 9/0. Thread `0x0544` got CoreWindow HWND `0x000B0052`. CoreWindow was QI'd for `{7A5B6FD1-CD73-4B6C-9CF4-2E869EAF470A}`. The getter then returned `RoOriginateErrorW(0x80004001)`. Windows 0. | Yes (39/39, registry, no Wine) |
| 2415 | Fallback ApplicationViewClientAdapter: IUnknown-only QI; slots 3–40 log and return E_NOTIMPL without writing outputs | The getter returned non-NULL `O` with S_OK. twinapi then **QI'd `O` for `{F02EE4C8-92B3-48C7-9B48-3D0A0D2F1BF4}`**, got E_NOINTERFACE, and failfasted `0xC0000409`. Exit after 339 ms. No adapter slot, CreateView, Initialize or SetWindow call was seen. Windows 0. | Yes (39/39) |

### Restore evidence

- 2415 cycle receipt: `$TOOLING/cuphead/cuphead-owned-ordinary-20261012-2415/receipt.json`,
  sha256 `2696bff0968b613eceab973e42607ab32081bf4efc44b02e7093b8a4dd4e71de`. It records
  39 swaps and `restoration_errors: []`.
- Independent check right after 2415: all 39 targets matched baseline; 13 created
  registry keys were removed; CoreApplication `DllPath` was back to its baseline
  value; no Wine processes.
- **Current state was not re-verified.** On 2026-10-12 one read-only check
  re-confirmed the receipt contents above, but did not re-hash the installed
  targets or list processes. Re-verify before any change (§6).

## 3. Hashes

| Role | Artifact | sha256 |
|---|---|---|
| Baseline (installed) | windows.ui | `c7a53a3b02fca3f4f06fc1a3b8fe7474a6a304146443640f36af9d2c9754b0a2` |
| Baseline (installed) | ole32 | `640d93709c50e016b67a1c92d847a0f2988a20b2b151036f673bd325eba5cf8f` |
| Reusable candidate | ole32 (git.c fix) | `0a79743f89b1ab100efad548c89680ce27e56bae24bc44ca9b52301893d8afb4` |
| Candidate | windows.ui 2414 (AUTOCREATE + logging) | `75adb25d443eb38bf4e930c3a47cc3ffad7faf62be85e8415028f8e6370046f9` |
| Diagnostic | windows.ui 2415 (fallback adapter stub) | `5d8dcb51753da0c8712a8d1236e821a2dc7fd51fb5e312935f4372466faca71c` |
| Superseded | windows.ui `826ec2a7…`, `065dfcfc…` (trace), `ad86189e…` (setter storage); 2412 windows.ui `6c12b46c…`, ole32 `d459074e…` | prefixes only |
| Receipt | `cuphead-owned-pnp-unit-20261012-2413` | `4d212c9c56d0c165864acd09050d7e75d76c1b9844aa1433bf8431e350b11f9a` |
| Receipt | `cuphead-owned-ui-stub-unit-20261012-2415` | `829ed7fcf3b5ebf4f8e27891531c3ed87a72d3b4fb7910ffe3562ddc67ea6e06` |
| Receipt | `cuphead-owned-ui-unit-20261012-2414`, `-2415` (storage) | `484de85f…`, `006f1f28…` (prefixes only) |

Candidate directories (Mac only, under `$TOOLING/cuphead/`):
`corewindow-autocreate-candidate-20261012-2414/`,
`corewindow-adapter-trace-candidate-20261012-2415/`,
`corewindow-adapter-storage-candidate-20261012-2415/`,
`corewindow-adapter-stub-candidate-20261012-2415/`.

## 4. What is reusable and what is not

| Class | Content | Disposition |
|---|---|---|
| Our source, reusable | ole32 `git.c` loads `windows.ui.dll` (`.ui` was read as an extension, so `.dll` was never appended) | Upstreamable bug fix |
| Our source, reusable | windows.ui AUTOCREATE: `GetForCurrentThread` creates a real HWND-backed CoreWindow owned by the calling thread. Clears twinapi's NULL gate (helper `0x21260`, error tag `0x218`). | Keep as opt-in. **Must be scoped to the package process**; set globally it breaks the strict CoreWindow pre-gate. |
| Our source, reusable | Owner: AUTOCREATE env set only inside `package_capture` and removed in `finally`; launch replay guard; dirty-registry baseline checks | Keep |
| Our tests | `dlls/windows.ui/tests/corewindow.c`: AUTOCREATE plus adapter checks (73 checks in the 2415 state) | Keep the AUTOCREATE part; drop the adapter-stub assertions |
| Diagnostic only | 2415 fallback adapter (IUnknown-only, 38 generic E_NOTIMPL thunks), setter-pointer storage, raw setter logging, unmuted getter TRACE, ViewWait release logging | **Keep out of any baseline and any upstream patch.** Do not alias `{F02EE4C8}` onto the generic thunks. |
| Native Microsoft reference DLLs | `twinapi.appcore.dll` (from `decoded-attempt1`), CoreUIComponents and WindowManagementAPI used for static RE | **Never commit or distribute.** Reference only. |
| Artifacts, logs, secrets | Cycle work dirs, `server.log` (~200 MB+), relay stderr, screenshots; Celeste logs `~/Library/Logs/XodusRemote/t-celeste-*` (may contain URLs) | Never commit. No credentials, license keys, key blobs or CDN URLs belong in the repo. |

## 5. Interface findings for `{F02EE4C8}` (static RE, coordinator "Manifest" #20Y/#20Z)

Established:

- `{F02EE4C8-92B3-48C7-9B48-3D0A0D2F1BF4}` is an **IUnknown-derived** interface,
  not IInspectable. twinapi helper `0x21260` performs the QI at `0x21596` and
  failfasts on rejection. This matches 2415.
- Native implementations: CoreUIComponents vtable `0x1b4cc8` and
  WindowManagementAPI `0x149d30`. Source paths point to a
  "navigationclientapplicationviewadapter" implementation family. No formal
  interface name is known.
- **Slot 3 = `HRESULT (This, ApplicationViewMode *out)`**. twinapi maps public
  `IApplicationView4` `{15E5CBEC-9E0F-46B5-BC3F-9BF653E74B5E}` to table `0x1a4e08`.
  Its slot 6 `get_ViewMode` (`0x211e0`) forwards the same out pointer to adapter
  slot 3. Enum values: `Default=0`, `CompactOverlay=1`. Both are already declared
  in Wine's `windows.ui.viewmanagement.idl`.
- Slot 4: DWORD out, semantics unknown. Slots 5–7: boolean outs. Slot 8: boolean
  input. All semantics unknown.
- No declaration of `f02ee4c8` exists in Wine `include/`, `dlls/windows.ui`, or
  `dlls/twinapi.appcore` (grep). A dlls-wide grep was not completed.
- `{7A5B6FD1…}` is our local private `ICoreWindowAdapterInteropWine`
  (IInspectable layout). Its slot 7 is `get_ApplicationViewClientAdapter(This,
  IUnknown **)`. No Wine IDL declares it.
- `{96C7A5EF}` is HandleSignalNotifierFactory, `{46305C32}` is
  IHandleSignalNotifierFactory, and `{923C402E}` is ISignalHandler.

**Withdrawn claims** (do not build on them):

| Claim | Replaced by |
|---|---|
| `{96C7A5EF}` is a ThreadPool class needing a `DllGetClassObject` RunAsync adapter (#20P) | HandleSignalNotifierFactory; builtin already serves it (2413 S_OK). Adapter dropped. |
| twinapi calls `SetWindowClientAdapter`, so storing the setter pointer will feed the getter | twinapi only reads getters (#20X). The stored-pointer path stays empty. |
| twinapi never QIs the adapter `O` and calls its vtable directly (#20X) | It QIs for `{F02EE4C8}` and failfasts (2415, #20Y) |
| Slot 3 is a plausible DWORD/ID read through a failure-swallowing wrapper | Slot 3 is `ApplicationViewMode` (#20Z) |
| Slots `+0x70` and `+0x80` take scalar floats | Withdrawn (#20Y) |

## 6. Portable resume checklist

The reference Mac needs CrossOver 26.3, a bottle with entitled Cuphead, and
Homebrew `python3`. Other machines can do source and review work only.

1. **Clone:** `git clone https://github.com/dragoshont/xodus-macos.git`, then
   `git switch dragoshont-celeste-eappx-support`. Read this file, then the tail of
   `eappx-feasibility.md`.
2. **Preserve first:** export the §1 Mac-only files as patches into the repo and
   verify their sha256 values against §1. Until that is done, do not edit them.
3. **Preflight, read-only, on the Mac:**
   - Hash the installed CrossOver and bottle `windows.ui` / `ole32` against the
     §3 baselines.
   - Confirm no `wineserver`, `wine-preloader` or `wineloader` processes.
   - Check that `system.reg` CoreApplication and MRM `DllPath` values equal the
     owner's hard-coded baselines. Crashed cycles leave them dirty, and the owner
     then self-blocks.
4. **Build:** set `PATH=/opt/homebrew/bin:$PATH` and run `make` for the
   `dlls/windows.ui` and `dlls/ole32` targets in the build tree that matches
   `wine-source` (see the §1 inventory gap). Record the output sha256.
5. **Targeted test:** the windows.ui `corewindow` test and the strict CoreWindow
   test. Both must pass with AUTOCREATE **unset** globally.
6. **Owner dry-run, no launch:**
   `XODUS_CUPHEAD_CONTEXT_CAPTURE=1 XODUS_INTERFACE_TESTS_ONLY=1 XODUS_ORDINARY_PACKAGE_LAUNCH=1 XODUS_PLAN_DRYRUN=1 python3 cuphead-ordinary2411-owner.py`.
   The dry-run creates the label work dir, so archive it before any real launch.
7. **Launch, only with explicit authorization:** a new, unused label (never
   replay 24NN labels), started from the Aqua console session through Terminal
   (`osascript … do script … caffeinate -dimsu python3 <launcher>`). SSH
   background sessions have no WindowServer.
8. **Restore and verify independently:** re-hash all receipt targets against
   their baselines, confirm created registry keys are removed and `DllPath` is
   restored, and confirm no Wine processes.

## 7. Next grounded experiment (proposal only, not authorized)

Implement `{F02EE4C8}` in windows.ui as a real per-window ApplicationView
adapter, not by aliasing the diagnostic thunks:

- QI accepts IUnknown and `{F02EE4C8}` only.
- Slot 3 returns the window's actual `ApplicationViewMode` (initially `Default`).
  That state must be shared with any public ApplicationView mode getters and
  setters, not stored as a constant.
- Slots 4–8 stay E_NOTIMPL, leave outputs unwritten, and log, until their
  semantics are decoded.
- A targeted test covers the QI and slot 3, alongside the AUTOCREATE test.

Expected next observable: either progress to CreateView, Initialize or SetWindow
(SetWindow needs a QI for ICoreWindow `{79B9D5F2}`), or a new failure in slots
4–8. Gates remaining after that: Load/Run, Activate and window visibility, then
the D3D/swapchain first frame (never observed).

## 8. Celeste EAppx installer status (unchanged)

- `extract-eappx` runs fully offline with supplied keys and never enters
  `ensure_device_credentials`.
- `inspect` reads public package metadata without credentials.
- Store install still selects only `.msixvc` (`streaming.rs`), and `run.rs`
  requires `.xodus-streaming.msixvc` plus XVD metadata.
- No Celeste package, header or manifest has been observed locally. Getting one
  needs a credentialed download that has not been approved.
- **No Store EAppx install support is claimed.**
