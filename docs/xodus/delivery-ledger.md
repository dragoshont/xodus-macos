# Xbox-style macOS application delivery ledger

Updated: October 6, 2026. **The complete application is not finished.**

## Delivery outcome

Deliver a native SwiftUI/AppKit Xbox-style launcher, using the approved Figma
screens for composition and Exodus for real Microsoft/Xbox/Store operations.
Use the saved authentication context. Shipping screens must contain real
service/local results, not demo titles, simulated progress, internal reasoning,
verification terminology or a project roadmap.

The canonical controller remains
`xodus-ui-production-20261005`, objective version **2**, updated through the
public runtime for the user's explicit `/architrave` request. Existing Run
history is preserved; superseded tasks are deferred, not silently completed.
This tracked document is the human-readable delivery ledger, **not canonical
Run state** and not a substitute for registered gate evidence.

Two existing implementation owners remain:

| Owner | Responsibility |
| --- | --- |
| Xodus launcher management, `dragoshont/xodus-macos` | Provider, credentials, management contracts, staging, installation and runtime seams |
| Xodus app foundation, `dragoshont/xodus-macos-app` | Native screens, interactions, image loading, packaging and Mac execution |
| This coordinator | Ordered acceptance, scoped policy, source/artifact review, ledger and integration decisions |

No new launcher architecture, credentials broker, agent harness, UI framework
or parallel implementation team is planned.

## Status vocabulary

- **Observed:** a bounded real product/service result was seen.
- **Implemented:** source and targeted checks exist; real product acceptance
  may still be missing.
- **Active:** current dependency-ready delivery work.
- **Pending:** required work not yet implemented or qualified.
- **External:** a specific provider, selected-game, license, permission or
  human-input dependency exists. This must name the missing fact.
- **Deferred:** optional scope deliberately excluded from the first usable slice.

An observed row is not automatically canonical `PASS`. The canonical
acceptance matrix currently requires fresh, task-bound gate registration;
historical compiled artifacts, session reports and source tests do not
manufacture that registration.

## Current real baseline

| Fact | Actual evidence and limit |
| --- | --- |
| Microsoft session persistence | Reference app `1759a61` / engine `397dd02` saved a real login and read it after restart. No raw credentials were exported. |
| Authenticated Xbox package service | App `f30f1b18` / engine `d00a8b97` returned `credentialPresent` and one Halo `auth.verify` returned exact `verified:true`. This was not a license, ownership or gameplay check. |
| Current updated-engine saved access | Installed app `113be57c` / engine `c1073100` returned current `credentialPresent`, retaining the `2b31d199` foreground interaction correction. No new Microsoft login or credential reset was used. |
| Actual personal Xbox service | One `library.recent` read on the corrected pair returned **20 real titles**, live/partial, with reported platforms and no guessed Store product mapping. |
| Real personal artwork | All 20 titles now have available artwork; zero references were rejected. One preload batch succeeded for 20 references, with zero failures. References can share the cache; this is not a claim of 20 distinct HTTP requests. |
| Qualified artwork correction | Engine `c1073100` safely converts exact Store HTTP image metadata to HTTPS; nine focused native checks passed. Its exact sealed Release/provenance and 205 Git-LF inputs were checked. Matching app CI passed; package and installed inventories/signatures matched. The one actual changed Library PNG was retrieved and visually reviewed, with real tile artwork and a tile-based feature, not an invented marketing hero. |
| Real public artwork | Installed artwork code loaded Halo Infinite and Gears 5 covers plus Halo hero artwork from actual Store metadata: three successful loads, zero failures. Discover/Product own-view PNGs were visually reviewed. |
| Current working package | App `113be57c7ceba230bc9252585bd98e4ce1679fa4`, engine `c1073100ce8936a751b962b1401ddb641c8be38a`; installed CLI SHA256 `c8fe69a3bc2b6a84c5bad5ef0c1ae041f36466a87419c0f17d9567c6df3ed14d`. All 21 installed files match the admitted receipt; shipping is reopened with one owned engine and no active login helper. The complete working `4e` rollback and saved profile are preserved. |
| Native read correction | Foreground reads explicitly enable native interaction under the existing mutex, then restore the prior setting. Background/verification reads remain deliberately noninteractive. Fixed failed-read diagnostics contain no credentials or account identifiers. |
| Signing limit | The local signer works, but identical self-signed designated requirements do not stabilize macOS's independent per-binary partition. A future engine can need native permission again. No prompt-free rebuild guarantee is claimed. |
| Installation inventory | Shipping `installed.snapshot` remains an empty placeholder. Real read-only StagingStore registry wiring is active source work, with nullable runtime identity and unverified health rather than invented verification. No OS-wide scan or installed game is claimed. |
| Historical CrossOver controls | Existing No Man's Sky/Hogwarts user confirmations and a Hollow Knight menu result are preserved references, not proof this launcher installs or launches them. |

