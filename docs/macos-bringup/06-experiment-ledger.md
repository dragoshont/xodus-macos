# macOS Hogwarts Game Pass experiment ledger

Last updated: 2026-10-01 10:32 EEST

This is the durable handoff journal for the Hogwarts Legacy PC Game Pass
bring-up on Apple Silicon. Update it after every experiment that changes the
highest proven milestone, eliminates a hypothesis, or identifies a new
blocker. Do not record credentials, tokens, license payloads, CDN URLs, or
proprietary runtime traces.

## Current position

**Highest proven end-to-end point:** the WinGDK executable starts through the
decrypted macOS package overlay, private AF_UNIX-capable CrossOver Wine, and
D3DMetal. Public xgameruntime code creates the required task queue and executes
the asynchronous `XUserAddAsync(AddDefaultUserSilently)` provider.

**Current blocker:** the public XUser implementation reaches
`BCryptGenerateKeyPair`, but Wine returns `STATUS_NOT_IMPLEMENTED` because
`bcrypt.dll` has a null Unix-library handle. An isolated RSA smoke test
reproduces the same failure outside the game:

```text
BCryptOpenAlgorithmProvider(RSA): 0x00000000
BCryptGenerateKeyPair(2048):      0xc0000002
bcrypt:key_asymmetric_create no encryption support
```

The game currently retries initialization instead of reaching sustained
interactive startup. No game launch is intentionally left running.

## Status ledger

| Area | Status | Evidence | How it was addressed |
| --- | --- | --- | --- |
| Remote Mac control | Complete | SSH alias `xodus-mac`; Aqua LaunchAgent requests succeed | Installed a dedicated SSH key and allowlisted `com.xodus.remote-launch` actions. |
| Native Xodus build | Complete | Format, Clippy, tests, and release build pass | Installed the macOS/Rust toolchain and recorded reproducible bootstrap and doctor scripts. |
| Keychain access | Complete | Xodus login and unattended package operations work | Moved Keychain-dependent commands into the logged-in Aqua namespace; user approved **Always Allow**. |
| Graphics control | Complete | Steam Hogwarts reaches shader preparation under D3DMetal | Installed CrossOver 26.3 and selected `CX_GRAPHICS_BACKEND=d3dmetal`. |
| Performance overlay | Complete | Persistent bottle settings | Enabled `MTL_HUD_ENABLED=1` and `DXVK_HUD=fps,frametimes,gpuload,memory`. |
| UK entitlement | Complete | Product `9MT5NJ5W7B8Z`, market `GB` | Kept login locale separate from catalog market and passed `--market GB`. |
| Package extraction | Complete | 541 files, 113 directories, approximately 93.7 GiB | Streamed and extracted the licensed MSIXVC package with eight parallel jobs. |
| Encrypted executable preparation | Complete | WinGDK executable starts from a prepared path | Added a macOS RAM-disk package overlay containing decrypted PE files at their real relative paths and symlinks for unencrypted content. |
| Stock CrossOver AF_UNIX | Blocked by upstream capability | Winsock error `10047` | Confirmed stock CrossOver lacks Windows AF_UNIX; did not add application workarounds. |
| Private Wine AF_UNIX | Complete | `AF_UNIX_PRIVATE_WINE_OK` | Applied public Xodus Wine AF_UNIX commit `6b7313c1bd` and public file-map commit `183d5d90b6` to matching CrossOver source, then built a coherent x86_64 loader/server/runtime. |
| First GDK interface identification | Complete | GUID `{073b7dcb-1fcf-4030-94be-e3c9eb623428}` | Identified it from public Xodus IDL/docs as `XThreadingImpl`. |
| Initial task queue | Complete in private experiment | `XTaskQueueCreate(SerializedThreadPool, SerializedThreadPool)` succeeds | Replaced the in-tree stub with the public PR #18 C++ XThreading implementation. |
| PR #18 strict build | Locally repaired | MinGW build succeeds | Corrected the public AF_UNIX socket global from `HANDLE` to the Winsock type `SOCKET`; this is transport/type correctness, not GDK semantics. |
| XUser dispatch | Complete in private experiment | `XUserAddAsync`, provider begin/work/result callbacks execute | Built public branch `origin/xuser` at `44d97de` and delegated its internal XThreading queries to the public PR #18 runtime. |
| XUser credential initialization | Blocked | RSA key generation returns `0xc0000002` | Narrowed the failure to Wine bcrypt Unix-library initialization with an isolated smoke executable. |
| Sustained Game Pass startup | Not complete | Initialization retries | Depends on restoring asymmetric bcrypt support, then observing the next public API boundary. |

