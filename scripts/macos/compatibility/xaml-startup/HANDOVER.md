# Xbox original-app bring-up — HANDOVER (paused 2026-10-09 ~17:3x +03:00)

Status of the Microsoft **original** Xbox PC app (XboxPcApp.exe, Microsoft.GamingApp
2609.1001.16.0, AUMID `Microsoft.Xbox.AppL`) running under Wine on the Apple-silicon
Mac (`ssh xodus-mac`, host m5.hont.ro), no VM. **Project paused for budget. No work in
progress. Mac prefix is restored to the clean br171 candidate; no stray processes.**

This branch: `dragoshont-xbox-app-shell-navigation-client`. It is a research/EXPERIMENT
branch and is kept SEPARATE from the clean candidate (br171) and the cumulative patch
`wine-xbox-original-app-sprint.patch`.

---

## 1. Current milestone ladder (only what was OBSERVED)

- [x] App process alive (exit 92 = alive sentinel) under activator + shellhost broker.
- [x] XAML builds its DComp interop compositor, visuals, and listeners.
- [x] XAML enters its frame path: calls the interop callback **NotifyDirty**, then
      **GetFrameStatistics** returns S_OK.
- [ ] **BLOCKED HERE.** XAML never calls **Commit** and never creates a render target
      (**CreateWindowTarget** / **DCompTreeHost::EnsureResources** slot 7 never fires).
- [ ] Visible window — **UNTESTED** (never observed; a winemac window *is* shown at the
      Win32 level, see below, but XAML submits no frame, so no app content).
- [ ] Screenshot — **UNTESTED / NOT CAPTURED** (screencapture failed, see §4).
- [ ] Sign-in / library — **UNTESTED**, out of scope (do NOT sign in).

`XBOX-APP-STARTUP` remains **UNTESTED** — no real window has been proven.

---

## 2. The gate (exact next step)

XAML stops after `NotifyDirty` + `GetFrameStatistics`. The render target is never built.

### What was ruled OUT
- **Win32/winemac window visibility is NOT the gate.** The headed run (brG1, winemac.drv
  engaged) showed `WM_SHOWWINDOW SW_SHOW` on hwnd `0x20072` — a window *was* shown — yet
  XAML still submitted no frame.
- **XAML's visibility flag is NOT the gate.** EXPERIMENT X-01 (brE6) binary-patched
  `CJupiterWindow::UpdateWindowVisibility+0x6f` (RVA `0x3fdbfb`, `setne dl` `0F95C2` ->
  `mov dl,1;nop` `B20190`) to force `CCoreServices::SetWindowVisibility(visible=1)`
  regardless of `ICoreWindow::get_Visible`. No change. Reverted.

### The actual gate (zero-size backbuffer)
- D3D **is** up: `wined3d_cs` thread, DXGI factory/output, d3d11 device created.
- wined3d logs: `wined3d_swapchain_vk_create_vulkan_swapchain Image count 0 is not
  supported (2-3)` — the swapchain is created with a **0-size surface / BufferCount 0**:
  no backbuffer.
- XAML render-target path (from static analysis of system `Windows.UI.Xaml.dll` 26100,
  symbolized via `/tmp/xaml.syms.pkl` on the Mac):
  - `CCoreServices::Tick` (0x15846b) -> `WindowsGraphicsDeviceManager::`
    `WaitForD3DDependentResourceCreation` (func 0x1578c4, call at 0x157912)
    -> `DCompTreeHost::EnsureResources` (0x157ac4).
  - Per composition owner (8dac) xdis of `EnsureResources`: it calls `EnsureDCompDevice`,
    then takes the D3D device from `[host+0x180]+0x50 -> inst+0x10 -> +0x48`, and if
    non-null calls interop-compositor `d14b6158` (host+0x188) **vtbl slot 7 (+0x38)**
    `(d3dDevice, &out)` — believed to be **CreateSurfaceFactory** — then QIs the result 3x
    into host+0x1a0/0x1a8/0x1b0. **d14 slot 7 was NEVER called in any run** (brG1, brE6),
    so EnsureResources is either not reached or the D3D device at `inst+0x48` is null —
    consistent with the wined3d 0-image swapchain failure.

### Recommended next step (cheapest first)
1. **Confirm/instrument** whether `WaitForD3DDependentResourceCreation` (0x157912) is
   reached, and whether `CD3D11DeviceInstance+0x48` is populated. If the device there is
   null because the wined3d swapchain failed at 0x0 size, that is the root gate.
2. **EXPERIMENT (cheap): force the CoreWindow Bounds to 1280x800** so the D3D/DXGI
   swapchain gets a real surface. Open question (unresolved): does XAML's render target
   size come from `CoreWindow.Bounds` (WinRT) or the HWND client rect? `EnsureResources`
   itself reads no size; `CreateWindowTarget` is issued elsewhere. Determine the source,
   then force the right one (broker CoreWindow Bounds, or the HWND client rect).
3. Composition owner (8dac) will pre-implement **d14 slot 7 as a benign
   SurfaceFactoryPartner-returning object (logged)** in wincomp **v11**; once the 0-size
   swapchain is fixed and slot 7 is reached, iterate the next partner tuples as before.

---

## 3. How to re-run

