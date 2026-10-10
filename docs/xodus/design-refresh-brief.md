# Xodus design refresh: native macOS 27, Apple Games feel

User decisions (2026-10-08):
- Layout and flows: the v0.2 Figma screens (file `5iQu716UFImHjRxJkf0t8V`, nodes 3:115, 3:365, 3:288, 3:248, 3:2, 3:70, 3:187, 3:208, 3:321).
- Look: Apple Games on macOS 26/27: dark, art-first, Liquid Glass.
- Use native macOS 27 SwiftUI/AppKit components and materials wherever possible. Custom drawing only where no native component exists.

User refinement (2026-10-09):
- Overall intent: native macOS feel with an Xbox soul. Native controls and materials carry the interaction; real artwork, Game Pass, ownership, achievements and friends carry the gaming identity. The cloud icon is inspiration, not a mandatory visual prescription.
- Prefer Apple's cloud-download affordance and large native action buttons.
- For an owned, uninstalled game, use the SF Symbol `icloud.and.arrow.down` with an accessible Download label. In heroes/details, keep the visible label alongside the symbol; compact cards may use the symbol with a help tag and VoiceOver label.
- Keep large native Play buttons for installed games. When Game Pass package access is not yet verified, the primary action is **Check Game Pass access** and opens the existing protected review; **Install** belongs to the confirmed consent step. The cloud symbol must not imply ownership, cloud gaming or cloud-save support.
- Download/Install still opens the compatibility and storage review before any transfer; it never launches automatically.
- Implemented and installed at app commit `1d721c2`; exact CI `37846438226` passed. Existing composition, credential broker, engine and install-review safeguards are preserved.

## Why the current build looks dated
- Grey window background, with no material or depth.
- The hero is a cropped art strip on a grey slab.
- Games are a separated text list with small blue pill buttons and a `···⌄` control, which reads as a file browser.
- Small type and a flat hierarchy; cover art shrunk to 60 pt thumbnails.
- Primary actions such as "Import installed Xbox game" sit as body buttons.

## Global rules (all screens)
| Concern | Native choice | Never |
|---|---|---|
| Window chrome | Hidden title, full-size content, system toolbar. The toolbar Liquid Glass is supplied by the system | Painted capsules, sampled tints |
| Navigation | Existing native toolbar tabs (Library / Discover / Downloads), search in the same group, Account trailing | Custom segmented pills |
| Hero art | Full-bleed artwork under the toolbar via `backgroundExtensionEffect()`; `.scrollEdgeEffectStyle(.soft, for: .top)` | Art in a fixed strip with a grey caption slab |
| Buttons | Primary `.buttonStyle(.glassProminent)`, secondary `.glass`, `.controlSize(.large)` in heroes; clusters in `GlassEffectContainer` | Tiny tinted pills |
| Collections | `LazyVGrid` of portrait 2:3 cover cards; horizontal `ScrollView` shelves with `.scrollTargetBehavior(.viewAligned)` for rows of landscape cards | Separator lists for games |
| Per-game actions | `.contextMenu` plus a native `Menu` (ellipsis) on hover/detail: Manage, Repair, Uninstall, Show in Finder | `···⌄` text menus in every row |
| Queues/settings | Native `List`/`Form` (`.inset`, grouped) where a list *is* the right model (Downloads, Install review, Account) | Hand-built rows |
| Status | `Label` with SF Symbols in `.secondary` (Owned, Game Pass, Installed, Update); progress via `ProgressView` / `Gauge` | Coloured text badges with invented colours |
| Empty/error | `ContentUnavailableView` (incl. `.search`) with one recovery action | Bespoke empty states |
| Sheets | `.sheet` + `Form`, `.presentationSizing(.form)` | Full custom modals |
| Type | System font only. Hero `.largeTitle.bold()`, sections `.title2.weight(.semibold)` with trailing "See All", metadata `.subheadline` `.secondary` | Fixed point sizes |
| Colour | Semantic colours only; art provides colour | Hard-coded hex |
| Appearance | Follow the system setting; review and sign off in Dark first. The art keeps it dark-feeling in Light too | Forcing Dark |
| Accessibility | Native controls give Reduce Transparency, contrast and VoiceOver; every card has an a11y label (title, state) and keyboard focus/Return to open | Hover-only actions |
| Art | Real Xbox catalogue art: box art (portrait) for grids, super-hero/title art (landscape) for heroes and shelves; `AsyncImage` with placeholder | Upscaled icons |