## Experiment journal

### 2026-09-30: host and control plane

- Configured the M5 Max Mac as the authoritative execution host.
- Installed Apple Command Line Tools, Homebrew, Rust, build tools, Edge,
  Playwright, CrossOver, MinGW, Wine build dependencies, Vulkan/MoltenVK, and
  diagnostic tools.
- Added SSH, Aqua LaunchAgent, service, GUI-launch, bootstrap, doctor, and
  status tooling.
- Verified native Xodus workspace checks.
- **Result:** the host can be controlled remotely without moving
  Keychain-dependent work into a non-GUI SSH namespace.

### 2026-09-30: Windows graphics control

- Installed Windows Steam in a dedicated CrossOver bottle.
- Diagnosed Steam's update behavior and session conflict with native macOS
  Steam.
- Downloaded and launched the owned Windows Hogwarts Legacy Steam build.
- Confirmed D3DMetal and visible shader preparation.
- **Result:** the hardware, CrossOver bottle, and D3DMetal path are viable
  independently of Game Pass runtime integration.

### 2026-10-01: UK Game Pass package

- Completed Xodus Microsoft login and permanent Keychain approval.
- Queried the UK catalog and selected the entitled x64 MSIXVC base package.
- Acquired the license and completed resumable extraction to
  `~/Games/Xodus/HogwartsLegacy-Xbox`.
- **Result:** package download, license acquisition, and extraction are no
  longer blockers.

### 2026-10-01: macOS decrypted package overlay

- Stock CrossOver does not implement Xodus Wine's `WINE_DLL_FILE_MAP`.
- Added a macOS-only RAM-disk staging overlay:
  - decrypted encrypted PE files are written at their actual package-relative
    paths;
  - unencrypted files are symlinked from the extracted package;
  - the staged executable is passed to ordinary Wine;
  - early executable-selection failure now cleans up the RAM disk.
- **Result:** the Game Pass WinGDK executable creates a Wine window with
  D3DMetal loaded.
- **Remaining cleanup risk:** if the Wine child becomes orphaned, process and
  RAM-disk cleanup may still require explicit termination.

### 2026-10-01: AF_UNIX-capable CrossOver Wine

- Confirmed stock CrossOver returns `WSAEAFNOSUPPORT` for Windows AF_UNIX.
- Applied public Xodus Wine AF_UNIX and file-map commits to official CrossOver
  26.3.0 source.
- Built a coherent private x86_64 Wine tree rather than mixing a patched
  `ws2_32` into the stock runtime.
- Added `scripts/macos/private-xodus-crossover-wine.sh` to preserve the
  CrossOver bottle and GPTK/D3DMetal environment.
- **Result:** direct Windows-to-native `/tmp/xodus.sock` transport succeeds.

### 2026-10-01: XThreading diagnosis

- A narrow `+xgameruntime,+gdkc` trace identified the first failed call:

  ```text
  XTaskQueueCreate(work=SerializedThreadPool,
                   completion=SerializedThreadPool) -> E_NOTIMPL
  ```

- Public PR #18 commit `8dd2aa0` contains the complete C++ XThreading/task
  queue implementation but declares its Winsock socket as `HANDLE`.
- Changed that declaration to `SOCKET` in the private external build tree and
  rebuilt successfully with strict MinGW.
