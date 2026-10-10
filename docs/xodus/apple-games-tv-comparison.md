# Xodus vs Apple Games (and the Apple TV pattern)

Evidence: real window captures taken with Xodus Observe on 2026-10-10 (`xodus-home`,
`xodus-library`, `cmp-tv-home`, `cmp-games-home`), plus the sourced research report
(Apple Support "Games" guide, WWDC25 session 215, HIG, MacStories, MacRumors).

## Capability comparison

| Capability | Apple Games (macOS 26) | Xodus (fac58d6) | Gap |
|---|---|---|---|
| Catalog source | App Store + Arcade, buy/get in-app | Xbox owned + Game Pass + Discover catalog | — (different stores) |
| Install / update / remove | One CTA (Get → progress → Play), size in Game Details | Install, repair, remove, progress in Downloads | CTA state not unified on cards |
| Launch | Play from Library, Home "Continue Playing" | Play on card and in detail | No Continue Playing row |
| Cross-device resume | Continue Playing across Apple devices | Xbox cloud saves where the title supports it | Not surfaced |
| Social | Friends Are Playing, challenges, invites, Memoji around icon | "1 friend plays" text on detail only | No friends row |
| Achievements | Full list, progress, friends who earned | Count + Gamerscore only | No achievements view |
| Library sort/filter | Recent, Name, Size; On This Mac, Arcade, Friends Playing | Sections: owned, installed on this Mac | No sort, no filter chips |
| Search | Dedicated tab, results show icon/category/previews | Search field, Discover results | Comparable |
| Controller | Controller badge on page; game-mode | Not shown | Add badge where catalog says so |
| Compatibility honesty | n/a (all native) | Banner on detail | **Wrong**: Subnautica (MSIXVC, installable) says "needs Xbox features Xodus can't provide" |

## UX comparison (what the screenshots show)

| Area | Apple TV / Apple Games | Xodus today | Fix |
|---|---|---|---|
| Hero | TV: full-bleed key art, logo title, one-line genre row, 2-line synopsis, white capsule **Play** + round **+**, page dots inside the art. Games: tinted page from icon colours. | No hero visible on Library; scroll lands in a grid. Carousel exists only in code paths not shown at launch. | Library opens on a TV-style hero carousel of recently played / installed games. |
| Typography | SF Pro, title 28–34 pt bold, metadata 13 pt at ~85% white on art, section headers 22 pt bold with chevron. | Metadata 12–13 pt secondary grey on light grey; tab labels grey on glass; on sheet backdrop everything is dimmed. | Primary labels `.primary`, metadata `.secondary` never below 13 pt; no extra opacity; hero text on a scrim. |
| Cards | Landscape 16:9 tiles, art only, title below, progress bar overlaid for in-progress. One `…` button. | Tall portrait tiles with 3–4 metadata rows each (Owned, genre, hours, achievements, size, installed). Play + `…` overlaid. | Art + title only; one status line (Installed / size / Update). Details move to the page. |
| Rows | Horizontal shelves: Continue Watching, themed shelves with `>` to see all. | One vertical grid per section. | Shelves: Continue Playing, Installed on this Mac, Owned, Game Pass picks. "See all" opens the grid. |
| Detail | Games: full page (not sheet) — icon, title, subtitle, rating · genre · controller, wide primary button, Preview row, description card, Game Details grid (rating, genre, languages, size). | Modal sheet with Done button, blurry backdrop, single big trailer, label/value table. | Full-window page with back button; Apple Games section order. |
| Navigation | Games: top tab bar (Home, Arcade, Friends, Library, Search). TV: sidebar. | Top tab bar (Library, Discover, Downloads, Search) — matches Games. | Keep; add Home later if shelves grow. |
| Colour | Games tints page background from the art. TV dark hero, light shelves. | Flat light grey everywhere. | Tint detail page from key-art average colour. |

## Slices (in order)

1. **Legibility + honesty**: font sizes/contrast; fix Subnautica false "can't provide" banner.
2. **Hero carousel** on Library (TV pattern: logo/title, genre row, synopsis, Play, dots, auto-advance, pause on hover).
3. **Shelves + compact cards**: Continue Playing, Installed, Owned, Game Pass; 16:9 art, single status line, See all.
4. **Detail page**: full window, Apple Games order, tinted background, Game Details grid, controller badge.
5. **Library sort/filter** (Recent, Name, Size; Installed, Game Pass) and achievements view.

Every slice ships only with a before/after Xodus Observe capture of the real app.
