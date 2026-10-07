# Xodus remaining-work plan (spec-driven)

Status date: 7 October 2026. Baseline: installed app `0926414` with the
preserved signed engine. **Proven:** an acquired Xbox PC game (Hogwarts Legacy
1.0.16.0) can be imported and launched from the app's **Play** button through
the user's existing working Xodus launch path; the user confirmed it plays.

Each slice below is a spec: user outcome, acceptance criteria (AC), the
evidence that closes it, and its dependencies. Slices ship one at a time.
Testing uses Hogwarts Legacy only (user direction). Never interrupt a running
game session to test.

## Order and status

| # | Slice | Depends on | Status |
|---|---|---|---|
| S1 | Installed games look and behave like a launcher library | — | **Done** (installed `e401464`, live evidence below) |
| S2 | Launch polish: clean quit, no driver-warning stop | S1 not required | **Done** (AC2.1–AC2.3 passed) |
| S3 | One sign-in for launcher and games | — | **Decided: game service owns credentials**; backend done, app UI in progress |
| S4 | Owned PC library | — | **Done** (13 owned PC games live; follow-ups noted) |
| S5 | Install from the app | S3, S4 or explicit product ID | Backend done and verified; app UI in progress |
| S6 | Update, repair and remove | S5 | Backend done and verified; app UI in progress |

## S1 — Installed games look and behave like a launcher library

User outcome: the Library shows imported games with their real artwork and a
**Continue Playing** card for the game played most recently, matching the
approved Figma Library composition.

- AC1.1 Each installed game shows its own tile artwork, read from the game
  folder's `MicrosoftGame.config` `ShellVisuals` (`Square480x480Logo`, else
  `Square150x150Logo`, else `StoreLogo`). Paths must stay inside the game folder
  (no `..`, no symlinks), be regular PNG/JPEG files ≤ 8 MiB, decode ≤ 16 M
  pixels. Missing/invalid art falls back to the existing symbol, never an error.
- AC1.2 Import also records publisher (`PublisherDisplayName`) when present.
  Existing registry entries without new fields still load.
- AC1.3 The app records `lastPlayedAt` (launch start) and `lastSessionSeconds`
  (process lifetime) for a game it launched, persisted atomically in the same
  private registry. Only sessions launched by Xodus are recorded.
- AC1.4 **Continue Playing**: when at least one installed game has
  `lastPlayedAt`, the Library shows one hero card for the most recent one,
  using its `SplashScreenImage` (same path rules; tile art as fallback), title,
  "Last played …" relative date, and the same Play button/state as the list.
  It appears only for imported, launchable games — never from Xbox activity.
- AC1.5 Copy is product copy only; native SwiftUI and tokens; accessibility
  labels for art (decorative) and the hero card.
- AC1.6 Neutral checks: path containment/symlink refusal, size/pixel bounds,
  fallback order, registry back-compat decode, session recording on exit,
  hero selection ordering.
- Evidence to close: CI, admitted package with preserved engine, installed app
  shows Hogwarts tile art; after one real Play session (user or root, never
  interrupting a live game) Continue Playing shows Hogwarts with its splash.

Result (7 October 2026, installed app `e401464`, CI 37610002150, signed engine
unchanged): a capture of the installed app showed the real Hogwarts 480×480
tile in Installed. After one Play session (game window in about 20 seconds, no
driver warning; ended by a test stop after about 55 seconds) the Library showed
**Continue Playing** with the real 1920×1080 splash, title, publisher
"Warner Bros. Interactive", "Last played 1 min ago", and the shared button. The
test stop's nonzero exit showed the message and **Show log** (AC2.3). The
private registry recorded `lastPlayedAt` and `lastSessionSeconds` and kept
mode 0600. **S1 accepted.**

## S2 — Launch polish

- AC2.1 Quitting from the game menu returns the app to **Play** with no error.
  Evidence: observe one normal in-game quit.
