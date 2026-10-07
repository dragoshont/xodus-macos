// Single source of truth for the Xodus delivery dashboard.
// Update this file at every backlog transition (status, spent hours, evidence).
// index.html re-reads it every 5 seconds; no build step.
window.XODUS_STATUS = {
  updatedAt: "2026-10-07T21:43:59+03:00",
  phase: "Phase 2 â€” functionally complete, user-serviceable",
  budget: { totalHours: 24, spentHours: 13, note: "8 h delivered (S0–S6) + 16 h Phase 2" },

  shipped: [
    { id: "F0", title: "Feasibility and direction", detail: "Heroic plugin path reviewed adversarially; chose a native macOS launcher over the Xodus fork", proof: "BOUNDED_GO review", date: "Oct 1" },
    { id: "F1", title: "Native app foundation", detail: "SwiftUI app, approved Figma Library composition, Store sign-in, signed engine pairing", proof: "xodus-macos-app", date: "Oct 2â€“5" },
    { id: "F2", title: "Hogwarts plays on Mac", detail: "Private launch path: CrossOver + Xodus game runtime + service licensing + real HTTPS transport", proof: "Played full-screen", date: "Oct 6" },
    { id: "S0", hours: 1.5, title: "Installed, Import, Play", detail: "Import an installed Xbox game and launch it from the app", proof: "0926414", date: "Oct 7" },
    { id: "S1", hours: 1.0, title: "Library with real artwork", detail: "Tile and splash art from the game package, Continue Playing, session history, Show log", proof: "e401464", date: "Oct 7" },
    { id: "S2", hours: 0.5, title: "Launch polish", detail: "Clean quit returns to Play; graphics-driver warning suppressed", proof: "Live quit + launch", date: "Oct 7" },
    { id: "S4", hours: 1.5, title: "Owned PC games", detail: "13 owned PC titles with Store art via first-party sign-in and collections", proof: "7ec1daa", date: "Oct 7" },
    { id: "L2", hours: 1.5, title: "Second title end to end", detail: "Lara Croft downloaded, imported, played; four runtime and verifier fixes", proof: "Main menu, 119 FPS", date: "Oct 7" },
    { id: "S3", hours: 0.5, title: "Game sign-in status", detail: "Game service owns credentials; app shows and starts game sign-in", proof: "1463cdb", date: "Oct 7" },
    { id: "S5", hours: 1.0, title: "Install from the app", detail: "Consent, real byte progress, cancel, verified registration", proof: "3.25 GB fresh install", date: "Oct 7" },
    { id: "S6", hours: 0.5, title: "Repair and uninstall", detail: "Incremental repair; uninstall keeps saves; remove from list", proof: "Live in app", date: "Oct 7" }
  ],

  backlog: [
    { id: "B1", title: "Fix Discover", hours: 1.0, status: "now", done: "Live PC Game Pass titles listed in the app", evidence: "Root cause: app rejects the whole page when any Game Pass title is not flagged PC (A Way Out, AoE II/III). Fix with app owner." },
    { id: "B2", title: "Store-wide search with ownership badges", hours: 1.5, status: "now", done: "Lara, Hogwarts, Celeste found with correct badges; install from a result" },
    { id: "B3", title: "Plays-on-Mac check before download", hours: 1.5, status: "now", done: "Unsupported packages flagged with zero bytes downloaded", evidence: "Backend live: Lara supported (3.2 GB), Celeste and Subnautica flagged in 4–10 s, zero download. App UI in the B1–B3 package." },
    { id: "B4", title: "Game Pass licence spike", hours: 1.0, status: "done", done: "Yes/no evidence for subscription status and one Game Pass licence", evidence: "YES: 3 unowned Game Pass titles licensed; 2 unowned non-Game Pass titles refused. Status command live (active)." },
    { id: "B5", title: "Game Pass status and shelf", hours: 2.0, status: "now", done: "Subscription state correct; Install only when active and supported", evidence: "Backend proven: Game Pass title Abiotic Factor (not owned) installed 5.3 GB and reached its menu at 59 FPS. App UI is the next package." },
    { id: "B6", title: "One sign-in, no Keychain prompts", hours: 2.0, status: "planned", done: "Sign in once; restarts cause zero prompts" },
    { id: "B7", title: "Self-contained app", hours: 2.5, status: "planned", done: "Developer folders moved aside; Lara still installs and plays" },
    { id: "B8", title: "First-run setup and Repair Xodus", hours: 2.0, status: "planned", done: "Missing template rebuilt from the app; Lara plays" },
    { id: "B9", title: "Stop game", hours: 1.0, status: "now", done: "Stop ends the game; app returns to Play cleanly", evidence: "Backend live: Lara stopped in 4 s, no leftover processes. App button in the B5 package." },
    { id: "B10", title: "Cloud saves and online spikes", hours: 0.5, status: "done", done: "Feasibility recorded with evidence", evidence: "Cloud saves: local-only, upload not implemented (deferred). Online: Lara Xbox user OK but publisher service offline; Abiotic Xbox user fails at SISU (E_INVALIDARG). Single player works." },
    { id: "B11", title: "Release acceptance on a fresh account", hours: 1.0, status: "planned", done: "Full journey passes using only the app" }
  ],

  lineage: [
    { source: "Xodus (Exodus) fork", origin: "xodus-gaming/xodus", kind: "extended",
      took: "xodus-cli streaming, download, login and run; xodus-service; MSIXVC reader; licensing",
      changed: "Service-brokered licensing (no CLI Keychain reads), machine-readable install progress, private macOS launch path",
      powers: "Install, Repair, Play, game sign-in" },
    { source: "Xodus Wine + game runtime", origin: "Weather-OS bleeding-edge, xgameruntime", kind: "extended",
      took: "Wine fork with an Xbox game runtime (GDK) implementation",
      changed: "Optional sandbox ID out-parameter, local-only saves, ticket and identity bridge, AF_UNIX transport",
      powers: "GDK titles start and sign in (Hogwarts, Lara)" },
    { source: "CrossOver 26.3 + Apple GPTK", origin: "CodeWeavers, Apple", kind: "configured",
      took: "Wine runtime, D3DMetal for D3D11/D3D12",
      changed: "Per-game environments from a host-link-free template, real GPU identity for Nixxes GPU checks, D3DMetal for D3D11",
      powers: "Rendering at 100â€“350 FPS on M5 Max" },
    { source: "Microsoft services", origin: "Xbox / Store APIs", kind: "used",
      took: "Package CDN, licences, XSTS, owned collections, DisplayCatalog, Game Pass lists",
      changed: "First-party device-code sign-in for collections; catalog lookups for art and identity",
      powers: "Owned library, art, downloads, licences" },
    { source: "Heroic Games Launcher", origin: "Heroic-Games-Launcher", kind: "rejected",
      took: "Researched as a host for an Xbox store plugin",
      changed: "Not adopted: plugin model and Electron shell did not fit a native, casual-gamer Mac launcher",
      powers: "Informed the decision to build native" },
    { source: "Architrave", origin: "dragoshont/architrave", kind: "process",
      took: "Spec-driven delivery, gates, independent reviews, durable runs",
      changed: "Slice specs with live-evidence closure, package admission with a preserved signed engine",
      powers: "How every slice above was planned and accepted" },
    { source: "Xodus.app", origin: "dragoshont/xodus-macos-app", kind: "built",
      took: "â€”",
      changed: "Native SwiftUI launcher: Library, Installed, Import, Play, art, Continue Playing, PC games, Install, Repair, Uninstall, game sign-in",
      powers: "Everything the user touches" },
    { source: "Private backend", origin: "xodus-macos-private-ai (local)", kind: "built",
      took: "â€”",
      changed: "Manage tool (install, uninstall, sign-in), generic launcher, package residency verifier, environment template",
      powers: "The app's install, launch and repair actions" }
  ],

  parity: [
    { feature: "Installed library, Play, recently played", state: "matched" },
    { feature: "Owned games library", state: "matched", note: "PC titles" },
    { feature: "Install, repair, uninstall, import", state: "matched", note: "Supported titles" },
    { feature: "Search the Store, browse Game Pass", state: "planned", note: "B1â€“B2" },
    { feature: "Compatibility before install", state: "planned", note: "B3 Â· Xodus-only" },
    { feature: "Game Pass subscription and installs", state: "planned", note: "B4 proved licences work · B5 UI" },
    { feature: "One sign-in", state: "planned", note: "B6" },
    { feature: "Stop game", state: "planned", note: "B9 backend live" },
    { feature: "Cloud save sync", state: "deferred", note: "Local-only; upload not implemented" },
    { feature: "Achievements, online play", state: "unproven", note: "Xbox user works for some titles; SISU fails for others" },
    { feature: "Install location, add-ons, auto-update", state: "deferred" },
    { feature: "Purchase", state: "out", note: "Link to Store page" },
    { feature: "Friends, party, invites", state: "out" },
    { feature: "Cloud gaming", state: "out" }
  ],

  events: [
    { at: "2026-10-07T21:43:59+03:00", text: "B10 done: cloud saves deferred (local-only); online works for some titles, SISU sign-in fails for others" },
    { at: "2026-10-07T21:43:59+03:00", text: "B9 backend: Stop game command stops Lara in 4 s with no leftovers" },
    { at: "2026-10-07T21:39:40+03:00", text: "App B1–B3 source frozen (7c3c1f0) and approved; CI and packaging next" },
    { at: "2026-10-07T21:39:40+03:00", text: "Game Pass title Abiotic Factor installed and running on Mac (D3D12, 59 FPS); online sign-in still fails" },
    { at: "2026-10-07T21:22:13+03:00", text: "B4 done: Game Pass licences issue through the service; non-entitled titles are refused" },
    { at: "2026-10-07T21:16:49+03:00", text: "B3 backend: pre-download check live; unsupported installs stop before any download" },
    { at: "2026-10-07T21:12:36+03:00", text: "B1 root cause found: one non-PC flag fails the whole Discover page. B1+B2 handed to the app owner." },
    { at: "2026-10-07T21:00:00+03:00", text: "Phase 2 backlog and Xbox app parity matrix published" },
    { at: "2026-10-07T19:25:00+03:00", text: "S3, S5, S6 accepted live: fresh install, play, repair, uninstall" },
    { at: "2026-10-07T18:40:00+03:00", text: "Keychain prompts approved by the user; acceptance resumed" },
    { at: "2026-10-07T17:20:00+03:00", text: "Backend for install, uninstall and game sign-in committed" },
    { at: "2026-10-07T16:35:00+03:00", text: "Lara Croft plays from the app's Play button" }
  ]
};