On the Mac, `S=~/xodus-runs/xbox-service-principal-20261007-001`.

### Headless (no foreground needed)
```
S=~/xodus-runs/xbox-service-principal-20261007-001
# install a composition drop-in (example: v10) as C:\wincomp_poc.dll:
cp ~/xodus-composition-poc/cf/wincomp_exp10.dll $S/prefix/drive_c/wincomp_poc.dll
cd $S; export WINEPREFIX=$S/prefix; $S/build/server/wineserver -k
T=brXX
BROKER_ARGS="1280 800 6 coreui Microsoft.GamingApp_8wekyb3d8bbwe!Microsoft.Xbox.AppL Windows.Launch - 0x140 shellvm" \
  EXTRA_OVR='windows.ui=n;dataexchange=n;dcomp=n,b' APP_DEBUG_EXTRA='trace-ole' \
  bash guard.sh 440 150 perl -e 'alarm 420; exec @ARGV' \
  bash $S/run-br-ovr.sh $T XboxPcApp.App Microsoft.Xbox.AppL Windows.Launch > $T.guard.log 2>&1
grep ORIGINAL_APP_EXIT $T-original.stdout.log         # 92 = alive, 3221227010 = fail-fast
grep 'wincomp_poc:' $T-original.stderr.log | tail -30 # XAML partner walk
# ALWAYS restore the clean candidate afterwards:
cp $S/prefix/drive_c/wincomp_poc.dll.br170 $S/prefix/drive_c/wincomp_poc.dll
```

### Headed (needs the Aqua foreground — coordinate first)
Scripts are staged in `$S`: `gui-run.sh`, `gui-inner.sh`, `run-br-gui.sh`.
```
bash $S/gui-run.sh "$S/gui-inner.sh brGx" $S/brGx.gui.log 500
```
`gui-inner.sh` wakes the display with `caffeinate -u -t 900 &` (OWN pid, killed at end),
screenshots every 20s via `/usr/sbin/screencapture -x`, runs the app headed (winemac.drv),
and must be followed by restoring `wincomp_poc.dll.br170`. NOTE the screenshot blocker in §4.

---

## 4. Known risks / blockers

- **Screenshot capture is unproven.** Every `screencapture -x` returned
  `could not create image from display`, even from inside the Aqua LaunchAgent and after
  `caffeinate -u` set `UserIsActive=1`. Likely the built-in Retina panel is physically
  asleep (lid/clamshell) or the capture path lacks **Screen Recording (TCC)** permission.
  Before a headed run can yield evidence: confirm the lid is open / panel awake, and grant
  Screen Recording to the tool invoking screencapture. A per-window `CGWindowID` capture is
  an alternative but still needs that permission. User was unavailable to check at pause.
- **Unregistered COM classes** `{0134a8b2-3407-4b45-ad25-e9f7c92a80bc}` and
  `{228826af-02e1-4226-a9e0-99a855e455a6}` appear in every run (polled across threads) but
  are **non-fatal** — the app reaches the frame path regardless. Not in the composition
  owner's PDB tables. Likely optional shell/telemetry services; identify only if the size
  gate turns out to depend on them.
- `GamingServices` `FindPackagesByUserSecurityIdPackageFamilyName` is a Wine **stub**
  returning nothing; the app's JS polls it continuously. Not the render blocker.
- All EXPERIMENT changes have been reverted; see INVENTORY.md. The prefix is clean (br171).

---

## 5. Artifacts (sha256)

- Clean candidate wincomp on Mac: `$S/prefix/drive_c/wincomp_poc.dll.br170` and current
  `wincomp_poc.dll` = sha256 `3d8392a3573f7c0f...` (br171).
- Last composition drop-in run: `wincomp_exp10.dll` sha256
  `8efa3539ae3644c1af1a9cf5178553d27c03cdf55191d5b140ed9f5f4849f3ae` (owner INV-10:
  DeviceInternal slots 3-8, Partner4 45/46/57 benign).
- Composition drop-ins repo: `dragoshont/wine-composition-research`
  (`poc/handoff/wincomp_poc_EXP_v*.dll`; `docs/xaml-partner-vtables.txt` has the partner
  slot tables). Full EXPERIMENT drop-in history (v6..v10 = brE1..brE5) and sha256s are in
  `INVENTORY.md` and `probe-outputs/activation-20261007/br108-br118-outcomes.txt`.
- Mac disasm tools: `/tmp/xdis.py <pe> <start> <end>`, `/tmp/xref.py <pe> <targets...>`
  (uses `/tmp/xaml.syms.pkl`). `Windows.UI.pdb` was fetched to `/tmp/windows.ui.pdb`
  (sig `177CDFB6D167A7FC87FF756A911F588D` age 1) if CoreWindow symbol work is resumed.

## 6. Run records

- `INVENTORY.md` — the full EXPERIMENT/native-copy ledger (rows W-*, N-*, R-*, C-*, E-*,
  X-01, G-01), each with provenance, classification, status, and how to revert.
- `probe-outputs/activation-20261007/br108-br118-outcomes.txt` — per-run outcomes; the
  brE1..brE5 (v6..v10), brG1 (headed), and brE6 (X-01 force-visible) records are at the end.
