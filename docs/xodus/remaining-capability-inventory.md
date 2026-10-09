# Xodus remaining work: stopped baseline and triage inventory

Updated: 9 October 2026. The inventory was prepared while implementation was
stopped. **At 17:26 the user authorized analysis and implementation of this
body of work using SDD.** All 43 rows remain in scope; their presence is not a
claim of feasibility or completion.

The inventory itself used read-only checks. The new SDD program implements
dependency-ready slices, records genuine unsupported/human-gated outcomes, and
does not manufacture service capabilities. The Xbox lane's 17:11 foreground hold
remains in force until an explicit release; source work can proceed without it.

**Subsequent user-directed scope correction:** only Exodus macOS UI and
Architrave work may continue. Non-UI backend, engine/composition and broader
service implementation are paused and preserved. The 43 rows remain the
inventory, not permission to resume those lanes. The active UI work includes
three sequential independent adversarial UX reviews with one consolidated
frontend fix batch between rounds.

## Verified baseline

| Item | Verified state |
| --- | --- |
| Installed application | `39fd687adc65398442c76b0f3bdfffa183af7ae9`, exact CI `37854735645` passed. Launcher SHA-256 `da24ce152bff853586f2d8e2299752a653cace19e38a896ac68c5bfb24837ab9`. |
| Latest app source | `fcf8a36`, after `bce4407`: human-readable console states, clearer companion copy and Friends and following label. Source is committed; no tracked app changes remain. |
| Source versus installation | The installed launcher still matches `39fd687`. The later copy changes are **not installed**. CI `37887531029` passed for `bce4407`; no exact `fcf8a36` CI run was found in the latest runs inspected. |
| Private backend | `a3036bf`: authorization failures are no longer converted into “you don't own this game” or Mac-incompatibility verdicts. Eleven focused Python regressions passed earlier; runtime deployed. No obsolete generated unowned verdicts remain on disk. |
| Preserved components | Signed engine C8 `c8fe69a3...`, frozen credential broker `2c40354c...`, game files and saves remain the baseline. Installed registry retains mode `0600`. |
| Native evidence already obtained | Library, Discover/search, detail/media, install review, Downloads and Account from prior delivery; morning Profile, achievement-game list and actual console captures. Real Hogwarts HLS start/pause/cleanup and a successful Lara package check. |
| Acceptance not obtained | Final Engines capture; live dual-access overlap; installed per-game achievement drilldown; exact destination observation after browser Remote Play handoff; complete updated-build walkthrough. |
| Ownership | No active implementation agent is available in the current host. Do not restart or recreate one for this inventory. Xodus remains running; that is not permission to change it. |
| Foreground | Xbox runtime lane requested the Mac GUI at 17:11 for its own headed run. This inventory performs no GUI operations and does not manage that lane's jobs. |

The earlier source review PASS applied to the specific integrated contracts.
It was **not** a certification of every Xbox feature, every original release
requirement, or the latest Library-policy correction.

## Library policy to settle and prove first

The user's correction is recorded as the following personal-library rule:

- **Library / Your Games:** account-held PC entitlement **or** actual current
  PC Game Pass access. A game may show both Owned and Game Pass.
- **Installed:** local installation is a separate fact. It must not silently
  certify access. Imported games without verified access need an explicit
  treatment, rather than being mixed into an access-qualified Your Games count.
  Preserve their files and save data even if eligibility is unavailable.
- **Discover / Store search:** public candidates may appear here without being
  owned. They must be labelled as discovery, not added to the personal Library.
- **Profile / recent activity / achievements:** games played on Xbox or PC can
  appear in these history views without implying ownership or local playability.
- **Unknown or expired access:** do not present as eligible. Keep cached data
  visibly qualified rather than converting a failed read into “not owned.”
- **Owned:** currently means an active non-trial account-held collection entry.
  The response does not establish paid purchase or perpetual rights. Do not
  call it Purchased or invent free/paid acquisition classifications.

**Observed code gap:** `LibraryGame.collection` includes every local installed
record before it joins account entitlements and Game Pass. Therefore local
installation alone can populate the personal grid. It also joins loaded Game
Pass products against an active-status boolean, not an account-authorized exact
edition for every tile. This needs an eligibility decision and tests; changing
badges alone is insufficient.

## Minecraft: what the evidence actually says

The captured tile and install sheet were **Minecraft Launcher**, not a verified
selected playable Minecraft edition. The prior sheet combined an Owned badge
with “You don't own this game and it isn't included in your Game Pass.”