- AC2.2 Hogwarts' "Known issues with graphics driver" warning does not block
  launch. Candidate: the existing launch script's documented
  `XODUS_HOGWARTS_DRIVER_WARNING_PROBE=1` engine-setting override. Accept only
  if a real launch reaches the game window without the dialog; otherwise keep
  the dialog and document it.
- AC2.3 Failure messages offer the launch log location written by the script.
  **Passed** — shipped in S1; verified live (Show log after a nonzero exit).

Results (7 October 2026):

- AC2.1 **passed** — the user quit Hogwarts from the game; the launch script
  exited 0 and Xodus returned to **Play** with no error.
- AC2.2 **passed** — the launch-argument override reached the game but had no
  effect. Adding the standard Unreal Engine setting `r.WarnOfBadDrivers=0`
  under `[SystemSettings]` in the game's own
  `AppData/Local/Hogwarts Legacy/Saved/Config/WinGDK/Engine.ini` (inside the
  HogwartsPrivateStock bottle; original backed up) removed the dialog. Verified
  by a direct launch and then by **Play** in the installed app: full-screen
  game window in about 20 seconds, no warning.

## S3 — One sign-in for launcher and games

Today two separate credential stores exist: the launcher's management account
(app engine) and the Xodus game service's account (used for licensing at
Play). The game service lost its sign-in once already; the user had to sign in
again.

Decision (7 October 2026): **the Xodus game service is the single credential
owner.** Evidence: it already licenses games at Play, it runs as one stable
binary with an approved Keychain grant, and it issued Xbox user tokens to a
local client over its owner-only socket without any Keychain prompt (S4
probe). Next implementation: the app shows the game service's sign-in state
and a "Sign in for games" action that runs the existing Xodus sign-in, and new
account-scoped reads go through the service socket instead of a second
Keychain reader. No credential reset, export or ACL editing.

## S4 — Owned PC library

Source-backed lead: consumer Collections v7 query. Execute one bounded,
read-only query through the S3 credential owner; keep ownership, subscription,
installed and PC-edition facts separate (see `xbox-api-inventory.md`).
Accept only real, paged, account-correct results joined to exact PC SKUs.

Result (7 October 2026, four bounded read-only queries, aggregates only): the
game service issued a user token without any Keychain prompt (`MSA_TOKEN_REQUEST`,
legacy client `000000004424da1f`, full trust). Xbox user auth and XSTS for
`http://xboxlive.com` and `http://mp.microsoft.com/` succeeded with matching
identity. The v7 query was **accepted (HTTP 200) but returned zero items** for
US and GB markets, without beneficiary, and when targeted at Hogwarts
(`9MT5NJ5W7B8Z`) — a product this account demonstrably licensed and played.
Conclusion: this token identity is not authorized to see the consumer
collection. The pinned community implementation states the Store returns the
full library only to Microsoft's first-party web sign-in client
(`1f907974-…`, device-code OAuth). **Stopped per the two-attempt rule.**

Decision needed before more S4 work: allow one additional Microsoft sign-in
through that first-party web client (user approves on microsoft.com/link), used
only for read-only library queries, with the token kept in the existing
Keychain-backed store — or defer the owned list and keep Installed as the
library. No further variants without that decision.

**Decision (user, 7 October 2026): sign in.** Probe result with the
first-party web client (device code, `XboxLive.signin offline_access`, token in
memory only): Xbox user auth `RpsTicket=d=<access>`, XSTS for both audiences,
v7 query market GB, `validityType: All`, `excludeDuplicates: true`. **2 pages,
complete (no continuation), 136 items**: 69 Game, 25 Application, 20 Durable,
18 Pass, others; statuses 129 Active, 5 Expired, 2 Revoked. Hogwarts and No
Man's Sky present. Fields include `productId`, `skuId`, `productKind`,
`status`, `isTrial`, `skuType`, `ownershipType`, `purchasedCountry`.

### S4 acceptance criteria

- AC4.1 Library offers **Sign in to see your PC games**: an in-app device-code
  sheet (code, open microsoft.com/link, cancel). The refresh token is stored
  only in the macOS Keychain (app-owned item); access tokens stay in memory.
  Sign out removes it.
