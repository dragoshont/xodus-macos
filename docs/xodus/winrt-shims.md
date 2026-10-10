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
  monitor). **Cuphead always calls it with `coreWindow == NULL`** (Mac-lane
  argument diagnostic). Oracle (DPI-aware probe): genuine `ord265(NULL)` ==
  `GetScaleFactorForMonitor(primary)` = the legacy **recommended**
  `DEVICE_SCALE_FACTOR` (140 on this host) — an EDID/physical-size heuristic
  distinct from the live effective scale (168 dpi = 175%) and distinct again from
  what a DPI-unaware process sees (96 dpi = 100%). That recommended-scale
  heuristic is a Windows-host concept Wine cannot faithfully model.
- **Clean-room impl** (`src/shcore/shcore_265.c`):
  - NULL `coreWindow` → the scale from the bottle's **measured** primary-monitor
    DPI (`GetDpiForMonitor` → real `GetDpiForMonitorInternal` in Wine, **not** the
    shcore `GetScaleFactorForMonitor` 100-stub), mapped to the nearest
    `DEVICE_SCALE_FACTOR` by a pure `shcore_dpi_to_device_scale`. 96 dpi → 100% on
    a default bottle, but **computed** from live DPI (a scaled Wine DPI yields the
    right value), with DPI-query errors propagated. Stays off the genuine gdi path
    so `GetCurrentDpiInfo` is never reached via SHCORE.
  - non-NULL `coreWindow` → `E_NOTIMPL` (explicit unsupported; confirmed never hit;
    per-window/ViewPresentation scale not yet established).
  - NULL `pScale` → `E_INVALIDARG`.
- **Validation:** native A/B (`test/shcore_265_test.c`) — pure DPI→scale mapping
  table, measured-consistency (ours == mapping of the independently measured real
  DPI, proving the value is **not** a constant), explicit unsupported/guard
  returns, and a genuine pin loaded by absolute System32 path. **0 failures.** It
  deliberately does **not** assert `genuine == ours`: genuine returns the host
  *recommended* scale (140, an EDID heuristic), ours returns the bottle *measured*
  DPI scale (100 on a default bottle) — divergence by design.
- **Integration:** `shcore` is a system DLL — no overlay possible. Add to
  `dlls/shcore/shcore.spec`:
  `265 stdcall -noname GetScaleFactorForCoreWindow(ptr ptr)` and compile
  `src/shcore/shcore_265.c` into the module.

### bcp47mrm.dll!GetApplicationLanguagesWithUserLanguagesFallback — done (native-validated)

The boundary after SHCORE #265: .NET Native / MRT resource resolution imports
`bcp47mrm.dll!GetApplicationLanguagesWithUserLanguagesFallback` **by name**. Wine
ships a `bcp47mrm` module but doesn't export this function, so the bind fails at
load (no override can add a missing export). With the genuine DLL the Mac lane
saw Cuphead advance to `KERNELBASE.FindPackagesByPackageFamily`, so one faithful
export unblocks the language step.

- **Recovered** the contract (Ghidra decompile of genuine 10.0.26100.9278, export
  ord 7 at RVA 0x4B70 + a read-only native oracle):
  `LONG(PCWSTR packageFullName, UINT32 *pRequiredChars, PWSTR buffer, ULONGLONG flags)`.
  Two-call **size-query/fill** ABI — `pRequiredChars` is **OUT-only** (required
  chars incl. NUL; input ignored), `buffer==NULL` → size query, the list is a
  **`;`-delimited** BCP-47 string. `pRequiredChars==NULL` → `E_POINTER`; `flags`
  reserved (no observable effect).
- **Oracle decode (171-package sweep + edge cases):** the result is derived from
  the **user/system** language profile, *not* the process override
  (`SetProcessPreferredUILanguages` had no effect). **`packageFullName` is never
  rejected** — NULL / empty / malformed / unknown all return the user-language
  fallback (`"en-US"`); only 1 of 171 installed packages
  (`Microsoft.Winget.Source`, a language-neutral resource package) returned
  `"und"`, the lone evidence of installed-package manifest/PRI intersection.
- **Clean-room impl** (`src/bcp47mrm/bcp47mrm_applang.c`) builds the list from the
  live user locale (`GetUserPreferredUILanguages`) via a pure, unit-tested
  `bcp47_join_multisz` — **never fabricates tags**: if the OS language API fails
  the real error is propagated as an HRESULT (`HRESULT_FROM_WIN32`, or
  `E_UNEXPECTED` when the API fails but `GetLastError()` is 0 so `S_OK` never
  leaks; `E_OUTOFMEMORY` on alloc failure). Since `pRequiredChars` is OUT-only
  with no caller capacity, the export always writes the full list.
