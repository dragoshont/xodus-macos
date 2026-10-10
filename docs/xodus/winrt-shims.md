# WinRT / .NET Native shim DLLs

Some Xbox PC titles are built on Windows-only frameworks whose DLLs are **not
redistributable** (they ship only inside Windows or signed Store framework
packages). Under stock CrossOver/Wine those imports can't be resolved and the
game fails at startup.

Rather than ship Microsoft binaries, the replacements are being built as
clean-room reimplementations in a separate **private** repository:

> **https://github.com/dragoshont/xodus-winrt-shims**

Microsoft binaries and any decompiler output stay out of git in both repos. The
original DLLs are used only locally, from an owned Windows install, to observe
behaviour. This page is just a pointer plus the current DLL list and results.

## Non-redistributable DLL list

Export counts are from a local Windows 11 `System32` (via `pefile`). For the
WinRT components the classic export count is tiny and **understates** the real
surface, which is exposed through WinRT activation (runtime classes), not flat
exports.

| DLL | Source | Needed by | Surface (System32 build) | Notes |
|---|---|---|---|---|
| `mrt100.dll` | Windows `System32` | Cuphead, Celeste (.NET Native titles) | **1 export** (`GetManagedRuntimeService`), ~40 KB | OS-services broker for the .NET Native runtime. **Done** — contract recovered, replacement built and validated (below). |
| `Windows.UI.dll` | Windows `System32` | XAML UWP titles | 15 classic exports, ~1.4 MB | Real surface = WinRT runtime classes (`Windows.UI.*`). Large. |
| `Windows.UI.Xaml.dll` | Windows `System32` | XAML UWP UI | 16 classic exports, **~18 MB** | The full XAML framework; by far the biggest. Reimplementation impractical. |
| `dcomp.dll` | Windows `System32` | Composition / DirectComposition | 42 exports, ~2.3 MB | Flat-ish C API, but deep (ties into DWM/D3D). |
| `CoreMessaging.dll` | Windows `System32` | CoreApplication / dispatcher | 31 exports, ~1.2 MB | Message/dispatcher plumbing behind CoreWindow. |
| `Windows.Graphics.dll` | Windows `System32` | Graphics interop | 3 classic exports, ~0.6 MB | Surface is WinRT activation. |
| `CoreUIComponents.dll` | Windows `System32` | Core UI / input | 24 exports, ~3 MB | Pairs with CoreMessaging/Windows.UI. |
| `gameplatformservices` | Windows / Gaming Services | Xbox services | n/a | Xbox platform services; stub/trace candidate. |
| `icu.dll` / `icuuc.dll` | Windows `System32` | Unicode (titles using system ICU) | 1059 / 542 exports | **Wine already ships ICU** — usually resolvable without a custom shim. |

### What the Celeste/Cuphead runtime session actually hit

The companion runtime session ("Celeste eappx support") did **not** block on the
WinRT UI DLLs above. Its recorded blockers are a different, lower layer —
package **identity and activation**:

- `CheckTokenMembershipEx`, `RtlQueryWnfStateData`
- `RtlQueryPackageClaims`, `GetCurrentPackageInfo3`
- `CoreApplication` / `CoreWindow` activation, `DisplayInformation`

`mrt100.dll` is the specific **.NET Native** startup import that Cuphead/Celeste
fail on; it is the smallest, best-understood blocker and the first one tackled.
The WinRT UI DLL list above is forward-looking for the XAML path.

## Results so far

### mrt100.dll — done (pending in-game trace)

