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
| S1 | Installed games look and behave like a launcher library | — | **Next (in progress)** |
| S2 | Launch polish: clean quit, no driver-warning stop | S1 not required | **AC2.1, AC2.2 passed**; AC2.3 in S1 batch |
| S3 | One sign-in for launcher and games | — | **Decided: game service owns credentials**; app integration next |
| S4 | Owned PC library | S3 (credential owner) | **Blocked: query accepted but empty for this identity; decision needed** |
| S5 | Install from the app | S3, S4 or explicit product ID | Planned |
| S6 | Update, repair and remove | S5 | Planned |

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

## S2 — Launch polish

- AC2.1 Quitting from the game menu returns the app to **Play** with no error.
  Evidence: observe one normal in-game quit.
- AC2.2 Hogwarts' "Known issues with graphics driver" warning does not block
  launch. Candidate: the existing launch script's documented
  `XODUS_HOGWARTS_DRIVER_WARNING_PROBE=1` engine-setting override. Accept only
  if a real launch reaches the game window without the dialog; otherwise keep
  the dialog and document it.
- AC2.3 Failure messages offer the launch log location written by the script.

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

## S5 — Install from the app

Reuse Xodus's existing streaming download/extract and package readiness
checks. Spec: explicit product + destination + space check + consent, progress
from real bytes, cancel, and registration into Installed on success. Disk space
is limited; real-test only with explicit approval.

## S6 — Update, repair, remove

Remove from Xodus already keeps files. Full uninstall must preserve saves
(`XodusPrivateLocalSaves`) and require confirmation. Update/repair reuse S5.

## Stop rules

- A slice is done only with its real-product evidence, not CI alone.
- Two failed attempts on a slice's runtime evidence: stop and report the exact
  blocker instead of adding scaffolding.
- Do not expand Figma screens, auth architecture or contracts beyond the slice.