- AC4.2 Fetch: refresh → Xbox user auth → XSTS (`http://xboxlive.com`,
  `http://mp.microsoft.com/`, matching user hash) → v7 collections, following
  continuation to completion (max 20 pages, 4 MiB/page, 30 s each). Partial or
  failed fetches are shown as such, never as an empty library.
- AC4.3 "Your PC games": items with `productKind` Game, `status` Active, not
  trial, joined to public DisplayCatalog metadata (batched by product ID) and
  kept only when a package declares `Windows.Desktop`. Title and box art come
  from the catalog. Console-only and unresolved products are excluded and
  counted in a quiet "not shown" note.
- AC4.4 A game that is also imported shows **Play**; others show
  "Not installed" (install is S5). Installed stays separate from Owned.
- AC4.5 No Xbox activity, Game Pass catalogue or guesses fill this list.
- Evidence to close: installed app signed in by the user, owned PC games
  shown with art, Hogwarts marked installed with working Play.

Result (7 October 2026, installed app `7ec1daa`, CI 37617059339, signed engine
unchanged): the user completed the in-app device-code sign-in. **Your PC
games** shows 13 owned PC titles with real Store box art (including Celeste,
Cuphead, Fortnite, Gears 5, Gears of War 4, Hogwarts Legacy, Lara Croft and the
Temple of Osiris, Minecraft Launcher, Minecraft: Java Edition, No Man's Sky,
Roblox, Subnautica). Hogwarts matches the imported installation and shows the
same Play control verified in S1/S2; the others show "Not installed".
**S4 accepted.** Follow-ups: (a) the Windows Xbox app's Owned view also listed
HITMAN 3, Candy Crush Saga and Minecraft for Windows, which are not in the first
12 visible tiles — check whether they are non-Game kinds, bundle-satisfied
entitlements or console-only, and adjust only with evidence; (b) container
accessibility identifiers override child button identifiers in the Installed
and PC games sections (buttons are reachable but not individually
identifiable).

## S5 — Install from the app

Reuse Xodus's existing streaming download/extract and package readiness
checks. Spec: explicit product + destination + space check + consent, progress
from real bytes, cancel, and registration into Installed on success. Disk space
is limited; real-test only with explicit approval.

### S5 investigation — second title end to end (2026-10-07, user-approved)

Title: Lara Croft and the Temple of Osiris (`C3553MB4P5TT`, MSIXVC, 3.07 GB
resident, GDK, D3D11, Nixxes port). Result: **downloaded, imported in the app,
launched from the app's Play, reached the main menu with the user's Xbox
gamertag signed in, and played in-level at ~119 FPS** (D3D11 → D3DMetal, GPTK
3.0). Continue Playing and the PC games tile ("Not installed" → Play) update.

Install mechanism that worked (no Keychain prompt):
`XODUS_LICENSE_VIA_SERVICE=1 xodus-cli streaming …` — service-brokered package
metadata and keys. Direct CLI Keychain reads can block on a macOS prompt after
the service rotates tokens, so S5 must always use the service path.

Blockers found and fixed, in launch order (each is a per-title or platform
lesson for a generic installer):

1. **Readiness verifier** rejected GDK layouts: named `Registration` chunk and a
   `Movies\PC\*.bik` wildcard. Fixed: skip `Registration`; accept wildcards only
   when every matching file in the authoritative package inventory is resident
   with the exact size (the existing "wildcard cannot certify completeness"
   rule still holds without an inventory). Tests: 8/8.
2. **`xblInitalize is failed.`** modal — our runtime's
   `XSystemGetXboxLiveSandboxId` returned `E_POINTER` when the optional
   `sandboxIdUsed` was NULL. Fixed in `xgameruntime` (`xsystem.c`); deployed
   only to the Lara bottle (Hogwarts keeps its admitted DLL `73a15743`).