Personal history is intentionally memory-only. The development render session
does not inject its history into a newly reopened shipping session. In the
shipping app, loading Recently played is an explicit action after saved status
is confirmed.

Private results remain in ignored session/Mac evidence locations:

- `app-native-permission-f30/nativepermission-saved-status-and-halo-read-result.json`
- `app-foreground-read-4e20427/personal-history-confirmation/actual-final-proof.json`
- `app-foreground-read-4e20427/personal-history-confirmation/after/live-library.png`
- `app-foreground-read-4e20427/personal-history-confirmation/after/live-account-signed-in.png`
- `app-art-history-fb39072/public-art-confirmation/images/live-discover-search.png`
- `app-art-history-fb39072/public-art-confirmation/images/live-product.png`
- `app-recent-art-113be57/personal-art-confirmation/after/live-library.png`

These paths are relative to `/Users/dragoshont/xodus-app-tooling/`.
Do not commit personal title rows, raw provider responses, XUIDs, credentials,
signed package URLs, license/key material or temporary private keys.

## Complete acceptance ledger

Order is dependency-driven; independent source work can proceed concurrently.
The first installed slice is already useful browsing plus real played history.
The next game-lifecycle slice must be one legitimate selected PC edition.

| ID | Deliverable | Status | Prerequisite | Owner | Completion test |
| --- | --- | --- | --- | --- | --- |
| AUTH-01 | Saved Microsoft context readable by the installed engine | Observed | Existing saved item and human OS permission if required | Backend/App | Current typed `credentialPresent`, no replacement login, fixed error on denied/unavailable access |
| AUTH-02 | Real Xbox request using saved proofs | Observed | AUTH-01 | Backend | Actual checked Halo package read and actual TitleHub history; correct separate relying parties, no persisted refresh |
| AUTH-03 | Read/consent lifecycle and prompt recovery | Implemented | AUTH-01 | Backend/App | Native permission budget, responsive actor, no overlapping action, checked restoration, final flow reconciliation, cancel/timeout/late-result regressions and real recovery |
| AUTH-04 | Credential/privacy isolation | Implemented | AUTH-01 | Backend/App | Read-only provider consumers; publication/profile/epoch/generation fences; no secret logs; personal history cleared on account checks/change/disconnect |
| CAT-01 | Real public Store browse/search/detail | Observed | Supported catalog capability | Backend/App | Actual scoped response shown; partial errors, no-result, continuation, offline and failure remain distinct |
| ART-01 | Real Store BoxArt/Poster/SuperHeroArt | Observed | CAT-01 | Backend/App | Real covers and hero load through bounded native loader; final own-view images are not placeholders |
| LIB-01 | Real Recently played Library | Observed | AUTH-02 | Backend/App | One actual bounded TitleHub result; native grid/filter/search use actual titles, not ownership or install claims |
| ART-02 | Real history tile artwork and Library feature | Observed | LIB-01 | Backend/App | Actual history references accepted safely, image downloads/decode succeed and Library PNG contains real title art |
| LIB-02 | Useful history navigation | Active | LIB-01, CAT-01 | App/Backend | Explicit Find in Store action reuses real search; candidates are user-selected, never an inferred verified edition or ownership claim |
| OWN-01 | Complete authoritative owned-PC inventory | External | Supported consumer entitlement API and account/product coverage evidence | Backend | Real entitlement enumeration, pagination and expired/subscription behavior; played history never substitutes |
| UX-01 | Nine Figma flows with native composition | Active | Real contracts below | App | Screen-by-screen layout/interaction checks with real data; no fixture hero, project status or faux native chrome |
| UX-02 | Loading/error/empty/partial/offline states | Implemented, integration pending | Each implemented surface | App | Every state has concise product copy and an appropriate action; no failure-to-empty success fallback |
| UX-03 | Keyboard, VoiceOver, resize and native appearances | Pending | Integrated shipping screens | App | Native focus/shortcuts, visible controls, long labels, contrast/preferences and constrained-window checks; own-view PNGs alone do not certify these |
| PKG-01 | Positive immutable plan for one selected PC edition | Pending / External target | CAT-01, AUTH-01, explicit game/edition | Backend | Exact product/SKU/content/package/version/architecture/language/dependency identity, authentic digest and expanded size; missing values block rather than guess |
| LIC-01 | Real authorization/key for that selected package | External | PKG-01, scoped license-request grant | Backend | Actual entitled response and private key policy; no purchase or subscription activation; license POST is not a read-only inventory probe |
| DL-01 | One actual managed package download | Pending | PKG-01, LIC-01 | Backend/App | Immutable private source, authoritative checksum, byte limits, progress and owned staging; cancel/failure never corrupts active installation |
| INSTALL-01 | Bounded extraction and complete manifest | Pending | DL-01 | Backend | Real supported MSIXVC format, authenticated content/integrity, safe names/counts/sizes and exact disk expansion |
| INSTALL-02 | Atomic install promotion | Pending | INSTALL-01 | Backend/App | Staging verify/commit succeeds, restart reconciliation works; previous version and saves survive failure |
| LOCAL-01 | Genuine managed installed-game list | Active source / real install pending | Reviewed local-state contract; INSTALL-02 for live installation proof | Backend/App | Actual StagingStore-backed entries, versions and health survive restart; no fabricated runtime fingerprint or OS-wide discovery |
| JOB-01 | Real install/download job queue | Pending | PKG-01, install/job contract | Backend/App | Genuine progress/events/cancel/retry/restart semantics; catalog-check jobs never masquerade as downloads |
| UPDATE-01 | Full-base update with old-version retention | Pending | INSTALL-02, PKG-01 for new version | Backend/App | New immutable base staged and verified; atomic promotion, crash reconciliation and failed update preserve old version |
| ROLLBACK-01 | Verified installation rollback | Pending | UPDATE-01 | Backend/App | Prior manifest/version revalidated; saves are untouched, unrelated paths never deleted |
| REMOVE-01 | Scoped uninstall preserving saves by policy | Pending | LOCAL-01, explicit save/retention policy | Backend/App | Only managed owned version paths removed, confirmation and restart recovery are correct |
| RT-01 | Genuine official CrossOver dependency | Observed baseline / recheck required | User-installed licensed runtime | App | Signature/bundle identity and actual version checked; no redistribution, purchase or license inference |
| PLAY-01 | Launch one actually installed game | Pending / External runtime proof | INSTALL-02, LIC-01, RT-01 | Backend/App | Exact provider/bottle, required shim/RPS/license and compatible graphics; real launch/render/input/exit, not `runtime-plan` success |
| SAVE-01 | Save-location separation and preservation | Pending | Install/runtime contract | Backend/App | Version cleanup/update/remove never overwrites user saves or external CrossOver bottles |
| REL-01 | Exact integrated package and recovery | Implemented per shipped slice | All release-blocking slice criteria | App/Coordinator | Native tests, risk-appropriate independent gate, immutable pair, signature/resources and verified installed rollback |
| REL-02 | End-to-end usable release | Pending | Required product rows above | Both | Browse, select, authorize, install, list, update, play and recover one real supported game from the shipping app |

