# Xodus design refresh: native macOS 27, Apple Games feel

User decisions (2026-10-08):
- Layout and flows: the v0.2 Figma screens (file `5iQu716UFImHjRxJkf0t8V`, nodes 3:115, 3:365, 3:288, 3:248, 3:2, 3:70, 3:187, 3:208, 3:321).
- Look: Apple Games on macOS 26/27: dark, art-first, Liquid Glass.
- Use native macOS 27 SwiftUI/AppKit components and materials wherever possible. Custom drawing only where no native component exists.

User refinement (2026-10-09):
- Overall intent: native macOS feel with an Xbox soul. Native controls and materials carry the interaction; real artwork, Game Pass, ownership, achievements and friends carry the gaming identity. The cloud icon is inspiration, not a mandatory visual prescription.
- Prefer Apple's cloud-download affordance and large native action buttons.
- For an owned, uninstalled game, use the SF Symbol `icloud.and.arrow.down` with an accessible Download label. In heroes/details, keep the visible label alongside the symbol; compact cards may use the symbol with a help tag and VoiceOver label.
- Keep large native Play buttons for installed games. Game Pass installation stays clearly labelled Install; the cloud symbol must not imply ownership, cloud gaming or cloud-save support.
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
