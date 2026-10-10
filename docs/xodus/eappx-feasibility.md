# Celeste EAppx feasibility and implementation plan

Evidence checked on 2026-10-10. This plan concerns the Microsoft Store PC edition,
not the Steam, Epic or console edition. No game download, license retrieval,
runtime launch, deployment or account change was performed.

## Decision

**BOUNDED_GO for discovery and the package pipeline; DEFER a playable-Celeste
promise.** Encrypted package extraction has an existing implementation to build
on. Bundle selection, licensed key matching and installation semantics are still
missing. Celeste also declares .NET Native frameworks, making activation and
runtime compatibility a separate, material risk. An extracted executable is not
evidence of a playable game.

The initial implementation window is one public-metadata command, focused model
and filtering tests, documentation, and CI. No Wine fork, license adapter,
download or launcher changes belong to this window. Stop expanding scope once
the command is implemented and its checks pass. Revisit the package lane when an
authorized manifest/header observation becomes available.

## Verified product and repository facts

The [official listing](https://www.xbox.com/en-us/games/store/Celeste/BWMQL2RPWBHB)
identifies Celeste. The public
[Microsoft catalog response](https://displaycatalog.mp.microsoft.com/v7.0/products/BWMQL2RPWBHB?market=US&languages=en-US)
reports:

| PC property | Observed value |
|---|---|
| Package format | EAppxBundle |
| Package identity | MattMakesGamesInc.Celeste_20.9.11.2_neutral_~_79daxvg0dq3v6 |
| Architectures | x64 |
| Content ID | 076c8c94-e612-bc34-08b9-b5e773ea7955 |
| Application ID | App |
| Minimum desktop version | Windows 10.0.16299.0 |
| Framework | Microsoft.NET.Native.Framework.2.2 >= 2.2.27912.0 |
| Runtime | Microsoft.NET.Native.Runtime.2.2 >= 2.2.28604.0 |
| Visual C++ framework | Microsoft.VCLibs.140.00 >= 14.0.27810.0 |
| Catalog download estimate | 1,264,963,872 bytes |
| Catalog install estimate | 1,210,236,928 bytes |

These are catalog declarations, not inspected payload facts. The Xbox console
package is separately declared as XVC; it is not a PC fallback. The catalog's
application record does not expose an executable, EntryPoint, RuntimeBehavior or
TrustLevel. Bundle identity `neutral` does not override the declared x64 payload.

Local Windows has Xbox, Store and Gaming Services installed, but no current-user
Celeste package registration or Start app. All-users inspection was denied;
no elevation or WindowsApps permission change was attempted. The parent session
also observed no local Celeste package on its Mac.

Repository seams:

- `xodus-cli/src/commands/streaming.rs` selects only `.msixvc` from Store files.
- `xodus-cli/src/commands/run.rs` requires `.xodus-streaming.msixvc`, XVD metadata
  and executable mapping. An EAppx install cannot reuse this path unchanged.
- `extract-eappx` handles a local package plus explicitly supplied keys, not a
  Store install. Its startup no longer initializes credentials or provisions a
  device.
- SPLicense content keys use single UUID identifiers; EAppx key IDs use GUID
  pairs. Matching has not been observed for Celeste and must not be guessed.
- The upstream parser reads package/bundle headers and block maps. Its app
  manifest model exposes Identity only, not activation or framework dependencies.

## Other attempts: what they prove and what they do not

Search-engine summaries were used for discovery, not as proof of compatibility.
The following conclusions use the projects' own documentation/source:

| Attempt | Source-backed result | Relevance and limit |
|---|---|---|
| [eappx-rs](https://github.com/tuxuser/eappx-rs) | README describes work-in-progress `unpack`, `unbundle` and `info`, requiring per-content keys. Source includes EXPH/EXSH/EXBH handling. | Useful extraction reference and existing dependency; not Store installation or Celeste launch evidence. |
| [Microsoft MakeAppx](https://learn.microsoft.com/en-us/windows/win32/appxpkg/make-appx-package--makeappx-exe-) | Official packaging, bundling and supplied-key encryption/decryption tooling. | Windows fixture/oracle for legitimately created test packages. Does not furnish entitlement or license-key mapping. |
| [Wine-UWP](https://raw.githubusercontent.com/Rosentti/wine-uwp/master/README-uwp.md) | Experimental fork implements some UWP APIs and uses special DXVK. README lists a partially working Marble Maze, Minecraft hang and unimplemented APIs for other apps. | Evidence that some UWP execution is possible, contradicting a blanket impossibility claim. No documented Celeste success or macOS proof; not a drop-in replacement for the project's Wine runtime. |
| [CelesteLinuxifier](https://github.com/Wartori54/CelesteLinuxifier) | Runs a Windows Celeste installation with Mono; README specifically motivates the Epic edition lacking a Linux release. | Alternative-edition technique, not evidence for Store .NET Native packages. Do not substitute another edition or assume its IL/Mono structure exists in this payload. |
| [Microsoft .NET Native](https://learn.microsoft.com/en-us/windows/uwp/dotnet-native/net-native-and-compilation) | Explains compile-time native code generation from UWP IL and how it differs from ordinary JIT applications. | Declared .NET Native dependencies are a genuine runtime-risk signal. Installing generic Mono or desktop .NET is not established as sufficient. |

At the observed upstream eappx-rs HEAD
`e39222821f8004c663f61a917304e8f5f679b224`, `src/lib.rs` defaults extraction
checksum checks off, contains panic-based parsing/checksum failures, and joins
package filenames to the output path. `read_manifest` calls the buffer reader
without a crypto context. These are inspection findings, not an assertion that
Celeste triggers every issue. Before trusting it in automated installation,
validate path containment, metadata limits, integrity and structured failures
with synthetic fixtures. Do not simply enable an option and call the installer
safe. The repository dependency has no explicit revision in Cargo.toml; local
Cargo.lock has no EAppx entry, so do not claim that HEAD is the resolved revision.

An UnpEax repository and a Winetricks UWP issue also surfaced in search, but their
pages did not provide usable content in this investigation; neither is relied
upon as a verified solution.

## Ordered feasibility gates

| Gate | Smallest action | Evidence needed to proceed | Stop/defer condition |
|---|---|---|---|
| 1. Public discovery | `inspect-product` returns distinct desktop-package JSON before credential initialization. | Format, architecture, identity, dependency IDs retained; no key fields or download locations exported; legacy catalog parsing and MSIXVC paths preserved. | Missing/malformed required catalog data is an explicit error, not a compatibility verdict. |
| 2. Payload classification | Prefer read-only manifest inspection of a legitimately installed Windows copy. If absent, seek separately scoped permission for an entitled package observation/download. | Actual bundle manifest, application child and resources, executable/EntryPoint/RuntimeBehavior/TrustLevel, target family and dependencies. Header algorithm and key count, without keys or IDs in reports. | No entitled data/authorization, ambiguous activation, unsupported algorithm or missing dependencies. Do not download implicitly. |
| 3. Licensed key adapter | Compare package IDs and existing entitlement/license response in memory under explicit authorization; test a documented mapping with synthetic data. | Exact unambiguous mapping, key-size validation, missing/duplicate-key failures, no key material in logs/files. | No observed mapping: park adapter rather than force a single-key assumption or bypass entitlement. |
| 4. Validated bundle extraction | Isolated supplied-key public/synthetic package fixtures; validate before filesystem writes. | Single package and bundle, x64/neutral resources, dependencies, traversal/absolute paths, truncation, corrupt integrity, wrong keys, unsupported algorithm, rollback and pre-existing files. | Upstream panic/silent corruption or unchecked output paths remain; no production installation until fixed. |
| 5. Installation contract | Introduce explicit package-kind receipt and activation metadata, without fabricating an MSIXVC cache. | Atomic staged installation, correct architecture/dependencies, resumable failure cleanup, truthful receipt, original MSIXVC install/run regression checks. | Extraction alone or missing framework/activation requirements cannot produce an installed/ready status. |
| 6. Runtime experiment | Separately authorized isolated runtime/prefix, after gates 2-5. Inspect imports first, then one bounded launch probe on the actual target Wine/macOS stack. | Activation and dependency behavior, usable game window/input, entitlement behavior and safe save handling. | UWP/WinRT/.NET Native gap exceeds one bounded compatibility slice; do not grow a new Wine runtime here. |
| 7. Product integration | Parent owns launcher UX and any deployment. | Install, launch, readiness and failures accurately represented for each package kind; no regressions to existing MSIXVC games. | Unknown/blocked games must not be advertised as playable. |

Each future gate needs its own concrete bounded criterion; this plan is not
authorization to download, retrieve licenses, copy runtimes, launch games or
deploy. Synthetic tests establish mechanics, not actual Celeste compatibility.

## Choices and recommendation

**KEEP: staged EAppx support.** It benefits other packaged games and preserves
the current working MSIXVC path. Start with discovery and obtain the decisive
manifest before spending effort on launch integration.

**CUT: extension relabeling, skipping format checks, treating extraction as an
install, or copying the single-key MSIXVC assumption.** None addresses the actual
bundle, license or runtime contracts.

**DEFER: Wine-UWP fork integration and Mono conversion.** Their documented
contexts are not Celeste Store on macOS. Use them as narrowly scoped research
references only after the payload's activation/imports are known.

**PARK playable Celeste if the runtime gate fails.** A separately owned native
or classic desktop edition can be a product alternative, but it requires its
own entitlement and explicit scope; it is not success for Xbox Store support.

## Implemented first slice

`xodus-cli inspect-product BWMQL2RPWBHB --market US` uses the existing public
catalog API. It neither creates a token manager nor initializes the credential
store or provisions a device. Output is an allowlisted typed metadata projection,
deduplicated across SKU records and limited to Windows.Desktop. It does not
retrieve package files or licenses, export key IDs/CDN locations, select a
download, or assert support.

The related tests cover old catalog records lacking the new optional metadata,
the metadata projection's exclusion of key/download fields, SKU deduplication,
console exclusion and empty catalog handling. Linux/macOS CI must validate this
slice; this Windows environment currently lacks cargo/rustc.