3. **`Launcher_NoGpuInstalled`** modal — D3DMetal reports "AMD Compatibility
   Mode" (1002:66AF) while SetupAPI lists the real Apple GPU, so Nixxes could not
   match the active adapter. Fixed per launch with `D3DM_VENDOR_ID`,
   `D3DM_DEVICE_ID` and `D3DM_DEVICE_DESCRIPTION` read from the bottle's active
   PCI display device. D3D11 titles also need
   `CX_GRAPHICS_BACKEND=d3dmetal` in `CX_ENV`; the Hogwarts STA COM flag is not
   needed.
4. **macOS privacy prompts block the game invisibly**: winmm probes the
   microphone ("Allow xodus-cli to access your microphone?") and the bottle's
   Windows Documents symlink to `~/Documents` triggers "Xodus would like to
   access files in your Documents folder". Both were answered Don't Allow.
   Fixed for Lara by giving the bottle a private `Documents` folder. A
   productised installer must create bottles without host-folder symlinks and
   avoid capture-device probes (or explain the prompt) before first launch.

Known limits: the game's online banner ("connection to the network has been
lost") remains — single-player works, multiplayer/leaderboards unverified;
in-game ⌘Q is ignored (SIGTERM ends the session, recorded as code 1).

Implications for S5: per-title data a generic installer must derive (not
hard-code) — executable from `MicrosoftGame.config`, content ID from the
package inventory, D3D level/backend, GPU identity, a fresh per-title bottle,
and the runtime build. Work remaining before an in-app Install button: one
shared generic launcher (replace per-title scripts), bottle provisioning
without host symlinks, the runtime fix promoted to all bottles after a
Hogwarts regression run, and an installer that streams with service
licensing, reports real bytes and registers into Installed.

### S3/S5/S6 backend (7 October 2026, private commit `043045d`)

App-facing entry points in `~/src/xodus-macos-private-ai/scripts/macos/`, all
taking a run ID and writing `processed/<run>.progress.json`, `.result.json`
and `.status` (last) atomically with mode 0600:

- `private-xodus-service-status.sh` → `{serviceRunning, signedIn}` from the
  game service socket (no Keychain read). Verified: signed in.
- `private-xodus-service-signin.sh` → runs the existing Xodus sign-in window,
  restarts the game service, confirms a token.
- `private-xodus-install.sh <run> <productId> <~/Games/Xodus/Name>` →
  service-licensed incremental stream with real byte progress (new
  `XODUS_PROGRESS_FILE` in `xodus-cli streaming`), manifest/StoreId +
  inventory + residency verification, a fresh per-title CrossOver environment
  copied from the host-link-free `XodusGameTemplate`, and a generated launcher
  for the generic `private-xodus-launch.sh`. Codes: 10 space, 11 sign-in, 12
  unsupported package, 13 verification, 14 cancelled. Verified: bad product and
  outside destination (2), cancel (14, no orphaned download), Lara full run
  (0, result written; re-run is the S6 update/repair path).
- `private-xodus-uninstall.sh <run> <storeId> <folder>` → refuses foreign or
  running folders (21/22), copies saves to `XodusRemote/saves/<StoreId>/<time>`
  and verifies them before deleting (20 on failure). Verified on a synthetic
  title: saves kept, folder and environment removed, mismatch refused.
- Generic launch of Lara in the new environment reached D3DMetal rendering
  (first-run Nixxes launcher appears without `-nolauncher`, now a per-title
  argument). Main-menu proof in the fresh environment is pending the in-app
  Play check after the app slice ships.

App UI (S3 sign-in state, S5 Install/progress/cancel, S6 update/uninstall) is
being implemented by the app owner against this contract.

## S6 — Update, repair, remove

Remove from Xodus already keeps files. Full uninstall must preserve saves
(`XodusPrivateLocalSaves`) and require confirmation. Update/repair reuse S5.

## Stop rules

- A slice is done only with its real-product evidence, not CI alone.
- Two failed attempts on a slice's runtime evidence: stop and report the exact
  blocker instead of adding scaffolding.
- Do not expand Figma screens, auth architecture or contracts beyond the slice.
