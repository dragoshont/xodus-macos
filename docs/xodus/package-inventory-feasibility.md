# Package inventory before download — feasibility and plan

Status: research complete, 2026-10-10. Nothing in this document requires downloading game payloads.

## Question

Can Xodus classify every owned and PC Game Pass game by package type (MSIXVC, EAppx, Appx, none)
and likely Mac portability **before** the user presses Install, so we know what we can port?

**Answer: yes.** Microsoft's public Display Catalog exposes the package format and install
capabilities for every product. It needs no sign-in, no licence and no download. A second,
already-implemented signed-in check (MicrosoftGame.config only) settles the remaining MSIXVC
cases.

## Evidence

### 1. Public catalog fields (no auth)

`GET https://displaycatalog.mp.microsoft.com/v7.0/products?bigIds=<≤20 ids>&market=GB&languages=en-GB`

Each `DisplaySkuAvailabilities[].Sku.Properties.Packages[]` entry has:

| Field | Use |
|---|---|
| `PlatformDependencies[].PlatformName` | `Windows.Desktop` = PC package; `Windows.Xbox` = console XVC (ignore) |
| `PackageFormat` | `MSIXVC` (GDK) / `EAppx` / `EAppxBundle` / `Appx` / `AppxBundle` / `MsixBundle` |
| `Architectures` | x64 / x86 / arm64 |
| `Capabilities` | `runFullTrust` (Win32 desktop process) vs. absent (sandboxed UWP); `customInstallActions` (installer-time actions, often anti-cheat or redistributables) |
| `FrameworkDependencies` | VCLibs, `Microsoft.NET.Native.*` (UWP .NET Native), `Microsoft.UI.Xaml` |
| `MaxDownloadSizeInBytes`, `MaxInstallSizeInBytes` | size before download |
| `PackageFamilyName` | stable key for a known-results table |

Product `Properties.Attributes` adds `XboxLive`, `XblOnlineMultiPlayer`, `XblCloudSaves` and similar flags.

### 2. Whole PC Game Pass catalog (GB, 521 products, ~27 requests, seconds)

| PC package format | Products | Share |
|---|---:|---:|
| MSIXVC (GDK) | 372 | 71% |
| No `Windows.Desktop` package | 127 | 24% |
| EAppx / EAppxBundle | 15 | 3% |
| Appx / AppxBundle / MsixBundle | 7 | 1% |

- 37 of 372 MSIXVC products declare `customInstallActions`. Examples: Fortnite, Call of Duty,
  Dead by Daylight, Chivalry 2, and also Gears 5, which works.
- 20 of the 22 UWP-era packages lack `runFullTrust`. These are pure UWP apps: Cuphead-like,
  Gears 4, Quantum Break, ReCore, Sunset Overdrive, RE7 (Windows 10 Edition) and others. The two
  exceptions with `runFullTrust` are Broken Age and World of Warships.
- "No Desktop package" products are third-party-launcher titles (EA app, Ubisoft Connect,
  Battle.net) or items sold under a separate PC product. Xbox cannot deliver them as packages.

### 3. Prediction accuracy against real Xodus checks (15 cached verdicts)

| Real verdict | Count | Catalog-only prediction |
|---|---:|---|
| Supported | 8 | all MSIXVC x64 ✔ |
| Package type unsupported (Cuphead, Celeste, Gears 4) | 3 | EAppx/EAppxBundle, no `runFullTrust` ✔ |
| No PC package (Age of Empires II DE) | 1 | no `Windows.Desktop` package ✔ |
| No executable in config (Fortnite) | 1 | MSIXVC + `customInstallActions` → "needs check" (Gears 5 has the same flags and works) |
| Missing `MSAAppId` (Subnautica) | 1 | MSIXVC → needs config check |
| Licence unavailable (Tomb Raider DE) | 1 | MSIXVC → needs licence check |

The catalog alone settles 12 of 15 (80%). The remaining 3 need the existing config check.

### 4. Runtime evidence for UWP (Cuphead, Celeste)

These failures happen at runtime, not at download. Both games download, decrypt and verify.
They then fail in Wine/CrossOver on UWP-only surfaces:

