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

### Renewed Celeste original-image activation

With the user's renewed Celeste priority, an untouched-vendor launch in
`Xodus-Celeste-Clean` again exits 5 with the original write AV: ntdll RVA
`0x45216`, destination Celeste.dll security-cookie RVA `0x2b9718`. The app's
actual native dependencies include SDL2/FNA3D and .NET Native 2.2; a Unity/Cuphead
runtime equivalence is not inferred.

Preparing truthful local activation exposed two distinct format details.
The preserved child EAppx's signature offset (`1264949258`) is beyond the
extracted child stream, but references the signature bytes in the original
outer bundle. The original 10,987-byte PKCX signature was recovered from that
bounded outer region; PKCS7 signature math and its signed AXBM digest match the
actual child's blockmap. No credential/key operation or replacement signature
was used; signer-chain trust is still not claimed.

The Celeste blockmap explicitly marks encrypted files. Their Block hashes bind
ciphertext, not extracted plaintext. The signed FileHash matches every nonempty
decrypted original file. A zero-byte GUID-named entry has no blocks/FileHash;
its signed size and actual empty file uniquely bind empty content. The
experimental catalog verifier now requires signed FileHash for nonempty
encrypted plaintext, continues ordinary block validation for unencrypted files,
and records that ciphertext blocks were not checked by this registration step.
Six independently signed synthetic cases cover these domains, missing/wrong
digests, unknown encryption and the empty-file exception. This is not a
post-hoc hash receipt substituted for a signed plaintext digest.

The verified original main/framework registration contains 1,260 file receipts,
with real .NET Native Framework/Runtime 2.2 and VCLibs manifests. Its honest
activation origin is local developer origin 4, not authenticated Store
installation, AppContainer isolation or inferred privilege grants.

Under the coordinator's serialized shared-tuple lease, the four current
matching-source modules plus private genuine mrt100 were tested. The readonly,
writable and initialized cookie controls and named delayed-import callback
control pass. Actual original Celeste activation and token claims pass 13
checks. Native mrt100 loads, then the application exits `4103` (`0x1007`) after
about 643 ms; original PID observation reports zero windows. The precise
reached fatal activation is
`Windows.ApplicationModel.Core.CoreApplication`: `RoGetActivationFactory`
returns `0x80040154` (class not registered), followed by the language exception.
An earlier MemoryManager activation also fails, but its fatality is not proved.
Explorer additionally logs a WineMac driver failure in the SSH context; the
next rendering probe must use the GUI login context, not infer a graphics
defect from that observation.

Receipt prefix: `celeste-registered-genuine-mrt100`. All four shared vendor
modules were restored byte-for-byte, the temporary genuine runtime removed,
and deep/strict signature verification passed. The lease was explicitly released
to the Cuphead owner. Next bounded probe: the already measured genuine twinapi
CoreApplication factory in this game's own bottle, from GUI context, with
restorable registration and no fabricated interfaces.

The subsequent leased GUI-login probe binds genuine CoreApplication successfully.
Its temporary registration is
`HKLM\Software\Microsoft\WindowsRuntime\ActivatableClassId\Windows.ApplicationModel.Core.CoreApplication`,
with DllPath `C:\windows\system32\twinapi.appcore.dll` and wrapper
`--dll=twinapi.appcore=n`. Direct native controls verify CoreApplication IID
`1ada0e3e-e4a2-4123-b451-dc96bf800419` and ApplicationView IID
`a28d7594-8c41-4e13-9719-5164796fe4c7`; these are factory/interface checks, not
CoreWindow creation. Actual Celeste passes the missing-class boundary and
reaches unimplemented `combase.CoGetSystemSecurityPermissions`. Exit remains
4103 with zero windows (~710 ms); the SSH-context WineMac warning is not present
in this GUI trace. Four vendor modules, bottle twinapi and temporary registration
were restored/removed, private mrt100 removed, and signature verified before the
lease was released. Receipt prefix: `celeste-registered-core-provider`.

Adding the existing configured-policy-only combase candidate in the next
serialized GUI probe removes that missing-export abort. Six policy controls
pass; the game's bottle has no configured default policy, so each control
returns explicit `E_FAIL` with untouched output, not fabricated success.
Actual Celeste still exits 4103/zero windows (~609 ms), now originating
`E_INVALIDARG` (`0x80070057`) twice before its language exception. The precise
native return site is not yet established, so this is not attributed to
security policy, MemoryManager or CoreApplication.Run by inference.
Five shared modules and temporary bottle/runtime/registration changes were
restored, signature verified, and lease released.
Receipt prefix: `celeste-registered-core-policy`.

The complete validated Celeste five-module tuple is below. Candidate source
root is `~/xodus-app-tooling/celeste/wine-build`; shared destination root is
`/Applications/CrossOver.app/Contents/SharedSupport/CrossOver`. These identities
precede the parallel WNF work; changed candidates require revalidation.

| Candidate relative path | Shared destination | SHA256 before local signing |
| --- | --- | --- |
| `server/wineserver` | `CrossOver-Hosted Application/wineserver` | `7e9babbb12905045bd1b3c070523a068db0a9a3d601bd816d7b9eba2f5634079` |
| `dlls/ntdll/ntdll.so` | `lib/wine/x86_64-unix/ntdll.so` | `e4d34518f2627d73a0035223939ec53bea0dd9829b07238486f51c35cae34422` |
| `dlls/ntdll/x86_64-windows/ntdll.dll` | `lib/wine/x86_64-windows/ntdll.dll` | `66546068f0150760fdbafe4d5fd8406a5cbc3e1564eecb2ea4fb211aa445840a` |
| `dlls/kernelbase/x86_64-windows/kernelbase.dll` | `lib/wine/x86_64-windows/kernelbase.dll` | `9308d50feb4f7a702d9935708680a91b60c6e053082164284a809af35e32c73f` |
| `dlls/combase/x86_64-windows/combase.dll` | `lib/wine/x86_64-windows/combase.dll` | `f45f4721f5908aa4ed000ec484384955db5477f5d94222194da8f369be54a3b8` |

Only server/Unix module are locally ad-hoc signed. The owner harness refuses
active Wine processes, snapshots the candidates, uses unique backup/receipt
names and restores every target before vendor signature verification. Frozen
copies are retained under Celeste scratch as
`celeste-registered-core-policy-candidate-{server,unix,ntdll,kernelbase,combase}`.

The complete wrapper override for this Celeste probe is
`--dll=mrt100,mrt100_app,SharedLibrary,vccorlib140_app,vcruntime140_app,msvcp140_app=n,b;twinapi.appcore=n`.
Private genuine twinapi source remains
`~/xodus-app-tooling/cuphead/decoded-attempt1/twinapi.appcore.dll`
(SHA256 `0d3e56c5068633f975bcab528267789f520f82a6d6a50f1330104e7597289b43`),
temporarily staged into **the Celeste bottle's own** system32, never shared vendor
system32. Its temporary CoreApplication registration is listed above;
ApplicationView's existing system32 registration is unchanged. Private genuine
mrt100 source remains `~/xodus-app-tooling/cuphead/decoded-attempt1/mrt100.dll`
(SHA256 `4df94f5591a42d2e7a720cb9143eaa4dd4df972be3443e1a28209e4bc0f64c0a`),
temporarily staged next to the genuine Celeste executable, then removed.
No other genuine DLL, CoreWindow or MemoryManager registration was added.
This recipe does not replace the additional profapi/SHCORE/language/
StateRepository experimental providers already observed in Cuphead's lane,
nor establish cross-title support.

The final focused `+ole,+rpc,+seh` probe was admitted by the coordinator but
**refused before any mutation** because other Wine processes were active:
an NMS winewrapper (`--wait-children` on the real NMS executable), services in
`Xodus-BQVQTL3PCH05`, and Cuphead bottle services. These are not this lane's
owned Celeste launch. No unrelated game was stopped, no shared module changed,
and vendor signature verification still passed. Receipt prefix:
`celeste-registered-core-policy-trace` (owner exit 1, preflight refusal).
The exact E_INVALIDARG return site remains unobserved; retry requires a new
receipt label and genuinely quiescent shared runtime, not merely a lease.

A read-only comparison also prevents an unsafe WNF module merge.
`~/src/crossover-wine-xodus-afunix-26.3.0` and this lane's
`~/xodus-app-tooling/celeste/wine-source` have the same vendor VERSION
(`Wine version 11.0`), but **different server protocol and syscall ABI**.
The former lacks all NtXodus catalog transports and has x64
`__wine_rpc_NtReadFile` at `0x0102` versus this lane's `0x0105`;
x86 is `0x0108` versus `0x010b`. This lane additionally carries x64 activation
`0x0103`, query `0x0104`, family `0x0106`, and x86 activation/query
`0x0109`/`0x010a`. Therefore a WNF-built ntdll pair from the former tree is not
compatible with this lane's existing server/kernelbase tuple merely because
both are CrossOver 26.3.0. The bounded WNF source change must be ported into the
catalog-bearing tree under exclusive source ownership, preserving existing
IDs/protocol and rebuilding/regression-checking the coherent tuple. Both owners
were notified before any mixed deployment.

The fresh quiescent GUI trace establishes a concrete **ABI defect in this
branch's configured-policy candidate**, rather than a guessed missing policy.
The original native caller reaches `CoGetSystemSecurityPermissions` with
`rcx=0x10fd10` (output pointer) and `rdx=2` (kind). The candidate currently
declares `(int kind, PSECURITY_DESCRIPTOR *output)` and logs
`(1113360 0000000000000002)`, then returns E_INVALIDARG. That is immediately
followed by the observed `RoOriginateErrorW(0x80070057)` chain. Its own six
synthetic controls use the same wrong argument order, so their success is
**not native ABI validation**. Do not integrate the current COM candidate as a
faithful binding; the output-first signature needs independent genuine
System32 verification and correction across implementation/spec/tests before
the next actual run. Configured-policy-only semantics remain separate from
unimplemented resolver defaults.

Receipt prefix: `celeste-registered-core-policy-trace-quiescent`. Actual Celeste
still exits 4103 with zero windows (~722 ms). All five shared modules and bottle
twinapi were restored, temporary registration/private runtime removed and
vendor signature verified; the lease was released. The coordinator and native
contract researcher were sent the exact call and invalidated ABI evidence.

**Correction after the independent genuine oracle and import-table inspection:**
the trace establishes the candidate's received values, not a pointer-first ABI
for the named Windows API. Genuine System32
`CoGetSystemSecurityPermissions` is kind-first/output-second; kinds 0..3 return
valid LocalAlloc-owned self-relative descriptors, and pointer-first calls are
rejected. No argument-order code change was made here.

The actual cause of the apparent reversal is now the **ordinal binding**.
The genuine twinapi provider imports combase ordinals
157, 90, 69, 95, 122, 120 and 168, not the named COM policy export.
On this owner's genuine System32 combase those seven exports are NONAME;
the named `CoGetSystemSecurityPermissions` is ordinal 304. In the current
Wine candidate, auto-assigned ordinal 122 instead names
`CoGetSystemSecurityPermissions`, so twinapi's private ordinal-122 call lands in
the wrong function. Its pointer/int arguments are not proof of a changed named
API contract. Other imported ordinal collisions are likewise unsafe:

| Twinapi imported ordinal | Current Wine binding (not native identity) |
| --- | --- |
| 157 | CoRevokeInitializeSpy |
| 90 | CoDisableCallCancellation |
| 69 | CLSIDFromOle1Class |
| 95 | CoFreeUnusedLibraries |
| 122 | CoGetSystemSecurityPermissions |
| 120 | CoGetStandardMarshal |
| 168 | CoTreatAsClass |

The genuine private ordinal contracts now require independent investigation,
starting at reached ordinal 122. Do not reverse the named policy API, infer
default ACLs from this misbinding, or broadly remap private ordinals across
Windows versions without matching-provider evidence. Factory/IID controls
remain valid for their exact calls but do not establish downstream
CoreApplication.Run compatibility. The configured-policy subset still does
not implement genuine absent-policy resolver defaults; the oracle's native
success on that path is not permission to fabricate descriptors.

The required `python gates/gate_runner.py quality-gate` invocation returned
exit 2 because that runner is absent from this worktree. The focused
matching-source build/activated-fixture/actual-game receipts above are separate
evidence, not a substitute quality-gate PASS or completion claim.

### Private apartment identifier candidate: build and export-map hold

The genuine Windows oracle identifies private ordinal 122 as
`HRESULT CoGetApartmentIdentifier(UINT64 *)`, not the named security-policy API.
Its result is an opaque, stable identity for an actual apartment instance:
same-thread STA reconstruction creates a new identity, simultaneous STAs have
distinct identities, and an uninitialized thread can use the live process MTA.
No apartment returns `CO_E_NOTINITIALIZED` and clears the output. An OXID based
on PID/TID is unsuitable because it is reused after same-thread reconstruction.

`wine-combase-apartment-ordinal.patch` adds a lifecycle-owned monotonic identity
under Wine's existing apartment lock and queries the current or implicit MTA
apartment with reference release. It leaves RPC/OXID identity unchanged.
The defensive `NULL` output check returns `E_POINTER`; this deliberately differs
from genuine Windows' observed access violation and is not parity evidence.
`com-apartment-identifier-test.c` covers the initialized/uninitialized,
reconstruction, implicit-MTA and simultaneous-STA cases. Its matching-source
candidate and test executable compile, including `-Wall -Wextra -Werror` for the
test. The subsequently leased runtime run passes all 27 apartment controls;
this validates the bounded identity contract, not a complete COM implementation.

The first seven-slot spec candidate built successfully, but export inspection
found that adding explicit ordinal 69 raised Wine's ordinal base from 1 to 69
and shifted all 355 named exports. Preserving names alone does not preserve
ordinal consumers. That candidate is **not approved for deployment**.