## Screens (v0.2 layout → native)
1. **Library** (first slice; v0.2 node 3:115)
   - Hero: last-played game, full-bleed art extending under the toolbar. Bottom-left: title, publisher, "Last played …", and a glass cluster: **Play** (prominent) + ellipsis `Menu`.
   - "Continue Playing" shelf: landscape cards (art, title, progress or last played).
   - "Your Games": portrait cover grid. Section header with a native segmented `Picker` (All · Installed · Owned · Game Pass) and a sort `Menu`. Installed games show a **Play** glass button on hover/focus; others **Install**.
   - "Import installed Xbox game" moves to the toolbar/app menu (File ▸ Import…).
2. **Discover / Browse** (3:365): paging hero carousel (`.scrollTargetBehavior(.paging)`), then shelves: Game Pass, Owned not installed, categories.
3. **Discover / Search** (3:288): toolbar search with scopes (Library · Store), results as a cover grid with status labels; `ContentUnavailableView.search` when empty.
4. **Game detail** (3:248): pushed in a `NavigationStack`, extended hero, glass Play/Install + Menu. Below it, a `Grid` of `LabeledContent`: Access, PC download, Compatibility, Install, Size, Publisher.
5. **Install sheet** (3:2): `Form` sections (What gets installed, Disk space with `Gauge`, Location) with Cancel / Install (prominent).
6. **Downloads** (3:70): native inset `List`, rows with cover, `ProgressView`, speed/ETA, Pause/Cancel glass icon buttons; finished section.
7. **Onboarding** (3:187), **Blocked** (3:208), **Download error** (3:321): `ContentUnavailableView` / inline sections with one clear recovery action, product copy only.

## Process
- Build Library first, natively, against real data (live profile) and the fixture. Capture at 1440×980 in Dark and Light and send to the user for sign-off before other screens.
- No backend contract change. Existing verification, admission and C8 pins apply; normal Quit/replace with the user's library preserved.

## User-requested three-round adversarial UX cycle

Scope of these completed review rounds: **macOS UI only**. Backend/engine/composition work and other
program phases are paused and preserved. No automatic restart schedule was
created by this lane. Native input/capture requires a released foreground and
an unlocked Mac; source and neutral UI checks may proceed without either.

**Release checkpoint, 19:20:** the Xbox lane released its foreground at 19:15.
The Mac itself is independently confirmed locked, which is a separate human
gate. Exact `a0c5aba` source/tree and prior signed-input receipt are staged under
`~/xodus-app-tooling/app-ui-review-a0c5aba-20261009/`; the one-use package recipe
passes syntax checks and refuses a locked console. No signing job has been
loaded and the installed app is unchanged. Local unlock is required before
the controlled package/install/native acceptance sequence.

| Round | Frozen subject | Review and follow-up |
| --- | --- | --- |
| 1 | Library/actions/catalogue candidate `67ed6fa` | Independent REVISE; fixes `bb9ced6`: hide impossible continuation at the 512-entry bound, expose its partial-limit reason, remove the unused action-copy helper/test. Synthetic 300-entry pagination and 528-entry bounded-stop/no-extra-call regressions pass. |
| 2 | All native views at `bb9ced6` | Independent REVISE; fixes `ad7ead4`: primary Account failure copy is human-readable, closed machine diagnostics stay in a disclosure, anonymous progress gets accessibility descriptions and the unused catalogue grid matches shared alignment. |
| 3 | Integrated source `ad7ead4` | Independent source UX PASS. Final small batch `a0c5aba` labels game-service, Game Pass, indeterminate download and recent-activity progress. Real manual VoiceOver, new compositor screenshots and live catalogue totals remain native acceptance gates. |

