# Xbox-hearted Mac experience: parity inventory and implementation plan

Status: expanded intake, 9 October 2026. The prior design delivery is a working
baseline, not evidence of complete Xbox feature parity.

## User outcome

A lifelong Xbox and PC gamer who has just bought a Mac can recognize their
account, purchases, subscription games, consoles and achievements; understand
which games work locally; choose a supported engine; and play without Terminal
or understanding Wine internals. Interaction, typography and materials are
native macOS. Xbox identity comes from the account, artwork, Game Pass,
achievements, friends and games, not Windows chrome.

The user requested an independent adversarial review, a one-to-one Xbox
capability comparison, missing-original-spec inventory, an ordered plan and
implementation. Newly discovered capabilities are catalogued for tomorrow's
triage. Onboarding and first login are explicitly deferred until the user
returns. The separate M5 acceptance test remains deferred.

## Delivery decisions

| Decision | Reason |
| --- | --- |
| KEEP the installed native launcher and existing backend | Its install/play/stop/repair/save-preserving removal paths are the working reference. |
| KEEP Apple-native controls and art-first composition | This is the pinned design direction, not permission to reproduce Xbox Windows chrome. |
| KEEP purchases and Game Pass as independent facts | A purchased game can also belong to Game Pass; neither fact establishes installation or Mac support. |
| KEEP profile, achievements, consoles and an Engines page | These are explicit additions in this request, not speculative launcher features. |
| KEEP browser remote-play handoff | The user requested a browser handoff, not a new native streaming engine. Verify the official target first. |
| CUT invented console lists, ownership, achievements and compatibility | Missing data must be an explicit unavailable/loading/error state. |
| CUT a parallel auth stack and arbitrary Wine/DLL editor | Reuse the credential owner and configured runtime presets. |
| DEFER onboarding/first-login changes | User-directed review tomorrow; preserve current working sign-in. |
| DEFER second-Mac and general-release claims | A successful development-Mac flow does not qualify a fresh installation. |
| DEFER unresearched Xbox services to triage | Purchases, social writes, party/chat, Rewards and remote console commands need proven capability and appropriate consent. |

## Scope and acceptance

1. **Inventory:** each Xbox feature has a current Xodus state, evidence, delivery
   mode (native, browser handoff, unavailable or deferred), dependencies and
   acceptance criteria. Official sources and inspected implementations take
   precedence over assumptions. Research does not certify an endpoint.
2. **Access:** show Owned and In Game Pass independently, including both on the
   same game. Distinguish free entitlement, installed/imported, subscription
   availability, active subscription and Mac support. Never promote played
   history or local files into purchase.
3. **All views:** review Library, Discover, search, detail/media, install review,
   Downloads, updates/repair/removal, Account, Profile, Achievements, Consoles,
   Engines, errors, offline and account-change behavior.
4. **Profile:** actual gamer identity/avatar/gamerscore and relevant account
   state, with an honest cached/partial/error model. Store purchasing identity
   and Xbox player identity must not be silently conflated.
5. **Achievements:** real game achievement summaries and, where supported,
   achievement details/progress. Title IDs and Store products must join using
   exact service identities. Unknown totals are not zero completion.
6. **Consoles:** show the authenticated user's actual consoles when the
   legitimate service supports enumeration. Do not invent devices from history
   or show generic console thumbnails as account data. Provide official browser
   management/remote-play fallback if native enumeration is unavailable.
7. **Remote play:** open the verified official HTTPS remote-play destination in
   the default browser, clearly marked as a browser handoff. Do not claim an
   undocumented per-console deep link, wake success or native streaming.
8. **Engines:** a dedicated native page shows observed CrossOver/runtime/
   graphics component versions and readiness separately. Experimental configured
   alternatives remain experimental. Provider selection cannot imply that
   unsupported runners execute; preserve per-game configuration and saves.
9. **Preservation:** existing Play/Install checks, cancellation, repair,
   uninstall/save policy, fixed C8, frozen credential broker and account fences
   remain intact. No bulk reinstall, secret export, security-policy changes or
   blind retries.
10. **Verification:** meaningful deterministic tests plus an independent
    adversarial verdict and runtime evidence on the installed product. Record
    partial features explicitly; never replace missing product proof with test
    counts. Push validated source before deployment and retain rollback.

## Current baseline

Installed app source `1d721c2`; exact CI `37846438226` passed. Native action
refinement is installed. C8 is byte-preserved at `c8fe69a3...`; the stable
credential broker is byte-preserved at `2c40354c...`. This is a baseline for
comparison, not full parity.