The separate generated `wine-combase-stable-ordinal-spec.patch` instead pins
the entire matching build's existing named export map. It retains all 355
names and preserves 348 original ordinals. Only the seven names conflicting
with genuine twinapi's private slots move to unused ordinals 356..362:

| Preserved named export | Displaced ordinal | New ordinal |
| --- | --- | --- |
| CLSIDFromOle1Class | 69 | 356 |
| CoDisableCallCancellation | 90 | 357 |
| CoFreeUnusedLibraries | 95 | 358 |
| CoGetStandardMarshal | 120 | 359 |
| CoGetSystemSecurityPermissions | 122 | 360 |
| CoRevokeInitializeSpy | 157 | 361 |
| CoTreatAsClass | 168 | 362 |

The seven private slots are NONAME. Ordinal 122 implements the apartment
contract; the other six remain unsupported Wine abort stubs, not guessed
success/no-op or safely callable failure implementations. This is a
matching-source **x64 experimental map**, not Microsoft's full export map
or compatibility with all Windows-native ordinal consumers. Apply the stable
spec patch and the apartment implementation patch to the matching catalog
source with its existing COM-policy patch; do not reapply already-installed
implementation hunks.

The stable spec patch passes `git apply --check` and application on an isolated
temporary copy, with 362 unique explicit ordinals spanning 1..362. After the
coordinator released its ntdll source/build window, it was applied to the
matching source and rebuilt using `make -j4 dlls/combase/all`. The directory
target `make dlls/combase` was a no-op and was not counted as build evidence.
The rebuilt PE export-table audit verifies base 1, all 355 original names,
348 unchanged ordinals, the seven specified relocations and seven occupied
NONAME slots. Ordinal 122 points to the apartment implementation at RVA
`0x9130`, not the named policy function.

The audited combase SHA-256 is
`f4a2ba0ef1752b47a37480fb1a37371be8ec8576fa634421a69bea5ec09f8641`.
The coordinator's ntdll source files and built artifacts remained unchanged:
PE `0c6120e7b6738f3d79b4b96d94543aa62a7015362c4bed6d8028a6da250b04a9`,
Unix `e4d34518f2627d73a0035223939ec53bea0dd9829b07238486f51c35cae34422`.
This preserves its WNF and RtlReportException changes; their runtime behavior
is separate coordinator evidence.

The actual Celeste trace records 83 loaded paths. Inspection of 82 available
restored files found no additional combase ordinal consumers, but restored
Wine files are not the temporary genuine replacements. Inspecting the
authentic replacement providers separately confirms that twinapi imports
exactly the seven reported private slots and mrt100 imports none. This does
not cover future dynamically loaded providers or the wider UWP stack.

The remaining genuine Celeste framework providers were also inspected:
`mrt100_app.dll` (`c3f74a65...`), `vccorlib140_app.dll` (`6a32fd00...`),
`vcruntime140_app.dll` (`b8acc56e...`) and `msvcp140_app.dll` (`40d3e499...`)
have no direct combase ordinal imports. Their extracted payload and framework
copies have identical SHA-256 hashes. None of these four, genuine mrt100 or
genuine twinapi has a direct combase ordinal delay import. This does not
resolve API-set forwarders, runtime `GetProcAddress` calls or the other
libraries in twinapi's delay-import graph.

The researcher's 220-System32-module scan reports imported combase ordinals
no higher than 232. Thus relocations to 356..362 do not collide with that
sampled genuine import set. This is not a proof of safety for every caller:
existing Wine ordinal consumers of the seven displaced slots remain
incompatible, and future native providers need their own intersection audit.

The completed combase source/build window was released, followed by an
explicitly granted runtime window for apartment controls and original Celeste.
Receipt prefix: `celeste-registered-apartment-stable-map`. Apartment controls
pass 27/0, native factory/IID controls 6/0, configured-policy controls 6/0,
four loader regressions pass and original activation checks pass 13/0.

The actual original Celeste trace calls `CoGetApartmentIdentifier` with output
pointer `0x10fd10` and receives its real apartment instance identifier `1`.
It then reaches **private ordinal 120, CoRegisterForApartmentShutdown**, whose
unsupported Wine stub aborts from caller address `0x6fffff4ada77`. Celeste still
exits 4103 with zero windows (292 ms). This is a new causal boundary after the
repaired ordinal binding, despite the unchanged final game exit code.
No CoreWindow activation request was observed in this trace; the coordinator's
Cuphead CoreWindow frontier is not a reproduced Celeste result.

The genuine ordinal-120 oracle was requested before implementation: its
internal three-parameter contract, middle `UINT64 *` direction, callback and
cookie lifetime remain unverified. No output clearing or fabricated registration
success has been added. All five vendor modules were restored and signatures
verified, bottle twinapi restored, temporary CoreApplication registration and
private mrt100 removed. The runtime lease was released. Neither rendering nor
playability is established.

### Apartment shutdown registration: validated lifecycle and next native boundary

Independent genuine-combase probes now ground private ordinal 120
`CoRegisterForApartmentShutdown(IApartmentShutdown *, UINT64 *, COOKIE *)`
and paired ordinal 121 `CoUnregisterForApartmentShutdown(COOKIE)`.
The registration outputs are the same real apartment identifier as ordinal 122
and an opaque registration cookie. The registry is per apartment; a foreign
STA/MTA cannot consume another apartment's cookie.

The initially reported "private listener IID" was corrected before implementing
it: `94EA2B94-E9CC-49E0-C0FF-EE64CA8F5B90` is public `IAgileObject`, not
`IApartmentShutdown`. A distinct-pointer oracle proves that registration QIs
only to check agility and releases that marker immediately. It retains the
original typed listener directly and invokes its slot-3
`OnUninitialize(UINT64)` on teardown. The public shutdown IID is not renamed,
and no method is invoked on the agility marker. Non-agile registration returns
`RO_E_MUST_BE_AGILE` (`0x8000001c`), writes the real apartment identifier and
clears the cookie.

The oracle also establishes one retained listener reference, an additional
protective reference during notification, exactly one callback and release of
both references afterward. Explicit unregister removes the registration and
releases the stored reference without notification. No apartment takes
precedence over invalid-cookie checks; teardown reentrancy returns
`E_UNEXPECTED` before cookie validation. Unknown, stale, NULL and
foreign-apartment cookies return `E_INVALIDARG` in an initialized apartment.

`wine-combase-apartment-shutdown.patch` wires real notifications into existing
apartment destruction, before registered objects and loaded libraries are
released. Registry mutations use `apt_cs`; listener QI, reference operations
and notifications do not run under that lock. Cookies are monotonic opaque
handles, not exposed allocation pointers, and are matched without dereferencing
caller-supplied values. Handle exhaustion fails explicitly instead of reusing
a stale cookie. Defensive invalid-output/listener checks return `E_POINTER`,
a documented deviation from native invalid-pointer faults.

The patch replaces the ordinal-120 abort stub and adds ordinal 121, relocating
only the newly conflicting named `CoGetStdMarshalEx` from 121 to 363.
The matching `dlls/combase/all` build succeeds; binary export audit confirms
base 1, all 355 original names, 347 unchanged named ordinals, the previous
seven relocations and this eighth relocation. Ordinals 120/121/122 are real;
five other private slots remain unsupported abort stubs.
Candidate SHA-256:
`363c507f4731462b4efdd55f1e17a5b26037ce8043b66c75c953baf340ba9fd8`.
The retained patch passes reverse-application checking against the actual
applied source using `--recount --unidiff-zero`.

`com-apartment-shutdown-test.c` compiles with `-Wall -Wextra -Werror`. It uses
independent COM declarations and a distinct IUnknown-only agility marker,
covering direct listener ownership, real notification, reentrant unregister,
cross-apartment rejection, stale cookies and implicit MTA. Under a separately
granted GUI runtime lease, all **35 shutdown controls pass**, including actual
STA/MTA teardown notification and reference balance. The previous apartment
identity controls pass 27/0, native factories 6/0, configured-policy controls
6/0, four loader regressions pass and original activation checks pass 13/0.
Receipt prefix: `celeste-registered-apartment-shutdown`.

The actual original Celeste calls the real registration implementation with
listener `0x14f178`, identifier output `0x14f188` and cookie output `0x14f198`,
then advances to the unsupported **RoGetDesignMode private ordinal 90**.
That abort leaves Celeste at exit 4103, zero windows, 525 ms. The unchanged
final exit code does not erase the newly observed progression past ordinal
120. No CoreWindow request or rendered frame was observed.

All five vendor modules were restored and signatures verified, bottle twinapi
restored, temporary CoreApplication registration removed and private mrt100
removed. The runtime lease was released before the coordinator's next Cuphead
run. Existing windows.ui, dxgi and ntdll changes remain applied and were not
modified by this repair. The next bounded repair is the reached design-mode
contract, grounded separately against genuine normal-host behavior; neither
designer-host fidelity nor complete game support is established.

### Normal-host design mode: validated, downstream cleanup abort remains

Genuine combase oracles establish private ordinals 90/157
`RoGetDesignMode[ V2 ](BOOL *)`: a normal non-designer process returns `S_OK`
and writes `FALSE` with no apartment, STA or MTA. A NULL output returns
`E_INVALIDARG`. `wine-combase-design-mode.patch` implements this bounded normal
hosting contract, not a XAML designer state or package identity override.
Existing named export ordinals and the eight prior relocations remain
unchanged. Matching-source build and reverse-application checking pass;
candidate SHA-256:
`63657660278e7053b583c65b256bbf1097e4af3579909dba5407790207ca0ee4`.

Under the explicit GUI runtime lease, all 21 independent design-mode controls
pass. Apartment identity 27/0, shutdown lifecycle 35/0, native factories 6/0,
configured-policy controls 6/0, four loader regressions and original activation
13/0 remain passing. Receipt prefix: `celeste-registered-design-mode`.

Actual original Celeste passes the repaired design-mode call, then aborts in
the named Wine `RoRevokeActivationFactories` stub (candidate ordinal 316).
Its preceding trace includes repeated `RoOriginateErrorW(0x80070057)` calls;
there may be an earlier causal failure masked by this cleanup abort.
The intended import binding and genuine cleanup contract need investigation
before implementing that name. Celeste still exits 4103 with zero windows
(365 ms), and no CoreWindow request or rendered frame was observed.

All five vendor modules and bottle twinapi were restored, temporary registration
and private mrt100 removed, and signatures verified before releasing both
source/build and runtime windows. No new SHCORE/windows.ui/dxgi candidate was
deployed in this Celeste probe. This advances one bounded native contract;
it does not establish game support.

### Activation-factory cleanup: non-aborting unsupported-registry subset

Genuine `RoRevokeActivationFactories` is `void(cookie)`, apartment-agnostic,
and tolerates NULL/unknown cookies without dereferencing them. The actual
native twinapi delay-imports it by name through
`api-ms-win-core-winrt-l1-1-0.dll`, not by a mismatched private ordinal.
Matching Wine's paired `RoRegisterActivationFactories` previously returned
`S_OK` while tracking nothing and leaving its cookie output untouched.

`wine-combase-activation-revoke.patch` explicitly rejects that unsupported
registration with `E_NOTIMPL`, a NULL cookie and a diagnostic (`E_POINTER`
for missing output). It supplies a correctly shaped void revoke that ignores
unknown cookies with a diagnostic: no registered table can exist in this
implementation. This is not real in-process factory registration/revocation
support, and a future registration implementation must wire corresponding
cleanup rather than retaining this empty-registry behavior.

Matching-source build and the independent control executable compile;
candidate SHA-256:
`362236ba4b9877e65b22ba0ada13aa5bb83cbfda2025586d6f146402836122d1`.
Under the explicit GUI runtime lease, all 11 cleanup/unsupported-registration
controls pass. Identity 27/0, shutdown 35/0, design mode 21/0, factories 6/0,
configured-policy 6/0, four loader regressions and original activation 13/0
remain passing. Receipt prefix: `celeste-registered-activation-revoke`.

The actual original Celeste no longer aborts in revoke. Its new trace explicitly
fails delay-loading **RMCLIENT.dll.HamConnectForExtendedExecution**, then
aborts on that missing entry point. It still exits 4103 with zero windows
(319 ms). No RMCLIENT.dll was found in the existing Celeste/Cuphead tooling
trees during the bounded read-only lookup. Authentic provider availability
and its dependency/contract boundary are the next discriminating evidence;
no invented extended-execution success or provider download was attempted.

All five vendor modules and bottle twinapi were restored, private mrt100 and
temporary registration removed, and signatures verified. Both source/build
and runtime windows were released. The new trace is progress in identifying
the underlying boundary, not a rendered-frame or playability result.

### RMCLIENT: explicit unavailable HAM boundary, not extended-execution support

Read-only analysis of two genuine twinapi call sites grounds the reached ABI
as `HRESULT HamConnectForExtendedExecution(const void *, HANDLE *)`. Both
callers initialize the output to NULL and check failure before using it.
The genuine provider exports this name at ordinal 32; the exact failure code
for an absent HAM service was not oracled because the research host has a real
server and calling it would create RPC side effects.

`wine-rmclient-unavailable.patch` adds a Wine builtin with **only this reached
export**, preserving ordinal 32. It does not interpret the descriptor, clears
the connection output, logs that extended execution is not granted and
returns `HRESULT_FROM_WIN32(RPC_S_SERVER_UNAVAILABLE)`. This HRESULT is a
synthetic backend-unavailable diagnostic, not genuine Windows parity.
Missing output returns defensive `E_POINTER`. There is no HAM/CRM/PSM backend,
connection handle, permission grant, MemoryManager factory or other Ham surface.