Final neutral source evidence: Debug build, 175 preview/layout checks and
967 native-session checks passed; shipping-configuration Release compilation
passed. All review fix batches are committed/pushed. Exact CI passed for
`67ed6fa`, `bb9ced6` and `ad7ead4`; final `a0c5aba` exact CI
[`37956191963`](https://github.com/dragoshont/xodus-macos-app/actions/runs/37956191963)
also passed with its SHA independently matched. No new source is installed:
the preserved app remains `39fd687`,
with no signing, deployment, account changes, game launch or licence operation
in these UI review rounds.

Round-two coverage mistakenly looked at the fixture Downloads file; final
round traced the actual shipping `LiveActivityView` in `LiveRootView.swift`.
The final source PASS does not certify full release accessibility or all
43 service capabilities. No fourth source-review loop is started.

Live Apple HIG content was inspected for
[Collections](https://developer.apple.com/design/human-interface-guidelines/collections),
[Lists and tables](https://developer.apple.com/design/human-interface-guidelines/lists-and-tables),
[Buttons](https://developer.apple.com/design/human-interface-guidelines/buttons)
and [Progress indicators](https://developer.apple.com/design/human-interface-guidelines/progress-indicators).
Artwork-led content uses a predictable native grid, text-heavy account/jobs use
appropriate native list/form structures, primary buttons name actions rather
than states, and catalogue progress never invents a total or completion percent.

## Current priority: freshly installed No Man's Sky

The user explicitly prioritized the No Man's Sky 7.6 crash over remaining UI
refinements. This is a named, title-only repair exception, not a restart of
paused engine/backend/composition research. Preserve the unfinished animated
hero, recent carousel, capability-copy edits and the installed app.

The new installation's stock launch fails `vkCreateDevice` with
`VK_ERROR_FEATURE_NOT_PRESENT`; the requested missing feature is `wideLines`.
The existing, byte-pinned experimental MoltenVK reference was rechecked on this
Mac: `wideLines=1` and real device creation returns Vulkan success. This proves
the device-creation difference, **not current 7.6 gameplay**.

An isolated source candidate makes this renderer explicitly opt-in and
No Man's Sky-only, checks its SHA-256 before use, handles spaces and apostrophes
in the driver path through CrossOver's actual environment parsing convention,
and includes the verified driver in assembled runtime manifests. Nine inert
wrapper/assembly checks pass, including other-title rejection, missing/tampered
driver failures, unchanged stock selection and preservation of an existing
runtime on verification failure. No game, account or real prefix is used in
these checks.

The candidate reduces MoltenVK logging to errors only; it does not hide fatal
device failures or claim to remove the game's own graphics-warning dialogs.
The renderer uses Metal private APIs, so explicit title-scoped consent is held
before deployment or one controlled launch. The current installed launcher,
runtime, account and saves are unchanged. Actual renderer loading, a native game
window and gameplay remain acceptance requirements.

The Mac unlock was confirmed after the user's 20:25 message, but newer feedback
superseded the prepared `a0c5aba` release. Do not run that older package recipe
blindly. The user also requested **Launching** rather than immediate **Stop**;
the current primary-action ordering and five-second process timer are not
gameplay-readiness evidence. That UI correction and inspection of the reported
Your Games issue remain unfinished, not silently counted as delivered.

### NMS runtime follow-up: rendering is not playability

The user explicitly approved the disclosed experimental renderer. The current
NMS launcher now selects an isolated runtime; other games retain their original
runtime. The first controlled attempt stopped before Windows/game startup:
the shared CrossOver bundle was missing its native `x86_64-unix/wine` executable.
The missing file was identified against the publisher's signed resource
inventory. An official matching 26.3 archive supplies exactly those bytes; its
full app copy passes strict deep signature verification inside the NMS-only
runtime. The shared CrossOver installation is not modified and no preview
Wine build is mixed in.

The reconciled retry starts the real 7.6 game in its existing managed bottle.
The game's actual memory map contains the pinned experimental MoltenVK, and a
native capture shows the rendered Press to begin screen. The original
wideLines/device-create failure is absent in this run. This remains **partial**:
the user reports no mouse response, and gameplay has not been observed.

Native activation inspection shows an inactive regular application with only
its Stage Manager thumbnail on screen. Raising the AX window and requesting
AppKit activation did not establish stable active-window input. The native main
thread is alive in its event loop; a screenshot alone must not be interpreted
as working input or a proven visible interactive game. Automated clicks stopped
after the user's repeated no-click report. Do not declare this repaired or
silently change global Stage Manager settings, credentials, or saves.

### Subsequent correction and current acceptance hold

The earlier observer image was stale: its modification time predates the later
game retries. It must not be used to certify their visible state or attribute
the user's input problem solely to Stage Manager. Subsequent runtime diagnostics
provided stronger evidence: the real game-user initializer failed
`default_endpoint_parse` with `E_NOTIMPL`. A native Windows API fixture reproduced
`IJsonValue::GetObject` failing in the stock JSON provider. The preserved genuine
JSON implementation was packaged as a native DLL rather than a Wine builtin,
so CrossOver no longer substituted the stub. Real object, array, vector, string
and invalid-input fixture results then passed. Existing byte-pinned real HTTP/
TLS transport is selected only in the temporary NMS launch tree; original game
assets and TLS verification are unchanged.

The user then reported choppiness. The actual 7.6 log showed repeated Metal
vertex-shader pipeline compilation failures, not harmless cache warm-up.
Captured SPIR-V reproduces the invalid `device float*` cast on a local
vertex BlockIO output. A six-line isolated SPIRV-Cross classification correction
keeps that output thread-local until it is copied into the device output buffer.
It does not change real storage buffers, rewrite game shader binaries, fake
Vulkan features, or add another Metal-private-API workaround.

The exact failing shader fails on the M5 Max Metal compiler before the fix and
compiles afterward. Ten further real captured vertex shaders also change from
compile failure to success. Relinking unchanged renderer inputs reproduces
the previous pinned renderer exactly; the candidate changes only the isolated
MSL translator archive. Actual wideLines device creation remains successful.
The new NMS-only renderer and its matching fail-closed pin are installed with
the original retained for rollback; shared CrossOver and other games are
unchanged. CrossOver remains the user's default engine.

The owner authorized autonomous iteration and went to bed. The initial locked
console hold prevented a launch. After the owner reported unlocking, an
independent console observation confirmed unlock and the canonical hold was
resolved. One real-app retry launched NMS (PID 15945); the process loaded the
corrected isolated renderer, and its initial live log contains none of the
previous shader-cast, compiler or Vulkan feature failure fingerprints.

The existing NMS Observer initially returned `SCREEN_RECORDING_APPROVAL_REQUIRED`;
that stale image was rejected. The owner then approved Screen Recording.
Launching the observer through macOS LaunchServices produced a genuinely fresh
game-only capture showing the character, terrain and HUD in-world. Direct SSH
executable launch still reports missing permission and is not the correct
observation context. The canonical consent hold is resolved. Previous shader,
feature and crash fingerprints remain absent after ten minutes, but smooth
frame pacing is not yet verified.

The reinstall writer previously selected the generic launcher and recreated a
bottle without the native JSON correction. Private commit `87ccc73` installs a
version-bound NMS-only selection of the pinned corrected runtime, atomically
provisions its pinned native JSON DLL after template creation, and honours the
actual reinstalled folder during temporary transport staging. Missing/tampered
components and versions other than the verified 7.6 fail explicitly, without
silently selecting the broken generic path. Fourteen focused checks cover
launcher regeneration, fresh-bottle configuration, relocated paths, save
restore, other-title isolation and transport guards; eleven existing companion
checks also pass. The real game was not uninstalled or interrupted, so a live
uninstall/reinstall is still unqualified.

Caffeinate is running to prevent idle sleep; it does not bypass screen locking.
No account, save or installed-app reset/replacement occurred.

The owner subsequently confirmed **"game works great"** and explicitly resumed
the launcher's UI/UX work. The successful current session is owner-accepted;
the actual reinstall remains a separate unqualified lifecycle check. The game
is not interrupted for app packaging or interface work.

### Launcher candidate after owner acceptance

App commit `02e67f8` is committed and pushed. It retains Discover results and
per-tab searches, adds manual sliding hero/recent-game controls, fences muted
hero playback by visibility and app/game lifecycle, presents Launching/Stopping
before generic Play/Stop, and gives empty Downloads a Browse games action.
The bounded adversarial fix batch prevents unrelated background checks from
blanking Downloads and uses static artwork on macOS 14, where viewport visibility
cannot be observed by the newer SwiftUI API.
Discover also observes the outer vertical carousel viewport, not just its
selected horizontal slide, before allowing playback.

Debug and shipping Release builds pass, as do 175 presentation and 981 native
fixture checks. The final disconnected Downloads own-view render was inspected;
it is not installed-product or animated-media acceptance. Exact-source CI is
`dragoshont/xodus-macos-app` run `38002242443`, passed on the full-Xcode runner.
Installed app `39fd687` remains unchanged. The durable UI Run's policy check
denies signing; a scoped packaging/sign/deploy grant is required before replacing
it, preserving the frozen credential broker and signed CLI.

CrossOver's official installation/trial handoff and signed detection are in the
candidate. Alternate-engine execution is not delivered: the native client
implements a pure `runtime-plan`, while the current install/setup/stop writer
uses CrossOver-specific bottles and tools. A picker cannot substitute for an
execution contract with per-game isolation and save-preserving migration.

The owner subsequently approved packaging, signing and installation. SSH signing
continued to fail after local Keychain unlock, but the same signer succeeded in
the user's GUI Terminal context. A short local packaging runner avoided terminal
command-length issues and disabled generated Python bytecode in the frozen
source checkout. Package verification passed; after normal Quit at an observed
game/download-idle boundary, `02e67f8` replaced the installed launcher.
The complete 22-file inventory and strict deep signature were independently
verified after installation, and the new launcher process remained alive.
The original signed CLI and credential broker bytes are unchanged; the previous
`39fd687` bundle is retained for rollback. No game, save or shared-runtime mutation
occurred. Live visual/media acceptance and functional alternate engines remain
unqualified; installation is not full product acceptance.

### Owner-directed streamlined flow and hover previews

App source `fac58d6` supersedes the earlier manual check/review flow: games offer
Install or details, with Play for installed titles. Install runs the existing
package/entitlement/storage safeguards internally before transfer; progress,
cancellation and real failures remain visible. Repair/removal retain their
protected flows. Details load selected-title metadata independently of bulk
artwork and no longer expose unverified-access, PC-package or Mac-support
diagnostics. Game Pass catalogue membership is separate from download rights;
Discover continues loading the available catalogue in the background.

Hero trailers are muted hover previews, with all existing viewport, app, power
and accessibility fences. Known-duration previews begin 35 percent into the
trailer, capped at 60 seconds; this is not a semantic guarantee of skipping
credits. Centered clickable dots replace Discover's previous/next controls,
with hidden scroll indicators. Continue Playing uses native trackpad scrolling.
A higher streaming bitrate and bottom-only contrast scrim replace the washed-out
whole-frame treatment. Actual hover/video appearance remains unobserved.

The source is committed and pushed; 176 presentation and 992 native fixture
checks, Debug/shipping Release, and exact full-Xcode CI
`dragoshont/xodus-macos-app` run `38065402103` passed. The existing GUI Keychain
signer produced the package; its strict signature, 22-file inventory and exact
preserved signed CLI/frozen broker were independently checked. Public-art native
preview became ready, but screen capture was unavailable; this is not visual
acceptance or streaming proof. The bounded reviewer did not establish a source
defect but returned REVISE for insufficient accessible evidence, not semantic PASS.

Installation stopped before replacement: the running `02e67f8` launcher refused
normal Quit from SSH and GUI Terminal with Apple event `-128` (User canceled).
The launcher and its owned management CLI remain alive. A verified candidate
copy is staged separately; the original installed bundle, games, saves,
runtime and previous rollback are unchanged. No force termination or repeated
replacement was attempted. The scoped deployment Run records the shutdown
blocker; safe installation and live acceptance are still pending.
