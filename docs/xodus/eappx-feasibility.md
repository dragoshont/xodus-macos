# Celeste EAppx feasibility and implementation plan

Evidence checked on 2026-10-10. This plan concerns the Microsoft Store PC edition,
not the Steam, Epic or console edition. The initial discovery slices below did
not download or launch a game. Subsequently authorized Mac experiments obtained
and legitimately decrypted the actual package, then reproduced a native loader
failure. Celeste is **not playable**; see the final runtime evidence section.

## Package format versus application runtime: catalog census

EAppx is encrypted Appx, not an application model. Microsoft documents
[packaged desktop applications](https://learn.microsoft.com/en-us/windows/msix/desktop/desktop-to-uwp-behind-the-scenes)
separately from UWP: `RuntimeBehavior="packagedClassicApp"` or `"win32App"`
versus `"windowsApp"`, and `TrustLevel="mediumIL"` versus `"appContainer"`.
Older Desktop Bridge manifests can identify a full-trust executable through
`EntryPoint="Windows.FullTrustApplication"` and full-trust extensions.
Packages may also contain both UWP and desktop processes. A filename, executable
extension, missing .NET Native dependency or absent `runFullTrust` capability
alone does not establish the runtime or playability.

The anonymous GB PC Game Pass census on 2026-10-10 contains **521 products**:
372 MSIXVC, 10 EAppx, 5 EAppxBundle, 4 Appx, 1 AppxBundle, 2 MsixBundle, and 127
with no Desktop package in the selected catalog projection. The last category
does not itself prove a third-party launcher, absent entitlement or impossibility
of a separate PC edition. Cuphead and Celeste were separately inspected; they
are not in this particular Game Pass snapshot.

There is a concrete encrypted-package desktop candidate:
**World of Warships (`9NK9K07FDPJV`)**. An independent public catalog observation
reports EAppxBundle
`7458BE2C.WorldofWarships_15.9.0.2_neutral_~_x4tje2y229k00`,
`runFullTrust`, `Microsoft.VCLibs.140.00.UWPDesktop` and the application extension
`fullTrustProcess-fullTrustProcess`. This proves a desktop-process declaration
inside an encrypted package; it does not prove that every process is desktop,
that the bundle was inspected, or that the game plays in CrossOver.
Broken Age (`9MZZNL8J2MSZ`) similarly declares full trust and UWPDesktop VCLibs,
but its current catalog format is **Appx**, not EAppx.

`gamepass-package-inventory.sqlite` preserves the 521-product census, source,
market and date. `products` keeps packaging, architecture, capabilities and
dependency signals separate from `runtime_evidence`. The
`encrypted_desktop_candidates` view returns World of Warships. No gameplay
verdict is inferred. This initial database preserves one selected Desktop
projection per product, not every SKU/architecture/version variant.
`package-runtime-evidence.json` records the separately checked candidate
declarations, without license, key or download fields.

To regenerate a new database from the public snapshot:

```powershell
python scripts\package_inventory.py <snapshot.json> <new-inventory.sqlite> --market GB --observed-at 2026-10-10 --evidence docs\xodus\package-runtime-evidence.json
```

The input is an array with `id`, `title`, `fmt`, `arch`, `fullTrust`,
`customInstall`, `fw` (semicolon-separated frameworks), `drivers` (count) and
`attrs`. Unexpected fields are not persisted. Use `--snapshot-encoding cp1252`
for the original Windows-encoded census; UTF-8 is the default. The builder
refuses overwrite, malformed/duplicate records and evidence for absent products.
It does not fetch payloads or credentials. Refreshing the snapshot is separate
from importing it; observations are dated, not silently carried onto new versions.

Example query:

```sql
SELECT product_id, title, package_format
FROM encrypted_desktop_candidates;
```

## Reuse of the original Xbox-app runtime work

The preserved `dragoshont-xbox-app-shell-navigation-client` branch at
`c1ad528` contains **the exact two APIs reached by Cuphead**:
`CheckTokenMembershipEx` and `RtlQueryWnfStateData`. Its sprint README also
records successful activation of `DisplayInformation` with native
Windows.Graphics.dll and supporting shcore changes. The earlier conclusion
that there was no reusable local candidate was incomplete: this branch is
directly relevant and should be evaluated before another implementation.

However, relevance is not conformance or playable-game proof. The token API
checks token type, impersonation level and AppContainer state; it is not simply
a success stub. The WNF implementation models three hardcoded states measured
as empty on one Windows build. It allocates subscriptions but never delivers
display/DPI or language-change notifications. A native empty-state observation
does not establish correct ongoing host behavior. Neither this WNF layer nor
the experimental XAML composition drop-ins should be called a production-ready
CrossOver repair.

The Xbox-app handover explicitly records no submitted XAML frame, no visible
content proof and no sign-in/library proof. Its compositor work addresses XAML's
private composition interfaces; Cuphead uses Unity, so that entire rendering
stack is not established as a requirement for Cuphead. Package identity,
activation, WinRT resources and display prerequisites are potential shared
seams. Adopt only the reached, tested APIs on a matching runtime, not the whole
113-file research patch.

Celeste is also an observed UWP/.NET Native package and may benefit from those
shared seams after its separate loader/delayed-import failures are resolved.
Its next runtime path has not been observed past those failures; Cuphead's
exact downstream gaps must not be asserted for Celeste without a trace.
Neither game is currently playable.

### Narrow token-component evaluation

`scripts/macos/compatibility/cuphead-token-component.c` evaluates the token
membership seam using actual token queries and the runtime's existing
`CheckTokenMembership`. It does not invent group membership or package identity.
It rejects requested AppContainer/LPAC membership rather than returning
a success-shaped result. It is an experimental standalone DLL, not a replacement
kernelbase, installed API-set forwarder or production installer component.

The accompanying `token-membership-test.c` probes the native kernelbase export
by default, or a supplied candidate DLL. Twelve cases cover null effective
token, primary token rejection, impersonation token, flags 0/1/2, invalid flags,
membership and last-error behavior. Those cases passed against native Windows
and against the standalone component on both Windows and official CrossOver
26.3.0.39832. The CrossOver run used only the owned Cuphead bottle and vendor
wrapper; no shared runtime file or signature was changed.

Build the candidate and probe with the existing mingw toolchain:

```text
x86_64-w64-mingw32-gcc -Wall -Wextra -Werror -shared -o cuphead-token-component.dll cuphead-token-component.c -ladvapi32
x86_64-w64-mingw32-gcc -Wall -Wextra -Werror -o token-membership-test.exe token-membership-test.c -ladvapi32
token-membership-test.exe cuphead-token-component.dll
```

These controls do not cover restricted groups, actual AppContainer/LPAC tokens,
thread impersonation transitions or arbitrary invalid pointers. They establish
a narrow reusable API candidate, not full API conformance or Cuphead advancement.
The genuine resource DLL still binds its import to kernelbase; no import
rewriting, core-DLL override or shared-bundle deployment was performed.
Faithful WNF event/state behavior and package activation remain separate
unresolved prerequisites; the game was not relaunched against unchanged gaps.

`kernelbase-membership-backport.patch` adds the same limited API to the actual
matching vendor-source kernelbase export, without a DLL-name/ordinal bridge or
editing the game or genuine Windows resource DLL. In the isolated source/build
tree, `git apply --recount --unidiff-zero` and
`make -j4 dlls/kernelbase/x86_64-windows/kernelbase.dll` succeeded. The resulting
PE exports `CheckTokenMembershipEx`; candidate SHA-256:
`fb721e42e242f99ed26eda1cb4a84ab3edcaf0217d1fccb6edd4ae917c1651a9`.
This kernelbase candidate has **not** been loaded or installed. Compilation
and export inspection are not drop-in ABI compatibility or game-runtime proof.
The installed CrossOver bundle remained unchanged and passed deep/strict
signature verification after this build.

A subsequent ABI inventory found all 1,429 vendor named exports retained, with
only `CheckTokenMembershipEx` added; both binaries import only ntdll. This is
necessary but not sufficient compatibility evidence. An isolated probe tried
loading the candidate under the distinct filename `kernelbase-candidate.dll`,
without replacing actual kernelbase. Default loading did not expose the new
export (`ERROR_PROC_NOT_FOUND`, 127). An explicit candidate-name-only native
override failed module loading (`ERROR_MOD_NOT_FOUND`, 126). Neither run entered
the candidate's membership tests. Builtin core-module loading is therefore an
additional unresolved integration boundary; these results must not be recorded
as an API pass. The probe now prints exact load/export/token-setup errors.
No actual kernelbase override, shared installation change or game relaunch was
performed. The standalone DLL's earlier narrow contract pass remains distinct
from this failed source-built core-module loading experiment.

Follow-up inspection showed that the supported CrossOver wrapper rebuilds
`WINEDLLPATH` from bottle configuration, explaining why exporting that variable
did not select the candidate. A fresh, explicitly owned
`Xodus-Cuphead-Kernelbase-Probe` bottle was therefore created with the documented
`--param Wine:DllPath=...` setting, candidate-only staging first and vendor paths
retained. Its effective kernelbase still lacked the added export (127).
A second controlled test put the candidate at that probe bottle's actual
system32 kernelbase path and used a bottle-scoped `kernelbase=n,b` override.
The effective export was still absent. The probe bottle's original kernelbase
was restored byte-for-byte in `finally`, and its owned wineserver stopped.
No existing game bottle, installed app bundle or shared Wine engine was changed.
These distinct loading attempts establish that neither a standalone DLL nor
the attempted supported bottle path currently provides the missing core export.
Further deployment requires resolving builtin core-module selection, not
repeating these commands or reporting the source build as runtime support.

### Actual vendor-path test: resource activation advances

The matching loader's `set_dll_path()` unconditionally prepends its own vendor
`dll_dir` ahead of `WINEDLLPATH`. That explains the ignored bottle candidates.
After the parent explicitly confirmed no game/Wine process and held deployments,
one exact x64 vendor kernelbase file was backed up and temporarily replaced with
the source-built candidate. The official wrapper and existing vendor loader/server
were retained. The now **14-case** token probe, including absent SID and invalid
handle, passed against this actual kernelbase export in the owned probe bottle.

A second bounded test used the genuine, privately held Cuphead Windows resource
component in its already registered owned bottle.
`cuphead-resource-probe.c` calls `RoGetActivationFactory` for
`Windows.ApplicationModel.Resources.Core.ResourceContext`, releases its factory
and uninitializes WinRT. With the real kernelbase repair it returned **S_OK and
a non-null factory**, exit 0. The earlier unimplemented token-membership
exception is therefore resolved for this observed activation path. No WNF
implementation, fake token result, game patch or alternate runtime was involved.

Both tests used `finally` to stop only the owned bottle's wineserver, restore the
exact vendor backup and verify byte equality plus deep/strict signature. The
official bundle was restored after each test; parent deployment hold released.
This is genuine resource-factory activation advancement, **not** resource lookup
conformance, game activation, display support or gameplay. The candidate is not
left installed and no persistent playable-support verdict is justified.

### Display factory versus current-view requirement

`cuphead-display-probe.c` measures the documented base display ABI without
hardcoded DPI/orientation or fabricated events. It queries the real statics
factory, calls `GetForCurrentView`, and only queries actual display properties
if a view was returned. Its native Windows control successfully obtained the
factory but `GetForCurrentView` returned `0x80070490` (element not found) and a
null object in this ordinary unpackaged console process. Consequently, merely
implementing a display activation factory is not a complete game-launch
solution: a real current-view/CoreWindow activation context is another seam
that must be verified. This control does not prove Cuphead reaches that exact
error, and it was not counted as display conformance or gameplay.

The display probe compiles with the existing mingw toolchain using
`-Wall -Wextra -Werror -lruntimeobject -lole32`. Its interface method ordering
and GUIDs follow the public `Windows.Graphics.Display` declarations; it does
not register a class, alter the game or substitute a runtime. Full UWP launch
requires selecting the game-backed package activation and host window path,
not returning a synthetic display object from an unpackaged launch.

### Failure-explicit display diagnostic

After an explicit deployment/launch hold, a scratch-only matched-source ntdll
diagnostic exposed the reached WNF query/subscribe APIs returning
`STATUS_NOT_IMPLEMENTED` rather than Wine's missing-export exception. It did
not publish empty states, return success, fabricate change stamps or issue
notifications. With this diagnostic and the tested kernelbase repair,
the genuine Windows `DisplayInformation` statics factory returned **S_OK**.
`GetForCurrentView` then returned **0x80040154**, with actual missing activation
classes `Windows.ApplicationModel.Core.CoreApplication` and
`Windows.UI.Core.CoreWindow`.

This discriminates factory initialization from real view activation: the
native factory tolerates unavailable WNF on this observed path, while an
actual current view is still absent. It does not prove notifications unnecessary
for the game or justify shipping the diagnostic as WNF support.
The genuine Windows registry identifies the class providers as
`twinapi.appcore.dll` and `Windows.UI.dll`; vendor modules with those filenames
exist, but the matched source has no observed implementations of these classes.
The earlier Xbox work has a catalog-backed activation and shell-broker seam,
not a standalone CoreWindow drop-in.

Both shared DLLs were restored byte-for-byte in `finally`, deep/strict vendor
signature passed, and only the owned bottle's wineserver was stopped. The parent
deployment lane was released immediately. The failure-only WNF patch was then
removed from the experimental source and ntdll rebuilt successfully, so it is
not left in the reusable candidate. Diagnostic scripts/logs remain private
scratch evidence, not production compatibility components. Cuphead remains
unplayable; the next coherent target is genuine package/view activation.

### Manifest-backed Unity activation

The actual Cuphead manifest declares `UnityPlayer.AppCallbacks` and
`UnityPlayer.XamlViewManager` in-process classes in its original
`UnityPlayer.dll`, threading model `both`. This means Unity alone does not
exclude XAML-related requirements; the prior separation from Xbox's rendering
work remains a hypothesis until the game takes its real view path.

Only these actual classes were registered in the owned Cuphead bottle, using
their original payload module and manifest data. The generalized
`cuphead-resource-probe.exe <class> [--activate]` distinguishes factory lookup
from instance construction. Both factories succeeded under official CrossOver.
The genuine XamlViewManager instance also constructed (S_OK, non-null object).
AppCallbacks initially failed instance construction at missing
`Windows.UI.Core.CoreCursor`.

The authoritative local Windows activation registry maps CoreCursor to
Microsoft-signed `Windows.UI.dll` version 10.0.26100.7840, 1,439,464 bytes.
A private copy remains only in the Mac experiment, not the repository or a
shipping component. Registering its actual class mapping alone selected Wine's
builtin with the same name, which returned class unavailable. An explicit
**owned-probe-only** `windows.ui=n` override selected the genuine component.
Then AppCallbacks instance construction succeeded (S_OK, non-null object).
The trace also reported absent `ExtendedExecutionSession`, but that absence
did not prevent this constructor from succeeding.

These console probes ran `--no-gui` and reported no GUI driver for auxiliary
windows. They prove factory/constructor advancement, not graphics, actual
CoreWindow activation, package registration, input, entitlement or gameplay.
No game launch or shared runtime change occurred; the owned wineserver was
stopped after each probe. Native Windows DLL redistribution remains undecided
and these private experimental copies must not be shipped.

The private authoritative CoreApplication provider
`twinapi.appcore.dll` (Microsoft-signed, version 10.0.26100.1,
2,443,552 bytes) was subsequently supplied in the same experiment. Exact
registry-backed mappings for CoreApplication and CoreWindow, with
probe-scoped `twinapi.appcore,windows.ui=n`, allowed both genuine factories to
activate (S_OK, non-null objects) through otherwise official CrossOver.
This still does not create a view or supply package identity. A combined
actual-game diagnostic is prepared to distinguish that boundary from isolated
factory success; it requires a separately confirmed shared-runtime/launch hold.

### Combined actual Cuphead startup

After a fresh parent-confirmed hold, the original Cuphead executable was
launched through the official CrossOver wrapper in the Mac GUI Terminal
session. The temporary matching-source kernelbase repair, failure-explicit WNF
diagnostic and genuine private Windows UI/CoreApplication providers were
combined with the actual manifest-backed Unity registrations. The unmodified
game now reached **package/token identity prerequisites**:

- `NtQueryInformationToken`: unsupported `TokenSecurityAttributes`;
- missing `ntdll.RtlQueryPackageClaims`;
- missing `kernelbase.GetCurrentPackageInfo3`.

The bounded observer expired after 120 seconds (diagnostic status 124);
this is not process health or gameplay evidence, and no usable game window
was verified. Cleanup stopped only the owned bottle, restored both vendor
DLLs byte-for-byte and passed deep/strict signature verification. The owner
script completion status 0 records diagnostic cleanup, **not game success**.
The parent deployment lane was released.

This actual-game trace confirms the older Xbox branch's catalog-backed
package/token work is directly applicable, beyond the two initial missing
APIs. Its implementation includes Wine server token storage and explicit
registered-image activation, not merely a PE DLL export. It must not be
replaced with hardcoded package identity or imported as an unverified whole
runtime. A coherent CrossOver-compatible server/activation backport and
per-game catalog verification remain unfinished. The GUI diagnostic and
private native components are not shipping artifacts or support claims.

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

## Authorized experiment: local manifest inspection

The owner subsequently authorized experimentation and gap completion. Local
worktree experiments now include `inspect-manifest` for a supplied package or
bundle manifest. It reads at most 1 MiB, requires a supported manifest root
namespace, rejects malformed/multi-root/deep XML and DTDs, and does not initialize
credentials or make network requests. Executable and child-package filenames
must be relative without traversal; bundle filenames must be distinct ignoring
case. Errors produce failure, not guessed metadata.

Package output retains executable, EntryPoint, RuntimeBehavior, TrustLevel,
StartPage, TargetDeviceFamily and PackageDependency fields. Absent attributes
stay absent/unknown; this is intentionally not an inferred launch verdict.
Bundle selection is exact-architecture and non-stub, fails when ambiguous or
missing, and reports only matching/neutral resource candidates. It does not
silently substitute x64 on ARM64, resolve language/scale resources, verify bundle
offsets against a payload, extract files or produce an installation receipt.
Inspection does not replace full XML schema or package signature validation.

Synthetic fixtures exercise modern namespaced activation attributes, missing
attributes, ordinary XML entities, framework metadata, x64/ARM64 bundles,
neutral resources, unsafe paths, duplicate names, stubs, ambiguous applications,
truncation, multiple roots, DTD and size limits. CLI tests verify local operation
with an unavailable credential service and no output-package creation.
These are package mechanics, not a Celeste manifest or runtime observation.

The seven manifest unit tests and two local-manifest CLI tests passed on Linux
and macOS in [CI run 38071081756](https://github.com/dragoshont/xodus-macos/actions/runs/38071081756).
The initial experiment exposed a quick-xml API-version mismatch; the reader was
corrected to the repository's string API before the passing run.

Further real-data discovery did not obtain a package: `winget show --id
BWMQL2RPWBHB --source msstore` found no entry. The public catalog had no
`PackageDownloadUris`; its `PackageUri` used Microsoft's
`productingestionbin1.blob.core.windows.net` host but an 88-byte, anonymous range
probe returned HTTP 400. Redirects were disabled and no keys, licenses, account
tokens, CDN query strings or payload bytes were exported. This ingestion URI is
not established as a working distribution URL. Do not retry unchanged or treat
it as a download implementation.

At that checkpoint the next gate remained an entitled installed manifest or a
working authorized distribution path. The subsequent experiment below
supersedes that payload-availability blocker, not the runtime requirement.
The Architrave deterministic gate also remains unconfigured in this repository;
invocation of the installed gate reported `architrave.config.json not found`
with exit 2. Linux/macOS CI is observed; an Architrave PASS is not.

## Authorized Mac experiment: actual package and runtime

The owner explicitly authorized downloading and running Celeste on the Mac in
an isolated prefix. All package data and experiment artifacts remain on that
host under `~/xodus-app-tooling/celeste/`; they are not repository fixtures.
Neither the signed Xodus CLI nor the frozen CredentialBroker was replaced.

The broker-authenticated `packagespc` GetBasePackage operation returned no
package files for this product. This is a distribution-seam limitation, not
proof of absent entitlement. The actual working distribution path was the
catalog's fulfillment update category and Microsoft's FE3 service:

1. Obtain an anonymous service cookie and synchronize the product category.
2. Advance the returned category dependencies until synchronization stops
   yielding new update identities, even when `Truncated` is false.
3. Join core and extended fragments by update ID, not response-array position.
4. Select the exact catalog package identity and resolve its file locations.
5. Bind the selected location's file digest to authenticated FE3 file metadata;
   verify the complete download's declared length and digest.

No third-party embedded authentication token was used. Locations stayed in
memory and were not printed. FE3's TLS chain required Microsoft's Update root,
which was obtained from Microsoft's HTTPS certificate distribution and matched
against the existing Windows trusted root. Trust was scoped to the experimental
client; TLS verification was not disabled and no system trust store was changed.

Observed package facts:

| Payload property | Observed value |
|---|---|
| Downloaded bundle size | 1,264,963,872 bytes |
| Bundle SHA-256 | `18f395b052b40695240db396f4849c68ce754a073392c89c6a0eb288dc9bea10` |
| Bundle header | EXBH, XTS-AES, one key entry, 32-byte key |
| Application child | `Celeste.UWP_20.9.11.2_x64.appx` |
| Child header | Single encrypted package, XTS-AES, one key entry |
| Application executable | `Celeste.exe` |
| Actual activation entry point | `Celeste.UWP.App` |
| Actual target family | Windows.Desktop >= 10.0.16299.0 |
| Actual runtime | .NET Native; native `Celeste.dll` and Microsoft framework DLLs |

The existing broker supplied an entitled content key for the catalog content
ID. Its protocol does not preserve the license key identifier. Consequently,
the experiment did not infer a general GUID-pair mapping: it required an
authenticated plaintext SHA-256 for the encrypted executable to prove the
candidate binding before extracting the rest. Decryption produced a valid PE
executable with that exact digest; extraction then completed with upstream
checksum checking enabled for the application files. Key material was not
exported to files or logs. This observed single-package binding does not replace
an identity-preserving production key-adapter contract.

The declared x64 frameworks were also obtained directly from FE3 and verified
against its file metadata and their manifests:

- Microsoft.NET.Native.Framework.2.2, version 2.2.29512.0.
- Microsoft.NET.Native.Runtime.2.2, version 2.2.28604.0.
- Microsoft.VCLibs.140.00, version 14.0.33519.0.

### Reproduced native loader failure

The installed CrossOver initially failed before reaching the game with
`could not exec the wine loader`, and its original application bundle failed
resource-signature verification. An isolated loader-alias experiment was
abandoned. Under the owner's reinstall authorization, CrossOver 26.3.0 was
restored at `/Applications/CrossOver.app` from the matching official CodeWeavers
installer. Its Developer ID signature, team `9C6B7X7Z8E`, and notarization passed
verification. The original bundle and all existing bottles were preserved.
No loader alias or game-binary modification was installed into that bundle.

Using CrossOver's supported wrapper, both a template-derived Celeste bottle
and a pristine Windows 10 x64 bottle reproduced the same startup failure:

| Failure evidence | Observed value |
|---|---|
| Native executable loaded | `Celeste.exe` |
| Process exit | 5 |
| Exception | `0xc0000005`, write access violation |
| Faulting module/instruction | Vendor `ntdll.dll` RVA `0x45216`, `mov [r15], rax` |
| Write destination | `Celeste.dll` security cookie, RVA `0x2b9718` |
| Cookie section | `.rdata`, not writable |
| Initial cookie value | Standard MSVC default `0x2b992ddfa232` |
| Microsoft SharedLibrary cookie | Also in a non-writable `.rdata` section |
| Usable Celeste window | Not observed |

The target address in the exception matches the DLL's load-configuration
security-cookie pointer after relocation. Upstream Wine's
[`update_load_config`](https://github.com/wine-mirror/wine/blob/master/dlls/ntdll/loader.c)
initializes that cookie directly. This identifies a concrete loader memory-
protection compatibility gap before application activation; it does not yet
establish which later WinRT, input, graphics or Xbox-service APIs will work.

A subsequent authorized matching-source ntdll experiment passed synthetic
read-only, writable and preinitialized-cookie checks and removed the actual
Celeste cookie-writing fault. It exposed a .NET Native delayed-import callback
defect in `mrt100_app.dll`/`ucrtbase._strnicmp`, followed by a missing delayed
`mrt100.dll` import when the callback-name field was corrected. Celeste never
reached a usable game window.

The owner's final decision was **report upstream only; move on to a game type
that already works**. Runtime patching stopped. The complete official
CrossOver application was restored and passed deep/strict signature and
Gatekeeper verification with CodeWeavers' Developer ID. An existing Lara
bottle's Windows command bootstrap returned exit 0; full Lara gameplay was
not tested. The Celeste payload and experiment artifacts remain preserved.

The [upstream report drafts](celeste-wine-upstream-report.md) contain the
payload-free synthetic reproducer, observed repair results and subsequent
failure details. They have not been submitted. No experimental ntdll remains
installed, and this branch does not claim working Celeste or EAppx Store play.

These experimental FE3, extraction and launch operations are not integrated
Store-install commands in the current PR. Product installation receipts,
framework/package registration, activation and playable-game evidence remain
required before claiming EAppx Store support.

## Authorized Cuphead experiment

The owner subsequently authorized making Cuphead run, checking for a CrossOver
update, and testing matching-source drop-in repairs while retaining CrossOver.
This is a separate experiment from the stopped Celeste lane.

Cuphead's public PC catalog now establishes the previously uncertain format:
`EAppx`, x64, package
`StudioMDHR.20872A364DAA1_1.3.8.2_x64__tm1s6a95559gt`. Its console XVC is a
different payload and was not used. The exact PC application was downloaded
through official FE3, with declared size 5,399,841,994 bytes and SHA-256
`5f624bb79bb9192895fc9234f1b2777060f12e68c80f1dc4526f8e15c53e4814`.
FE3's declared SHA-1 and additional SHA-256 both matched the complete file.

The actual single-package header is EXPH, XTS-AES, one 32-byte key entry.
An entitled broker key passed the executable's authenticated plaintext hash
before extraction of the remaining files. Extraction completed for 808 files,
5,396,705,414 plaintext bytes, with checksum checking enabled. No keys were
written to disk or exported. The actual activation is `Cuphead.exe`,
`cuphead.App`, Windows.Desktop >= 10.0.15063.0, with .NET Native Framework 1.3,
Runtime 1.4, and VCLibs 140. The declared x64 frameworks were acquired and
verified; the game's own authenticated ClrCompression.dll was retained rather
than overwritten by a differing framework copy.

The official CrossOver updater advertised `26.3.0.39832`, exactly the installed
version; no newer official update was available at this observation.

### Actual runtime boundary

| Probe | Observed result |
|---|---|
| Untouched application and declared dependencies, stock CrossOver | `mrt100.dll.GetManagedRuntimeService` missing during mrt100_app initialization |
| Matching vendor-source ntdll candidate | Four synthetic loader checks passed; the actual missing OS runtime remained |
| Genuine Microsoft-signed mrt100.dll from the owner's Windows system, private experiment only | Application passed .NET Native startup, then failed WinRT activation for DisplayInformation and ResourceContext |
| Genuine Microsoft-signed display/resource components registered only in the owned Cuphead bottle | Windows.Graphics.dll reached unimplemented `ntdll.RtlQueryWnfStateData`; MrmCoreR.dll reached unimplemented `CheckTokenMembershipEx` |

Cuphead did **not** reproduce Celeste's read-only security-cookie write fault.
Its runtime callback supplied no DLL failure hook; changing the callback
handling could not supply the missing OS component. There is no evidence that
the tested ntdll replacement fixes Cuphead.

The experimental Wine-UWP implementation was also examined as a potential
component source. Its DisplayInformation code contains fixed orientation/DPI
values and success-returning event stubs; its documentation requires a special
DXVK runtime and lists missing input APIs. Neither establishes a faithful,
playable drop-in solution for this CrossOver/macOS target. Returning invented
WNF state, token checks, display values, or activation success is not a repair.

All actual launches were bounded to the experiment's own bottle. The ineffective
ntdll candidate was removed, the complete vendor CrossOver app was restored,
and deep/strict signature verification plus notarized CodeWeavers Developer ID
acceptance passed again. The payload, owned bottle, source candidates and logs
remain under `~/xodus-app-tooling/cuphead/` and the earlier Celeste source-build
directory; none of the proprietary components or payloads is committed here.

**Cuphead is not playable, and no new EAppx install-and-play support is claimed.**
The observed remaining work is real UWP display/resource activation and its OS
API dependencies, not package decryption or the original Celeste loader fix.
That work must preserve actual host-derived behavior and CrossOver compatibility;
a separate runtime, a substituted game edition, or a purchase needs a separate
owner decision. Installer integration remains unfinished and must not mark this
application supported based on successful extraction.

### Matching-source package and token continuation

The owner subsequently authorized reversible wineserver repairs, conditional on
using the latest official CrossOver and matching vendor source, and private use
of genuine Windows components from their own installation. CrossOver
26.3.0.39832 and the official 26.3.0 source were reconfirmed before these repairs.
The fresh source archive is 149,054,023 bytes, SHA-256
`ac99c8ca4b3848f3e81784135f023df266b61c2345726ea55a50b3e030dd6872`.
This is an observed release check, not a claim that this version stays latest.

The token/explicit-activation groundwork was reused from the Xbox-app
investigation; bounded package-claims and manifest parsing slices were taken
from its frozen `97fa66a` runtime. The whole cumulative runtime was not imported.
These are experimental source candidates, not changes to the production
installer or a replacement for CrossOver.

The original reused server protocol inserted requests among vendor requests,
renumbering existing operations and crashing unmodified vendor clients.
Moving extensions to the end was insufficient until `tools/make_requests`
regenerated the headers. The regenerated candidate preserves every original
vendor request ordinal. New token/activation/integrity requests remain
append-only. Server, Unix ntdll, PE ntdll and kernelbase must be built and tested
as a coherent tuple; mixing protocol generations is not supported.

Authentic Cuphead registration also exposed a BlockMap interpretation error.
EAppx `Block` hashes cover uncompressed encrypted bytes, not the extracted
plaintext. The signature-bound 2015 `FileHash` extension authenticates the
plaintext. `appx_file_integrity.py` keeps per-block verification for unencrypted
files, requires a valid signed plaintext hash for nonempty encrypted files, and
rejects size/hash/flag errors. A zero-length encrypted file may omit `FileHash`;
it still must have declared and actual size zero. Eight synthetic tests pass.
All 808 original Cuphead files and genuine declared frameworks passed the
private registrar's plaintext checks.

The retained registrar additionally checks receipts, PKCS7 signature mathematics,
signed AXBM binding, manifest identity/architecture and dependency closure.
Signature mathematics is not signer-chain trust, and this registrar does not
establish full original ZIP AXPC/AXCD/AXCT coverage. Previously acquired original
package integrity was checked separately. Registration is explicitly local
developer registration, origin 4, with 836 original game/framework files; it is
not a Store-install receipt, user authentication, AppContainer, or entitlement
grant. Activation is bound to the actual original executable and manifest.

| Discriminating control | Verified result |
|---|---|
| Integrated token attributes/claims | 21 assertions; absent identity and invalid handles do not report claims |
| Suspended original-image activation | 21 assertions; mismatched images rejected, launcher stays unpackaged |
| Package-graph generation oracle | 11 assertions on native Windows and the candidate |
| Explicit developer graph fixture | 56 assertions; head and three genuine Microsoft framework dependencies |
| Sandbox token-state oracle | 9 assertions on native Windows and the candidate |
| Package-family conversion oracle | 17 assertions on native Windows and the integrated candidate |
| Profile storage ordinal 114 oracle | 12 assertions on native Windows and CrossOver, including explicit synthetic-key cleanup |

The graph fixture is self-signed test data, not a substitute game identity.
Its publisher differs from Microsoft's: framework publisher IDs are derived
from their own actual manifest publishers using SHA-256 of UTF-16 and the
Windows publisher-ID encoding. Dependencies resolve their declared name,
publisher, minimum version and architecture. Current identity, package paths,
AUMID, OS max-tested version and staged paths follow the activated token and
actual manifests. No mutable directory or dynamic dependency is invented.

Vendor token integrity queries always reported high integrity, and its setter
returned success without storing a label. That was insufficient for an honest
`RtlCheckSandboxedToken`. The candidate stores token-local integrity, preserves
the high vendor baseline initially, copies labels on duplication, checks
query/set access and allows lowering. Raising a lowered label returns the
native-observed privilege-not-held result. This does not enforce a macOS
sandbox or create an AppContainer/capability grant.

The package API patch sequence uses `package-graph-identity.inc` and
`package-graph-manifest.inc` in kernelbase's `version.c`. In the existing private
build, changing these includes alone did not invalidate `version.o`; rebuilding
that known generated object explicitly is required when updating them.
The protocol patches require the earlier Xbox token/activation groundwork and
freshly generated protocol tables. These prerequisites must not be mistaken for
a self-contained, production-ready installation command.

### Private StateRepository dependency experiment

After the verified staged-path repair, the actual original Cuphead image reached
the missing export
`ext-ms-onecore-appmodel-staterepository-cache-l1-1-0.dll.SRCacheManager_Open`.
The Xbox investigation's cache provider is only a partial, failure-explicit
registry implementation; it never established an initialized working cache.
It was not used to fabricate repository state.

The owner's genuine Windows `Windows.StateRepositoryCore.dll`, Microsoft-signed,
version 10.0.26100.9444, 133,744 bytes, SHA-256
`6d274c7630fadee57f0b4ff8ef66e818b800b52e557e7aa8a3905c779002f295`,
was privately tested in the Cuphead bottle under its native name and the reached
API-set alias. No Windows registry database or another game's identity was
copied. Actual startup then advanced to missing
`KERNELBASE.PackageFamilyNameFromFullName`. This is an observed dependency delta,
not proof that the Windows repository service/cache works under Wine.
The alias files were removed after the test. No native binary is committed or
redistributed.

Each shared-runtime experiment saves exact vendor originals, shuts down only
the owned bottle, restores all four modules byte-for-byte, and checks
CrossOver's deep/strict code signature. Successful harness cleanup and an
alive-for-ten-seconds process do not indicate a running game: these actual
launches timed out and were terminated at 60 seconds with original-image exit
92. Screen capture returned 1, and no usable game window/frame/input was proven.
The private failure-only WNF diagnostic explicitly reports unavailable state;
it is not WNF support and is excluded from reusable patch claims.

The owner has now authorized broader test-Mac repair/reinstall/bottle work and
joint work with the mrt100 reimplementation session. That session owns mrt100
semantics; this lane owns Mac runtime mutation and controlled original-game
tests. A genuine-versus-candidate mrt100 trace is the next shared-runtime
comparison, not an automatic support verdict. Cuphead declares
`UnityPlayer.XamlViewManager`; XAML/composition/rendering remains unproven.
Celeste and other EAppx/UWP titles need their own actual activation, rendering,
input and gameplay evidence. Licensed installer integration remains unfinished.

### Joint mrt100 comparison and current boundary

The mrt100 session supplied its independently authored x64 service candidate.
The exact v0.1.0 release (source
`668bc7249b7eb0a9edb1be9821c9a995b349fbe2`, SHA-256
`9b067d6cbf550473ac1aea0f18ab8ae20792b99b9dbe8c4cb87ae4cd6a1651e0`)
and revised v0.1.1 (source `2913748`, SHA-256
`ffb3edee63b7608ae65dd971981689d456777940aa5aac37ea37260b6292ddc8`)
were each compared against the genuine private mrt100 under the same actual
Cuphead activation path. Both passed the observed .NET Native startup slice
and reached the same next failure as genuine, `profapi.dll` ordinal 114.
Their traces contained 29 startup entries: service acquisition, interface
query, write-watch reserve, commit, memory-load query and high-priority
thread creation. GC thread scanning and COM wait were not reached; this does
not prove those semantics, later execution, or gameplay. The genuine private
mrt100 was restored exactly after each comparison.

Private native profapi placement next to the executable and then in the bottle's
system32 did not replace the reached provider. Load traces showed `profapi.dll`
still loaded as builtin. This was treated as a placement/provider-selection
failure, not evidence that the real Windows ordinal returned success.
The native test component was Microsoft-signed 10.0.26100.9444, 179,192 bytes,
SHA-256 `ce53b082348fae6b4bef396c542a7415f39e1b28928233a9cc1b516311fb32c6`.
Both placements were withdrawn, including byte-exact restoration of the
preexisting bottle's profapi.

The matching vendor profapi source was then extended only at ordinal 114,
with `profapi-storage.c` and `wine-profapi-storage.patch`. The helper opens
existing HKCU AppContainer storage registry paths using the actual requested
access and returns the registry's HRESULT. It creates no key, AppContainer,
package identity, or cache. Missing state remains an explicit file-not-found
failure. The four-case native oracle binds the actual Windows system DLL:
NULL package preserves the output sentinel and returns E_INVALIDARG; invalid
package with NULL output requires no dereference; a missing package clears the
output and returns file-not-found, including empty optional segments.
The same unchanged executable passed against the integrated Wine candidate.

The resulting controlled launch used the coherent server/ntdll/kernelbase
tuple plus matching-source profapi, genuine private StateRepositoryCore, and
mrt100 v0.1.1. It advanced past ordinal 114 to the missing
`KERNELBASE.GetCurrentPackageGlobalizationContext` export. The original game
still timed out at 60 seconds with exit 92; no frame or gameplay was observed.
All five shared vendor modules and the genuine private mrt100 were restored
byte-for-byte, temporary StateRepository aliases removed, and deep/strict
CrossOver signature verification passed. Globalization context/locale/resource
semantics are the next shared investigation, not an empty-success stub.

The expanded profile storage test explicitly binds system32 and refuses to
overwrite an existing test key. It creates only the named
`Xodus.ProfApiStorageTest` synthetic fixture, verifies real root/child/subkey
handles and missing-child failure, then checks removal of its exact known keys.
The unchanged executable passed 12 assertions on native Windows and the
matching-source CrossOver module. This fixture is not game registration or
AppContainer creation. The isolated CrossOver control restored profapi exactly
and passed the deep/strict signature check without another blocked game launch.

Screen evidence was separately diagnosed: the Mac has one active, awake
2560-by-1440 display, but the probing process reports
`CGPreflightScreenCaptureAccess=false`. Screen capture failure therefore cannot
establish that no window was rendered. The private activation harness now
records top-level window count, visibility and dimensions for the actual game
PID only; these observations will supplement, not replace, frame/input/gameplay
verification. No capture permission bypass was attempted.

### CrossOver wrapper selection and COM policy

A direct CoreApplication control established a launcher-level error:
CrossOver's official `bin/wine` wrapper deletes `WINEDLLOVERRIDES` from the
environment unless overrides are supplied with its supported `--dll` option.
`--dlloverrides` is not that option. Previous env-only settings therefore did
not establish provider selection. The copied twinapi file is authentic
Microsoft `twinapi.appcore.dll` 10.0.26100.1, 2,443,552 bytes, SHA-256
`0d3e56c5068633f975bcab528267789f520f82a6d6a50f1330104e7597289b43`.
Without `--dll`, the load trace nevertheless identified the provider as Wine
builtin, with CoreApplication class-not-available and the reached ApplicationView
interface unsupported. With `--dll=twinapi.appcore=n`, the identical direct
factory probe loaded the native module and both actual reached interface IDs
returned S_OK, matching native Windows outside any package.

The private launch harness now passes overrides through `--dll`; previous
native-provider conclusions must be interpreted using observed load traces,
not the environment alone. With correct selection, the actual original Cuphead
image additionally reached missing `combase.CoGetSystemSecurityPermissions`.
The resource path still reached missing `GetCurrentPackageGlobalizationContext`.
The game PID had zero top-level windows and zero visible windows at 11 seconds,
and the 60-second run timed out with exit 92. All five shared vendor modules
and genuine mrt100 were again restored exactly and signature-verified.

`com-policy.c` and `wine-com-policy.patch` implement only configured COM policy
retrieval. They read the actual binary OLE permission/restriction registry values,
validate bounded self-relative descriptors and return a LocalFree-compatible
copy. No registry state or allow-all ACL is invented. Missing/malformed policy
returns a logged E_FAIL; invalid selector/output returns E_INVALIDARG.
Wine's resolver-backed default policy remains **unimplemented**, unlike the
four valid descriptors returned on the native Windows oracle. This is partial
capability, not full COM security parity or proof of authorization enforcement.
Vendor `CoInitializeSecurity` also remains a success-returning stub; this work
does not claim that COM transport authorization is enforced.

An isolated probe-bottle fixture tested actual configured descriptor bytes,
rejection of an out-of-bounds owner SID, and deletion of its temporary policy.
Five fixture checks and six error/output contract checks passed. No synthetic
policy was installed into the game bottle. An initial attempt using vendor
`RtlValidRelativeSecurityDescriptor` incorrectly rejected valid descriptors:
its implementation compares the BOOLEAN `RtlValidSecurityDescriptor` result
with STATUS_SUCCESS. This candidate avoids that defective helper and performs
bounded offset/SID/ACL validation locally; the global ntdll validator was not
changed. Vendor combase was restored byte-for-byte and the deep/strict signature
check passed after each isolated test.

The collaborator's first globalization handoff matched only an unpackaged
Windows process and unconditionally returned APPMODEL_ERROR_NO_PACKAGE.
That does not describe this explicitly catalog-activated Cuphead token and
manifest graph. It was not integrated. Real packaged context/property semantics
remain required; export binding alone or a contradictory no-package result is
not a support repair.

The next coherent actual-game experiment included the configured-policy combase
export and correctly admitted native overrides. It reconfirmed the official
26.3.0.39832 feed before patching. The missing COM export abort no longer
appeared; the observed remaining abort was
`GetCurrentPackageGlobalizationContext`. No COM policy was fabricated or
installed into the game bottle, and the absence of that abort is not evidence
of transport authorization or resolver-default parity. The game still had
zero visible/top-level windows at 11 seconds and timed out with exit 92.
All six shared runtime modules, the genuine private mrt100 and temporary native
aliases were restored/removed as appropriate, followed by deep/strict signature
verification.

The ApplicationView registration separately names bottle system32's
`twinapi.appcore.dll`. That file is Wine builtin, while the CoreApplication
registration names the authentic executable-adjacent copy. With native-only
selection, ApplicationView's system32 path failed to load. The next private
harness now preserves the exact bottle twinapi original and prepares the same
verified genuine twinapi at that system32 location; its cleanup restores the
bottle original exactly. Subsequent results are recorded below; simultaneous
runtime mutation remains excluded.

### Manifest-bound default globalization and private SHCORE probe

The collaborator corrected the globalization contract using genuine
`OpenPackageInfoByFullName` references on Windows. A two-application
StartMenuExperienceHost oracle disproved its initial single-context assumption:
contexts follow manifest application order and carry variable-length `GLOB`
records, including the actual UTF-16 application ID. The fixed header is 20
bytes; fields are tag, total size, flags, application-ID byte length including
NUL, and reserved zero. `"App"` produced 28 bytes; `"FullTrustApp"` produced
46 bytes. Properties 1 and 2 read flags bits 0 and 1.
Reference source/evidence: `dragoshont/xodus-winrt-shims` commit `73079cd`.
The source of non-default flags remains unknown. All sampled genuine contexts
had zero flags; this supports the **default subset**, not general manifest
override fidelity or a packaged-current Windows differential.

`kernelbase-package-globalization.patch` exports only the two reached functions,
implemented in `package-graph-manifest.inc`. Current contexts are allocated from
the existing identity-checked manifest, one per declared application in order,
and atomically published with the package-resource cache. They do not hardcode
Cuphead identity or copy Windows' opaque package-reference offsets into Wine.
Unpackaged identity and manifest/allocation failures propagate; out-of-range
indices preserve output. Property queries validate owned context pointers,
honor native buffer/error behavior, and reject foreign contexts without
dereferencing them. `GetPackageGlobalizationContext`/open-package references
and non-default flag sources are **not implemented**.

The private developer fixture now declares `App` and `Secondary`, both using
the test executable. `package-graph-test.c` retains its 56 graph checks and adds
21 globalization checks, including two distinct variable-sized records,
declared IDs, property sizing/full-buffer clearing, pointer lifetime, invalid
arguments and output preservation. The unpackaged executable adds four
no-package/null-output checks. Matching-source kernelbase and both tests built;
all these checks and prior token/sandbox/family/profapi/COM/generation controls
passed in the integrated tuple. The identical unpackaged executable also passed
all four checks against genuine kernelbase on the Windows host.

The authentic system32 twinapi placement passed six direct factory/interface
checks and was then exercised by actual Cuphead. Both registered native
providers loaded. Actual Cuphead called
`GetCurrentPackageGlobalizationContext(0)` and property 2, then advanced to
CoreWindow activation and missing **SHCORE ordinal 265**. This is an observed
resource-path advance, not just successful import binding. At 11 seconds the
original game PID still had zero top-level/visible windows; the 60-second
experiment timed out with exit 92.

For the next scoped experiment, the user's own Microsoft-signed
`C:\Windows\System32\shcore.dll`, version **10.0.26100.8117**, 1,022,152 bytes,
SHA-256 `e7b47e7b381849dd1b32fd67deedd16ad46b50aaf614cdd28e03a7bac7fd72c8`,
was transferred privately to the test Mac. It was temporarily placed only in
the owned Cuphead bottle's system32, with wrapper `--dll=...;shcore=n`.
No binary was committed, packaged or redistributed. The original bottle file,
82,512 bytes, SHA-256
`a55b6791154de75575945658eea4d831c97b967ade2325668ca16694630f5cf9`,
was preserved and restored exactly.

An initial fixture attempt incorrectly inherited `shcore=n` in the separate
probe bottle, where no native SHCORE was supplied. Its shell dependencies
failed and the fixture timed out. Cleanup still restored every modified file;
no actual-game result was claimed for that attempt. The corrected harness
restricts this override to the owned game bottle. All integrated controls then
passed. Genuine SHCORE loaded native during actual CoreWindow activation and
the next delay-load abort became
**`api-ms-win-gdi-dpiinfo-l1-1-0.dll.GetCurrentDpiInfo`**.
The game still had zero windows and exit 92. The collaborator owns the bounded
SHCORE/DPI contract investigation; Wine/native GDI is not replaced speculatively.

Each completed experiment restored all six shared vendor modules, genuine
private mrt100, bottle twinapi/SHCORE and temporary StateRepository aliases,
then passed deep/strict vendor signature verification. The private harness now
attempts every restoration even if an earlier restoration fails, accumulating
explicit failures rather than skipping later cleanup. Official feed/source
matching remains checked before mutation. These are experimental runtime
results; Cuphead gameplay, rendering, input and general EAppx support remain
unverified.

The collaborator's initial SHCORE candidate returned S_OK/100 without a DPI
query or CoreWindow QI. Its native oracle actually returned 140; omitting value
comparisons did not establish parity. That candidate was rejected. A subsequent
measured-effective-DPI candidate is still distinct from Windows' recommended
scale heuristic (the native host returned 140 while effective DPI was 168,
or 175%). Neither discrepancy is represented as genuine scaling fidelity.

A private, **failure-only** SHCORE argument probe added ordinal 265, logged
argument presence, left its output untouched and returned E_NOTIMPL. Actual
Cuphead passed `coreWindow=NULL` and a non-NULL scale output. It continued after
that explicit unsupported result, reaching
`bcp47mrm.GetApplicationLanguagesWithUserLanguagesFallback`. This diagnoses the
reached subset; it is not a scaling implementation or a supported runtime patch.
The diagnostic is retained only in session artifacts/private matching source,
not in this repository's reusable patch set.

One attempt stopped before the game because Wine stderr interleaved inside an
otherwise successful regression's stdout result line. Rather than weaken exact
assertion checks, the private harness now separates stdout from Wine debug
stderr for fixtures, regressions and the game. With that change, all prior
controls passed and the argument observation completed. The seventh shared
SHCORE module was restored exactly along with the other six; signature
verification passed.

The user's own Microsoft-signed `C:\Windows\System32\bcp47mrm.dll`,
version **10.0.26100.9278**, 224,664 bytes, SHA-256
`c8a840f9681ee5e9bccba59ef94f50ea9eb82db928869c7e3b559f80da077614`,
was next supplied privately in the owned Cuphead bottle's system32, using the
wrapper native override. Its destination previously did not exist. Native
language-provider loading was observed; the next abort was
**`KERNELBASE.FindPackagesByPackageFamily`**. The game still had zero windows
at 11 seconds and exit 92 after the bounded timeout. The language DLL and
StateRepository aliases were removed, the seven shared vendor modules and
private mrt100/twinapi restored exactly, and the signature verified. No Windows
binary or language/identity registry state was committed or invented.

`package-family-enumeration-probe.c` is a read-only native oracle for that
public API. The absolute module path was genuine System32 kernelbase; installed
Calculator supplied a real main package and two real resource packages.
The native probe establishes that character-buffer lengths count WCHARs,
HEAD selects the main package, DIRECT excludes it, and RESOURCE selects actual
resource packages. A NULL names array with a nonzero input count is invalid.
An input count of zero suppresses name/property/buffer writes even when the
character-buffer length is sufficient, but still reports the required outputs.
Short character buffers return 122; invalid pointers preserve outputs.
With otherwise valid sizing arguments, an empty family returns 234 and a
well-formed unknown family returns success/count zero. The frozen Xbox source
is not adequate authority for these details: it also enumerated neighboring
directories and inherited a publisher ID. That discovery strategy is not
imported. A justified implementation must enumerate the actual user's
registered catalog, including resource-package/filter/property semantics;
the current process's dependency graph alone is not a complete user catalog.
No replacement `FindPackagesByPackageFamily` implementation is claimed yet.

Read-only matching-source inspection located the existing trusted transport:
`server/package_activation.c` and `dlls/ntdll/unix/package_activation.c`,
`NtXodusQueryRegisteredPackage`. `load_catalog` reads the owned prefix's
`.xodus-local-packages/catalog.bin`, verifies owner/mode/bounds and every pinned
original-file SHA-256, and rejects malformed data. Its XPA1/version-1 schema has
one registered full name, app ID, family and image plus file receipts. Framework
manifests have receipts, but separate registered package records, resource roles
and enumeration-property metadata are not represented. This is the next
bounded backend seam; reading unrelated directories or treating receipt files
as additional user registrations would not supply the missing contract.

### Verified prefix registration inventory and legacy staged path

The first inventory slice now projects exactly the **one main registration**
represented by the prefix's XPA1 catalog. It does not turn framework file
receipts into registrations, infer resource packages, or scan neighboring
directories. `wine-registered-family-query.patch` adds a read-only server request
that reuses `load_catalog` and its ownership/bounds/original-file hash checks,
matches the requested family case-insensitively, and returns the registered full
name. Corrupt/unreadable catalog errors propagate. The kernelbase export
implements the observed name-array/character-buffer contract and verifies that
the returned full name derives the requested family. Dynamic and other
unimplemented inventory modifiers fail explicitly; this is not a Windows-wide
Store/service catalog implementation. The returned base package-property value
is zero; Windows deployment-mode/static/resource registration metadata is not
claimed. The actual language caller passed HEAD and no property output.

The new protocol request is appended after existing requests. The new ntdll
syscall is **x64 only**, pinned at the next available `0x0106`; x86 support is not
claimed. An initial build missed syscall regeneration. A two-architecture spec
attempt then collided because Wine's spec generator groups entries by name.
Neither incomplete tuple was deployed. The corrected x64-only build preserves
the original x86 table and the existing x64 syscall tail, including
`NtXodusActivateRegisteredPackage`, `NtXodusQueryRegisteredPackage` and
`__wine_rpc_NtReadFile`. Reproduction requires `perl tools/make_requests`,
`perl tools/make_specfiles` and rebuilding server/Unix ntdll/PE ntdll/kernelbase
as a coherent tuple; `version.o` must be refreshed after include changes.
The pinned ID is specific to this already-extended matching source and must not
be assumed free in another Wine build.

Fifteen activated-fixture checks cover the exact registered full name, no
invented resource/framework registrations, zero-capacity write suppression,
short buffers, argument errors and explicit unsupported dynamic inventory.
The 56 graph/21 globalization/four unpackaged checks and prior activation/token/
sandbox/family/profapi/COM/generation controls also pass.

With genuine private bcp47mrm, actual Cuphead now calls
`FindPackagesByPackageFamily` for its original family with HEAD. It advances
to missing `GetStagedPackagePathByFullName`. The legacy staged-path export was
then implemented by delegating to the existing
`GetStagedPackagePathByFullName2(PackagePathType_Install)`, with no new path or
registry discovery mechanism. Six activated-fixture checks pass. Actual native
language loading then advances to missing
**`KERNELBASE.OpenGlobalizationUserSettingsKey`**.

Both actual runs still report zero game windows at the first wait (about
15 seconds elapsed) and timeout exit 92. Native bcp47mrm, genuine twinapi/mrt100,
the failure-only SHCORE diagnostic and the original StateRepository provider
remain private experimental components, not shipped dependencies. Seven shared
vendor modules, bottle twinapi and genuine mrt100 were restored byte-for-byte;
temporary language/StateRepository files were removed and deep/strict signature
verification passed. The clean-room language handoff is not integrated: user
fallback evidence is useful, but installed-package/PRI selection must not be
described as unreachable for this genuine manifest/PRI/catalog-activated game.
The collaborator owns the reached settings-key ABI/semantics.

### Unredirected globalization user key and runtime ownership handoff

The collaborator's clean-room contract at `xodus-winrt-shims` commit `e1585e3`
establishes the three-argument NTSTATUS ABI for
`OpenGlobalizationUserSettingsKey(ACCESS_MASK, void *, HANDLE *)`. Its native
oracle compared opened registry-key names, not just successful status. The
unredirected desktop subset opens the real current-user hive root with the
requested access; the ordinary handle is closed with RegCloseKey/NtClose.
The matching-source implementation checks the machine
`CommonGlobUserSettings\RedirectedKey` first and explicitly refuses present
redirection rather than inventing its semantics. Other policy-open/query errors
propagate; no registry key or language setting is created. The current bottle's
persisted machine/user registry contained no CommonGlobUserSettings entry.
Multi-user/server token semantics are not claimed.

The new export passes 16 focused checks: export/provider presence, NULL-output
NTSTATUS, four access masks, exact opened-key-name equality with
RtlOpenCurrentUser and ordinary handle closure. The existing integrated controls
also pass. Actual Cuphead calls it repeatedly with access `0x20019` and a NULL
second argument; each observed call returns status zero. Native bcp47mrm then
reaches missing **`ntdll.NtQueryWnfStateData`** on a worker. No selected-language
output has been observed. Game exit is `0x80000100`, with zero windows at the
first wait (~16 seconds), not playability. The unobserved
`QueryGlobalizationUserSettingsStatus` success/default candidate is deliberately
not integrated.

Receipt prefix: `registered-cuphead-globalization-userkey`. All seven shared
vendor modules, bottle twinapi and original mrt100 were restored; temporary
native providers were removed and deep/strict signature verification passed.
The coordinator subsequently took exclusive Cuphead runtime ownership and
directed this lane back to Celeste. Concurrent bottles do not permit concurrent
shared CrossOver module replacement; future shared-tuple experiments require
explicit ownership coordination.

The required `python gates/gate_runner.py quality-gate` invocation returned
exit 2 because that runner is absent from this worktree. The focused
matching-source build/activated-fixture/actual-game receipts above are separate
evidence, not a substitute quality-gate PASS or completion claim.