Under the explicitly granted source/build window, configure was regenerated
and the existing build rechecked with Homebrew bison/flex first in PATH.
`dlls/rmclient/all` and the independent test compile successfully. Protected
combase, ntdll, windows.ui, dxgi and bcp47mrm source hashes are unchanged.
The retained patch passes reverse-application checking against the applied
source. RMCLIENT candidate SHA-256:
`803a6491c762db37feca7c7d69fb2ba21a3bd4021dfd0c6695c81b53e96c720c`.

The separately owned GUI probe installs this provider only temporarily in the
Celeste bottle; it does not replace a shared vendor RMCLIENT module. All seven
unavailable-boundary controls pass, together with identity 27/0, shutdown 35/0,
design mode 21/0, cleanup 11/0, native factories 6/0, policy 6/0, four loader
regressions and original activation 13/0.
Receipt prefix: `celeste-registered-rmclient-unavailable`.

Actual original Celeste loads the builtin and calls HamConnect with a static
descriptor and output pointer. It handles the explicit unavailable result and
advances to a missing `Windows.Foundation.Collections.Detail.Vector` factory,
then aborts at private ordinal 95 `CoSignalPendingGitRegistrationWaits`.
The vector failure may be an earlier causal boundary; fixing the later abort
alone would not establish activation success. Celeste remains exit 4103,
zero windows (319 ms), with no rendered frame.

All five vendor modules and bottle twinapi were restored, signatures verified,
temporary registration/private mrt100 removed and the temporary bottle RMCLIENT
removed. Source/build and runtime leases were released. No proprietary provider
was acquired or distributed and no absent-service state was represented as
permission.

### GIT completion signal: verified synchronous-Wine empty-state subset

Genuine private ordinal 95 `CoSignalPendingGitRegistrationWaits` is void with
no arguments. Genuine normal processes with no pending registration waiters
return without signaling anything. The matching Wine source's sole standard
GIT implementation, `ole32/git.c`, marshals and inserts registrations
synchronously under its table lock; it has no pending-registration event,
completion-wait queue or producer paired with this signal.

`wine-combase-git-signal.patch` implements the corresponding empty-state
return with a TRACE diagnostic and preserves the void ABI at NONAME ordinal
95. It is not an implementation of native pending GIT wait signaling; adding
an asynchronous producer later requires corresponding real signaling.
Matching combase build and independent controls compile successfully.
Candidate SHA-256:
`52ed4c9a03781b8df2bb04d81af6f97fb39f5696f72bc85dc6b748098874862b`.

The leased GUI run passes all 18 real GIT controls: no-apartment/STA/MTA calls
return, and actual GIT registration, retrieval and revocation remain intact.
All prior apartment, shutdown, design-mode, cleanup, RMCLIENT, factory, policy,
loader and original-activation controls remain passing.
Receipt prefix: `celeste-registered-git-empty-waiters`.

Actual original Celeste passes the ordinal-95 boundary but still fails to find
the `Windows.Foundation.Collections.Detail.Vector` factory, then aborts at
private ordinal 69 `CoRegisterServerShutdownDelay`. It remains exit 4103 with
zero windows (514 ms). The Vector failure remains a potentially earlier causal
boundary, not proof that repairing successive aborts will create a working app.
The next required evidence is the reached ordinal-69 contract and the exact
Vector factory/IID behavior, not an unchanged runtime rerun.

All five shared vendor modules were restored with signature verification,
bottle twinapi restored, temporary registration/private mrt100 and bottle
RMCLIENT removed. Source/build and runtime leases were released before the
coordinator's Cuphead experiment. No rendering or playability is established.

### Detail.Vector: genuine provider direct-export trial

Genuine Windows oracle evidence identifies `wintypes.dll` as the provider for
`Windows.Foundation.Collections.Detail.Vector`, requested interface
`08c77958-89bf-5cf8-a9cd-c72147b9b3a9`. Direct
`wintypes!DllGetActivationFactory` succeeds on Windows. Its only direct combase
ordinal import is 90, the already implemented normal-host design-mode query;
other COM/WinRT imports are named and still require behavioral compatibility.
The consumer invokes slot 30, proving a minimum of 31 interface slots, not the
full interface size or a generic collection specialization. Concrete method
types and the direction/ownership of slot-30 pointer arguments remain unknown.

The coordinator authorized a runtime-only genuine-provider trial, not our-own
parity. Owner System32 `wintypes.dll` version `10.0.26100.7705`, 1,501,432 bytes,
was transferred privately to the owner Mac and verified against SHA-256
`5a94b019c7190d9ec47f98e0bc28dced70036f1f97e9a84ef9d75340e364158a`.
No proprietary binary is committed or included in a distributable artifact.
The initial trial stopped before mutation when the coordinator's active
shared-vendor experiment invalidated the app seal. Execution resumed only after
explicit restoration/release and a successful signature preflight.

Receipt prefix: `celeste-wintypes-direct-20261011`, under private
`~/xodus-app-tooling/celeste/wintypes-provider-trial`. The failure-counted probe
uses only the direct factory export and standard GetIids/QueryInterface/Release;
it neither traverses unknown vtable slots nor invokes slot 30. With bottle-local
combase candidate `52ed4c9a...`, the genuine native provider returns the exact
factory, GetIids returns the expected two IIDs, and all three QIs succeed:
IActivationFactory and IInspectable share the factory pointer; the requested
interface is a distinct tear-off. Controls pass 5/0, process exit 0.

Although an identical temporary provider copy was placed in bottle system32,
the loader actually selected the staged DLL beside the probe executable,
`Y:\xodus-app-tooling\celeste\wintypes-provider-trial\wintypes.dll`, marked
**native**. Thus this receipt proves direct provider availability, not system32
resolution or combase catalog routing. No registry was changed and no game was
launched. The finally receipt verifies original bottle combase
`a6d35827...` and wintypes `1404067f...` hashes restored, and CrossOver's
signature verification passed. No shared vendor module was modified.

The next discriminating probe is temporary class registration followed by
`RoGetActivationFactory` for the exact IID, with the same restoration controls.
Private ordinal 69 remains unsupported: the genuine caller registers a 2,000 ms
server-shutdown delay, but no real delay behavior has been implemented and no
success-shaped no-op was added. Rendering and playability remain unverified.

### Detail.Vector: verified temporary-registration routing

The existing runtime-only grant explicitly covered temporary class registration
and a native override. The coordinator released runtime and requested the
Celeste-only catalog probe; no source/build, game launch or unknown slot call
was authorized or performed. The direct-export probe and its receipts remain
preserved.

Receipt prefix: `celeste-wintypes-catalog-20261011`, in the same private trial
directory. Signature and Celeste target-identity admission passed. The exact
class key under
`HKLM\Software\Microsoft\WindowsRuntime\ActivatableClassId` was first confirmed
absent. The temporary `DllPath` was
`C:\windows\system32\wintypes.dll`, with argv overrides
`--dll=wintypes=n;combase=n,b`, authentic provider SHA-256 `5a94b019...` and
bottle-local combase candidate `52ed4c9a...`.

The probe executable was outside the provider staging directory. The loader
explicitly reports **native** `C:\windows\system32\wintypes.dll`, resolving the
direct trial's executable-directory ambiguity. `RoInitialize` succeeds, and
`RoGetActivationFactory` returns non-NULL objects with `S_OK` for both
IActivationFactory and the exact requested
`08c77958-89bf-5cf8-a9cd-c72147b9b3a9`. GetIids returns the expected two IIDs;
all three standard QIs succeed with the same factory/tear-off identity pattern
as the direct control. Failure-counted routing/interface controls pass 6/0,
process exit 0.

Finally cleanup deleted the temporary key and confirmed it absent, restored
original bottle combase `a6d35827...` and wintypes `1404067f...` with matching
full hashes, and verified CrossOver's signature. The restoration JSON records
the provider hash, registry restoration and both DLL baselines. No Cuphead
bottle or shared vendor files were modified; runtime was released.

This proves exact-IID registration routing to the genuine provider, not our-own
implementation parity, the behavior of slot 30, or progress in either game.
Celeste's latest actual-game result remains exit 4103 with zero windows; private
ordinal 69 is still unsupported. Any game experiment needs its own serialized
runtime scope and must preserve these distinctions.

### Cuphead stall capture: owned native sample, not first-frame evidence

The coordinator corrected the interpretation of Cuphead run9: the activation
harness waits 11 seconds, observes windows, then waits another 49 seconds and
calls `TerminateProcess(child, 92)` on timeout. Exit 92 is not a voluntary game
exit or evidence against a particular native fail-fast branch.

Explicit user-directed scope authorized one safe Cuphead diagnostic capture,
with temporary runtime swaps and verified restoration, but no disputed ABI
aliases or new module builds. A new private harness preserves genuine mrt100,
native twinapi and the stale `a6d35827...` combase baseline. It uses an explicitly
owned foreground wineserver, checked registry commands, frozen module copies,
backups and unique receipts. PID mapping, census completeness, historical Wine
lineage and reused-PID rejection were checked before the capture.

The first owner, `cuphead-owned-sample-20261011-1034`, stopped before game launch
or registry mutation: its live census omitted an exited wineboot ancestor.
Cleanup refused ambiguous ownership rather than reporting restoration success.
Retained successful `new_process` records then established the full chain:
owned initial Wine wrapper -> exited wineboot -> services -> service clients.
Identity-verified numeric-PID recovery stopped those clients and the owned
foreground server, restored all 13 runtime targets, removed the three
originally absent files and passed signature verification. Its `recovery.json`
records complete restoration; no game was launched by that owner.

The corrected harness retains historical requester-thread/process-handle
parentage and explicit launcher PID receipts. This is Wine process provenance,
not an assumed persistent macOS parent chain: CrossOver may double-fork and
reparent clients. Successful server startup replies print status `0`; decimal
activation Wine PIDs are matched against hexadecimal reply PIDs and paired
decimal Unix PIDs.

The one actual capture, `cuphead-owned-sample-20261011-1042`, maps activation
Wine PID 340 (`0x154`) to macOS PID 41826 with exact Cuphead image and start-time
checks. `/usr/bin/sample` captured that numeric PID for three seconds at a
one-millisecond interval, yielding 2,578 samples. The private report SHA-256 is
`8a6a589b03bf6af5b817e6f88e68c654fb874e75ce1356e635fab63003f35d4e`.

The macOS main thread is in the Wine/AppKit event loop and Mach message wait.
Other sampled threads reach `NtWaitForAlertByThreadId` /
`os_sync_wait_on_address`, or `NtWaitForSingleObject` and
`NtWaitForMultipleObjects` through `server_wait`, `server_select` and
`wait_select_reply`. These native frames are attributed to ntdll.so, loaded at
`0x2086ef000`; the frozen source hash is `e4d34518...`. Long repeated syscall
dispatcher frames limit the translated-process unwind. No twinapi PE caller
or exact blocking object is established by this sample; ordinary wait frames
alone do not prove deadlock or QI causality.

The game again activates successfully, loads genuine mrt100 and native twinapi,
creates two hidden windows (1280x720 and 1x1), survives the first 11-second wait,
and is terminated by the harness's 60-second timeout with exit 92. No visible
frame is verified.

The final receipt verifies all 13 runtime targets restored, the three temporary
files removed, original CoreApplication `DllPath` checked/restored as `REG_SZ`,
owned runtime processes absent and CrossOver signature verification successful.
Private sample, server/game logs and `receipt.json` remain under the unique
owner directory on the Mac; no raw capture or proprietary binary is committed.
Runtime was released. Further wait-object or PE-caller attribution is a separate
diagnostic step, not authorization for another launch or an ABI implementation.

### Cuphead filtered relay: unreturned native SRW acquisition

The next explicitly authorized single diagnostic launch used unique receipt
`cuphead-owned-relay-20261011-1053`, the same genuine-mrt100/native-twinapi/stale
combase baseline, and no new Wine module build or ABI change. A separate
activation controller waits 11 seconds plus 139 seconds before the existing
`TerminateProcess(child, 92)` timeout path. The original controller is untouched.
Checked temporary HKCU `Software\Wine\Debug\RelayInclude` configuration limits
relay to the requested synchronization exports; UI and SEH tracing were added.
No native sample or debugger attach was performed.

Activation Wine PID 456 (`0x1c8`) maps to macOS PID 45217. The successful startup
reply identifies primary Wine thread `01cc`; exact image/start-time and the
historical owned Wine lineage were admitted. The game again produces only two
hidden windows, remains alive after 11 seconds, then records
`APP_TIMEOUT=150000` and exit 92. No first frame is established.

The primary thread's final relay line is:

```text
01cc:Call ntdll.RtlAcquireSRWLockExclusive(6ffff7725ad0) ret=6ffff750f205
```

No matching return follows before termination. Actual native twinapi load base
`0x6ffff7500000`, PE image size `0x250000`, and frozen provider SHA-256
`0d3e56c5068633f975bcab528267789f520f82a6d6a50f1330104e7597289b43`
attribute the caller return address to **twinapi RVA `0xf205`** and the lock to
**twinapi RVA `0x225ad0`**.

Earlier, the same thread acquires the same lock successfully with return address
`0x6ffff750ed9c` (twinapi RVA `0xed9c`). Immediately afterward, stale combase's
unimplemented `CoGetSystemSecurityPermissions` raises
`EXCEPTION_WINE_STUB` (`0x80000100`). This is a concrete lock/exception sequence,
not proof the lock remains owned: release exports were not in the requested
filter, and unwind/cleanup and actual lock ownership are unresolved. Relay
return registers from a void lock method are not success/error result codes.

The last 30 primary-thread synchronization lines comprise repeated completed
shared acquisitions from MrmCoreR RVA `0xedff`, completed exclusive acquisitions
from its RVAs `0x45e62`, `0xee15`, `0x4a337`, then completed twinapi shared
acquisition from RVA `0x2c0fa` and the final unreturned exclusive acquisition
from RVA `0xf205`. MrmCoreR is loaded natively at `0x6ffff73d0000`, image size
`0x114000`, SHA-256
`6186f981d1aa1e1991697133aa066045097ab52227a240632a0c7d8fd04352be`.