- **Result:** Hogwarts passes task-queue creation.

### 2026-10-01: composite public runtime

- PR #18 implements XThreading but not XUser.
- The Wine in-tree runtime exposes XUser but initially contains stubs for both
  `XUserAddAsync` and XAsync.
- Public branch `origin/xuser` at `44d97de` implements asynchronous XUser
  addition, while public PR #18 provides the required task queue and XAsync
  machinery.
- Built a private composition:
  - primary `xgameruntime.dll`: PR #18 XThreading;
  - fallback `xgameruntime_legacy.dll`: public `origin/xuser`;
  - unknown primary interfaces delegate to the fallback;
  - fallback XThreading queries delegate back to the primary.
- **Result:** provider operations `Begin`, `DoWork`, `Cleanup`, and
  `XUserAddResult` execute. This is the furthest runtime progress so far.

### 2026-10-01: bcrypt blocker

- `user_Initialize` fails while creating its asymmetric key.
- Added CrossOver's bundled x86_64 `libgnutls.30.dylib` directory to
  `DYLD_FALLBACK_LIBRARY_PATH`.
- Added the private Wine `bcrypt` and `ws2_32` Unix-library directories to
  `WINEDLLPATH`.
- Installed a matching private `bcrypt.dll` in the experimental bottle, with
  the original retained as `bcrypt.dll.before-xodus-private`.
- Built and ran a minimal BCrypt RSA smoke executable under the exact private
  wrapper.
- **Result:** all three approaches reproduce the same
  `__wine_unixlib_handle == 0` failure. GnuTLS itself can be loaded under
  Rosetta, so the remaining issue is the Wine builtin PE-to-Unix companion
  path, not missing cryptographic libraries.

## Current private experimental composition

These files are intentionally not proposed for upstreaming:

```text
Primary runtime:
  ~/src/xgameruntime-pr18/build/windows-x64/bin/xgameruntime.dll

Fallback runtime:
  ~/src/build/xodus-wine-macos-x86_64/dlls/xgameruntime/
    x86_64-windows/xgameruntime.dll

Bottle installation:
  .../drive_c/windows/system32/xgameruntime.dll
  .../drive_c/windows/system32/xgameruntime_legacy.dll

Private CrossOver Wine:
  ~/src/build/crossover-wine-xodus-full-26.3.0-x86_64
```

Public provenance used:

| Component | Revision |
| --- | --- |
| xgameruntime PR #18 | `8dd2aa0` |
| xgameruntime `oot-cpp` | `c5e6ac1` |
| xgameruntime `xuser` | `44d97de` |
| xgameruntime `xasync` reference | `bfecf58` |
| xgameruntime `xtaskqueue` reference | `90ba727` |
| Xodus Wine AF_UNIX | `6b7313c1bd` |
| Xodus Wine file map | `183d5d90b6` |
| CrossOver source archive SHA-256 | `ac99c8ca4b3848f3e81784135f023df266b61c2345726ea55a50b3e030dd6872` |

## Next actions

1. Correct the private Wine builtin Unix-companion lookup for
   `bcrypt.dll` and rerun the isolated RSA smoke test.
2. Do not launch Hogwarts again until
   `BCryptGenerateKeyPair(2048)` succeeds in isolation.
3. Once the smoke test passes, rerun with only
   `+xgameruntime,+gdkc` and record the next failing public API.
4. Keep all GDK semantic behavior sourced from existing public code. If the
   next requirement has no public implementation, stop and prepare a human
   clean-room implementation brief.
5. Keep the Mac checkout authoritative and update this ledger after each
   milestone or eliminated hypothesis.

## Operational notes

- Market must remain `GB`.
- Keychain-dependent Xodus operations must run in the Aqua LaunchAgent.
- Temporary passwordless sudo remains enabled until the user explicitly asks
  for removal.
- The CrossOver bottle retains the legacy name `GroundedControl` for script
  compatibility.
- Never enable token-bearing or proprietary Microsoft runtime traces.
