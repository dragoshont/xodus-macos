# Xbox parity review and research findings

9 October 2026. Baseline: app `1d721c2`; independent review observed successor
`c12a89d` after the atomic detail/trailer reliability fix. This review is not a
claim that the new parity features were already installed.

**Current status:** implementation is stopped by the user. See
[remaining-work inventory](remaining-capability-inventory.md) for the verified
installed `39fd687` versus committed `fcf8a36` baseline and the reopened
Library/Minecraft correctness issues. Earlier source PASS is narrow and does
not close those new acceptance requirements.

## Verdict: REVISE

The existing local launcher is functional. It is not yet a complete Xbox
companion: the full profile, achievement list, console list and discoverable
Engines page are missing. These are newly approved scope, not features that
automated test counts can certify as shipped.

| Finding | Severity | Evidence and action |
| --- | --- | --- |
| Owned suppresses Game Pass | Blocker for this request | `LibraryPresentation.swift`, `LibraryAccess.init`: Owned-first/else-Game-Pass presentation loses a true second fact. Render both independently on all views. |
| Collection acquisition semantics are incomplete | Major | `PCGamesProtocol.swift`, `PCGamesCollectionPage.Item.isCandidate`: Active/non-trial is retained but acquisition kind is not. Do not imply paid purchase or perpetual rights. A retained free account grant can legitimately be in the collection; distinguish that from subscription/trial where evidence exists. |
| Profile, full achievements and consoles absent | Blocker for this request | Existing stats sidecar is not a profile or achievement-detail page. Add real account-scoped views, not populated-looking placeholders. |
| Engines is buried | Major | Runtime configuration is in Settings; navigation exposes only the three game tabs. Add an explicit native destination reusing the existing runtime seam. |
| Install verification reason collapses into code 13 | Major | `GameScriptContract.swift`: one generic message covers multiple causes. Preserve backend step/reason and meaningful recovery without inventing a hash failure. |
| Original release criteria lack full evidence | Major for a general release | Proposed original requirements include durable queue/recovery, atomic update rollback, stale/partial inventory, accessibility, localization, performance and fresh-install distribution. Map each separately; do not equate tests or one dev-Mac journey with release qualification. |

The independent reviewer is a host-native read-only reviewer, separate from
implementation. It inspected the actual source, original specs and existing
Dark/Light screenshots. It did not launch the app or claim manual VoiceOver,
full network behavior or new-feature runtime evidence.

## Reliability closure retained in the parity batch

`c12a89d`, exact CI `37849893052`, fixes:

- Same-base-language fallback: Celeste's `en-gl` can satisfy `en-US`; unrelated
  languages remain rejected.
- One unsupported optional 9 Kings trailer no longer rejects valid metadata.
- Malformed optional text/hardware fields omit only that field.
- Actual Hogwarts playback exposed a SwiftUI `VideoPlayer` Release crash.
  Native `AVPlayerView` hosts the existing player instead; explicit playback and
  pause/close cleanup remain.

This commit does not fix the separate 9 Kings package-check failure by itself.
Post-fix actual playback, installed metadata and supported-title consent
acceptance are still required before closing the parity build.

## Source-backed research

