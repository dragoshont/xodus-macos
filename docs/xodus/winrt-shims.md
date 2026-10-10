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

### WinRT UI shims — built, awaiting a trace that reaches them

Loadable **logging** shims for `Windows.Graphics`, `Windows.UI`,
`CoreMessaging`, `dcomp` and `CoreUIComponents` are built (x64; export tables
match System32 exactly). Each logs every call to `XODUS_SHIM_LOG`, so the real
runtime-class needs are discovered from traces rather than reimplemented blind.
They are forward-looking — Cuphead's current chain has not reached them.

## Actual Cuphead x64 blocker chain

```
.NET Native startup ──(mrt100 ✅)──► StateRepository / PackageFamilyNameFromFullName
    ──► profapi.dll #114 ✅ ──► (next boundary: TBD by the Mac-lane trace)
```

## Method & tooling

The per-DLL reverse-engineering playbook, the decision tree (decompile vs
logging shim), the clean-room discipline, and the bottle-side load-provenance
lessons are documented in the shims repo at `docs/METHODOLOGY.md`. Note: **REA
(rea.tools) is not usable here** — it inspects a running web page's JavaScript
over a browser debug connection, not native Windows PE DLLs. The toolchain is
Ghidra headless + pefile + zig.