- **Tooling:** REA (rea.tools) could not analyse Windows native PE **DLLs**
  (its Windows Ghidra provider accepts PE *applications* only; the Job
  Object/DACL host isn't implemented, and Hopper is unsupported on Windows).
  Fallback: **Ghidra headless** directly, which fully recovered the contract.
- **Contract:** single export `GetManagedRuntimeService(REFIID, void**)`
  returns a process-wide COM singleton with two interfaces (20 + 4 vtable
  slots) covering virtual-memory/write-watch, GC thread + stack-scan, cache
  size, memory load, VEH and a COM wait. Full spec in the shims repo
  (`docs/mrt100-contract.md`).
- **Replacement:** reimplemented from scratch on public Win32 APIs (zig build,
  x64 + x86). Single clean export; imports only kernel32, ole32 and the UCRT
  apisets (all Wine-supported).
- **Validation:** a local smoke test exercising every slot **passes
  identically against our DLL and the real System32 copy**, confirming the
  recovered vtable order, IIDs, QueryInterface/`E_NOINTERFACE` behaviour,
  memory ops and the exchange tear-off.
- **In-bottle result:** the Mac lane ran a genuine-vs-reimplementation A/B on
  the actual x64 Cuphead package. **Our mrt100 passed .NET Native startup
  identically to the genuine DLL, with no observed divergence**, and both
  reached the *same* next boundary. The trace showed Cuphead's real mrt100
  surface on startup is just 6 slots — `QueryInterface(IF1)`,
  `ReserveWriteWatch`, `Commit`, `GetMemoryLoad`,
  `CreateSuspendedHighPrioThread` (slot 15/19 not reached). v0.1.1 added a
  Wine-robust exception-state gate to the stack scan and fixed a log-init race;
  it A/B'd as a verified bounded delta.

### profapi.dll ordinal 114 — done (native-validated)

The boundary *after* mrt100 on the real Cuphead run is
`unimplemented function profapi.dll.114`, reached through the genuine
`StateRepositoryCore.dll`. Wine stubs all 17 profapi ordinals.

- **Contract (Ghidra):** ordinal 114 is a registry helper that opens
  `HKCU\Software\Classes\Local Settings\Software\Microsoft\Windows\CurrentVersion\AppContainer\Storage\<pkg>[\Children\<child>][\<subkey>]`
  with a caller-supplied `REGSAM`; returns `S_OK` / `E_INVALIDARG` /
  `HRESULT_FROM_WIN32`.
- **Validation:** a native A/B (`test/profapi_ord114_test.c`) calls our function
  and the genuine `profapi.dll` ordinal 114 side by side — **identical return
  codes and key-returned state, 0 failures.**
- **Integration:** standalone native-override `profapi.dll` (ord 114 real, the
  rest logged) needs no Wine rebuild since Wine stubs them all; or a one-line
  Wine `.spec` patch. Must go in the prefix `system32`, not next to the exe
  (system DLLs resolve from system32). Mac-lane integration in progress.

### kernelbase.dll package globalization — done (native-validated)

After profapi #114, the next **unresolved import** was
`KERNELBASE.dll!GetCurrentPackageGlobalizationContext`. Wine's `kernelbase`
doesn't export the package-globalization family, so the import fails to bind at
load time — a missing export a `--dll`/`WINEDLLOVERRIDES` override can't fix.

- **Reimplemented** 3 exports clean-room from a Ghidra decompile + a read-only
  packaged native oracle:
  `GetCurrentPackageGlobalizationContext`, `GetPackageGlobalizationContext`,
  `GetPackageGlobalizationProperty`.
- **Corrected semantics (packaged):** an earlier pass assumed the process is
  unpackaged under Wine and returned `APPMODEL_ERROR_NO_PACKAGE` (0x3d54). The
  Mac lane proved the suspended Cuphead image is **catalog-activated with real
  package identity**, so it hits the *packaged* branch. Genuine returns a
  constant **28-byte "GLOB" context** (tag "GLOB", size 0x1c, flags@+8 = 0,
  inline UTF-16 ApplicationId "App" at +0x14). The oracle (read-only
  `OpenPackageInfoByFullName`, no activation/mutation) confirmed flags = 0 across
  **all 123 installed packages**, and Cuphead's manifest has no globalization
  element → flags 0 is authentic for Cuphead. `0x3d54` is now returned only for a
  genuinely unpackaged process.
- **Validation:** rewrote the test as a **genuine-ref differential** — feeds the
  SAME genuine `PACKAGE_INFO_REFERENCE`/context to genuine and ours.
  `GetPackageGlobalizationContext` returns genuine's own context pointer;
  `GetPackageGlobalizationProperty` identical; unpackaged
  `GetCurrentPackageGlobalizationContext` A/B → 0x3d54. **0 failures.** Reserved
  arg resolved: ignored on x64 (width immaterial). 1st arg of
  `GetPackageGlobalizationContext` confirmed to be a `PACKAGE_INFO_REFERENCE`.
- **Integration:** `kernelbase` is a core/KnownDLL — no overlay possible; the
  exports go into Wine's `kernelbase.spec` + source (snippet provided). Wire the
  current-package path to emit the GLOB context (inline the live ApplicationId;
  flags from the manifest, 0 until a package is found that sets a bit).

### shcore.dll ordinal 265 — done (native-validated)

After globalization, **CoreWindow activation** calls into SHCORE and the next
**unresolved import** is `SHCORE.dll` **ordinal 265** — a no-name export Wine's
`shcore` doesn't define (confirmed on upstream master and the CrossOver tree).
Dropping in the genuine SHCORE resolves it but then drags in the *next* missing
import, `api-ms-win-gdi-dpiinfo-l1-1-0.dll!GetCurrentDpiInfo`, via its
per-monitor DPI path — so the genuine binary just moves the wall.

- **Recovered** the contract clean-room (Ghidra decompile + read-only native
  oracle; corroborated by prior-art notes):
  `HRESULT GetScaleFactorForCoreWindow(IUnknown *coreWindow, DEVICE_SCALE_FACTOR *pScale)`
  — ordinal 265 at RVA 0x19FA0. Genuine defaults `*pScale = 100`, QIs
  `coreWindow` for ICoreWindowInterop `{45D64A29-A63E-4CB6-B498-5781D298CB4F}`
  (vtable slot 3 → HWND), then does a per-monitor DPI query (NULL → primary
  monitor). Oracle on a 140%-DPI host: `ord265(NULL) → S_OK, s = 140` — real,
  deterministic per-monitor scaling.
- **Clean-room impl** returns `S_OK + 100` (`SCALE_100_PERCENT`) without QI or
  DPI queries. That is the faithful answer under Wine's 100% bottle (Wine's own
  `GetScaleFactorForMonitor` is a FIXME stub that already returns 100), AND it
  deliberately **avoids the gdi DpiInfo path**, so `GetCurrentDpiInfo` is never
  reached via SHCORE.