That refusal was invalid as an ownership conclusion:

1. A package-licence failure can be an account, scope, edition, package or
   authorization problem. It does not prove the Library's ownership data false.
2. The PC Library purchasing sign-in and the Xbox game-service account can
   differ. An active Game Pass check does not resolve that difference.
3. Minecraft Launcher, Minecraft for Windows, Java & Bedrock bundle editions and
   console editions must retain distinct product/SKU/package identities.
4. Public Game Pass membership alone does not identify the exact downloadable
   package or establish current account access.

The backend's incorrect blanket wording is fixed at `a3036bf`. **The actual
Minecraft edition/access issue is not resolved.** No new entitlement request,
licence issuance or install was attempted for this inventory. Required future
proof is the selected product/SKU, account-specific access, PC package, support
check and correct review-sheet result. If access is not established, it must not
be offered as a known eligible personal-library item; retain an honest error.

## Priority and status definitions

- **P0:** correctness/trust blockers before claiming a reliable personal Library.
- **P1:** finish and verify already requested product behavior.
- **P2:** broader parity implementation requiring scope/contract triage.
- **P3:** general-release gates and separate-platform acceptance.
- **Missing:** capability is not implemented.
- **Partial:** some functioning behavior exists; named work is still required.
- **Unverified:** code/evidence exists but the specified outcome has not been
  observed. This is not synonymous with a defect.
- **Source only:** committed change is not in the installed application.
- **Deferred:** explicit user decision or separate scope; not silently complete.

Rows below are the accepted program inventory. Work status is tracked on the
dashboard; completion requires the exit evidence, not merely a committed view.
Earlier user deferrals are now queued for feasibility/specification, but real
first-login, fresh-Mac access, new permissions and release credentials still
require the applicable human action.

## Complete remaining-work ledger

### Access, identity and the core personal library

| ID | Priority | State | Remaining work | Exit evidence |
| --- | --- | --- | --- | --- |
| R01 | P0 | Partial | Enforce eligible personal Library; distinguish unknown-access imports from verified Owned/active Game Pass. | Installed-only/no-entitlement, revoked/expired, offline, unknown, public-search and history-only cases cannot become eligible Library entries; existing imports/files/saves remain safe. |
| R02 | P0 | Partial | Resolve Minecraft Launcher versus playable PC editions, SKU/bundle and package applicability. | Exact identity/account access and package check produce a consistent badge and install review for the actual chosen edition. |
| R03 | P0 | Partial | Reconcile PC Store account and Xbox game-service account for install/launch. Preserve authorization failure separately from ownership and Mac support. | Same-account and mismatched-account flows show correct identity/recovery, with no “not owned” inference from a refusal. |
| R04 | P0 | Partial | Finish Game Pass coverage, freshness, expiry/revocation and overlapping access proof. A partial feed's absence is not non-membership. | Known owned-and-Game-Pass title shows both; stale/expired status cannot authorize an install; paging/partial state is explicit. |
| R05 | P1 | Partial | Paid/free/trial/subscription acquisition semantics. Preserve exact collection facts; do not infer payment from Active. | Verified acquisition fields or explicitly unknown classification, including free entitlement, trial and bundle controls. |
| R06 | P1 | Partial | Complete/partial/offline Library refresh behavior and account-change cache isolation. | Failed or incomplete refresh retains qualified last-complete information; account B never sees account A's access as current. |
| R07 | P1 | Partial | Edition selection, market/language, PC-versus-console and DLC/base-game identity. | Exact product/SKU/package is selectable and propagated through detail, review, jobs and registration; names are not identity keys. |

### Installed product acceptance and native UX