## Figma screen-to-product matrix

Authority: [approved v0.2 Figma file](https://www.figma.com/design/5iQu716UFImHjRxJkf0t8V),
the app's `DESIGN.md`, `design/screens/`, committed generators/tokens and Apple
platform constitution. The mockups contain invented titles and simulated
states; reproduce their composition with real data, not those invented facts.
Later native toolbar revisions supersede hand-drawn capsule chrome.

| Screen / Figma node | Real data required | Current gap | Acceptance |
| --- | --- | --- | --- |
| Library, `3:115` | Recent history, real tile/feature art; later actual installed entries | Real artwork grid and tile-based feature visually confirmed; Store navigation and genuine installed entries still in progress | Real art grid/feature, platform filter/scoped search; no invented owned or installed counts |
| Discover Browse, `3:365` | Real public catalog and actual title art | Public search/art observed; broader browse composition still needs integrated review | Useful real browse content, native navigation and real title selection |
| Discover Search, `3:288` | Actual scoped results and cover URLs | Real Halo/Gears covers verified; pagination/partial behavior retained | Search keyboard/focus, real cards and one actionable partial notice |
| Game detail, `3:248` | Store hero/cover, real product/SKU/availability; later genuine actions | Hero/cover observed; Install/Play not yet implemented | Artwork-led native detail, actual capabilities only, technical provenance in secondary information |
| Install review, `3:2` | Real immutable plan, verified access, actual disk expansion and destination | No positive install plan yet | Real review/confirmation and runtime choice; no simulated space, keys or download URLs shown |
| Downloads, `3:70` | Actual install/download jobs, bytes/stages/rates | Existing jobs are catalog refresh work | Live queue, cancel/retry/recovery, no invented progress or false pause/resume |
| Onboarding/cancel, `3:187` | Real saved/unknown/pending/failed/cancelled auth state | Working saved-session reads observed; long-running recovery needs integrated UI acceptance | One clear next action, native prompts human-handled, no reasoning/log/status wall |
| Blocked, `3:208` | Actual unavailable package/license/runtime/format cause | Capability/source seams exist; installed-game flow missing | Plain reason plus useful action; missing support never presented as a working Install/Play button |
| Download error, `3:321` | Real job/provider/integrity error and preserved installation | Needs DL-01/JOB-01 | Specific safe retry/resume policy, actionable error and unchanged old version/saves |

Settings and Account are native supporting scenes: signed-in status, exact
runtime selection, download destination and user-level preferences. Internal
contracts, artifact identities, proof fields and diagnostic codes belong in
developer evidence or explicitly opened advanced diagnostics, never the main
game flow.

## Current ordered task graph

1. **ART-02 observed complete:** exact Store HTTP metadata is normalized to
   HTTPS without broadening origin/path/query trust. One corrected live
   history read returned 20 available images; all 20 preload references
   succeeded. The actual Library image was visually confirmed. No further
   personal read, public-art rerun or polishing loop is required.
2. **Deliver usable navigation and genuine local-state reads.** The app reuses
   Discover for an explicit Find in Store action, without inferred mapping.
   The backend reads the existing StagingStore registry without creating,
   migrating, repairing or scanning it. Missing state is distinct from
   permission, corruption, concurrent mutation and recovery failure.
3. **Freeze PKG-01/LIC-01 and install-job contracts** from existing Exodus
   seams, including rollback/save/crash policy. Select one legitimate supported
   PC edition with the user; personal history is not proof of entitlement.
4. **Perform one scoped package/license proof** after its target and operation
   are approved. Unknown authoritative checksum, key, immutable version,
   expanded size or format is a real blocker, not permission to fabricate a plan.
5. **Deliver download -> extract -> verify -> commit -> installed-list**
   for that one game as a vertical slice, with real job progress and recovery.
6. **Deliver full-base update, rollback and scoped remove**, preserving saves.
7. **Deliver actual CrossOver play** for the same installed game and exact
   runtime configuration. Historical manually working games are references,
   not this launcher acceptance.
8. **Integrate all nine Figma flows**, one bounded visual review and one
   correction/confirmation batch per integrated slice, then native
   accessibility/keyboard/runtime release gates.

Supporting source, test and signing work must serve a named failing product
criterion; no repeated certificate, observer, inspection or status-only loops.

## Existing seams to reuse, and actual limits

| Seam | Reuse | Must not be claimed as complete |
| --- | --- | --- |
| `package.rs::get_packages_verified` | Saved profile witness, update-audience exchange, checked base-package identity and file sources | `auth.verify` returns only `verified:true`; no positive install plan or content license |
| `license.rs`, `licensing/content.rs` | Content-license request and private device/content-key derivation | License POST can affect concurrent sessions; not an entitlement enumeration probe |
| `download.rs`, `msixvc::HttpRead/XvdFile` | Owned staging, byte counts/fsync, format/extraction primitives | Provider FileHash/HashOfHashes interpretation and extraction authentication are not qualified; direct streaming install is unsafe |
| `StagingStore` | Prepare/write/verify/commit/snapshot/rollback/recover/reconcile | Adapter is not wired to actual installs; committed removal and save policy are absent |
| Management jobs/events | Ordered revisions, idempotency, replay, cancel/retry | Only catalog refresh JobKind is implemented; not real download/install progress |
| Runtime plan/private RPS seams | Configuration and protocol knowledge | No verified stock-CrossOver driver/bottle setup/game launch in this management adapter |

Bounded staging currently has a 256-file limit, ASCII path/component limits,
a bounded registry and local SHA256 manifests. Real game layouts can exceed
those bounds. Reject incompatible inputs first and change only evidenced limits;
never truncate a game manifest. Local hashes are not authoritative provider
authentication. `KEEP_ENCRYPTED` page writes may consume more disk space than
logical file lengths. Registry and job events are separate stores and require
explicit crash reconciliation before claiming restart-safe completion.

## Scope and policy

**KEEP:** native Figma composition with real artwork; saved-auth native service
requests; personal recently played history; one legitimate complete game
lifecycle; real local state, cleanup and rollback; concise actionable copy.

**CUT:** demo data in shipping; inferred ownership/installability; direct CLI
streaming into active directories; fake progress/runtime fingerprints;
certificate/reasoning/receipt copy in product UI; a new broker, authentication
rewrite, provider SDK orchestration or extra implementation workers.

**DEFER:** full consumer entitlement enumeration until a supported route is
proven; delta/XSP updates; arbitrary external game/bottle adoption; experimental
runtime-provider breadth; unattended access promises for self-signed rebuilds.

Continue dependency-ready source/contract/test work under the approved program.
Prior scoped Xodus close/restart/install/launch approval remains valid and does
not require asking at each internal phase. Keep package/source/target identities
and rollback checks proportionate to their trust boundary.

Human credentials, MFA, OS passwords and permission prompts remain human actions.
Never capture or inject them. Purchases, subscription activation, license
acquisition for a particular game, game-data download/install/remove and
runtime/bottle mutation need the exact operation/target grant; the generic
application objective does not identify that game, imply ownership or approve
unrelated changes. No Keychain deletion/reset or global ACL/trust modification
is part of this program.

## Close criteria

Do not close the complete-app request on builds, PNGs, an API schema, a single
auth probe or a catalog list. Close only after the required product rows and
all nine real-data screen flows are verified, or report a precisely named
external blocker with the working product preserved.

Ledger updates record meaningful completed/failed product evidence and the
next dependency. Detailed credentials and hidden reasoning are never recorded.