| Source | Verified meaning | Limits |
| --- | --- | --- |
| [Xbox app for PC](https://www.xbox.com/en-US/apps/xbox-app-on-pc) | Official reachable product reference for the Xbox PC experience | Marketing surface is not an API or end-to-end feature test. |
| [Xbox Game Pass](https://www.xbox.com/en-US/xbox-game-pass) | Official subscription/catalogue reference | Catalogue membership is not ownership or account entitlement. |
| [Xbox Remote Play](https://www.xbox.com/en-US/consoles/remote-play) | Page content states console streaming; inspected PLAY NOW anchor points to `https://www.xbox.com/remoteplay` | The canonical CTA currently responds 301 without Location to our command-line client. Use the publisher's exact link plus working setup/help alternatives; do not certify browser playback from that HTTP probe. |
| [Remote-play setup](https://support.xbox.com/help/games-apps/game-setup-and-play/how-to-set-up-remote-play) | Official setup destination directly linked from the Remote Play page | No invented console-ID URL or native/wake command. |
| [Remote-play troubleshooting](https://support.xbox.com/help/games-apps/troubleshooting/troubleshoot-remote-play-gaming) | Official support destination linked from the same page | Help handoff is not successful streaming. |
| [Xbox browser play](https://www.xbox.com/en-US/play) | Official reachable cloud-gaming destination | Cloud gaming is not remote play from the user's console. |
| [Heroic features](https://heroicgameslauncher.com/docs/features) | Public runtime/Wine-manager and per-game settings reference | Reuse the functional idea, not cross-platform chrome, unsupported runner execution or unlicensed redistribution. |
| [Greenlight console seam](https://github.com/unknownskl/greenlight/blob/main-v2/packages/desktop/main/ipc/consoles.ts) | Existing public client calls Smartglass console enumeration | Community source, not an official SDK or stability guarantee. Do not adopt its auth/token logging. |
| [Pinned Xbox WebAPI console implementation](https://github.com/unknownskl/xbox-webapi-node/blob/6ce18b630b66702bafe64b3bb8af7adf4eb1cf11/src/provider/smartglass.ts) | Read-only console list request shape, contract 4 and RemoteManagement header | Power/command methods are not implemented or invoked by Xodus. |
| [Pinned profile implementation](https://github.com/unknownskl/xbox-webapi-node/blob/6ce18b630b66702bafe64b3bb8af7adf4eb1cf11/src/provider/profile.ts) | Actual profile settings request and field names | Availability and privacy must be verified for the signed-in user. |
| [Pinned achievements implementation](https://github.com/unknownskl/xbox-webapi-node/blob/6ce18b630b66702bafe64b3bb8af7adf4eb1cf11/src/provider/achievements.ts) | Account/title-scoped achievement history and continuation shape | A route is not proof of permission, completeness or arbitrary-account access. |

Official support routes for achievements, Game Pass, storage/downloads,
profile/privacy, parties and accessibility were reachable, but many are
JavaScript-rendered pages. HTTP 200 alone is **not** content verification; their
slugs do not establish exact current behavior. No feature was declared absent
merely because that research pass did not find a help article. In particular,
repair/verify, offline and add-on behavior remain comparison/triage items, not
automatically "out of scope."

Community implementations being unsupported does not, by itself, establish that
a read is forbidden or requires scraped tokens. Xodus's bounded live probes use
the existing credential owner and keep secrets in memory; no community auth
client is imported. Availability is recorded per section rather than inferred.

## Live capability results

Private backend commit `8cfcbaf`, 7 parser regressions and 6 account-cache
regressions passed. No raw account data is stored in tracked source.

| Operation | Actual result | Boundary |
| --- | --- | --- |
| Profile | Valid gamertag, display name, avatar and gamerscore | Bio/location absent; hide missing fields rather than fill sample text. |
| Console list | One actual console returned | Stable local console IDs are hashed. No wake, shutdown, install or stream commands. |
| Friends | Four returned | No chat/party/invite mutation. Following/follower totals are not guessed. |
| Recent activity | 100 bounded records | Activity includes console titles and is separate from PC ownership; this first page is not claimed to be complete history. |
| Hogwarts achievements | 45 entries, 1 earned, complete pagination | Exact Xbox title ID; Store/name identity is not inferred. |

`xbox-companion` and `xbox-achievements` caches, receipts and current service
status share a server-derived opaque account hash. Before publishing a result,
the backend re-resolves current account identity; late account-switch results
are discarded. Files are 0600, cache directory 0700. Sign-out/account changes
clear account-scoped caches. Section failures are explicitly unavailable with
step/HTTP reason, never success-shaped empty arrays.

## Original-spec coverage still needing evidence or tomorrow triage

| Requirement | Current evidence / gap |
| --- | --- |
| AUTH-01/02 account lifecycle | Working sign-in and account-bound caches; store/player mismatch and full cancelled/expired first-login journey need user-led review. |
| LIB-01/02/03 inventory/access | Real PC collection and feed; fix dual badges. Complete paid/free/subscription/partial/offline acquisition semantics require exact service facts. |
| FIND-01/ID-01 discovery/editions | Owned-first real search verified; full editions, locale, package applicability and continuation remain explicitly scoped. |
| COMPAT-01 | Backend supported-package checks and known working titles; catalogue features are not Mac compatibility or universal runtime certification. |
| PLAN-01 | Native consent and exact package-check size exist; dependency/DLC/language peak-space and invalid-package reasons require independent rows. |
| QUEUE-01/02 | Cancel/progress implemented; pause intentionally absent. Crash/power recovery and multiple-job durability need broader evidence. |
| UPDATE-01/REMOVE-01 | Repair/save-preserving uninstall proven; delta updates, automatic scheduling and atomic version rollback are not claimed. |
| SETTINGS/RUNTIME | Existing presets are reused in Engines; installed components, selected config and actual playable runner stay distinct. |
| A11Y/LAYOUT/I18N | Native controls and automated checks exist; full manual VoiceOver, RTL, string-catalog coverage and contrast thresholds are not certified by compilation. |
| OFFLINE/PERF/RELEASE | Cached artwork/library are useful; offline licences, 500-title timing, notarization and fresh-Mac distribution remain separate acceptance. |

## Tomorrow triage: not silently added or claimed

- Profile customization/privacy, presence changes, follower counts and full
  activity pagination.
- Friends management, messaging, party audio, invitations and LFG.
- Game Pass benefits/perks, leaving-soon/expiry, upgrade/manage subscription.
- Purchases, editions, add-ons/DLC, optional content, install language and mods.
- Download pause/resume, ordering/concurrency, bandwidth/schedules, moving
  storage and automatic updates/rollback.
- Cloud gaming browser entry versus native streaming, controller navigation,
  notifications and controller/accessibility settings.
- Rewards, offers/credits, Insider, support and feedback browser handoffs.
- Cloud-save upload/conflict handling and title-specific online/SISU support.
- Engine downloads/removal, per-game runner migration and redistribution rights.
- Full onboarding/first login and second-M5 acceptance, explicitly deferred by
  the user.

## Integrated follow-up: source PASS, visual acceptance held

App `39fd687adc65398442c76b0f3bdfffa183af7ae9`, exact CI `37854735645`,
is pushed and installed. A separate bounded host-native integration review
returned **PASS** for resolving the specific REVISE findings and named
contracts, not for every original release gate or every Xbox feature.

- Both access badges render on all live surfaces; overlapping filters pass.
  Acquisition kind remains explicitly unknown; Owned does not mean paid purchase.
- Native Profile, Achievements, My Consoles and Engines are reachable from the
  Account hub and menu/keyboard commands. Library/Discover/Downloads remain the
  primary tabs. Actual Xbox game-service identity is distinct from PC Store
  sign-in.
- Current account hashes, section failures, partial achievement results and
  late-result invalidation are enforced. Activity is not used as ownership.
- Exact Xbox avatar/icon origin is guarded. No console wake/power actions,
  guessed console deep links or unsupported engine execution controls exist.
- Specific safe backend failure-progress messages reach the install UI.

Runtime evidence: Hogwarts HLS reached advancing playback at 0.341709 seconds,
paused at rate zero and released its player item. Lara's package check returned
supported with 3,226,315,134 bytes; no repair/download was confirmed. Private
companion reads returned the profile, one console, four friends, 100 recent
activity records and 45 complete Hogwarts achievements.

Root independently verified the exact CI SHA, strict signatures and all
22 installed files against the package receipt
`eba5c729f2ba85e92ce019cb8290287005b6c1353b7c65b6b6e5d9a1e360c499`.
C8 `c8fe69a3...` and broker `2c40354c...` remain byte-preserved. Evidence is
under `~/xodus-app-tooling/app-parity-39fd687-20261009/`, including
`root-independent-admission.json` and `runtime-receipt.json`.

**Remaining P7 gate:** macOS independently reports
`CGSSessionScreenIsLocked = true`, and the app has zero AX-visible windows.
Do not bypass the lock or claim screenshots of Profile/Consoles/Engines that
were not taken. The installed app is running on its default Library route;
the all-view native walkthrough and manual accessibility check await the user
unlocking the Mac. This is a human visibility gate, not another implementation
or signing hold.