| ID | Priority | State | Remaining work | Exit evidence |
| --- | --- | --- | --- | --- |
| R08 | P1 | Source only | Decide whether to ship `bce4407`/`fcf8a36` copy/state fixes, then validate exact final source and install it. | Exact CI, admitted package and installed hash match; console labels and social copy are observed in the installed build. |
| R09 | P1 | Unverified | Final all-view walkthrough: Engines, live dual badge, per-game achievement drilldown, window sizes and keyboard routes. | Own-window installed captures and actual route/actions; no screenshot-harness substitution. |
| R10 | P1 | Unverified | Observe Remote Play's exact default-browser destination and working official setup fallback. | Browser opens publisher's real target; redirect/failure is reported honestly; no per-console guessed link or native streaming claim. |
| R11 | P1 | Partial | Repeat an approved end-to-end install/play/stop/repair/remove/save-restore journey on the final UX after correctness fixes. | Title-scoped runtime evidence and receipts; successful review alone does not count as a completed download/install. |
| R12 | P1 | Partial | Make account/navigation/error copy consumer-facing throughout, and check actionable control prominence and focus return. | No raw service codes, internal spec text, broken-image affordances or misleading enabled actions in every view. |
| R13 | P1 | Unverified | Prove persistent image-cache reuse across app restarts, bounded eviction, offline images and Clear Cache in the actual installed product. | Cold/warm behavior and controlled disk/network observations; unit tests remain separate from product proof. |

### Account, profile, achievements and consoles

| ID | Priority | State | Remaining work | Exit evidence |
| --- | --- | --- | --- | --- |
| R14 | P1 | Partial | Account lifecycle: refresh, failures, token expiry, sign-out semantics and both account scopes. | Account-bound commands cancel/reap on change; stale identity is not shown as current; intended credential owner/sign-out behavior is explicit. |
| R15 | P1 | Partial | Complete achievement loading/paging and partial/error/secret/progress handling for multiple title types. | Installed game selection loads exact title-ID achievements; full/partial/unavailable distinction, unlock date and unknown totals remain correct. |
| R16 | P2 | Missing | Full recent activity pagination and browsable history beyond the bounded first 100 records/current eight visible profile items. | Paging/cursor/error handling; history stays outside ownership and local Continue Playing. |
| R17 | P2 | Partial | Friends versus following versus followers, presence freshness and profile navigation. | Actual relationship fields/counts and privacy behavior; no mutual-friend count inferred from a social list. |
| R18 | P2 | Missing | Profile customization, privacy controls and appear-offline changes or verified official web handoffs. | Correct user-visible scope and consent; no UI-only fake writes. |
| R19 | P2 | Partial | Console-status freshness, account changes, multiple/no-console/error cases and storage presentation. | Actual returned consoles and known-state human labels; failed enumeration is not a fabricated empty list. |
| R20 | P2 | Deferred | Console wake/power, remote install or native streaming. | Separate supported service contract, scoped consent and real outcome; currently only read-only inventory/browser handoff exists. |
| R21 | P2 | Missing | Party, chat, game invitations, LFG and friends-management writes. | Research/contract/permissions first, then genuine interaction and cancellation; not part of the installed baseline. |

### Installation, storage, jobs and updates

| ID | Priority | State | Remaining work | Exit evidence |
| --- | --- | --- | --- | --- |
| R22 | P1 | Partial | Pre-install peak-space plan: exact download, expansion, staging/reserve, destination, dependencies and space lost mid-job. | Clear preflight values or explicit unresolved stage; sufficient-space check re-runs before writes and handles exhaustion safely. |
| R23 | P1 | Partial | Crash/power/app-restart recovery through download, verify, extract and register. | Disposable interruption matrix; no incomplete game registration, duplicate job or deletion of the last runnable version. |
| R24 | P2 | Missing | Pause/resume, queue ordering/concurrency and bandwidth limits. | Negotiated backend support and real persisted jobs; no inactive buttons/sliders. Cancel is already implemented. |
| R25 | P2 | Missing | Automatic/scheduled updates, delta updates and atomic version rollback. | Failed update preserves runnable old version and saves; successful promotion and rollback demonstrated. Repair is not full update-policy parity. |
| R26 | P2 | Missing | Move installed games/drives and storage cleanup. | Validated owned paths, sufficient target space, reversible registry update, no save loss or arbitrary directory removal. |
| R27 | P2 | Missing | DLC/add-ons, optional components, install languages and mods where actually supported. | Exact dependencies/edition/licence semantics, per-component jobs and recovery; unsupported content is labelled, not silently installed. |
| R28 | P1 | Partial | Running-game conflicts, external/imported installations and uninstall/save boundaries. | No repair/remove/migration while active; foreign files protected; default removal preserves saves and recovery is tested. |

### Runtime/engine behavior and online features