- **SCOPE — subset, not packaged parity.** This returns the user languages, which
  *coincides* with genuine on a single-language host. The Mac lane confirms
  Cuphead in the bottle carries **genuine private PRI/manifests** and is
  **catalog-activation verified**, so the genuine per-package language
  **selection** path (the lone `"und"` case) **is reachable there**; this shim
  does not yet perform it. Align `packageFullName` handling to genuine once the
  Mac-lane native `bcp` work observes the real selection. Not "the only reachable
  path."
- **Validation** (`test/bcp47mrm_applang_test.c`): pure-join table,
  measured-consistency, error guards, and — the key differential — **same-host
  A/B equality**: genuine == ours on **HRESULT + required count + value** for
  NULL / empty / malformed / unknown `packageFullName` (valid because both read
  the same host user-language profile). **0 failures.**
- **Integration:** add to `dlls/bcp47mrm/bcp47mrm.spec`
  `@ stdcall GetApplicationLanguagesWithUserLanguagesFallback(wstr ptr ptr int64)`
  and compile `src/bcp47mrm/bcp47mrm_applang.c` into the module.

### kernelbase.dll!OpenGlobalizationUserSettingsKey — done (native-validated)

The import the Mac lane requested after the package-catalog legs. Wine's
`kernelbase.spec` lists the globalization user-settings-key family only as
**commented-out stubs** (`OpenGlobalizationUserSettingsKey`,
`CloseGlobalizationUserSettingsKey`, `IsGlobalizationUserSettingsKeyRedirected`),
so Cuphead's import of `OpenGlobalizationUserSettingsKey` fails to bind at load.
This genuine build (`10.0.26100`) actually exports only two of the family by name:
`OpenGlobalizationUserSettingsKey` (ord 1148) and
`QueryGlobalizationUserSettingsStatus` (ord 1374) — there is **no**
`CloseGlobalizationUserSettingsKey` here (the handle is an ordinary registry
handle, closed with `RegCloseKey`).

- **ABI** (Ghidra, confirmed from the prologue — ecx/rdx/r8), returns **NTSTATUS**:
  `OpenGlobalizationUserSettingsKey(ACCESS_MASK samDesired, PVOID reserved, PHKEY phkResult)`.
  The OUT handle is the **3rd** arg; `NULL` → `STATUS_INVALID_PARAMETER`
  (`0xC000000D`) before any open. `reserved` is consulted only on the
  multi-user-in-session server SKU (ignored on desktop / in a bottle).
- **Runtime oracle** (`NtQueryKey(KeyNameInformation)` on the returned handle):
  for every access mask genuine returns the **current-user hive root**
  `\REGISTRY\USER\<SID>` (== `HKEY_CURRENT_USER`). The decompiled SKU branches all
  collapse to that root when no machine-level
  `HKLM\…\CommonGlobUserSettings\RedirectedKey` redirection is configured —
  **always true in a Wine bottle** (that policy is the "redirected" companion the
  Mac lane asked about).
- **Clean-room impl** (`src/kernelbase_glob/globalization.c`): opens the
  current-user root directly via the documented ntdll API
  `RtlOpenCurrentUser(sam, phk)`, reproducing the observable key **without**
  copying the SKU/redirection internals.
- **Validation** (`test/kernelbase_globkey_test.c`): same-host A/B — **ours opens
  the byte-identical registry key as genuine for all 4 access masks**, exact
  NTSTATUS parity, matching `NULL`-guard. Status value of
  `QueryGlobalizationUserSettingsStatus` is SKU-dependent (genuine `2` on this
  multi-session box vs `0` on a desktop/bottle), so it is **not** host-pinned.
  **24/24, 0 failures.**
- **Integration:** uncomment + implement in `dlls/kernelbase/kernelbase.spec`
  `@ stdcall OpenGlobalizationUserSettingsKey(long ptr ptr)` (and, for
  completeness, `QueryGlobalizationUserSettingsStatus(ptr ptr)`), compiling
  `src/kernelbase_glob/globalization.c` into the module. See
  `docs/kernelbase-globalization-userkey-contract.md` in the shims repo.

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
    ──► bcp47mrm.dll!GetApplicationLanguagesWithUserLanguagesFallback ✅ (user-languages subset)
    ──► KERNELBASE.dll!FindPackagesByPackageFamily ✅ (Mac-lane: catalog enumeration)
    ──► KERNELBASE.dll!GetStagedPackagePathByFullName ✅ (Mac-lane: legacy→stagedPath2)
    ──► KERNELBASE.dll!OpenGlobalizationUserSettingsKey ✅ (RE lane: RtlOpenCurrentUser)
    ──► (next boundary: TBD by the Mac-lane trace); still 0 windows / exit 92
```

## Method & tooling

The per-DLL reverse-engineering playbook, the decision tree (decompile vs
logging shim), the clean-room discipline, and the bottle-side load-provenance
lessons are documented in the shims repo at `docs/METHODOLOGY.md`. Note: **REA
(rea.tools) is not usable here** — it inspects a running web page's JavaScript
over a browser debug connection, not native Windows PE DLLs. The toolchain is
Ghidra headless + pefile + zig.