- **Validation:** native A/B (`test/shcore_265_test.c`) — genuine loaded by
  absolute System32 path, no-name export resolved by ordinal, self-test-trap
  guard. **0 failures.** The differential pins genuine's ABI/return/NULL
  semantics with real (non-default, 140%) scaling evidence and confirms ABI
  compatibility; it deliberately does **not** assert value equality (100 vs host
  DPI is by design).
- **Integration:** `shcore` is a system DLL — no overlay possible. Add to
  `dlls/shcore/shcore.spec`:
  `265 stdcall -noname GetScaleFactorForCoreWindow(ptr ptr)` and compile
  `src/shcore/shcore_265.c` into the module. Caveat: if a future trace shows
  Cuphead passing a **non-NULL** CoreWindow and needing a real per-window scale,
  revisit (Wine's `GetDpiForMonitor` delegates to a real
  `GetDpiForMonitorInternal` that could back a genuine NULL-primary slice).

### WinRT UI shims — built, awaiting a trace that reaches them

Loadable **logging** shims for `Windows.Graphics`, `Windows.UI`,
`CoreMessaging`, `dcomp` and `CoreUIComponents` are built (x64; export tables
match System32 exactly). Each logs every call to `XODUS_SHIM_LOG`, so the real
runtime-class needs are discovered from traces rather than reimplemented blind.
They are forward-looking — Cuphead's current chain has not reached them.

Note: `twinapi.appcore` CoreApplication/ApplicationView factories initially
appeared broken, but the Mac lane traced it to the CrossOver `wine` wrapper
stripping `WINEDLLOVERRIDES` unless an explicit `--dll` arg is passed; with
`--dll=twinapi.appcore=n` both return `S_OK` natively — **no reimpl needed**.

## Actual Cuphead x64 blocker chain

```
.NET Native startup ──(mrt100 ✅)──► StateRepository / PackageFamilyNameFromFullName
    ──► profapi.dll #114 ✅ ──► KERNELBASE.dll!GetCurrentPackageGlobalizationContext ✅
    ──► SHCORE.dll #265 (GetScaleFactorForCoreWindow) ✅
    ──► (next boundary: TBD by the Mac-lane trace)
```

## Method & tooling

The per-DLL reverse-engineering playbook, the decision tree (decompile vs
logging shim), the clean-room discipline, and the bottle-side load-provenance
lessons are documented in the shims repo at `docs/METHODOLOGY.md`. Note: **REA
(rea.tools) is not usable here** — it inspects a running web page's JavaScript
over a browser debug connection, not native Windows PE DLLs. The toolchain is
Ghidra headless + pefile + zig.