| ID | Priority | State | Remaining work | Exit evidence |
| --- | --- | --- | --- | --- |
| R29 | P1 | Unverified | Dedicated Engines page installed acceptance and observed/configured/actually playable component distinction. | Actual page/keyboard evidence; choosing an Experimental preset never claims its runner executed. |
| R30 | P2 | Missing | Engine downloads/removal, redistribution/licensing, per-game runner overrides and safe generation migration. | Supported artifact source/signature/licence, isolated prefix/saves policy and actual runner outcome; current presets are configuration only. |
| R31 | P1 | Partial | Offline launch licence policy and runtime-fingerprint compatibility invalidation. | Offline permitted/refused cases, exact OS/engine/graphics identity and explicit stale support; installed does not equal licensed/playable. |
| R32 | P2 | Deferred | Cloud-save upload/sync/conflicts. Local save preservation is implemented, Xbox sync is not. | Title-specific service authorization and conflict/recovery acceptance without overwriting the only valid save copy. |
| R33 | P2 | Deferred | Title-specific Xbox online/SISU, multiplayer/co-op and publisher services. | Real title session evidence; Store capability metadata is not proof that multiplayer works through Xodus. |
| R34 | P2 | Missing | Cloud gaming entry/handoff and any native streaming. | Cloud versus console Remote Play stays distinct; browser target and account/region/subscription needs verified; native stream needs separate scope. |

### Xbox extras, first use and general release

| ID | Priority | State | Remaining work | Exit evidence |
| --- | --- | --- | --- | --- |
| R35 | P2 | Missing | Game Pass perks/benefits, leaving-soon, expiry, subscription management and offers. | Real source and date/account scope or verified official web link; no perpetual access implied. |
| R36 | P2 | Missing | Checkout, purchases/editions, Rewards, credits, Insider, support and feedback surfaces. | Triage native versus browser delivery; correct destination and consent, no unauthorized payment/account changes. |
| R37 | P2 | Missing | Controller-first launcher navigation, notifications and accessibility/controller settings. | Real navigation/focus/input and permissions; game controller support does not prove launcher navigation support. |
| R38 | P3 | Unverified | Manual VoiceOver, Full Keyboard Access, measured contrast, real Reduce Transparency/Motion and responsive sheet/scroll behavior. | All supported journeys tested with named OS settings and actual assistive technology, not only compilation. |
| R39 | P3 | Partial | Localization/String Catalogs, text expansion and RTL. | Extracted production strings and real pseudolocalized/RTL layout checks; formatted numbers alone are not localization. |
| R40 | P3 | Unverified | Minimum hardware/storage policy and 500-title/cached-load/search performance. | Measured Release behavior on named hardware; no unmeasured <=2 s or p95 guarantees. |
| R41 | P1 | Deferred | Onboarding and first login: new-Mac Xbox/PC gamer, cancellation/expiry and account relationship explanation. | User-led review and first-run acceptance; explicitly deferred, not counted complete. |
| R42 | P3 | Deferred | Second M5 (not Max) Mac acceptance. | Fresh install with no `~/src`, `.local` or hand-installed services; owned/Game Pass and local/browser journeys verified. |
| R43 | P3 | Partial | Packaging/provisioning, notarization, signing/broker update policy, runtime redistribution and release/rollback. | Fresh-Mac package and licence/trust/integrity/rollback evidence; current controlled dev install is not a public release certificate. |

## What does not need to be rebuilt

Keep the working native composition and shared action controls, real artwork,
account-scoped PC collection, backend install/check/Play/Stop/repair/remove
seams, local saves preservation, signed C8 pairing and frozen credential broker.
Keep the implemented dual-access presentation, real companion readers/native
pages and image-cache implementation. The remaining work is selective
correctness, acceptance and deliberately triaged features—not replacing the
launcher again.

## SDD execution order

1. Resolve R01-R04 together: personal Library inclusion, Minecraft exact identity,
   account scope and Game Pass freshness/coverage.
2. Decide/install the already committed copy fixes and complete R08-R12/R29,
   keeping one final source/package/acceptance boundary.
3. Do the deferred onboarding/first-login review with the user.
4. Prioritize jobs/updates/storage and social/account extras from the inventory.
5. Finish release-quality gates and perform the second-M5 acceptance.

No reliable estimate for all 43 rows follows from the previous six-hour budget.
Keep that historical grant; estimate the chosen next batch only after triage.

## Frozen first slice: eligible Library and truthful package access

Owner: one app implementer. Mutable scope: app source, directly related tests
and specifications. Root owns the private backend and the inventory/dashboard.
No packaging, signing, app replacement or GUI use before this slice's code
checks and independent review; no new authentication or uncertain licence retry.