UI trace records lazy experimental `GetForCurrentThread` and two rejected
`9ac40241...` QIs. Its last UI operation is the stub
`AccessibilitySettings.add_HighContrastChanged`. There are no observed
dispatcher/RunAsync/event-registration/ICoreWindow.Activate calls, and no
`err` or critical-section timeout lines. Absence of filtered relay output does
not establish that an unrelayed/inlined operation never ran. Another thread
`01d8` records handled RPC-unavailable SEH (`0x6ba`); it is not the primary
thread's final acquisition.

Initial cleanup refused a changed PID identity and intentionally left file
backups intact rather than signal an uncertain target or claim restoration.
Read-only reconciliation subsequently proved all traced/recorded clients absent.
The exact owned foreground wineserver PID 44994 retained its recorded
image/start time and was stopped by numeric PID. Recovery independently restored
all 13 target baselines, removed the three absent-baseline files, verified both
CoreApplication and temporary debug registry restoration, verified no remaining
Wine runtime, and passed deep/strict CrossOver signature verification.
`recovery.json` records complete restoration with no errors; runtime is released.

Private evidence hashes:

| Evidence | SHA-256 |
| --- | --- |
| `game.log` | `5a4095bde59883e89109ad2209249d853a64f9ea618346a714901b5d0f9ae09a` |
| `server.log` | `6e031f1ef803426beecdce12911bd0c40190e04f67605145509525c661fccf82` |
| `recovery.json` | `995acde7cdd9799682143dcc39edf7be13a0cd5be3f19c6f15fc9ed6fbb44a2e` |

The next discriminating action is bounded read-only inspection of cleanup/unwind
around the two already attributed twinapi callsites and their shared lock.
No additional launch, unknown-interface synthesis, Vector expansion or
ordinal-69 implementation is implied or authorized by this result.

### Public CoreWindow context/adapter contracts and corrected-combase control

Public Windows SDK declarations resolve the two formerly unidentified IIDs:

| Interface | IID | Public contract |
| --- | --- | --- |
| `ICoreWindowWithContext` | `9ac40241-3575-4c3b-af66-e8c529d4d06c` | IInspectable prefix; slot 6 is `get_UIContext(IUIContext **)` |
| `IUIContext` | `bb5cfacd-5bd8-59d0-a59e-1c17a4d6d243` | IInspectable, no additional methods; runtime class `Windows.UI.UIContext` |
| `ICoreWindowAdapterInterop` | `7a5b6fd1-cd73-4b6c-9cf4-2e869eaf470a` | IInspectable; seven adapter getters at slots 6-12, `SetWindowClientAdapter(IUnknown *)` at slot 13 |

