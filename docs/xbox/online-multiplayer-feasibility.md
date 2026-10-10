# Online multiplayer / Xbox Live online features — feasibility & minimum‑viable fix

Status: investigation + MVP fix landed as a source patch for the private GDK runtime
(`xgameruntime` legacy DLL). Tracker: `dragoshont/xodus-macos-app#16`.

This note covers Xbox Store (GDK) titles launched through Xodus on macOS that gate
behaviour on **Xbox Live sign‑in / connectivity**. The worked example is **No Man's Sky**
(Xbox GDK build, StoreId `BQVQTL3PCH05`).

> **Honesty doctrine.** Nothing here forges Xbox Live identity, tokens, package identity,
> or entitlements. The fix makes the runtime *admit* that there is no authenticated Xbox
> Live path (signed‑out / offline) promptly and honestly, which is the opposite of forging
> an online capability. No Microsoft binaries are committed; only our own source patch.

---

## 1. Symptom

No Man's Sky now **renders** and reaches its frontend using the pinned experimental
MoltenVK renderer, and the GDK licence check succeeds
(`Microsoft-issued matching full-game licence validated`, `gdkc` channel). It then **stalls
indefinitely** at its online "connecting to servers" step:

- `NMS.exe` stays alive, the game log freezes after licence validation.
- **Zero outbound TCP** from the whole wine process tree.
- No `xbl` / `xbox` / token markers in the default trace.

## 2. How NMS does "online" (seam map)

Loaded modules in the live process (via `lsof` on the running `NMS.exe`) and PE import
tables (`objdump -p`) show the online stack is **PlayFab**, not a direct Xbox Live SDK:

```
PlayFabServices.dll ─┐
PlayFabCore.dll      ├─ import ─► libHttpClient.dll ─► WINHTTP.dll        (wine builtin)
PlayFabMultiplayer.dll ┘                         └► XCurl.dll ─► curl / WINHTTP.dll
```

- `libHttpClient.dll` exports the `HCHttpCall*` surface (`HCHttpCallPerformAsync`, …) and
  imports **only** `WINHTTP.dll` + `CRYPT32.dll` for transport.
- `XCurl.dll` is Microsoft's curl distribution (`curl_easy_*`, `.xbld` section) and also
  imports `WINHTTP.dll`.
- **Nothing** in the game's binaries statically imports `xgameruntime` / `xuser` / `Xal` /
  `xsapi`. Xbox identity is reached, if at all, through the GDK's `xgameruntime.dll`
  (which *is* mapped in the process), not through a dedicated Xbox Live SDK DLL.

### Xodus GDK runtime shape (for context)

`xgameruntime.dll` in the bottle is a thin **primary** DLL that implements `XThreading`
natively and **forwards every other class id** (XUser, XNetworking, XStore, …) to a
**legacy** `xgameruntime_legacy.dll`. The online seam this note touches lives in the legacy
DLL, built from `dlls/xgameruntime/` in the Xodus wine tree. The honest Xbox Live auth
implementation already present there (`xuser.c`) performs a genuine RPS‑ticket → device
auth → SISU → XSTS exchange and requires a **real MSA account** brokered over a local
unix socket (`Z:\tmp\xodus.sock`), which is why sign‑in never produces outbound TCP from
the game itself.

## 3. Runtime evidence (instrumented run)

An instrumented build of the legacy DLL (honest `gdkc` markers added at the seam, see §5)
was deployed to the NMS bottle and the title launched. Observations:

| Observation | Evidence |
|---|---|
| Licence validates, renderer comes up | `gdkc` licence marker + MoltenVK init in stderr log |
| NMS **subscribes** to connectivity changes | `XNetworkingRegisterConnectivityHintChanged` worker runs |
| Connectivity is **polled every 500 ms** by the subscription worker | hundreds of `XNetworkingGetConnectivityHint -> level N` markers at a steady cadence |
| NMS **never calls `XUserAddAsync`** at the stall | zero `XUserAddAsync entry` markers across the whole run |
| No token calls | zero `XUserGetTokenAndSignature*` markers |
| Still zero outbound TCP | `lsof -i` on the wine pids shows no TCP sockets |

**Conclusion:** the hang is **not** at the `XUser` sign‑in seam — NMS does not even reach
it. NMS gates earlier, and the stall sits in the **PlayFab → libHttpClient/XCurl → WinHTTP**
path (an `HCHttpCallPerformAsync` that never reaches a socket). The connectivity hint is the
one GDK signal NMS is actively consuming at the stall.

## 4. Classification of the online seam

Each call the runtime services at the seam, classified as **(a)** should fast‑fail to
offline/signed‑out so the title drops to single‑player, vs **(b)** a genuine
online‑multiplayer prerequisite that cannot be satisfied without real credentials.