**Objective:** Your Games and its hero/counts must not promote public catalogue
candidates or installation alone into personal access. Existing local games,
launchers and saves remain preserved and reachable as explicitly local records.

| Acceptance | Required behavior |
| --- | --- |
| SDD-LIB-01 | A current account-held PC entitlement is eligible whether installed or not. No assumption that it was purchased for money. |
| SDD-LIB-02 | A PC Game Pass product with current active-access evidence is eligible even if not owned. Owned and Game Pass remain independent and may coexist. |
| SDD-LIB-03 | Public-only, console-only, inactive/trial-only, expired/revoked and unresolved access do not enter the access-qualified personal grid or inflate its count. |
| SDD-LIB-04 | A locally installed/imported game with unverified current access remains a preserved local record. Expose it separately with an access warning, not as Owned or an eligible featured game. Do not delete files or saves. |
| SDD-LIB-05 | Personal search, sorting, hero and Continue Playing use the same eligibility decision. A public Store search result may remain in Discover, clearly distinct from Your Games. |
| SDD-LIB-06 | Partial Game Pass discovery does not establish non-membership. Failed/partial access refresh is not proof that the user has no games. Show qualified stale/partial/error state and preserve safe cache/account boundaries. |
| SDD-LIB-07 | Package-authorization failure preserves the known collection fact and is distinct from package incompatibility. Explain Store/game-account or edition resolution, never infer “not owned” from a licence refusal. |
| SDD-LIB-08 | Minecraft Launcher versus actual playable editions remains unresolved until exact product/SKU/package facts are traced. No title-name matching, invented ownership or broad new sign-in/licence experiments. |

Focused regressions cover owned-only, Game-Pass-only, both, public-only,
installed-without-access, console-only, account change, stale/partial feed,
revocation and package refusal. Tests must reach the live presentation/model
seam, not a disconnected test-only eligibility helper.

The first slice can correct Library classification before every broader feature
is feasible. It must not claim complete Game Pass enumeration, paid/free
acquisition proof, fresh-Mac distribution or all 43 rows done.

### First independent identity observation

Bounded public DisplayCatalog reads, joined only to existing title/product
metadata, confirmed:

| Product ID | Observed catalogue identity | PC applicability observed |
| --- | --- | --- |
| `9PGW18NPBZV5` | Minecraft Launcher; Xbox title `1794566092` | Windows.Desktop MSIXVC SKUs `0010`, `0017`, `0011` |
| `9NBLGGH2JHXJ` | Minecraft for Windows; Xbox title `896928775` | Windows.Desktop MSIXVC SKUs `0011`, `0017`, `0010` |
| `BZ8MZF8444Z5` | Minecraft: Windows 10 Edition Beta | No Windows.Desktop SKU in the inspected response |

This establishes distinct identities, not ownership, Game Pass membership,
licence access or Mac playability. No credential exchange, licence issuance,
game download or GUI operation was used for the observation.

### Program traceability

Canonical program: `xodus-sdd-program-20261009`, created through the repository
runtime API with all 43 inventory acceptance rows. First active task is
`eligible-library`; root's independent `minecraft-identity` work stays read-only.
Subsequent dependency tasks are `acceptance-account-ux`,
`jobs-storage-updates`, `runtime-online-services`, `social-xbox-extras` and
`release-onboarding`. The rows remain the detailed acceptance authority; task
registration does not declare any feature implemented.

## Evidence and related ledgers

- [Original expanded parity plan](xbox-parity-plan.md).
- [Independent review, research sources and integrated follow-up](xbox-parity-review.md).
- [Historical working-title evidence](remaining-work-plan.md).
- App original `docs/REQUIREMENTS.md`: AUTH/LIB/FIND/ID/DETAIL/COMPAT/PLAN/QUEUE/
  PLAY/UPDATE/REMOVE/SETTINGS/RUNTIME/PARITY and quality/release gates.
- Inspected current app sources: `LibraryPresentation.swift`,
  `LiveLibraryView.swift`, `PCGamesProtocol.swift`, `XboxCompanionViews.swift`;
  HEAD `fcf8a36`, installed baseline `39fd687`.
- Private runtime and current package receipts were inspected read-only. Real
  account details/caches, screenshot assets and tokens are not committed here.

This inventory is complete for the currently documented scope; it is not a
promise that every Xbox service has been exposed or that undocumented/future
Xbox features have been exhaustively discovered.