| Surface | Established baseline | Open parity questions |
| --- | --- | --- |
| Library | Owned PC collection, installed games, Game Pass feed, filters, covers, local recent sessions | Dual Owned + Game Pass badge; free versus paid entitlement; partial inventory; durable offline access labels |
| Discover/search | Public PC catalog, owned-first search, artwork, game detail entry | Complete paging, genre/category accuracy, exact editions, subscription expiry, unsupported-game discoverability |
| Detail/media | Four facets, About, catalog ratings/requirements, screenshots and explicit HLS player | Remaining optional-field errors, live trailer proof, full achievement detail, exact PC versus console capability applicability |
| Install | Compatibility/package check, destination, free space, native consent | Successful final-build review on supported title, dependencies/DLC/languages, peak-space explanation |
| Downloads | Native progress/cancel/error/completion views over current operations | Persisted queue/recovery, pause/resume capability, update automation and bandwidth policy |
| Play/repair/remove | Real local play/stop, check-and-repair, save-preserving uninstall | Offline launch policy, running-game conflicts, atomic update rollback, external/imported-game semantics |
| Account/setup | PC sign-in, game-service sign-in, Game Pass status, readiness/repair and public-art cache controls | Player profile, account mismatch, consistent sign-out, failed-refresh cache isolation |
| Xbox stats | Play time, achievement count/G score and friends-who-play cache | Complete achievements/profile/social views, optional-service failure versus empty-data semantics |
| Consoles/remote play | Not implemented | Verified enumeration/auth scope and official browser handoff |
| Engines | Configured runtime presets and read-only CrossOver detection | Discoverable native Engines page, actual component health/version, per-game overrides and unsupported-runner honesty |
| Accessibility | Native components plus automated presentation/behavior checks | Real VoiceOver, Full Keyboard Access, contrast, reduced transparency, resize and localization coverage |
| Distribution | Controlled signed development-Mac installation | Fresh-Mac setup, notarization/distribution rights and second-M5 acceptance |

## Ordered delivery

| Step | Work | Exit |
| --- | --- | --- |
| P0 | Preserve/push the current atomic reliability fix and working app | One mutable owner; source and installed baseline identified |
| P1 | Source-backed Xbox feature research and independent all-view adversarial review | Cited inventory, explicit gaps and one consolidated verdict |
| P2 | Freeze contracts/spec additions and tomorrow-triage list | Each named feature has exact identity/auth/read/write/UX/acceptance boundaries |
| P3 | Repair independent access labels and current product defects | Owned + Game Pass coexist; installed/support remain separate; regressions pass |
| P4 | Profile and achievements vertical slice | Real account-scoped read, native views, paging/error/cache/account-change tests |
| P5 | Consoles and browser remote play | Actual enumeration if supported; truthful fallback and verified official HTTPS handoff |
| P6 | Native Engines page | Observed provider/components, sensible defaults, safe experimental configuration |
| P7 | Integrated all-view UX/functional fixes | Consistent navigation/actions/account/offline/capability copy; no new untriaged services |
| P8 | Independent acceptance, exact CI, package/install and dashboard | Real usable product; unresolved capability/human gates named |

## Original-spec reconciliation

`xodus-macos-app/PRODUCT.md` still lists social/achievements as not promised v1
and describes pre-redesign views as pending. These are stale with respect to the
current source and newly requested scope. Update targeted sections with source
and runtime evidence, not a blanket "all features complete" claim.

`docs/REQUIREMENTS.md` contains release gates for account changes, stale/partial
library, exact edition identity, durable queue, safe updates, offline behavior,
accessibility, localization, performance and distribution. Existing test suites
do not by themselves prove all those gates. The independent review must identify
which are implemented, partially covered, absent or deliberately deferred.

The older root `xbox-api-inventory.md` predates live PC ownership/Game Pass
acceptance. Preserve its historical evidence but add current findings rather
than treating old Unverified rows as the latest capability state.

## Research and review status

[Independent review and source-backed findings](xbox-parity-review.md) record
the REVISE verdict, original-spec gaps, exact public references and live
read-only companion results. Profile, one actual console, friends and a full
Hogwarts achievement list are feasible through the existing credential owner.
The backend contract is implemented/tested at private commit `8cfcbaf`.
App integration remains in progress; those reads do not certify installed
native pages until P4/P5 runtime acceptance.

Social writes, purchase flows, remote console commands and experimental engine
execution alternatives have no new capability claim. The six-hour slice
implements the named P3-P6 vertical slices and consolidated defects first;
the rest remains a specific, evidence-labelled tomorrow-triage inventory.