Authoritative references are Microsoft's recompiled SDK
[windows.ui.core.idl](https://github.com/microsoft/win32metadata/blob/main/generation/WinSDK/RecompiledIdlHeaders/winrt/windows.ui.core.idl),
[windows.ui.idl](https://github.com/microsoft/win32metadata/blob/main/generation/WinSDK/RecompiledIdlHeaders/winrt/windows.ui.idl)
and [CoreWindow.idl](https://github.com/microsoft/win32metadata/blob/main/generation/WinSDK/RecompiledIdlHeaders/winrt/CoreWindow.idl).
These typed declarations replace the earlier unidentified-slot hold for these
interfaces only; they do not resolve internal UIContext interfaces or adapters'
concrete identities.

`scripts/macos/compatibility/wine-corewindow-context-interop.patch` extends the
owner's existing HWND-backed experimental `corewindow.c`, not generic upstream
Wine. Apply with `git apply --check` / `git apply` in that matching source tree;
do not force it onto a different baseline. It adds a separate per-window
reference-counted UIContext, owning outputs, its standard QIs/runtime class,
CoreWindow's two typed interface tear-offs, and stored/replaced/released adapter
references. All seven adapter getters name their boundary, clear the output
and return `E_NOTIMPL`. No fabricated adapter or callback behavior is added.
The existing lazy-window experiment remains an explicit limitation.

Only windows.ui was rebuilt (`make dlls/windows.ui/all`); the dependency step
regenerated the combase import library but did not rebuild combase itself.
The windows.ui candidate SHA-256 is
`08128df6393600d9645a9048bcf704b414addf76452a63ee0cda7e30d243977d`.
The failure-counted `corewindow-context-test.c` checks the public slot layouts,
stable UIContext identity, runtime class, standard QIs, all seven honest getter
failures, adapter replacement reference counts, and UIContext survival after
window destruction. Compile with MinGW and `-lole32 -luuid -luser32`; run only
through an explicitly owned, backed-up runtime harness, not an unowned bottle.
Observed result: **48 checks, zero failures**.

Provider admission exposed two distinct loader issues. A first pre-game attempt
(`...context-...1119`) failed because shared windows.ui replacement alone did
not replace the selected bottle/payload PE; it launched no game and was fully
recovered. The next launch (`...context-...1128`) verified the new context calls
but still used stale builtin combase despite a bottle-only `52ed4c9a...` swap.
It is **not** a valid corrected-combase control: the old stub exception and
150-second harness timeout remained. Both observations retain separate receipts.

The coordinator authorized one corrective control. Even explicit argv
`--dll twinapi.appcore=n;combase=n,b` was insufficient with stale shared vendor
combase: ordinal admission (`...context-native-...1131`) failed before any game
launch and was fully restored. The final control therefore temporarily backed
up and replaced both bottle and shared vendor combase, as well as the selected
bottle/payload/shared windows.ui surfaces. All replaced combase copies were
pinned to `52ed4c9a03781b8df2bb04d81af6f97fb39f5696f72bc85dc6b748098874862b`.

Final unique receipt: `cuphead-owned-context-pinned-20261011-1140`.
Before the game, actual loaded `C:\windows\system32\combase.dll` resolves ordinal
122 at RVA `0x90f0`, matching that pinned PE's export table. Apartment-identifier
checks pass **27/0**, followed by the context/adapter checks **48/0**. Wine
labels its built PE `builtin`; this is a verified replacement Wine provider,
not an authentic Windows-native combase or proof based on argv alone.

The game activates as Wine PID 584 (`0x248`), primary thread `024c`, with genuine
native twinapi loaded at `0x6ffff72c0000`. The stale
`CoGetSystemSecurityPermissions` stub exception is absent. Relay now records
successful acquisition **and release** of the same twinapi lock:
acquire caller RVA `0xed9c`, release caller `0xef2a`, acquire caller `0xf205`,
release caller `0xf396`. It passes the previously unreturned acquisition.

The game successfully queries `ICoreWindowAdapterInterop` and calls only
`get_ApplicationViewClientAdapter`, which returns the intended `E_NOTIMPL`.
It calls `get_UIContext` twice and queries internal IID
`f21e14c1-e669-52af-99cd-171eee8e940d` twice; that unsupported interface remains
`E_NOINTERFACE`. Neither `SetWindowClientAdapter` nor other adapter getters are
observed in the game trace.

The next observed unsupported CoreWindow QI is
`84acbbfe-649c-5ac7-8130-7b6b76db463c`. It is followed by unhandled
`0xc0000409` at genuine MrmCoreR RVA `0x49943` (base `0x6ffff52a0000`);
that temporal sequence alone does not prove the QI caused fail-fast.
Missing Detail.Vector activation and unavailable CoreCursor also appear.
The controller observes a **real early exit 3221226505 (`0xc0000409`)** after
1,146 ms, zero remaining/visible windows, and no timeout termination.

The live-sampling admission guard flags this early-exited process as an error;
it does not invalidate the controller's retained process-handle exit evidence.
No repeat game launch was performed. Finally, all 17 runtime targets were
restored to their baseline hashes, the three absent-baseline files were removed,
CoreApplication and relay registry state were restored/queried, owned runtime
was absent, and CrossOver deep/strict signature verification passed.
The final receipt reports no restoration errors; runtime is released.

Private final evidence hashes:

| Evidence | SHA-256 |
| --- | --- |
| `game.log` | `3dd41cbbf7c2da55cc4e81af63a25171e3d1a4becdec0db2d41ad06f768ac7f6` |
| `ordinal122-admission.log` | `23f374c76b54ef82316c5861dc05e78e1242ea61ae05e5bd5b0349705995cecc` |
| `interface-tests.log` | `906e1bb72c645d7f31aa0ee5de3f7ab6ccb61f72f6c83935b70db58181654b47` |
| `receipt.json` | `6b8781dfc07580a754a8c39b07aa09cc5d8d5dd7ee8c33a5e3794184a3089aa7` |

No first frame/playability is claimed. Unknown-interface synthesis, generic
Vector implementation and ordinal-69 success/no-op remain held.

### CoreWindow ownership and SHCore scale-registration controls

Read-only attribution of the 1140 failure establishes the immediate failed
HRESULT, rather than inferring it from adjacent log lines. MrmCoreR caller
`+0x4982a` invokes helper `+0x4b8e4`, which queries IID
`84acbbfe-649c-5ac7-8130-7b6b76db463c`. The caller tests that HRESULT at
`+0x49835`; failure branches to the reporter with tag `0x659`. `+0x49943` is
the reporter's return address, not an inline fast-fail instruction. The
reporting chain builds `0xc0000409` with parameter 7 and ultimately has an
`int 0x29` fallback. Detail.Vector, CoreCursor and the adapter getter are not
the result consumed by this particular path. No private-IID shim was added.

The experimental lazy window was returned before CoreApplicationView had
created a window. The composed CoreWindow patch now makes
`GetForCurrentThread` return `S_OK` with NULL by default when the calling
thread has no window. The old experiment requires the exact explicit opt-in
`WINE_COREWINDOW_AUTOCREATE=1`; the legacy no-autocreate flag takes precedence.
The `...default-null-...1151` control passes the previous MrmCoreR/private-QI
boundary, but encounters the SHCore scale-registration stub, with no visible
window and a 150-second harness termination (92).

The genuine twinapi caller independently grounds the ten-argument ABI of
windows.ui NONAME ordinal 1500. The patch exports `CreateCoreWindow`, passes
the title, bounds and owner to the existing HWND-backed implementation, registers
the calling thread's window, and returns the requested interface with an owning
reference. Unknown type/attribute bits are logged, not interpreted. Failed QI
destroys the unexposed window; duplicate creation on that thread is rejected.
The failure-counted test covers initial NULL, ordinal lookup, invalid arguments,
failed-QI cleanup, supplied title/rectangle, shared object identity, duplicate
creation, and NULL after destruction. The initial ordinal-1500 candidate
`c7d6097240b1f9f55bda25853505527e724e813a0feb3c42571a2f54aced2c91`
passes **77 checks / zero failures**. Its `...ordinal1500-...1210` game control
still stops at the older SHCore stub; no actual game invocation of 1500 is
observed. After an ESRCH cleanup race, exact client absence and the recorded
server identity were established before restoring all 17 targets and verifying
CrossOver's signature. The signal helper now tolerates that race only when an
immediate identity recheck proves the numeric PID absent.

The next approved control replaces aborting SHCore stubs 240/246 with explicit
`E_NOTIMPL`, clearing the registration cookie. This is an unsupported boundary,
not notification support. Candidate
`90ab758a09988c62f835ad1ca74aa202b515ae95c039ae73f2a6939141e73e11`
passes **7 checks / zero failures**. In unique run
`cuphead-owned-scale-failure-20261011-1218`, admission is apartment **27/0**,
CoreWindow **77/0**, scale **7/0**. Main thread `01f8` calls 240 with NULL
window through MrmCoreR IAT `+0x10e078`. The stub exception is gone, but the
held child exits after 4,240 ms with `0xcf30` (53040), zero windows, not a
timeout. Its exception address is SharedLibrary.dll `+0x41cf88` (load base
`0x6ffff7cb0000`, file SHA-256
`5f0fd3ae5d7840015263ae84fc05b72528d38c346c04f8e26186ebf4c0708161`).
Coordinator static analysis identifies this as the return address of the
RaiseFailFastException call: a .NET Native unhandled-managed-exception
fail-fast, not an identified native access violation.

The retained log does not show a CreateCoreWindow invocation or
ImmersiveNavigationClient activation. `+module/+loaddll` provides load and
delay-IAT evidence, but emits no GetProcAddress lookup records; absence of a
1500/1700 lookup line is therefore not proof that no lookup was attempted.
No ordinal-1700 implementation, internal ImmersiveNavigationClient factory,
or private `bb4cbbe9`, `84acbbfe`, `f21e14c1` interface was fabricated.
All 18 targets and registry state were restored, owned runtime was released,
and deep/strict signature verification passed.

Private 1218 evidence:

| Evidence | SHA-256 |
| --- | --- |
| `game.log` | `9c168b71f0181c3b14c6609f29ec788eae910b4970638dd34b8cd7c1415ee6a3` |
| `receipt.json` | `fc1b04672807c3aa054b23b2e3d6a327c321974f977e67d31edebc5b4e64d06b` |
| `interface-tests.log` | `72b6142941f0fc238a963c36d7ae35a83161f06697f68200f686c764c1aa3355` |
| `scale-notification-tests.log` | `3aadbcb0cf5e7659003a12ee91a2ac66f7bcb9d4fe55f3c6da10ed5bb81c5fc4` |

`wine-shcore-scale-registration.patch` is the subsequent approved fixed-scale
control: 240 retains the sink with AddRef and assigns a unique nonzero cookie;
246 detaches that exact registration and releases its reference. Invalid or
already-revoked cookies return `E_INVALIDARG`. No sink callback is invoked,
because this provider reports a fixed 100-percent scale. Cookie exhaustion is
an explicit failure rather than wrapping and aliasing an earlier registration.
External AddRef/Release calls occur outside the registration lock. Other
SHCore ordinals, including 227/245, are unchanged. This does not provide a
dynamic scaling backend or establish game playability.

The single retained-sink game control is
`cuphead-owned-scale-retained-20261011-1305`. SHCore is pinned to
`4086de64048cd6a911a228219f6fa4215d03cae7645d844a44f1416c18c05509`;
all other game candidates are unchanged from 1218, including the frozen
`c7d60972...` windows.ui and `52ed4c9a...` combase. Before activation, apartment
checks pass **27/0**, CoreWindow **77/0**, and retained-sink checks **16/0**.
The scale tests cover AddRef ownership, multiple distinct nonzero cookies,
independent sinks, NULL window, invalid input, revoke and double-revoke.
Main thread `01f8` reaches the new registration, but the child still exits
with `0xcf30` at the same SharedLibrary `+0x41cf88` after 2,918 ms, with zero
windows. No report tag `0x3e9` or `0x6c5`, CreateCoreWindow invocation or
ImmersiveNavigationClient activation is visible. Changing 240 alone therefore
does **not** eliminate the managed fail-fast; the final throwing HRESULT
remains unidentified. All 18 targets and registry state were restored and
CrossOver's signature passed.

| Private 1305 evidence | SHA-256 |
| --- | --- |
| `game.log` | `93b5a6e55feb525bb2dc100964021a0d5c331f95d88a81176df5feaf03cb2c73` |
| `receipt.json` | `96f5aca13300c9a89b7641eacb5dbc571e23e74ff819b776a6ee44b9acfda7fb` |
| `scale-notification-tests.log` | `836dd4f45207edd3e13b4b26036e2c012af0399508273978c5d89af11c6bf84c` |

The final composed CoreWindow patch also prevents failed creation QI from
posting `WM_QUIT` to the caller's message loop: only an exposed window owns
that quit notification. This additional regression fix was deliberately
**excluded** from 1305 and the subsequent diagnostic, preserving their single
variable. It was rebuilt as
`88244b6718750dcdfae5a95de9ffad72d739ee4c2c6a6bd7a77a8bd1626ee140`
and independently exercised by the unit-only owner
`cuphead-owned-final-unit-20261011-1320`: apartment **27/0**, CoreWindow
**78/0**, scale **16/0**, no game launch or game log. All 18 targets, registry
and signature were restored/verified. Its private receipt SHA-256 is
`c42a26c734fdf29372181aac80b0d65a64ce6abacdb20df8bfbe1529b71815a2`.

The coordinator's further static analysis identifies `0xcf30` as the
SharedLibrary-relative EEType of base `System.Exception`, not the HRESULT
itself. It can arise from an explicit base exception or an unmapped HRESULT;
the exception's actual Message/HResult cannot be inferred from the exit code.
One separately authorized, unchanged-candidate diagnostic,
`cuphead-owned-error-origin-20261011-1340`, adds `+debugstr,+seh` and an exact
relay filter for combase string creation/duplication, activation/error-origin/
restricted-error APIs and kernelbase debug output/fail-fast. The Debug key is
backed up, set/query-checked, restored and query-verified. Actual activation
Wine PID 612 (`0x264`) maps through the successful server startup reply to
primary thread `0268`; thread `01f8` must not be assumed across runs.

This diagnostic again exits `0xcf30` at SharedLibrary `+0x41cf88` after
4,737 ms, zero windows. It captures 638 primary-thread relay Call/Ret lines.
The last 40 lines are 20 successful Call/Ret pairs: CoreWindow string/factory
requests, a ResourceQualifierObservableMap class-name string, then repeated
ResourceContextLanguagesVectorView class-name strings interleaved with empty
string references and duplication of NULL HSTRINGs. All return `S_OK`.
The last call is WindowsCreateString of the languages-vector-view class name,
from SharedLibrary `+0x48356e`, immediately before the fail-fast.

| Last-40 relay operation | Return site |
| --- | --- |
| CoreWindow WindowsCreateStringReference | MrmCoreR `+0x2381b` |
| CoreWindow RoGetActivationFactory (two calls) | MrmCoreR `+0x4949e`, `+0x2364b` |
| ResourceQualifierObservableMap / ResourceContextLanguagesVectorView WindowsCreateString | SharedLibrary `+0x48356e` |
| Empty WindowsCreateStringReference | MrmCoreR `+0x5fb72` |
| WindowsDuplicateString(NULL) | MrmCoreR `+0x5fd0b` |

Earlier error-origin calls are not the final failure: twinapi `+0x7fbfc`
passes `0x80040154` and `0x80070490` with NULL messages; vccorlib140_app
`+0x18f47` / `+0x19043` passes `0x80040111` with NULL/empty messages in the
earlier caught CoreCursor path. Get/SetRestrictedErrorInfo return
`0x80004001`, and RoOriginate APIs return FALSE. No new such call or debug
output immediately precedes the final exception. **The final exception
Message/HResult is still unobserved.** No 1500/ImmersiveNavigation invocation
or first frame is established, and no further game launch was performed.
All 18 targets, CoreApplication and Debug registry state were restored,
owned runtime was released, and deep/strict signature verification passed.

| Private 1340 evidence | SHA-256 |
| --- | --- |
| `game.log` | `91b491b3c2fbf967d60ced0a5364e23e119400f4f16c0b00eeb653989aa9566b` |
| `receipt.json` | `f9700a4b47539a5ee80b19e9f6707ccaa340c8b4ade61054aada92f0a57ad67a` |
| `interface-tests.log` | `aa99344b603c7e979ed7edaf0903305738b85e2cbb88e103b468e9519650c152` |
| `scale-notification-tests.log` | `7ed33ca92162a8b62c822239257c23a3e1c48c9abe7fee05e02daf1e7a001536` |

Both generated source patches apply to their preserved exact baselines;
targeted builds and tests pass. The prescribed repository-wide command
`python gates/gate_runner.py quality-gate` cannot execute because that file
does not exist here (Python exit 2). No repository-wide quality PASS or
overall game-support completion is claimed.

### bcp47mrm language-buffer and layout-direction ABI corrections

The coordinator authorized one further control after identifying two called
ABI bugs in the experimental bcp47mrm provider. The existing language function
returned an empty list, wrote an eight-byte NULL pointer into a WCHAR buffer,
and updated only a UINT32 length. The existing no-argument layout function
never wrote the caller's output, leaving the native ResourceContext direction
at `0xffffffff`.

Independent read-only inspection of the genuine formatter confirms a
**SIZE_T capacity/required-length pointer**: `+0x52c4` reads eight bytes and
`+0x53c5` compares the capacity with the required character count. Its delimiter
is `';'`, and the required size includes NUL. The earlier saved handoff's
four-argument / UINT32 / out-only interpretation is not reused.
The coordinator also corrected the first argument: it is a package full name,
not a language list. Genuine `+0x4bda` calls PackageFamilyNameFromFullName;
NULL uses the current-package family path, then package language lookup with
user-language fallback. The corrected provider explicitly implements only the
user-language subset; it does not interpret a supplied package identity as
languages or claim manifest/registry selection or app/user matching.

`wine-bcp47mrm-application-languages.patch` changes only the two called
implementations and their spec signatures. The language export obtains live
GetUserPreferredUILanguages(MUI_LANGUAGE_NAME), joins multi-sz tags with `';'`,
and uses GetUserDefaultLocaleName only for an empty preferred-language list.
API failures propagate their Win32 HRESULTs, or `E_UNEXPECTED` for a failed
API without an error; allocation failures remain explicit. A NULL length gives
`E_POINTER`; short or NULL buffers report the required SIZE_T length and
`HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER)`, without a pointer-width buffer
write. Exact-fit output is NUL-terminated.

The layout export now accepts the grounded input/output pointer arguments,
returns `E_POINTER` for NULL output, and writes an actual direction value.
For the exercised NULL-input path it uses the live preferred language.
The deliberately limited, logged primary-subtag table recognizes ar/he/fa/ur
as RTL; other primary subtags are LTR. This is not full language/script or
per-package directionality parity. The other ten guessed exports are unchanged.

Only bcp47mrm was rebuilt:
`ca0f1ff45c6bda4084cc196a6de1b0430bd3cbcda72be1320a6d8a3c06d8b4cd`.
The provider-bound `bcp47mrm-language-test.c` passes **24 checks / zero failures**,
including NULL length, undersized-buffer canaries, required length, exact fit,
NUL bounds, live-profile equality, NULL/unknown/provided package arguments that
must not become language data, NULL direction, written LTR and bounded RTL
cases. It admits the actual payload bcp47mrm path and reports `en-US`, length 6.

Unique game control `cuphead-owned-languages-20261011-1410` keeps the 1305
combase/windows.ui/SHCore and other module hashes unchanged, plus the 1340
relay/SEH/debugstr filter. Pre-game checks are apartment **27/0**, CoreWindow
**77/0**, scale **16/0**, language **24/0**. Wine PID 640 (`0x280`) has the
startup-proven primary thread `0284`. Main log line 11180 and worker line 11328
produce `en-US` / six WCHARs; main line 11189 writes layout direction 0.
The old main-thread 1340 first-argument line is interleaved with loader text,
so its pointer cannot be recovered from that line. The explicit worker records
in 1340 and 1410 both pass NULL.

The game consumes the corrected data, not merely the unit-test executable:

| Private 1410 lines | Observed call / result | Return site |
| --- | --- | --- |
| 11552-11553 | WindowsCreateStringReference(`en-US`, 5), `S_OK` | MrmCoreR `+0x5fb72` |
| 11555-11556 | WindowsDuplicateString(non-NULL HSTRING), `S_OK` | MrmCoreR `+0x5fd0b` |
| 11558-11559 | ResourceContextLanguagesVectorView class-name string, `S_OK` | SharedLibrary `+0x48356e` |
| 11560-11563 | WindowsCreateString(`en-US`, 5), twice, `S_OK` | MrmCoreR `+0x36f0a` |

Nevertheless, the same unhandled `0xcf30` at SharedLibrary `+0x41cf88`
persists: **real exit 53040 after 2,740 ms, zero total/visible windows**.
No invocation of 1500 or ImmersiveNavigationClient, newly called remaining
bcp47mrm stub, or new final error-origin Message/HResult is observed.
The two real ABI/data bugs are fixed and consumed, but their correction is
not sufficient to remove the managed fail-fast. No new frontier or first
frame is claimed. No additional game launch followed.

All 18 temporary targets, CoreApplication and Debug registry state were
restored and verified, with no restoration errors; owned runtime was absent
and CrossOver deep/strict signature verification passed.

| Private 1410 evidence | SHA-256 |
| --- | --- |
| `game.log` | `6832d641bf630a605eeb98a510424da1387c9cb447760584e2f517a5c0da2b46` |
| `receipt.json` | `5f5801ef7bee4c48b5234234d477db7e2311138e69bca17f32a4848c8ab62af0` |
| `language-tests.log` | `428c64e97d3c80afde9c27cb0c6f3cf2e6386e76baac3cd8ef32f28c131fbf4a` |

### Managed fail-fast attribution and the public CoreCursor constructor

The single logging-only control
`cuphead-owned-failfast-object-20261011-1450` captured the exception at
SharedLibrary's fail-fast caller using one caller-frame unwind:
EEType offset `0xcf30`, HResult `0x80040111`
(`CLASS_E_CLASSNOTAVAILABLE`), Message
`Excep_FromHResult 0x80040111`, and a NULL inner-exception field.
Coordinator static attribution and the retained activation/SEH sequence
identified `Windows.UI.Core.CoreCursor` activation inside the
`UnityPlayer.AppCallbacks` constructor as the fatal class lookup.
The later resource-context and language calls were exception-message lookup,
not the original failure.

This diagnostic has two explicit limitations. The child really exited
53040 after 4808 ms with no windows, but the capture owner encountered its
100 MB server-log cap; it was not a clean owner-success run. Also, a guarded
read failure reused stale words for scalar field `0x40 = 5`; that field's
array output is discarded. The preceding successful root/message reads and
field `0x38` reads remain valid. The diagnostic and its unvalidated private
read-safety follow-up are not shipped in the CoreCursor slice. All temporary
targets and registry state were restored after that control.

`scripts/macos/compatibility/wine-corecursor.patch` implements the public,
parameterized CoreCursor constructor and registers its factory in Windows.UI.
The Microsoft SDK's
[`windows.ui.core.idl`](https://github.com/microsoft/win32metadata/blob/main/generation/WinSDK/RecompiledIdlHeaders/winrt/windows.ui.core.idl)
and the matching Wine IDL agree on:

- `ICoreCursorFactory` IID `f6359621-a79d-4ed3-8c32-a9ef9d6b76a4`,
  with `CreateCursor(CoreCursorType, UINT32, ICoreCursor **)`.
- `ICoreCursor` IID `96893acf-111d-442c-8a77-b87992f8e2d6`,
  with `get_Id` followed by `get_Type`, after the IInspectable methods.
- Agile marshaling and enum values 0 through 15. **Custom is 2**, not 10;
  10 is SizeWestEast.

The immutable cursor stores the supplied type/id, supports public interface
identity and agile QI, and uses interlocked references. NULL creation output
and out-of-range types return `E_INVALIDARG`; NULL getter outputs return
`E_POINTER`. Default activation remains `E_NOTIMPL`.
`ICoreWindow::put_PointerCursor` remains `E_NOTIMPL`: no SetCursor/LoadCursor
integration, OS cursor resources, private interfaces or other public classes
are synthesized.

The rebuilt Windows.UI candidate is
`a38a0027fe80edc85d5e3e45fef51e986ad5d120e19d416dc760d989eb6eac36`.
It also includes the previously unit-validated failed-QI/WM_QUIT cleanup
already preserved in the composed CoreWindow patch; that cleanup is not
exercised by this control. No other runtime module differs from the 1410
candidates, including kernelbase
`9308d50feb4f7a702d9935708680a91b60c6e053082164284a809af35e32c73f`;
the temporary fail-fast diagnostic was dropped.

The matching-source targeted Windows.UI build passed. Provider-bound
`corecursor-test.c` passed **78 checks, zero failures**, covering factory
QI/identity, default-activation rejection, Arrow and Custom round-trips,
all valid enum values, invalid types, NULL outputs, metadata, agile identity
and reference counts. The existing admission checks also passed:
combase 27/0 with ordinal 122 at RVA `0x90f0`, CoreWindow 77/0,
SHCore 16/0 and bcp47mrm 24/0.

The one authorized game control,
`cuphead-owned-corecursor-20261011-1530`, used the existing pinned
combase/twinapi argv, scoped activation/string relay, UI/SEH and module
tracing, and the 150-second controller bound. It ended naturally:

| Private game evidence | Result |
| --- | --- |
| Primary thread `02a0`, lines 5352-5361 | CoreCursor factory lookup returns `S_OK`; `CreateCursor(type=0, id=0)` is called. |
| Subsequent startup | AppCallbacks startup progresses into ThreadPool and CoreApplication activity; the complete AppCallbacks `ActivateInstance` return is not separately traced. |
| Lines 5953-5957 | Genuine twinapi fails to delay-load `RMCLIENT.dll.HamConnectForExtendedExecution`, raising `EXCEPTION_WINE_STUB` (`0x80000100`). |
| Line 11680 | Final fail-fast is now `0x1007` at SharedLibrary+`0x41cf88`, not `0xcf30`. |
| Controller outcome | Real child exit **4103**, elapsed **1469 ms**; **0 total / 0 visible windows**. |
| View/window frontier | No ordinal-1500 creation, ordinal-1700 resolution, ImmersiveNavigationClient or actual `corewindow_put_PointerCursor` call is observed. `Run`/`SetWindow` are not directly traced, so their completion is not claimed. |

The CoreCursor class boundary is crossed; this is not first-frame or
playability evidence. No RMCLIENT or further UI-class change, and no
additional game launch, followed.

Owner status is 0 with no owner or restoration errors. All 18 baseline
hashes/absent targets match independently, CoreApplication and Debug
registry state are restored, and CrossOver deep/strict signature
verification passes. Owned processes are released. Older unowned
Wine-named services started before this capture remain visible and were not
signaled; global Wine absence is not claimed. A subsequent read-only `lsof`
check attributes those unowned clients to the same Cuphead bottle and shared
CrossOver files. Further runtime swaps are held pending their ownership and
cleanup authority; the earlier capture's own process receipt does not prove
bottle-wide quiescence.

| Private 1530 evidence | SHA-256 |
| --- | --- |
| `game.log` | `077f625dbaf9ffe32e21e26f4d27d1e4bd84a99ed6903d6fd49f1194731310b0` |
| `receipt.json` | `11018494ab6315201c9167ab48dcf7ddc8951b94979931bf1b3fcf7010e8ab16` |
| `corecursor-tests.log` | `3ad77e862c51491a53f27b31f967b4f5fc6c99a515fe5fe4939f8b7e18357333` |

Patch application is checked against the exact preserved Windows.UI source
baselines and compared with the built source after LF normalization.
The mandatory integration command `python gates/gate_runner.py quality-gate`
still exits 2 because this repository does not contain `gates/gate_runner.py`;
no overall quality-gate PASS or task-completion claim is made.

### RMCLIENT conformance and two-surface admission

The coordinator attributed the four older clients to its own Oct 10
Cuphead run, verified exact process identities and disconnected server
sockets, and terminated those four processes. Bottle and host quiescence
were then verified before the next control. The private capture owner now
checks for `.wineserver` directory holders with `lsof` before preparation,
immediately before mutation, and after owned-runtime cleanup. Probe errors,
permission warnings and active holders block admission; six synthetic
capture-safety tests pass, including this rejection behavior.

The existing RMCLIENT unavailable-backend implementation is retained,
with the newly verified native contract:
`NTSTATUS HamConnectForExtendedExecution(const void *, HANDLE *)`.
NULL parameters return `STATUS_INVALID_PARAMETER_1` (`0xc00000ef`);
NULL output with non-NULL parameters is defensively rejected with
`STATUS_INVALID_PARAMETER_2`. Otherwise it returns `STATUS_NOT_SUPPORTED`
(`0xc00000bb`), never dereferencing the opaque parameters or writing the
output. HAM connectivity and extended execution are **not** granted.
The existing named export/ordinal 32 is unchanged; no other RMCLIENT
exports are implemented.

The matching-source targeted build passes, and the updated full creation
patch reproduces the built main implementation. Its main-file hunk count
also includes the complete return and closing brace, rather than truncating
those lines. Provider-bound RMCLIENT tests pass **10 checks, zero failures**,
including named/ordinal identity, the negative NTSTATUS values and
unchanged output on failure.
Candidate SHA-256:
`9d7019d192bb61ba54b345bbe520af885c6659dcb77d20b752f9ddaede94d6b9`.

The single control `cuphead-owned-rmclient-20261011-1600` admits that DLL
at both bottle system32 and vendor Wine system32: **20 targets total**.
All 18 existing candidate hashes match the 1530 control exactly.
The real game, primary thread `02bc`, loads
`C:\windows\system32\RMCLIENT.dll` at lines 5982-5987 and calls the honest
unavailable connection at lines 5991-5993. Genuine twinapi continues past
that failure. No success-only HAM handle path is claimed.

The next sequence is now:

| Private game lines | Observed result |
| --- | --- |
| 6000-6005 | Detail.Vector activation for IID `08c77958-89bf-5cf8-a9cd-c72147b9b3a9` returns `0x80040154`. |
| 6006-6007 | The already-admitted combase aborts on `CoRegisterServerShutdownDelay`, raising `EXCEPTION_WINE_STUB`. The matching spec identifies this as unimplemented noname ordinal 69. |
| 11730-11733 | Fail-fast `0x1007`, real child exit **4103** after **1407 ms**, **0 total / 0 visible windows**. |

No ordinal-1500 creation, ordinal-1700 resolution,
ImmersiveNavigationClient or actual `corewindow_put_PointerCursor`
invocation is observed. `Run` and `SetWindow` are not directly traced.
The next blocker is not an existing implementation omitted from admission
or one of the pre-approved public input classes. Ordinal 69 and
Detail.Vector remain held: no guessed ABI, private-interface synthesis,
new implementation or additional launch followed this control.

Owner status is 0 with no owner/restoration errors. All 20 baseline
hashes/absent targets, registry restoration, CrossOver deep/strict signature
and post-restore bottle quiescence are independently verified.

| Private 1600 evidence | SHA-256 |
| --- | --- |
| `game.log` | `c7c59b628c69cc11d2a0aaacc6185c5d07683cd48c71ae7a8dff103113cd7602` |
| `receipt.json` | `64c907eff9efb9e327ad74616b527b8b7439f04ee62ec48441788374ad297e08` |
| `rmclient-tests.log` | `84b426dc200de054a56af8d7ebb64efd3e552043fbe92020d3675cb08b52fae8` |
| `bottle-quiescence.json` | `923d0e339096d0a7da76af12e6c733a8760401603c9448e83b0c32d02d00b6c3` |

### Process-local activation registration and control 1640

The coordinator grounded noname combase ordinal 69 in genuine twinapi
callers: `HRESULT CoRegisterServerShutdownDelay(void *, DWORD)`.
Its signature is derived from caller reverse engineering, not a genuine
export definition. It returns `S_OK` with a FIXME: Wine has no idle-server
shutdown policy to which this delay hint could apply.

`wine-ro-activation-registration.patch` implements the requested
process-local subset of the public activation-registration APIs.
Registrations copy HSTRING names, reject empty/embedded-NUL and duplicate
names, retain callbacks, and publish an all-or-nothing batch behind an SRW
lock. Opaque cookies can revoke the batch. Factory lookup checks this table
before activation contexts and registry lookup, calls callbacks outside the
lock, and queries the returned factory for the requested IID. Refcounted
registrations survive self-revocation and concurrent revocation.
Out-of-process broker publication and apartment marshaling are not
implemented. Synthetic tests passed **44/0**; the updated revoke regression
passed **11/0**.

The one control `cuphead-owned-activation-20261011-1640` admitted combase
`9ad5feaad485df1e4eb08287382f376627fbe9374a9971720f050512b5d3b369`
with ordinal 122 at RVA `0x9130`. All other 1600 pins were unchanged.
Ordinal 69 is observed returning normally on primary thread `02f8`.
**Game registration and registered class names were not observed**;
synthetic registration success is not proof that genuine twinapi reached it.
The next abort is missing `ntdll.RtlLoadString`, during error-message lookup.
The child remained alive with **0 total / 0 visible windows** and was
terminated at the controller's **150-second bound**. Exit **92** is
controller termination, not a natural game exit.

No ordinal-1500 creation or ImmersiveNavigationClient is observed; `Run`
and `SetWindow` completion are not established. All 20 baseline
hashes/absent targets, owned-client absence, deep/strict CrossOver signature
and bottle quiescence were independently verified after restoration.

| Private 1640 evidence | SHA-256 |
| --- | --- |
| `game.log` | `c61bea0df9fff4d3e25585f4522185d6423d846b17569cc3c58d0de98ac745a6` |
| `receipt.json` | `09ea3f6fe0c3b642970abdf8bd78efd6de0c86b1e5ddab6139b00c59afd4d515` |
| `registration-tests.log` | `94e073a3dffd1b7ed51bd9856afc44d79abc185bf402d4c1ff94999c00cbfaa0` |

### RtlLoadString and error-propagation control 1700

The separately authorized slice adds `wine-rtl-load-string.patch`.
`RtlLoadString` uses `LdrFindResource_U`/`LdrAccessResource` to find
RT_STRING block `id / 16 + 1` and counted UTF-16 entry `id % 16`.
It returns a resource-backed pointer and an optional WCHAR length, not a
fabricated or NUL-terminated message. Truncated entries fail explicitly.
NULL language uses Wine's existing neutral/default language selection;
nonzero flags return `STATUS_INVALID_PARAMETER`. Named language selection
and language-name outputs are explicitly unsupported. Resource errors
propagate unchanged. Trace-only six-frame capture attributes the caller
without changing the lookup result.

The combase patch also includes the approved unavailable-error-propagation
subset: `IsErrorPropagationEnabled`/`RoTransformError`/`RoTransformErrorW`
return FALSE, matching restricted-error lookup initializes output NULL and
returns `E_NOTIMPL`, failed-delegate reporting returns `E_NOTIMPL`, and
`CoCancelCall` returns `CO_E_CANCEL_DISABLED`. The SDK signature of
`RoTransformErrorW` has **four** arguments, including `UINT cchMax`;
the shorter suggested signature is not used. Illegal-method-call stack
logging in error origination/transformation preserves existing results.
Apartment-result logging likewise changes no apartment state.

Only three of the 20 admitted file targets change from 1640: ntdll PE and
the two combase copies. The ntdll Unix library is unchanged at
`e4d34518f2627d73a0035223939ec53bea0dd9829b07238486f51c35cae34422`.
The owner required `cmd /c echo XODUS_NTDLL_ABI_SMOKE` and all unit gates
before launching the game. The echo passed; synthetic resource tests
passed **18/0** and provider-bound error tests **14/0**. Registry tests
remain **44/0**. New candidate hashes:

| Candidate | SHA-256 |
| --- | --- |
| ntdll PE | `f8bd19c7606e0f7c506380d1c5c1d8bea477cbfea935f8c6f115de2ae8a2cb42` |
| combase, ordinal 122 RVA `0x9150` | `4056e2f6538a2868238f970efb11b9c786e995fbb01a0f0e588bb9fca661c35a` |

The single `cuphead-owned-rtl-20261011-1700` control establishes:

| Private game evidence | Observed result |
| --- | --- |
| Lines 13850-13862, primary `0350` | String id `0x2011`; helper return twinapi+`0x5cd34`, originating return twinapi+`0xf505`, then MrmCoreR+`0x42c05`. Relay frames precede the genuine frames; frame 4, not frame 2, identifies the originating site. |
| Resource lookup | `STATUS_RESOURCE_TYPE_NOT_FOUND` (`0xc000008a`), matching the absence of RT_STRING in genuine twinapi. The caller tolerates it. |
| Lines 13870-13905 | `RoOriginateErrorW(0x8000000e, 0, NULL)` is observed. |
| Lines 13999-14011 | Final exception code 4 contains `0x8000000e`; natural child exit **3221226529** (`0xc0000409`) after **1573 ms**, **0 total / 0 visible windows**. |

The coordinator's static attribution identifies twinapi+`0xf505` as
CoreApplication Properties' "no current CoreApplication instance" path.
This is downstream error handling after `CoreApplication.Run` already
failed; HAM, Detail.Vector and ordinal 69 were teardown, not successful
startup. The preceding Run parser requires a final `-ServerName:` argument,
absent from the current launch contract. Registration/class names are still
not observed, and no CoreWindow/first frame is established.
`CoGetApartmentType` returns `S_OK` at the relevant MrmCoreR call; type and
qualifier were not captured because that function's channel is `ole`,
not `combase`. No private CoreApplication interface is synthesized.

Owner status is 0. All 20 baseline hashes/absent targets, owned-client
absence, deep/strict signature and bottle quiescence are independently
verified. Registry and temporary Debug restoration checks pass.
Both patches reproduce all five built source files from preserved baselines.
The mandatory `python gates/gate_runner.py quality-gate` still exits 2
because the configured runner is absent; no overall PASS is claimed.

| Private 1700 evidence | SHA-256 |
| --- | --- |
| `game.log` | `733bbc590c724b1dec0016c564f00c0c704e9d460f3f2a87e798e86ba707aef5` |
| `receipt.json` | `333d95b30d3369aabe03bb3f5cb15ed2c3ace23974fd2e6b734ff227d8fd97c9` |
| `rtl-load-string-tests.log` | `664b102903cff584efa0e4239013f37c60651d1e3b0d1d4e17d99ad4ed9b76cc` |
| `error-propagation-tests.log` | `66fb17f1dbab3153f5120645a2fb5150d07c9c1e30b707dedaa79b948d823a8a` |

### Registered server launch contract and control 1750

The public [RoGetServerActivatableClasses contract](https://learn.microsoft.com/en-us/windows/win32/api/roregistrationapi/nf-roregistrationapi-rogetserveractivatableclasses)
is declared in `roregistrationapi.h`, not `roapi.h`. The caller deletes each
returned HSTRING and frees the array with `CoTaskMemFree`. Unknown, empty
or classless servers return `REGDB_E_CLASSNOTREG`, with NULL array/count 0.
`wine-ro-server-classes.patch` replaces the empty-success stub with the
approved Wine machine-registry subset: a matching
`HKLM\Software\Microsoft\WindowsRuntime\Server\<name>` must exist, and class
records under `ActivatableClassId` must have `ActivationType=1` and a
matching `Server` string. Allocation ownership, input validation and
registry failures are handled explicitly. Package-scoped catalog lookup
and activation-manager publication remain unavailable, with a FIXME;
this is not complete Windows package-registration fidelity.

Provider-bound synthetic tests pass **31/0**, covering matching and
nonmatching registrations, activation-type filtering, unknown/classless
servers, NULL outputs and caller-owned string/array release. Test-owned
keys are removed. The DLL hash is
`4ed50352a810ded4a0ea23961f37435b06655ad773c961a4069f88c90fedd985`,
ordinal 122 RVA `0x9190`. Attempt 1740 safely refused before any game launch
because its gate still expected RVA `0x9150`; all files were restored.
The gate was repinned without a product change, using fresh owner 1750.

The manifest confirms `Application Id="App"`, `Executable="Cuphead.exe"`
and `EntryPoint="cuphead.App"`. The existing capture launcher already
supports a final server argument; no launcher source change was needed.
The single game control temporarily registers only `cuphead.App` and
server `Xodus.Cuphead.App`, then starts the original executable with final
argument `-ServerName:Xodus.Cuphead.App`. This server name belongs to the
explicit Wine registration equivalent; no Windows-generated AppX hash is
invented, and no Store entitlement or AppContainer identity is fabricated.

| Private 1750 evidence, primary `0434` | Observed result |
| --- | --- |
| Lines 7872-7876 | Server enumeration returns `cuphead.App`, `S_OK`. |
| Lines 7877-7878 | Startup shutdown-delay hint has context `0xac`, delay 2000 ms. |
| Lines 7880-7886 | Registration publishes one class, cookie 1, callback twinapi+`0x2fc90`, and returns `S_OK`. |
| Apartment traces | Main thread is MTA (`type=1`, qualifier 0); the later view thread is MAINSTA (`type=3`, qualifier 0). |
| Line 7907 | Main waits indefinitely/nonalertably in `NtWaitForSingleObject`, handle `0x214`, caller kernelbase+`0x7981d`. |
| Line 8009 | The view thread hits missing `wintypes.RoCreateNonAgilePropertySet`. This is the next actual wall, not proof of an activation-manager hang. |
| Controller | **0 total / 0 visible windows**; final child status **`0x80000100`**. The coordinator manually closed a Wine crash dialog, so this is **not a natural teardown claim**. |

No registered-factory lookup/callback invocation is observed. A bounded
native sample is retained privately, but the view-thread stub must be fixed
before interpreting the main-thread wait as an activation-manager gap.
CoreWindow creation, `SetWindow` and first frame remain unestablished.
No implementation of ordinal 295 or CoreMessaging was added.

Owner status is 0. All 20 baseline hashes/absent targets, owned-client
absence, deep/strict signature, bottle quiescence and both temporary
registration-key removals are independently verified. Debug and
CoreApplication registry state are restored. The source patch reproduces
the exact built file; the mandatory integration runner remains absent
(exit 2), so no overall PASS is claimed.

| Private 1750 evidence | SHA-256 |
| --- | --- |
| `game.log` | `571911882312e9f5038df2e0745163a9fe0d345f4ac72b21987449fecbd566f2` |
| `receipt.json` | `f711f39e2470372f4abc205f93dc291bb5146eed72b58601789dbdc0d4503c1f` |
| `server-classes-tests.log` | `0f18f554958b4b961cd7519d3931cde8a110cd83df279e205fffa3a4ab1edbb9` |
| `bound-sample.txt` | `05c69f4b248b1bbb388deb03c40472e1e142c8037da855a011d47d0806b26abf` |

### Non-agile property set and control 1840

The approved lookahead `wintypes.patch` was reviewed against the public
`ICoreApplicationView5.Properties` return type (`IPropertySet **`). The
export itself has no SDK prototype: its one-output-argument/HRESULT ABI
is grounded in the genuine twinapi call sites, not claimed as a documented
SDK API. `wine-nonagile-propertyset.patch` implements the export using
the existing PropertySet and map implementation.

The supplied wrapper alone was insufficient. Pre-game attempt 1810
returned **22 checks / 1 failure**: PropertySet delegated
`IAgileObject` QI to `locked_map_inner_QueryInterface`, which explicitly
advertises that interface. The owner refused before launching any game;
there is no `game.log`, and all 22 targets and registry state were
restored. The correction uses a shared constructor with an instance-local
non-agile flag. Only instances created by the new export reject the
inner map's agile marker; ordinary factory activation retains its prior
behavior. There is no change to `map.c`.

The strict provider-bound `nonagile-propertyset-test.c` passes **29/0**.
It covers NULL output, real PropertySet/map interfaces, non-agility through
the PropertySet, map and observable-map interfaces, controlling-IUnknown
identity, a UInt32 insert/lookup/remove round trip, separate instances
and unchanged ordinary-factory agility. The supplied Wine unit source
also compiles; its skip-capable harness is not used as the admission
gate. The admitted `wintypes.dll` hash is
`f9d398a02b1dc4c3ef7a964110253ee19468d721d62c6e5ee1adc443403d4f0c`.

Fresh owner 1840 admits both bottle/vendor wintypes copies, bringing the
target count to 22. It separately exports/backups
`HKCU\Software\Wine\WineDbg`, sets `ShowCrashDialog=0` as a DWORD, verifies
it, and restores the original key/value state. The original WineDbg key
was present but empty: its empty export and final empty registry-file
section are independently checked, and the temporary value is absent.
Six process/quiescence safety tests and six WineDbg restoration tests
cover absent keys, empty existing keys, absent/existing values, unrelated
values and mismatches.

| Private 1840 evidence | Observed result |
| --- | --- |
| Pre-game admission | ntdll PE/Unix command smoke succeeds; the exact wintypes provider passes 29/0 before the game. |
| Lines 7872-7886 | Enumerates `cuphead.App` and registers count 1, cookie 1, callback twinapi+`0x2fc90`, `S_OK`. No callback lookup/invocation is observed. |
| Apartment traces | Main `04bc` is MTA (`type=1`, qualifier 0); view `04d0` is MAINSTA (`type=3`, qualifier 0). |
| Lines 7907, 8065 | Main waits in `NtWaitForSingleObject(0x214, FALSE, NULL)`, caller kernelbase+`0x7981d`; the wait returns success while view initialization fails. |
| Lines 8021-8025 | `RoGetActivationFactory("Windows.UI.Core.CoreDispatcher", {4b4d0861-d718-4f7c-bec7-735c065f7c73})` returns `REGDB_E_CLASSNOTREG` (`0x80040154`): no library is found. This is the next grounded wall. |
| Line 8080 | Subsequent view-thread null-read AV at genuine twinapi+`0x2619c`; this is not a CoreMessaging stub exception. |
| Lines 10913-10915 | Child exits `0xc0000005` / 3221225477 after about 2002 ms; **0 total / 0 visible windows**. Early exit prevents the 140-second sample; no manual dialog closure is reported for this control. |

Coordinator static RE links the AV to teardown unconditionally querying a
NULL `view+0xc8` after the CoreDispatcher factory failed. That correlation
is distinct from the runtime observation; no AV-specific product change
is made. No CoreMessaging load/call, registered callback invocation,
CoreWindow creation or first frame is established. Native Windows.UI
admission and a private builtin dispatcher factory are unchosen follow-on
routes; neither is implemented or launched under this grant.

Owner status is 0, with no restoration errors. All 22 hashes/absent
targets, original empty WineDbg key, removed temporary server keys,
owned-client absence, bottle quiescence and deep/strict signature are
independently verified. Debug and CoreApplication registration are
restored. The mandatory integration runner remains missing (exit 2);
this is bounded compatibility evidence, not an overall PASS or
Store-install/playability claim.

| Private 1840 evidence | SHA-256 |
| --- | --- |
| `game.log` | `f97f1c6be9d1b7cd16f64b9151c01fd7548ef2f46740a480165e48d40fdb7a2a` |
| `receipt.json` | `87d2e0feaf74031c6e334aafcb9c59049b27526fcbd9e30385f7e9b033a93072` |
| `propertyset-tests.log` | `e4816e5cacbdd32799aa4e3b9f39f14c77b3fc4d28eada3a7d7360033a3bceb0` |
| `ntdll-abi-smoke.log` | `7f8b049121de2e790d4d0d6015d6235e65427963c9c52c387dc13d08d56af4f7` |

### Thread dispatcher factory and control 1910

Coordinator verified-static analysis of genuine Windows.UI establishes
private factory IID `{4b4d0861-d718-4f7c-bec7-735c065f7c73}` as an
eight-slot IInspectable interface: slot 6 returns the current thread's
public `ICoreDispatcher` or NULL; slot 7 creates it if absent. Returned
dispatchers implement private IID `{34fff979-bd36-4bb1-82d2-785a605b81fb}`
as a five-slot IUnknown interface sharing the public dispatcher's COM
identity. Slot 3 pumps messages while waiting for an optional HANDLE;
slot 4 begins queue shutdown. These are RE-grounded private contracts,
not public SDK interface names.

`wine-coredispatcher-thread.patch` adds the runtime class/factory,
reference-owned TLS dispatcher with owner-thread identity and thread-exit
cleanup, and the private pumping interface. The wait uses public
`MsgWaitForMultipleObjectsEx`/`PeekMessageW`/`TranslateMessage`/
`DispatchMessageW`, checks handles between messages, rejects other threads
with `RPC_E_WRONG_THREAD`, and returns `E_APPLICATION_EXITING` on quit or
shutdown. There is no queue controller in this builtin route; an
idempotent event signals its shutdown. Failures propagate as HRESULTs
instead of copying genuine initialization fail-fast behavior. Ordinal
1500 attaches to the existing thread dispatcher rather than allocating a
second one. Existing unimplemented public async/event features are not
silently promoted to working features.

No-game owner UNIT1900 passes **47/0** with the exact builtin provider.
The strict test covers factory QI/vtable shape, NULL-then-create lookup,
repeated and cross-thread identity, runtime-name/trust metadata, private
QI/controlling-IUnknown identity, wrong-thread rejection, real dispatch of
a posted HWND message before another thread signals completion, thread
detach reference release, ordinal-1500 dispatcher identity, and quit/
idempotent shutdown. The admitted DLL hash is
`c614649c78b0a3c1bd7b8b03659cd669b8246709b19dae62451bcb8d11ec39c3`.
The same 47-check admission passes again before the single game.

| Private 1910 evidence | Observed result |
| --- | --- |
| Lines 7880-7894 | Enumerates `cuphead.App`, registers one factory callback twinapi+`0x2fc90`, and returns `S_OK`. No callback lookup/invocation follows. |
| Apartment traces | Main `0550` is MTA (`type=1`, qualifier 0); view `0564` is MAINSTA (`type=3`, qualifier 0). |
| Line 7915 | Main waits in nonalertable `NtWaitForSingleObject(0x214, FALSE, NULL)`, caller kernelbase+`0x7981d`. |
| Lines 8087-8090 | CoreDispatcher factory activation now succeeds; private slot 7 creates a thread dispatcher on `0564`. |
| Lines 8091-8095 | GIT registers twinapi IID `{638bb2db-451d-4661-b099-414f34ffb9f1}`; `CoMarshalInterface` reaches missing `combase.CoGetStdMarshalEx` and aborts. Delay descriptor/IAT are twinapi+`0x21a920`/+`0x2422c8`. This is the next actual wall. |
| Lines 8278-8280 | Child exits `0x80000100` / 2147483904 after about 2365 ms; **0 total / 0 visible windows**. No 140-second sample is possible. |

The private pump, registered callback and ordinal 1500 are not reached by
the game before this new stub. No CoreMessaging/native Windows.UI change
is made. `ShowCrashDialog=0` avoids modal obstruction; no manual closure
is reported. Owner status is 0 and all 22 target baselines/absences,
three temporary registry-key removals, original empty WineDbg key,
signature, quiescence and owned-process absence are independently checked.

Post-control loader review identifies a necessary admission correction:
1910 loaded identical pinned payload and system32 Windows.UI DLLs at
different bases. Its factory registration selected system32, while the
imported ordinal-1500 provider was the payload copy. Separate DLL copies
have separate TLS, so a within-one-provider identity test does not prove
identity across that layout. The future owner registers CoreDispatcher
to the payload-selected `Windows.UI.dll`. No-game-only UNIT1930 tests
that explicit provider through both `RoGetActivationFactory` and
`DllGetActivationFactory`, and passes the same **47/0**, including
ordinal-1500 identity. All 22 targets and temporary registration are
restored and independently checked. This correction has **not** been
game-tested in 1910 and does not authorize replay.

The four-source patch applies to the exact preceding Windows.UI baseline
and reproduces the compiled files. CoreWindow creation/first frame remain
unestablished; the missing mandatory quality runner (exit 2) still blocks
an overall PASS.

| Private evidence | SHA-256 |
| --- | --- |
| 1910 `game.log` | `ff6bcf7fd3f4e9d9982471de05c089a0ebca9d00c6e1530c2db7af759166e1b4` |
| 1910 `receipt.json` | `a7d35b5d2033af8b6857f87a5ed1dedd81956ab4d2ffbacaf262260afc4bd3a4` |
| 1910 `dispatcher-tests.log` | `6b1e1907e2ac9417e1aa797138dfc93ca52ce3ded8e89978b0b1837a0d14d0ea` |
| UNIT1930 `dispatcher-tests.log` | `c62f797efef9a5aea9731b4a550131d6c4aec7fef0bb5513a9d9e5755464baa3` |
| UNIT1930 `receipt.json` | `c9528aa6e55839f6e7d20d9a6b53f4a128bafe21b4fab4fcf203c56e421ce1fd` |

### Server aggregated standard marshaler and control 2010

The public [CoGetStdMarshalEx contract](https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-cogetstdmarshalex)
is `HRESULT(IUnknown *outer, DWORD flags, IUnknown **inner)`.
`SMEXF_SERVER=1` requests a server aggregated standard marshaler;
`SMEXF_HANDLER=0` requests a client handler. The granted
`wine-stdmarshal-aggregation.patch` implements only the server case.
NULL outer/output returns `E_INVALIDARG`, with the output cleared where
possible. Unsupported flags are explicitly logged and return
`E_NOTIMPL`, not a handler-shaped success.

The inner IUnknown has an independent refcount and exposes only itself
and IMarshal. IMarshal QI/AddRef/Release delegate to the controlling
outer; storing the outer does not AddRef it. The six marshal operations
forward to an owned existing `StdMarshalImpl` initialized with context
0/NULL. Its MarshalInterface uses the caller-supplied object/current
apartment; no extra object reference or alternate private proxy ABI is
invented. This inherits the existing standard marshaler's capabilities
and limitations, rather than implementing missing application-specific
proxy/stub registrations.

The strict `stdmarshal-aggregation-test.c` passes **54/0** in no-game
UNIT2000 and again before the game. It checks ordinal/name identity,
NULL/error outputs, unsupported handler/flags, nondelegating/delegating
IUnknown identity and refcounts, marshal class/size, a real STA-to-MTA
IPersistFile proxy call executed on the original STA while it pumps COM
calls, GIT Register/Get/Revoke, and both release orders. Three test
outers are created and destroyed. The admitted combase hash is
`c0b1cd70ad7915cfadff083fe7491bf5a52e4faaded1e9dbb0e5f7e138a794b4`;
ordinal 122 is repinned to RVA `0x9050` and ordinal 363 to `0xab70`.
The ordinal-122 contract regression passes 27/0.

The control uses the accepted payload-provider correction: CoreDispatcher
registration and the 47-check ordinal-1500 identity test select
`Y:\xodus-app-tooling\cuphead\decoded-attempt1\Windows.UI.dll`.
The game factory interface is in that same payload DLL. A second system32
Windows.UI copy still loads for other lookups; its presence is not
misreported as single-module loading or cross-copy TLS identity.

| Private 2010 evidence | Observed result |
| --- | --- |
| Registration | One `cuphead.App` callback at twinapi+`0x2fc90` registers successfully; no callback lookup/invocation is observed. |
| Apartment traces | Main `0578` is MTA (`type=1`, qualifier 0); view `058c` is MAINSTA (`type=3`, qualifier 0). |
| Lines 8040-8048 | Payload CoreDispatcher slot 7 creates the thread dispatcher. `CoGetStdMarshalEx` receives flags `0x1`, returns `S_OK`, caller twinapi+`0x3120b`. |
| Lines 8049-8051 | GIT/CoMarshalInterface now enters standard marshaling for twinapi IID `{638bb2db-451d-4661-b099-414f34ffb9f1}`. |
| Lines 8187-8197 | `CoGetPSClsid` lookup for that IID is followed by stub-manager rollback; ifstub creation and CoMarshalInterface fail with `E_NOINTERFACE` (`0x80004002`). This is the next primary wall. |
| Line 8239 | The private dispatcher pump slot 3 is reached on `058c`, handle `0x210`. Its entry is observed; no private-method return HRESULT is directly traced. |
| Wait capture | Main's initial nonalertable infinite wait is `NtWaitForSingleObject(0x214, FALSE, NULL)`, kernelbase+`0x7981d`; additional teardown waits follow. Early exit prevents the 140-second sample. |
| Lines 14597-14610, 14820-14833 | After Run unwinds, the familiar twinapi+`0xf505` no-current-instance path originates `0x8000000e`; final managed exception code 4 contains it. Child exits `0xc0000409` / 3221226529 after about 1688 ms, **0 total / 0 visible windows**. |

Source correlation explains the next discriminator: `CoGetPSClsid` tries
process registration, activation-context interface redirection and
`HKCR\Interface\{IID}\ProxyStubClsid32` in both registry views. Offline
machine/user registry files contain no interface header for the failing
IID. Standard IRemUnknown proxy/stub creation succeeds immediately
before this private-IID lookup. The required next evidence is the real
interface's proxy/stub ABI and provider registration; no guessed proxy,
forced-agility bypass or additional game is added. CoreWindow creation
and first frame remain unestablished.

Owner status is 0. All 22 target baselines/absences, three temporary
registry-key removals, original empty WineDbg key, signature, quiescence
and owned-process absence are independently verified after UNIT2000 and
2010. Dialog suppression remains enabled only during the owned control;
no manual intervention is reported. The two-source patch reproduces the
compiled candidate. The mandatory quality runner remains missing
(exit 2), so no overall PASS or task-completion claim is made.

| Private evidence | SHA-256 |
| --- | --- |
| UNIT2000 `stdmarshal-tests.log` | `9586d807a97bc0ecd7295059d65f35af896397060129e5c5127cf99093664e19` |
| UNIT2000 `receipt.json` | `87b08be45db7190b1cedfedd7af8105e2e44dbd3e70102ff519098ae85e1a24f` |
| 2010 `game.log` | `f509c1515020d6d3b9662caed235b65736eb61418cef747db3af96cc01748ab2` |
| 2010 `receipt.json` | `eb0ea08b6a607be81225146bd6b7775727245ccdaf5b56b0c3d4e80a534faeb0` |
| 2010 `stdmarshal-tests.log` | `6c8692099f6fc7777bcb0427d0d20a5119aaa38234bfe0ecfa2c2bea48794d59` |
| 2010 `dispatcher-tests.log` | `7a49887bd87a1ae3ac38667e1443e4bcad5d5b5450ec4d4f26f6a277910b36d2` |

### Public CoreApplicationView proxy and control 2120

The 2010 IID is now identified from the public Wine/SDK metadata:
`{638bb2db-451d-4661-b099-414f34ffb9f1}` is **ICoreApplicationView**,
not an undocumented interface. Its eleven-slot ABI comprises IInspectable
and CoreWindow, add/remove Activated, IsMain and IsHosted. The granted
`wine-coreview-proxy.patch` adds an ABI-only IDL to ole32's existing proxy
build, without modifying twinapi or marking its objects agile.

Exact source inspection confirms ole32's `oleproxy.c` DllGetClassObject
calls the generated `OLE32_DllGetClassObject`. Its combined `dlldata.c`
includes the new proxy file; the existing Makefile selects
`CLSID_PSFactoryBuffer` (`00000320-0000-0000-c000-000000000046`).
The patch enables `PROXY_DELEGATION` for the generated inherited stubs.
Private source-namespace aliases retain the public wire IIDs and slot
order. CoreWindow's out parameter uses its public default ICoreWindow
interface, not a guessed runtimeclass layout.

The slice also needs generated proxies for public IInspectable and the
Activated TypedEventHandler. The latter's IID,
`cf193a96-eb13-5e3b-8bdf-87b6efae8339`, matches the WIDL-generated public
header. These three interfaces, and no other application interfaces, are
generated. Temporary interface keys bind all three to ole32's PS factory,
with NumMethods 11, 6 and 4 respectively; their absent baselines are
checked and restored. No persistent bottle proxy registration is added.

The strict `coreview-proxy-test.c` passes **30/0** in no-game controls
and again before the game. A non-agile public ICoreApplicationView test
object lives on an STA; an MTA obtains it through GIT and calls IsMain,
IsHosted, CoreWindow (NULL result), add_Activated and remove_Activated.
All five server calls execute on the original pumping STA. Activated
also makes a real reverse proxy call to a test handler with NULL sender
and arguments. Both objects return to their original reference counts;
CoGetPSClsid resolves both the view and handler to the expected factory.
Non-NULL CoreWindow and activation-argument marshaling are not established
by this test.

The final admitted ole32 SHA-256 is
`7edcde96e5512a1177c30acfb08b75bb057b85f9c72c2631e508430a4739f66b`.
The exported patch reproduces both compiled source inputs byte-for-byte:
IDL `90c0fc030776a3cbbd6e612b05fa1719f5abf0233c3ef874a3e8003a57caa8f9`;
Makefile `7d8549e9f0bc7d2d3ea22cc9de1a1ed31b1aeda1b8957521632ffa7ea5aaf5af`.
Regression admissions pass dispatcher 47/0, aggregated marshaler 54/0,
ordinal 122 27/0 at RVA 0x9050 and PropertySet 29/0.

| Private 2120 evidence | Observed result |
| --- | --- |
| Apartments | Main `06a8` is MTA, type 1/qualifier 0; view `06c0` is MAINSTA, type 3/qualifier 0. |
| Lines 7687-7689 | CoGetStdMarshalEx(flags 1) returns S_OK to twinapi+0x3120b. |
| Lines 7805-7890 | CoGetPSClsid resolves ICoreApplicationView to ole32; NdrDllGetClassObject and CreateStub construct its real generated stub. GIT registration succeeds. |
| Lines 7905-7918 | GIT Get unmarshals the public view in the same STA and returns the original object successfully. |
| Lines 7935-7952 | The **next** operation is CoMarshalInterThreadInterfaceInStream for private IID `2e5500b6-66ad-467f-abb5-022a64283d88`. Standard MSHLFLAGS_NORMAL marshaling fails proxy/stub lookup with E_NOINTERFACE (0x80004002). This is not another failure of the public-view GIT registration. |
| Static caller correlation | Runtime delay-slot resolution identifies twinapi IAT+0x242290. Disassembly shows the API call at twinapi+0x182ce, return +0x182d5, inside helper +0x1822c; its caller returns at +0x16f5d after loading the private IID. These are verified static return sites correlated with the runtime import slot, not a captured unwind. |
| Error propagation | RoOriginateErrorW(0x80004002) returns to twinapi+0x7fbfc. The filtered relay did not record a direct inter-thread-marshal API return address or call stack. |
| Pump / callback | Private pump slot 3 is reached on `06c0`, handle 0x214. Callback twinapi+0x2fc90 is registered but no invocation is observed. No game ordinal-1500/CoreWindow creation is observed. |
| Exit | Child exits 0xc0000409 / 3221226529 after 1395 ms, zero total/visible windows. Early exit prevents the 140-second wait sample; no manual dialog closure is reported. |

The post-reboot baseline first passed all 22 original target checks.
Adding bottle/vendor ole32 makes **24** restoration targets. An initial
SSH-owned no-game attempt passed the proxy test but failed the existing
HWND dispatcher test because it lacked GUI-driver access; all targets
were restored. GUI-owned UNIT2115 passed all admissions, then refused
file restoration while a coordinator-owned scratch prefix was using the
shared CrossOver providers. Prefix isolation does not isolate vendor
DLL mutation. The coordinator established a shared-runtime hold; scoped
recovery restored all 24 files before the single game.

2120's owner then refused cleanup on a historical/census parent conflict.
The long capture reused Wine process IDs; the old parser has no generation
model. Recovery did not weaken that check or replay the game. A fresh
current census, exact native PID/start time, startup mapping and bottle
working directory independently admitted the six remaining owned service
clients. Only those clients and the recorded server were terminated.
Pinned backups restored all 24 targets, including ad-hoc-signed Mach-O
providers whose signing changes were distinguished from code changes.
The original failed receipt remains unchanged; supplemental runtime and
file recovery receipts record the successful restoration.

All 24 baseline hashes/absences, six temporary interface/server/dispatcher
keys, original empty WineDbg key, original CoreApplication payload path,
signature and bottle quiescence were independently verified. The shared
provider hold was then released. Future captures must address Wine PID
generations and retain a shared-provider exclusion window before another
control; these are capture-owner issues, not game-product fixes.
The mandatory quality runner is still missing (exit 2), so no overall
PASS or playability claim is made. The next private IID requires grounded
ABI/marshaling evidence and a separate grant.

| Private evidence | SHA-256 |
| --- | --- |
| 2120 `game.log` | `8800a620e08f46c5589b1e898a27df3518859d58dbe1896af36246b611a14f16` |
| 2120 original `receipt.json` (cleanup refusal) | `465718a7fc1e65c319a6be52d0b58f2d091fe7033630738e091017814e2e2a0f` |
| 2120 `runtime-recovery-admission.json` | `0415ff68795d79e86e8f428b6301e4f3a6682f82962c114438ee87bf098d3552` |
| 2120 `file-recovery.json` | `a56aa58db970855b2f3ca13edf649c4d24f3d9cadd6ce4c443b35512ebc130b9` |
| 2120 `coreview-proxy-tests.log` | `bfffbbd29a551beec66815887f79b9a4e4c550972e819249f3a431eed53fb390` |