- Celeste: the loader writes the security cookie into read-only `.rdata`.
- Cuphead:
  - the .NET Native `mrt100` startup;
  - WinRT activation (`Windows.Graphics.Display.DisplayInformation`, `ResourceContext`);
  - missing `RtlQueryWnfStateData` and `CheckTokenMembershipEx`.

So "UWP without `runFullTrust`" is a strong **not portable now** signal: Wine has no complete
WinRT/CoreWindow application model. Porting this family means engine work, not launcher work.

## Proposed classification

| Tier | Source | Cost | Verdict produced |
|---|---|---|---|
| 0 | Public Display Catalog | none (anonymous, batched 20) | Format, size, family; one of: **Not on Xbox PC**, **UWP – not portable yet**, **Desktop (GDK) – likely**, **Desktop – needs check** |
| 1 | Existing `check` (FE3 + licence + `MicrosoftGame.config` only) | Signed in; a few MB; seconds | **Supported**, **Needs Xbox features (`MSAAppId`)**, **No game executable**, **Licence unavailable** |
| 2 | Xodus known-results table keyed by `PackageFamilyName` + version | none | **Plays on Mac** / **Launches, known issue** / **Fails: reason** (from our own launch outcomes) |
| 2b (optional) | Public anti-cheat database (e.g. AreWeAntiCheatYet, matched by title; licence to be confirmed) | none | **Anti-cheat: likely blocked** hint for `customInstallActions` titles |

Rules, in order:
1. No `Windows.Desktop` package → *Not available as an Xbox PC download*.
2. EAppx/Appx/Msix bundle **without** `runFullTrust` → *UWP — not portable yet*.
3. A Tier 1 or Tier 2 result exists → use it. A newer catalog version invalidates Tier 1.
4. MSIXVC x64 → *Likely* (with `customInstallActions`: *Likely — installer extras*).
5. Anything else (x86-only, arm64-only, Appx with `runFullTrust`) → *Needs check*.

## Implementation plan (smallest slices)

1. **Inventory script.** Add an `inventory` command to `private-xodus-manage.py`, or a Swift
   client in `XodusPreview`, that takes product IDs and returns a JSON row per product:
   - format, architectures, `runFullTrust`, `customInstallActions`;
   - frameworks, sizes, `PackageFamilyName`, version;
   - the Tier 0 verdict.

   Cache it per product with the catalog version. It needs no credentials, so it can run at
   Library and Discover load without blocking mutations. This replaces most of the c4729cb
   background `check` runs.
2. **UI.** Show a Mac-support chip on cards and heroes: Playable / Likely / Needs check /
   Not on Mac yet. Hide or de-emphasise Install for definite negatives, and add a filter.
   Discover and Game Pass get the same chip, so users can see what is portable before subscribing.
3. **Tier 1 on demand.** Run the existing config check only for *Likely* or *Needs check*
   games: on hover, on detail open, or idle-queued. Never run it for Tier 0 negatives.
4. **Tier 2 table.** Record launch outcomes per `PackageFamilyName` and version, from the
   journal and the first run, in the local runtime data. Optionally ship a curated seed list.
5. **Portability report.** Export the classified catalog (CSV/JSON) to the dashboard to track
   what we can port: MSIXVC coverage, the UWP backlog size, and anti-cheat exclusions.

Acceptance criteria:
- Inventory of 521 Game Pass products plus the owned library completes in under 30 s, with no sign-in.
- Every Tier 0 negative matches the existing `check` verdict for the 15 cached games.
- No Install is ever needed to learn "UWP" or "no PC package".

## Risks

- `customInstallActions` alone is not an anti-cheat signal (Gears 5 works). Keep it as a hint.
- Display Catalog data varies by market. Use the account market and refresh it with the catalog.
- `MSAAppId` and executable presence are only in `MicrosoftGame.config`, so Tier 1 remains
  necessary for about 20% of MSIXVC verdicts.
- Third-party-launcher titles might become portable later through their own launchers.
  That is out of scope for Xbox package delivery.