| Call | Class | Rationale |
|---|---|---|
| `XNetworkingGetConnectivityHint` | **(a)** | Advisory reachability signal. Honestly reporting "no authenticated Xbox Live path" here is the designed way to tell a title online is unavailable. |
| `XNetworkingRegisterConnectivityHintChanged` | **(a)** | Same signal, push form. Must deliver an honest low‑reachability level, not sit at `InternetAccess`. |
| `XUserAddAsync` (silent / `AddDefaultUserSilently`) | **(a)** | With no MSA account there is no default user. The honest result is `E_GAMEUSER_NO_DEFAULT_USER`, delivered **promptly** so the title treats it as definitive. |
| `XUserAddAsync` (`AddDefaultUserAllowingUI`) | **(a)** | No sign‑in UI exists in this environment; the honest result is still "no user". |
| `XUserGetState` | **(a)** | Must honestly report `SignedOut` when no authenticated handle exists. |
| `XUserGetTokenAndSignature[Utf16]Async` | **(b)** | Produces a real XSTS token/signature for an Xbox relying party. Cannot succeed without a real signed‑in account — a genuine online prerequisite, not something to fake. |
| PlayFab `LoginWith*` over `HCHttpCallPerformAsync` | **(b)** | Authenticated call to Hello Games' PlayFab title. Requires either a PlayFab‑accepted Xbox token or the title's own account flow. Genuine prerequisite. |

## 5. Minimum‑viable fix (implemented)

Patch: [`patches/xgameruntime-xbl-offline-fastfail.patch`](./patches/xgameruntime-xbl-offline-fastfail.patch)
(against `dlls/xgameruntime/` in the Xodus wine tree; the private runtime is not part of
this public repo, so the delta is committed here as a reviewable patch + notes and pushed
to the private runtime separately).

The fix introduces one honest gate, `xodus_xbl_online_enabled()` (reads `XODUS_XBL_ONLINE`),
and makes the class‑(a) seam resolve to a clean offline/signed‑out state **by default**:

1. **Connectivity (`xnetworking.c`).** When the host has raw internet but Xodus has no
   authenticated Xbox Live path, `XNetworkingGetConnectivityHint` reports
   `LocalAccess` instead of `InternetAccess`. This is honest (we have local networking, but
   no route to Xbox Live services) and is the designed signal for "online unavailable".
2. **Sign‑in (`xuser.c`).** `XUserAddAsync` fast‑fails with `E_GAMEUSER_NO_DEFAULT_USER`
   instead of attempting the RPS‑ticket bridge, so a title that *does* consult sign‑in gets
   an immediate, definitive signed‑out result rather than a long wait.
3. **Honest `gdkc` evidence markers** at the seam (`XUserAddAsync`, `XUserGetState`,
   `XUserGetTokenAndSignature[Utf16]Async`, connectivity level) so runtime behaviour is
   observable in real logs without enabling extra debug channels.

The real Xbox Live auth path is preserved and gated behind `XODUS_XBL_ONLINE=1`
(set per‑bottle via `HKCU\Environment`), so the honest user‑authenticated path (§6) remains
available for titles/accounts that can actually complete it.

### Build & deploy

```sh
export PATH="/opt/homebrew/bin:$PATH"
cd ~/src/build/xodus-wine-macos-x86_64
make dlls/xgameruntime/x86_64-windows/xgameruntime.dll
cp dlls/xgameruntime/x86_64-windows/xgameruntime.dll \
   "~/Library/Application Support/CrossOver/Bottles/Xodus-BQVQTL3PCH05/drive_c/windows/system32/xgameruntime_legacy.dll"
```

### Scope note (honest)

This fix makes the **GDK sign‑in / connectivity** seam resolve to offline promptly. For
No Man's Sky specifically, the stall observed in the instrumented run is **downstream** of
this seam, in the title's own PlayFab/libHttpClient/XCurl HTTP path (zero‑TCP hang before a
socket is opened). The connectivity signal is the only GDK lever NMS consults there; whether
`LocalAccess` (vs `None`) is sufficient to make NMS abandon the PlayFab attempt and fall to
single‑player is the open item being confirmed with a coordinated runtime window (see §7).
Titles that gate purely on `XUser` sign‑in / connectivity (rather than a bundled PlayFab
backend) are expected to drop to single‑player directly with this fix.

## 6. Authenticated online — what it honestly requires (stretch)

Honest, user‑authenticated online is **feasible in principle but not reachable without a
real account**:

- A **real MSA account the user owns**, brokered through the existing
  `xodus_get_rps_tickets` local bridge, producing a genuine **XSTS** token for the relying
  party.
- For **NMS multiplayer specifically**, that Xbox token must additionally be **accepted by
  Hello Games' PlayFab title** (PlayFab `LoginWithXbox`‑style exchange). That is Hello
  Games' backend, outside our control; we can only present an honest token.
- No part of this can be synthesised: without a real signed‑in account there is no valid
  token, and fabricating one is prohibited and would be rejected server‑side anyway.

The `XODUS_XBL_ONLINE=1` gate exists precisely to prototype this path honestly when a real
account is available, without disturbing the safe offline default.

## 7. Open items

- Coordinated runtime window to confirm whether honest `LocalAccess` (or `None`) makes NMS
  abandon the PlayFab connect and fall through to single‑player, and to capture the
  `HCHttpCallPerformAsync` target URL / failure for the record.
- If NMS ignores the connectivity hint, the next honest lever is the **wine WinHTTP** path
  (ours): return a prompt connection failure for unreachable Xbox/PlayFab endpoints so the
  title's online login fails fast instead of hanging — to be designed with the coordinator
  as it touches a shared wine DLL.
