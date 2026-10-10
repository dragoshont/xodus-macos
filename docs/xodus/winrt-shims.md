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
- **Open:** which slots Cuphead actually calls needs an in-bottle trace
  (the build has `XODUS_MRT100_LOG` env-gated logging for exactly this);
  slot-19 parameter order and the x86 ABI still want runtime confirmation.
  Any bottle test must be coordinated with the Cuphead runtime session and run
  only in a private, owned bottle — never the shared CrossOver install.
