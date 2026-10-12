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

### Native private-view proxy discriminator: no-game 2130

VERIFIED-STATIC report5 grounds private IID
`2e5500b6-66ad-467f-abb5-022a64283d88` as a thirteen-slot,
IInspectable-derived CoreApplicationView interface. Genuine Windows uses
standard server aggregation, not agility/FTM, and supplies its matching
proxy in OneCoreUAPCommonProxyStub.dll. The granted preferred route was
temporary native admission, then factory/proxy/stub and STA-to-MTA stream
gates before one game. No frame/dxgi changes were included.

The genuine DLL is pinned to
`daea6f97cbdc41aedcc3de3ee735d73117485169a2df83816f4a0444bf54f5fb`.
It is retained only on the authorized host, never in this repository.
`private-view-proxy-admission-test.c` is a fail-closed loader/factory
discriminator, **not** the complete thirteen-slot stream test. It loads
the system32 provider, requests IPSFactoryBuffer from class
`95e15d0a-66e6-93d9-c53c-76e6219d3341`, and would try CreateProxy only
after factory admission. Full CreateStub/stream/HSTRING gates remain
unperformed because the earlier prerequisite failed.

| Private no-game 2130 evidence | Observed result |
| --- | --- |
| Provider admission | OneCoreUAPCommonProxyStub loads native at `0x6ffffd1d0000`; rpcrt4 loads builtin at `0x6fffff1b0000`. Successful loading does not establish that each imported function is implemented. |
| Factory call | NdrDllGetClassObject receives the grounded PS class and IPSFactoryBuffer IID on STA thread `01d4`. |
| Primary failure | Access violation at rpcrt4+`0x3e14b`, instruction `movq %rax,0x10(%rcx)`, writes to native proxy+`0x10f90` (`0x6ffffd1e0f90`). PE section metadata places that address inside read-only `.rdata`. |
| Captured backtrace | rpcrt4+`0x3e14b` ← rpcrt4+`0x1c2d2` ← OneCoreUAPCommonProxyStub+`0x1cca` ← admission test+`0x12f8`. |
| Unreached gates | Factory return, CreateProxy/CreateStub, NdrClientCall3/NdrStubCall3 interpretation and STA/MTA stream calls are not established. The game was never launched. |

The selected vendor rpcrt4 SHA-256 is
`ada0fd663b9549a2f35810846247ade79b07b6b3d3b9f60afd1afcd94b051ec6`.
The source inspection also found NdrClientCall3 exported but no
NdrStubCall3 spec entry; that static observation is not substituted for
the earlier actual factory AV. The next discriminator is Wine's native
proxy factory initialization and its writes to const proxy metadata.
No rpcrt4 stub, forced-agility shortcut, writable-memory bypass or
fallback private ABI implementation is added.

The shared-provider exclusion window was held throughout this short
no-game capture. The new native DLL made 25 temporary targets, all
independently restored to their original hashes/absences. The native DLL
is absent from the bottle again; original CoreApplication registration,
empty WineDbg baseline, strict signature and bottle quiescence were
verified. No new proxy registration was needed before the direct factory
gate, and no companion IID was registered. The original receipt records
the failed command and successful restoration without a supplemental
cleanup recovery. The shared-provider hold was released.

The one-game grant remains **unconsumed**. Route A is blocked at no-game
factory admission; the incomplete companion-handler RE still prevents
claiming an honest fallback proxy. The missing mandatory quality runner
(exit 2) also continues to block an overall PASS.

| Private evidence | SHA-256 |
| --- | --- |
| No-game 2130 `private-view-proxy-loader-tests.log` | `8bc395abab6a3ce55eb91bb263423c9c92bfeae609127976da6a07eba94107f7` |
| No-game 2130 `receipt.json` | `429413fb169beda3ffb1337ebaf373abcf3a2df3c8a8285ecbc1623d8689c35c` |

### Modern MIDL metadata and NDR32 server candidate: no-game 2140

`wine-rpcrt4-constproxy.patch` preserves the public factory ABI and puts
per-factory/file/index proxy and stub table copies in a locked,
process-lifetime cache. Factory initialization no longer writes to
DLL-supplied tables. The copies retain their headers, including the
stubless proxy-info pointer and the stub's server-info layout; delegated
and stubless method entries are resolved only in those copies. Missing
interface-name arrays are allowed. No VirtualProtect or agility bypass
is used.

The patch also factors NdrStubCall2's interpreter into a shared helper
and exports a real win64 NdrStubCall3. It selects NDR32 procedure metadata
from the server's syntax list, preserving the server-routine table.
NDR64-only input raises RPC_X_WRONG_STUB_VERSION rather than reporting
success. Baseline rpcrt4's export directory confirms that NdrStubCall3
was absent; the earlier successful LoadLibrary was not proof that its
imports were implemented.

The private admission test now supplies the grounded thirteen-slot
object and attempts CreateStub, then STA-to-MTA stream calls for slot 10
and slot 8's HSTRING. Unused companion methods return E_NOTIMPL; their
interfaces are not invented. A separate public EventHandler<IInspectable*>
test distinguishes a genuinely thread-safe agile/FTM handler from a
non-agile STA handler, using GIT register/get/invoke/revoke.

2140's first candidate rpcrt4 pin was
`5e889a21cee4f4e37b828234fd42ba2a27325769107da1bbdea692dbd3bd56ee`.
The existing cstub suite, with original-table pointer expectations
changed to copied-table expectations and const input fixtures, ran
**234 checks, zero failures, two TODOs**. Native DllGetClassObject now
returned S_OK, clearing the const-table write AV.

CreateProxy encountered a different modern metadata convention first:
Wine tried to call pIIDLookupRtn == `(void *)-1`. The fault was execute
access at `0xffffffffffffffff`, on STA thread `03ec`. The saved stack
return was rpcrt4+`0x14066`; the metadata pointer was native PS+`0x10de0`.
Static bytes place the sentinel at +`0x10e00` and identify a 39-interface,
TableVersion 16 block. CreateProxy did not return; CreateStub, stream
calls, both NDR3 interpreter paths and handler tests were **not reached**.
No game was launched and the 2130 game grant remains unconsumed.

The capture owner now tracks Wine process generations and preserves
ancestor-generation references instead of merging recycled numeric
PIDs. Three focused generation tests and the existing six capture and
six WineDbg safety tests passed; the parser also accepted both original
2120 and no-game 2130 traces. 2140 cleanup/restoration needed no recovery:
all **27** target hashes/absences, temporary registry keys, crash-dialog
suppression, original CoreApplication path, strict signature, no Wine
runtime and no bottle holders were independently verified. The
shared-provider hold was released.

Subsequent VERIFIED-STATIC evidence grounds the sentinel convention in
391 of the native PS's 394 blocks. The current candidate searches
stub-header IIDs linearly when the routine is -1 or NULL, and preserves
generated lookup routines otherwise. pAsyncIIDLookup is an unused
`const IID **` field here, not a callable routine. Fresh no-game admission
is required for this additional change and the remaining stream/handler
gates; there is no overall PASS. The mandatory quality runner remains
absent (actual exit 2).

| Private no-game 2140 evidence | SHA-256 |
| --- | --- |
| `private-view-proxy-stream-tests.log` | `e35bd9f0fa2bc4c7b0e9c270cb8ac9b2f565f60fdbd955f11647cfbfce99df2d` |
| `rpcrt4-cstub-tests.log` | `242191ba54328f0771eaabee91ffbd9ddf36def1b1b5c20a5e2eeed9cd432761` |
| `receipt.json` | `300cfbd540f45c8fedd6d41523086361f786ff1d78f4efb14da7a24a4482f901` |
| `independent-baseline-verification.json` | `978c4ea1a2034baa8644ee2f4c87253396add4291c8df0bafc5ff0d77b9f3c8f` |

### Compact metadata admission and the single game 2130 control

Before the next admission, VERIFIED-STATIC evidence identified another
modern form: compact proxy entries contain only a header and -1 sentinel;
compact stub entries contain only a header and -2 sentinel. Copying a
full vtable from these entries would read neighboring metadata. The final
patch synthesizes these tables from DispatchTableCount and procedure
offsets instead. Delegated offsets (0xffff) use the existing base-interface
thunks and NdrStubForwardingFunction; other methods use real stubless
client/server interpretation. Compact stub Release uses the stored
factory with the existing standard/delegating release routines.
Legacy full tables are still copied, never modified in place.

The final rpcrt4 pin is
`efd3b865b9d53c1cb5d63c3ffe9363cfc9a1bc80883b36b257c00c30b7be3e0b`.
Fresh GUI no-game controls established the thirteen-slot layout and
STA-to-MTA slot 3 GetIids, slot 10, and slot 8 HSTRING round trips.
2155 additionally called NdrClientCall3 directly for slots 10 and 8:
both client and server selected NDR32, four method calls ran on the STA,
no wrong-thread calls occurred, and the object returned to one reference.
The earlier 2145/2150 owner failures were trace predicates, not proxy
failures: stubless client calls use NdrpClientCall2 directly, and merged
stdout/stderr split one success marker. Direct NdrClientCall3 calls and
separate output files resolved these evidence issues without weakening
the unit gates.

| 2155 no-game gate | Result |
| --- | --- |
| Existing cstub suite, const/copy expectations plus -1/NULL hit/miss and cache lifetime checks | 248 checks, zero failures, two TODOs |
| Public view proxy, dispatcher, aggregated marshaler, ordinal 122, PropertySet | 30 / 47 / 54 / 27 / 29 checks, zero failures |
| Native private proxy and both NDR3 paths | S_OK; compact shape and delegated base verified; four STA calls, balanced references |
| Agile public EventHandler<IInspectable*> | GIT register/get/Invoke succeeds through a real FTM |
| Non-agile public EventHandler<IInspectable*> | GIT register returns E_NOINTERFACE (80004002); no get/Invoke is performed |

The coordinator explicitly authorized the single 2130 game despite the
non-agile synthetic-handler failure, to discriminate the real delegate.
The **2155 provider set stayed unchanged**; no event-handler proxy,
frame/dxgi change, graphics-backend change or registration-295 patch was
added. The game owner required the exact 2155 target/candidate manifest
and successful native-private admission evidence before launch.

| One-game 2130 observation | Evidence |
| --- | --- |
| Private view transport | 2e5500b6 marshals on MAINSTA thread 07b0 and unmarshals successfully on MTA thread 0798; real native stub/proxy creation succeeds. |
| Apartments and pump | View 07b0 is MAINSTA (3/0); main 0798 is MTA (1/0). Private dispatcher pump slot 3 reaches handle 214. |
| Activation callback | cuphead.App callback is registered at twinapi+2fc90; no invocation is observed. |
| Earlier recovered error | twinapi's delay import ext-ms-win-mrmcorer-resmanager-l1-1-0.GetStringValueForManifestField is unavailable. Its exception is handled and execution continues through ResourceContext fallback. |
| Terminal fingerprint | On 0798, windows.ui's accessibilitysettings_add_HighContrastChanged is the last logged call and its exact source returns E_NOTIMPL. An unhandled exception 1007 follows at SharedLibrary+41cf88. The vtable-call return site/unwind was not captured, so the causal caller is not asserted. |
| Outcome | Exit 4103 after 1494 ms; zero total/visible windows, no live 140-second sample. No c50898f6 marshaling is attempted, so the actual delegate's agility remains unproven. |

The game grant is now **consumed**. The owner completed exact-identity
cleanup and restored all 27 targets without supplemental recovery.
Independent verification confirmed all baseline hashes/absences, every
temporary key absent, the original empty WineDbg key, original
CoreApplication DllPath, strict signature, no Wine runtime and no bottle
holders. The shared-provider hold was released. No further game, handler
proxy or accessibility fix is included in this slice.

The source patch reproduces the compiled Wine inputs; native Microsoft
DLLs remain outside Git. Mandatory integration validation still exits 2
because gates/gate_runner.py is missing, so this result is not an overall
PASS or a playability claim.

| Final private evidence | SHA-256 |
| --- | --- |
| 2155 private stream stdout | `617440296b1c40c3eb8595c661653d6f387da253e8fa276389d92ee03d836c8f` |
| 2155 private stream stderr (NDR3 traces) | `28596e327dd3ba41a6b5f532ca14d8325f24e7db083b6c6e98806a0df78e23a9` |
| 2155 handler stdout | `301c7936535885c4d5c3b5d0ecb9188d1d5b66761b9ff74eadad0d74908b2e76` |
| 2155 cstub log | `244fd7479dcee679ef53705f29eb4d10cffbc3c3f8206e85e4f581c8c30d261c` |
| Game 2130 game.log | `f4361bf60f41f1c06f6271f4e4eaa6ef02ebf5556772cda72700ec3943023458` |
| Game 2130 receipt.json | `e23b92a3ea872dde4301b5df0b8a98f3f0bd1619a1341849bf6570a56eb5cc81` |
| Game 2130 independent baseline proof | `ffbea12d9c053386b3037077c059153f142769aaa61527926bf0e0a66a1bf8f4` |

### AccessibilitySettings retained registrations: blocked no-game 2160

The next narrowly authorized source candidate replaces only
AccessibilitySettings.HighContrastChanged add/remove. It follows the existing
CoreWindow retained-handler pattern: a per-instance critical section and list,
one handler AddRef per registration, increasing nonzero tokens, E_INVALIDARG
for NULL handler/token arguments, and S_OK for removed or unknown tokens.
Remove releases the detached handler outside the lock; final object Release
releases every remaining registration and destroys the critical section.
Token exhaustion fails explicitly rather than wrapping. HighContrast querying
is unchanged. No event is fired; host high-contrast change notifications remain
unsupported. UISettings2.TextScaleFactorChanged, InputPane.Showing/Hiding and
the parked EventHandler proxy are not modified.

`wine-accessibilitysettings-events.patch` contains the one-file source change.
`accessibilitysettings-event-test.c` checks argument validation, increasing
cookies, duplicate/unknown removal, retained references, final-release cleanup,
no unsolicited Invoke and parity with SPI_GETHIGHCONTRAST. Both the provider
and strict `-Wall -Wextra -Werror` test compilation succeeded. The candidate
windows.ui SHA-256 is
`fd0637140939f592608fb3a8d85da562430680d29d869dac0a96c2f1b0e070dd`.

The authorized no-game 2160 cycle stopped at an earlier dispatcher regression:
CreateCoreWindow returned failure, with 39 checks and one failure, because Wine
reported that no graphics driver could load. The successful 2155 trace loaded
winemac.drv; 2160 has no such load. The new lifecycle test was **not reached**,
so its runtime behavior is not yet verified. No graphics/backend workaround,
gate weakening or automatic replay was made. The separately granted single
2160 game is conditional on passing gates and remains **unconsumed**.

The owner restored all 27 targets with no restoration errors. Independent
read-only verification confirmed exact baseline hashes/absences, temporary
keys absent, original empty WineDbg, original CoreApplication DllPath, no Wine
runtime or bottle holders, and CrossOver's deep/strict signature. The capture
is `cuphead-owned-accessibility-unit-20261011-2160`; its receipt and independent
baseline proof are retained privately. The mandatory quality runner still
does not exist (actual exit 2 retained), so no overall PASS is claimed.

Additional coordinator static analysis identifies SharedLibrary+41cf88 as the
return address after RaiseFailFastException: the preceding 2130 exception is
a deliberate managed fail-fast, not an access violation. The association with
HighContrastChanged remains correlation without the managed exception details
or its caller.

### AccessibilitySettings admission and the single game 2160 control

The launch-method comparison found the environmental difference: the recorded
2155 invocation used AppleScript to run the owner through Terminal; the first
2160 invocation ran directly under SSH (Background bootstrap). The constructed
Wine environment and dispatcher arguments were identical, but the original
inherited environment was not recorded. The coordinator independently verified
an unlocked, logged-in console session and authorized one renewed no-game
cycle. It used the original Terminal method with `caffeinate -du`, without
graphics-provider or bottle configuration changes.

The separately preserved `cuphead-owned-accessibility-unit-20261011-2160-v2`
passed the new lifecycle test (33 checks, zero failures), dispatcher (47/0),
public proxy (30/0), aggregation (54/0), ordinal 122 (27/0), cstub (248 checks,
zero failures, two TODOs), and the private compact/NDR3 transport gates.
winemac.drv loaded normally. All 27 baselines and registrations were restored
and independently verified before admitting the conditional game launch.

The one authorized game 2160 used the **identical admitted candidate manifest**.
HighContrastChanged registration is followed by further AccessibilitySettings,
CoreApplication and CoreWindow activity: execution advances beyond the earlier
last-call fingerprint. It still terminates with managed fail-fast 1007 at
SharedLibrary+41cf88 on MTA thread 07bc, exit 4103 after 1288 ms, zero windows.
No next UISettings2.TextScaleFactorChanged or InputPane.Showing/Hiding
E_NOTIMPL is observed. CoreWindow.GetForCurrentThread twice returns NULL on the
MTA because that thread has no owned window; this alone is not attributed as
the cause. The final logged operation is successful FTM-backed GIT marshaling
of IID e2663f37-2e1b-500c-ad68-c3ed7a8f74c8, returning cookie f101, immediately
before fail-fast. No c50898f6 transport is observed. RaiseFailFastException's
pointed-to exception record/message was not captured; no new causal diagnosis
is claimed.

The 2160 game grant is **consumed**. Owner exit was zero with no restoration
errors; independent verification again confirmed all 27 baseline
hashes/absences, temporary keys, original empty WineDbg and CoreApplication
registration, signature, and no runtime or bottle holders. The shared-provider
hold was released. No further launch, handler proxy or speculative next-wall
fix is included.

| 2160 private evidence | SHA-256 |
| --- | --- |
| Renewed no-game lifecycle log | `e6d9e56eab1171d04810ec9849d7cc3b530e52655a9a154203192bf616c11673` |
| Renewed no-game receipt | `2d97fe6d655f99b6582d0a65d897e159ec343cfa0727affb0a4b4aaeaa650dc8` |
| Game log | `1a335885f18ee6be8e3707340c93e9820fbf650ce1a0c8e5db1f6ec9cd99475a` |
| Game receipt | `5943e2764d8700704040a088142cf8c1613e9e5173c462a32083a466a2e49ec0` |
| Independent baseline proof (each successful cycle) | `62137be3a74de61bcf138aa37e0adc407c79774d1ad5c4510363a0a6ad3f0203` |

### Logging-only control 2170: no managed message observed

The coordinator statically identified e2663f37 as the public
MapChangedEventHandler<String,String> IID, also present in native MrmCoreR.
The ResourceManager/QualifierValues explanation remains a hypothesis.
One explicitly authorized 2170 launch retained the exact 2160-v2 provider
manifest and repeated the usual successful pre-gates. Its only change was
game logging: warn+seh and trace+file alongside existing combase/error relay
traces. No product source or provider changed.

No OutputDebugString call or text was observed. Between successful MapChanged
GIT registration (S_OK, cookie f101) and the terminal 1007 there was no
intervening logged exception code or HRESULT. The new warning records
RaiseFailFastException with exception-record pointer 0x10e5a8, NULL context
and flags 1 on thread 07b4; the pointed-to record was not captured. Fail-fast
again occurs at SharedLibrary+41cf88. Earlier RoOriginateErrorW calls report
80040154 and 80070490 with no message on 07b4; earlier RoOriginateError and
RoOriginateLanguageException report 80004001 with empty messages on 07c0.
These precede MapChanged and are not newly attributed as terminal causes.

Native MrmCoreR loads. Cuphead's decoded resources.pri path resolves and
NtCreateFile is reached. Framework resources.pri probes for VCLibs 140,
.NET Native Framework 1.3 and .NET Native Runtime 1.4 return
STATUS_OBJECT_NAME_NOT_FOUND (c0000034). The earlier missing
ext-ms-win-mrmcorer-resmanager-l1-1-0 DLL probes also remain visible.
Neither absent framework PRI files nor these recovered imports are proven
to cause the managed fail-fast.

File tracing interleaves launcher stdout writes with diagnostic stderr.
The original bytes contain exit 4103 and elapsed 2104 ms, but the owner's
strict result parser cannot recover its normal contiguous markers and
returns `Expected unique live activated process` (owner exit 1).
The capture is not relabeled PASS or replayed. The single grant is consumed.
Restoration completed without errors; independent read-only verification
confirmed all 27 baselines, temporary keys absent, empty WineDbg, original
CoreApplication registration, signature and no runtime/bottle holders.
The shared-provider hold was released.

| 2170 private evidence | SHA-256 |
| --- | --- |
| Game log | `ec260d80dfba2a7e92c06f2220b374e621c30b6f74884abd37821f0158129205` |
| Receipt | `6ffdb5aef8265132453fb1e8b9af6796dd455238c67966c9d232d0dd7098dd67` |
| Independent baseline proof | `329e3d32eee6c92a507874fbcca384073f32a86a952f786b50a5445e79c69bd1` |

### ApplicationData folder paths and the single game 2180 control

The coordinator authorized only LocalFolder, RoamingFolder and TemporaryFolder
after the observed Unity worker TemporaryFolder E_NOTIMPL. The reusable
`wine-applicationdata-folders.patch` follows Wine's existing package.c
IStorageFolder/IStorageItem object precedent. It implements immutable Path and
Name, identity-preserving QueryInterface including IAgileObject, and balanced
reference/string lifetime. Other storage operations remain explicit
FIXME/E_NOTIMPL; LocalCacheFolder and unrelated ApplicationData APIs are not
expanded.

The getters use real GetCurrentPackageFamilyName and
SHGetKnownFolderPath(FOLDERID_LocalAppData), never a guessed identity or current
executable path. They create missing directories at
LocalAppData\Packages\PFN\LocalState, RoamingState and TempState, reject a
non-directory collision, propagate identity/directory errors, and return NULL
folder outputs on failure. Wine's headers lack the package-family prototype;
its LONG WINAPI signature is declared against the verified kernel32 forwarder.
The module imports shell32/ole32 for directory/known-folder helpers.

The public no-game `applicationdata-folder-test.c` compiles the exact folder
source with **test-only** PFN/known-folder seams for positive path, creation,
QI, repeat-access, string lifetime and reference checks. It uses a uniquely
created temporary directory and removes only its named, empty test directories.
The deployed DLL negative checks use a genuinely unpackaged caller: all three
getters return HRESULT_FROM_WIN32(APPMODEL_ERROR_NO_PACKAGE), clear outputs, and
reject NULL outputs. No synthetic token or identity seam is shipped.
The coordinator explicitly accepted this distinction; actual packaged behavior
was reserved for the authorized game.

No-game 2180 passed **96 checks, zero failures**, plus the standard
30/47/54/27/29 regressions, cstub 248/0 (two TODOs) and private compact/NDR3
transport. The ApplicationData provider SHA-256 is
`93f9b03236f9f86102d351aa1f670f71cbf8b291fc78d43ffa8bb8674b575136`.
Two additional pinned bottle/vendor ApplicationData targets bring the restore
set to 29. All targets and registrations were independently verified restored
before the conditional single 2180 game, which required the exact admitted
manifest.

The actual game proves packaged TemporaryFolder progression: thread 07dc
creates the real PFN TempState directory and receives Path
`C:\users\crossover\AppData\Local\Packages\StudioMDHR.20872A364DAA1_tm1s6a95559gt\TempState`
and Name `TempState`. Unity subsequently opens TempState\UnityPlayer.log.
The new first worker language exception is **80040154** after
RoGetActivationFactory fails to find
Windows.Devices.Enumeration.Pnp.PnpObject (IActivationFactory IID). It is
followed by RoOriginateError/LanguageException and a C++ e06d7363 exception on
07dc. The terminal reason remains 1007 at SharedLibrary+41cf88 on MTA 07d0,
exit 4103 after 1776 ms, zero windows. Cross-thread causality is not proven.
No TextScaleFactorChanged/Showing/Hiding call justifies adding the prepared
events patch.

Further coordinator static analysis refines the earlier attribution: 1007 is
0x1000 plus fail-fast reason 7, an unmanaged exception escaping a
managed-to-native transition, rather than reason 6 for an unhandled managed
exception. Unity's folder failure path calls the native C++/CX
raiseNotImplementedException helper. The folder change removes that observed
E_NOTIMPL predecessor but does not establish a complete native-exception fix.

2180 kept the 2170 logging flags, including trace+seh, but split controller
stdout from diagnostic stderr with coordinator approval. Contiguous result
markers are now preserved; owner exit was zero, without weakening predicates.
The single 2180 grant is consumed. All 29 provider baselines/absences,
temporary keys, empty WineDbg, original CoreApplication registration,
signature and quiescence were independently verified restored, and the
shared-provider hold was released. Newly created game TempState/Unity log
remain; no existing saved-game contents were changed or deleted.
Mandatory quality validation still exits 2 because its runner is absent.

| 2180 private evidence | SHA-256 |
| --- | --- |
| No-game folder test | `7b1d4dd1b16f758eb3da4a0bec387e4d89fe476f53445f4f0ca03e53593f0ace` |
| No-game receipt | `71d4a937dd07851f0dd6c4878c39b37bb27466550969df26336c00a5fa8f8d83` |
| Game stdout | `f8bd5ff38eb5e074029b103f0533829b9e1435ca8e7f5221df56b84bd55d8ea4` |
| Game diagnostic stderr | `600111c5c59870ea01c4e43bc1affa100534665e5fffc61da6f7e26dbb4a9b95` |
| Game receipt | `f3797faae8303f5c3ad4b7058215f9697d699617ad384e566d4cc6714a2a7065` |
| Independent baseline proof (each cycle) | `404ab2af25d7a7206ac9ac417ea5d058a0d7619dbbd762d3baa0d8c11f821381` |

### Real Pnp Device enumeration and nullable properties (2190 / 2195)

**VERIFIED-STATIC:** UnityPlayer's intended call is
`IPnpObjectStatics::FindAllAsync(PnpObjectType.Device, requestedProperties)`.
The helper at RVA `0x4d1ad0` obtains IID
`b3c32a3d-d168-4660-bbf3-a733b14b6e01`, sets the type to 3 and calls
vtable slot 7 (`+0x38`) at RVA `0x4d1b47`. There is no AQS argument.
The four appended property names are the raw `{GUID} PID` forms for
Device ClassGuid, DriverVersion, Name and DriverInfPath. ClassGuid is a
boxed GUID, not a string. The earlier 2180 runtime failed at factory
activation, before this static method could execute. Its preserved
177-byte UnityPlayer.log contains only build metadata.

`wine-pnp-device.patch` adds the public Pnp ABI/class registration and the
narrow Device FindAllAsync adapter using Wine's existing async/vector and
property-boxing helpers. The backend now enumerates real CM device IDs,
locates devnodes and queries their requested typed properties. ID-list and
byte-buffer growth have bounded retries; CM failures propagate as HRESULTs.
Requested key strings are snapshotted and preserved for lookup, including
custom `{GUID} PID` keys. Missing individual properties become null map
values, not fabricated data. CreateFromId, AQS, watchers, Update and other
Pnp kinds explicitly return E_NOTIMPL. Device all-properties, filters,
localization, non-system stores and query parameters remain unsupported;
the existing interface enumeration path is preserved.

**REPRODUCED no-game 2190:** the new Pnp factory/backend completed far enough
to insert the first boxed property, then the existing wintypes map Insert
dereferenced a null value for a missing property, causing c0000005.
The cycle stopped immediately; all 33 targets and temporary registrations
were independently verified restored. This was not a successful gate and
the remaining standard pre-gates were not run in that cycle.

`wine-propertyset-null.patch` corrects all shared nullable-value reference
paths: insertion, copying, Lookup, pair Value, replacement, removal, Clear,
view destruction and final release. It also corrects the coupled
AddRef-instead-of-Release cleanup bug, releases copied view entries and
copies the correct source slice/offset when splitting a view.
Missing keys retain E_BOUNDS; invalidated views retain E_CHANGED_STATE.

**VERIFIED no-game 2195:** the real registered synthetic devnode is returned
through the public async Pnp adapter. ClassGuid QI/GetGuid matches the backend
GUID under the caller's original key; missing DriverVersion lookup returns
S_OK/null. Async callback identity, completion and retained references balance.
The test-only CM seams are compiled out of shipped providers.

| Gate | Result |
| --- | --- |
| Nullable PropertySet lookup/iteration/Split/replacement/remove/Clear/lifetime | 50 checks, 0 failures |
| Exact-source CM growth/retry exhaustion/access-denied unit | 35 checks, 0 failures |
| Exact-source async backend-error propagation unit | 22 checks, 0 failures |
| Original cfgmgr32 / augmented cfgmgr32 | 1292 / 1369 checks, 0 failures; 1 TODO each |
| Original enumeration / augmented enumeration | 1938 / 1987 checks, 0 failures; 91 TODOs each |
| Original full wintypes suite | 1120 checks, 0 failures; 49 TODOs |
| Existing non-agile PropertySet / HighContrast / folders | 29 / 33 / 96 checks, 0 failures |
| Existing proxy / dispatcher / aggregation / cstub | 30 / 47 / 54 / 248 checks, 0 failures; cstub 2 TODOs |

The standard private NDR transport and other existing pre-gates also passed.
Both cycles used the established Terminal+caffeinate launch path, never a
plain SSH Wine child. All 33 targets returned to their baseline hashes or
absences; the synthetic devnode and temporary Pnp registration are absent;
the original WineDbg/CoreApplication values, strict CrossOver signature,
runtime quiescence and absence of bottle holders were independently verified.
Both no-game holds are released. No game was launched in either cycle.
Actual Cuphead Pnp progression or a rendered frame is not claimed.

| New admitted provider | SHA-256 |
| --- | --- |
| cfgmgr32 | `8a24ebedd512b2615b889836b874271d4070693ef9a563d33593bc87fb26ea4a` |
| windows.devices.enumeration | `9f2cd21d3cd85ba90ecd46b9f7b21fdcac89b9d8f756a8984047289ada69fbd5` |
| wintypes | `90900b71e275676d61cff3b82c3b0c1968b9de453368b9836c9e71f6862d2438` |

Both exported patches apply to preserved baselines and reproduce all 13
tested source files byte-for-byte. The Pnp patch preserves CRLF payloads
in its two new source files; its targeted `-text` attribute prevents Git
normalization from changing those tested bytes. Existing build configuration
disables tests, so the original/augmented suites were compiled with their
existing STANDALONE runner and Wine import libraries. No new test tooling was
installed. The mandatory repository quality command still exits 2 because
`gates/gate_runner.py` is absent; this is not an overall PASS.

| Private evidence | SHA-256 |
| --- | --- |
| Failed 2190 receipt | `7807cbc75c868a92451ae510608f2f45ac202091bc0e185d03b6976eb306c2a8` |
| 2190 independent restoration proof | `d143bd9f1022b003b3272331fc7e913a8765e8d4d56fecbc3cf26d51dfc72b24` |
| Successful 2195 receipt | `6c62e9398e4d2250dba0b68bda7943320dad63841cf37a3fa81c1c64ea1fae93` |
| 2195 independent restoration proof | `f7758fc17a004f443fd3b1dd97928465ee420420b2d2059ad6a0971f72d57312` |
| Nullable map gate | `0eacaaeff3ee1bdad40b43185f993ea76b4e4ed4044b77ff5484acc0eb81f4bd` |
| Augmented Pnp enumeration gate | `e67d19f2cf5a5b5412d50b424d02071287f3dc872550bd68448da49442bbe13e` |
| Pinned 2195 test manifest | `b7ec4f66f0adf26b346df89697f03725f3031f4a12e2695ccd9dbbf9fa6dd6ee` |

### One-game 2200: Pnp async progression, separate Detail.Vector failure

The validated source/test slice was committed and pushed as `42f5eef` before
the coordinator's conditional single 2200 launch. The owner required the exact
2195 receipt and independent-proof identities, checked the unchanged 33-target
candidate set, and repeated the standard pre-gates. No provider was rebuilt;
no frame, registration-295, events or handler-proxy addition was admitted.
Execution used Terminal with `caffeinate -du`, unchanged diagnostic flags
including trace+seh, and separate controller stdout/diagnostic stderr.

**VERIFIED product delta:** on thread 012c the PnpObject factory resolves to
the admitted enumeration DLL and returns S_OK. The four requested property
strings are duplicated from the enumeration module, followed by creation of
async operation `0x198b07a0` with implementation `0x143590`. Thread 0130 later
calls GetResults on that same operation; its implementation copies a VT_UNKNOWN
result and AddRefs the returned vector view. This is actual game async
progression, distinct from the synthetic 2195 devnode test. No FindAllAsync
entry trace is present; method attribution additionally uses the verified
Unity static caller and exact candidate source. Neither device contents nor a
rendered frame are claimed from those traces.

The separate MTA startup path on 0138 requests
`Windows.Foundation.Collections.Detail.Vector` with IID
`08c77958-89bf-5cf8-a9cd-c72147b9b3a9`. RoGetActivationFactory returns
CLASSNOTREG (80040154), at native twinapi+0x3d199. This error appears before
the worker's Pnp GetResults, not afterward in a serial initialization chain.
The coordinator's static caller analysis establishes propagation after cleanup,
not a successful fallback; the error must not be treated as benign.
Its exact private factory contract and downstream caller still require
read-only attribution before a registration or implementation candidate.

The terminal exception is reason 1007 on 0138 at SharedLibrary+0x41cf88
(`0x6ffff7b0cf88`), with RaiseFailFastException's record pointer `0x10e5a8`.
No e06d7363 C++ exception is captured in this cycle. The game exits 4103 after
1906 ms with zero total/visible windows; no live sample or first frame exists.
UnityPlayer.log is preserved both in TempState and as a private capture
snapshot: 201 bytes of build metadata, not a rendering record.

The single game grant is consumed. Owner cleanup completed without errors;
independent verification checked all 33 baseline hashes/absences, temporary
registrations and fixture absence, original WineDbg/CoreApplication values,
deep/strict signature, no Wine runtime and no holders recursively under the
bottle's .wineserver directory. A separate comparison confirms all 33
target/candidate/baseline triples are identical to 2195. The exclusive hold is
released. No further game launch is authorized by this receipt.
Mandatory quality validation remains blocked by the absent runner; successful
owned execution/restoration is not an overall PASS or playability claim.

| 2200 private evidence | SHA-256 |
| --- | --- |
| Game stdout | `b1eb44857a155b20afa87422b191c9723f2cae308ec732836e66baf30f1c370e` |
| Game diagnostic stderr | `e230765efdee051e363dd7054e938d099771f4f98f060ef1d8fb31450158b8a2` |
| Receipt | `b7f772336a4b7ba42064e819d5796d1e31baf8678f1b25f254ebe179e8106032` |
| Independent baseline proof | `f7758fc17a004f443fd3b1dd97928465ee420420b2d2059ad6a0971f72d57312` |
| Preserved UnityPlayer.log | `7c396f108b1c46506fed6eda2a1283cf33ffb90695825a8b2b2102c37063d71d` |

### Source-only2240: exception events and verified minidump streams

The Rosetta hardware-register lane remains CUT. A fresh synthetic-only
`exception-event-controller.exe` now uses public debug events and
`GetThreadContext(CONTEXT_CONTROL | CONTEXT_INTEGER)`, without debug-register
writes, target instruction patches, interception or trap emulation. It accepts
only the pinned `exception-event-fixture.exe`; it is not a package/game
controller. The older package controller remains excluded and unexecuted.

Only the first-chance breakpoint on the original process thread at the frozen
ntdll `process_breakpoint` RVA110a8 is continued as the startup trap. Admission
checks ntdll SHA256
`f8bd19c7606e0f7c506380d1c5c1d8bea477cbfea935f8c6f115de2ae8a2cb42`
and remote bytes `cc c3 49 ff 80 f8 00 00 00 31 c0 c3`.
Other first/second-chance exceptions remain `DBG_EXCEPTION_NOT_HANDLED`,
including a separately prepared application-handled breakpoint fixture.

For a non-startup first-chance event the controller preserves the actual
exception code/flags/address/parameters and stopped-thread integer/control
context. `MiniDumpWriteDump(MiniDumpNormal)` receives controller-local
`EXCEPTION_POINTERS` with `ClientPointers=FALSE`. Callback flags request only
thread metadata/context and module metadata, not stack memory or symbol data.
The completed dump parser checks bounded directory/RVA ranges, exact exception
thread/record/parameters, integer/control register equality in both exception
and thread streams, and a named module containing the exception address.
There is no StackWalk64 call or unwind claim.

The existing bottle/vendor dbghelp.dll baseline is unchanged and both copies
hash to `e111a5b162e52207d04fe82d704c1b169297c22c2fdff64348322a98a15abbfb`.
The proposed owner explicitly selects `dbghelp=b`; the controller hashes its
actual loaded provider path before creating the fixture. This is an additional
read-only provider identity check, not a 34th replacement.

**Bounds limitation:** the inspected Wine minidump implementation does not
invoke CancelCallback. Callback cancellation is therefore not claimed as a
hard writer bound. The fresh owner monitors stdout, stderr, per-command server
growth and every in-progress dump at 100ms intervals; its 18s/20MB thresholds
cause a failed cycle and exact owned-runtime cleanup, not truncation or a
success-shaped fallback. The controller additionally checks a 12s deadline and
10MB stdout-plus-dump threshold (2s deadline / 64KiB pressure for the two
negative fixtures), and rechecks completed output. A writer stall or overshoot
remains FAILED evidence; finite polling cannot guarantee zero byte overshoot.
Completed and failed artifacts are preserved and hashed after cleanup.

Both AMD64 PE32+ candidates strictly cross-compile with
`-Wall -Wextra -Werror` and deterministic PE timestamps. The only diagnostic
suppression is localized to Wine's deliberately packed minidump header.
Import inspection rejects SetThreadContext, WriteProcessMemory,
DebugBreakProcess and unverified StackWalk64. The two new local dump-growth
fixtures bring capture-accounting tests to **13 passing tests**. Owner/wrapper
syntax parses and local/remote staged bytes match. No Windows executable,
synthetic fixture, Wine runtime or game was executed for this preparation.
The mandatory quality command again exits2 because `gates/gate_runner.py`
is absent; this is not an overall integration PASS.

Private staged directory:
`~/xodus-app-tooling/cuphead/exception-event-candidate-20261011-2240`.
Fresh unexecuted owner/wrapper are
`~/xodus-app-tooling/cuphead/cuphead-exception2240-{owner,launch}.py`;
the proposed unique cycle label is
`cuphead-owned-exception-unit-20261011-2240`. The wrapper accepts only `unit`,
checks the owner's exact hash and refuses existing logs/status/work. No hold
has been granted for2240.

|2240 staged artifact|SHA256|
|---|---|
|exception-event-controller.c|`6f5202217c47c8e17e782987c2a5e501066311c0439574df029b41034d6b349c`|
|exception-event-fixture.c|`2c94208718dde99e583ca68a4a70197e7357ef2c45849926114c23ffb2581d8a`|
|exception-event-controller.exe|`558195bb602a130b980e82c4a503e22328624718b17ad8083bd6d6bab843d8bf`|
|exception-event-fixture.exe|`8e4cde6727f77141c4b9d6fde3be2316665b3c203704d5ea507ec224a6c37fdd`|
|compile_exception_event.py|`5a57341c47fd866d2aac6bbc6363a853f32bec1587d15179e68988bb82f18131`|
|source-compile-manifest.json|`75b08079cae96089fc6652db4be2b3f04d9e451727739b2f813c3cd04798c551`|
|cuphead-exception2240-owner.py|`134962895e43263e88780bc96a5957bb65cf204a733e6a00df33ebb35e36367f`|
|cuphead-exception2240-launch.py|`5f9c604f1fba5c175cbcde811ec11beab95d92c130eb049d093eaafb29d8195d`|
|capture_accounting2240.py|`28add3e42f44916613dddd83f7db87e8380767738c24da10c71bd36c463ed1a1`|

**Proposed no-game admission only:** retain the exact2195 33-target
target/candidate/baseline triples, receipt
`6c62e9398e4d2250dba0b68bda7943320dad63841cf37a3fa81c1c64ea1fae93`.
The fresh owner checks all candidate identities and every baseline against
that receipt before replacement. Existing standard pre-gates, original and
augmented suites, nullable50 and compiled-out backend-error units remain.
Then run, stop-first-failure, these seven synthetic modes: handled,
unrelated breakpoint, unhandled (first plus second chance), injected
read-error, deadline, aggregate-size and live detach. Positive modes require
actual dump/context/module validation and normal owned exit. Negative modes
require the exact expected reason and demonstrated intended fixture boundary
plus successful owned termination/continuation/detach/quiescence; arbitrary
startup failure cannot pass. All modes require successful owned handle-close
accounting. Per-mode and completed aggregate accounting is retained.

No alias selection, Detail.Vector registration, #295/frame/events addition,
game/package launch or changes to package/save contents are proposed. Restore
all33 targets and original registration/fixture/settings baselines, verify
dbghelp and alias unchanged, strict signature and complete runtime/recursive
holder quiescence independently before releasing any future exclusive hold.
Failed2230 and original2210 captures/receipts remain intact. Exception-event
observations alone cannot establish mrt100_app guard entry, private Vector
slot30/outer-call return, or reason7 causality.

### Actual2240: handled capture succeeds, unrelated-breakpoint fixture fails

The coordinator granted exactly one exclusive bounded **NO-GAME2240** cycle
on the above pins and canonical2195 33 triples. Exact-byte/fresh-path checks
passed before the single Terminal+caffeinate launch. No package/game controller,
native alias selection, Detail.Vector registration or extra patch was used.
The cycle stopped at the second synthetic mode's failure; no replay occurred.

Standard pre-gates passed, including cfgmgr32 original1292/augmented1369,
enumeration original1938/augmented1987, wintypes1120, nullable50,
devnode35 and compiled-out backend-error async22, all with zero failures.
Retained proxy/marshaling/activation/error/resource/window-interface units also
passed their owner checks. This does not override the later FAILED cycle.

|2240 synthetic mode|Actual result|
|---|---|
|handled|PASS: controller exit0, fixture exit0, quiet1; one genuine first-chance e0425844 and one validated90552-byte dump; application handler ran;12 owned closes/0 failures|
|unrelated breakpoint|FAIL: controller exit7, fixture exit c000001d; original80000003 passed NOT_HANDLED and handler ran, then additional c000001d first/second chances; two validated90552-byte dumps; quiet1,13 closes/0 failures|
|unhandled|NOT RUN|
|read-error|NOT RUN|
|deadline|NOT RUN|
|aggregate-size|NOT RUN|
|live detach|NOT RUN|

Both processes observed `C:\windows\system32\dbghelp.dll` with actual admitted
hash `e111a5b162e52207d04fe82d704c1b169297c22c2fdff64348322a98a15abbfb`.
Both recognized the exact pinned ntdll startup trap once. The handled event
preserved parameters11223344/55667788, fault kernelbase+da77,
RIP at that address and RSP31fc70; actual exception/thread/module stream
correlation passed. It was continued NOT_HANDLED, and the fixture's own
vectored handler ran before normal exit. This is observed exception-event and
metadata-dump support, not inferred hardware support or game admission.

In the breakpoint fixture, first-chance80000003 at kernelbase+da77 was not
swallowed by startup recognition. Its dump validated and the application
handler ran. The next event was first-chance c000001d at kernelbase+da79,
with a second validated dump, followed by second-chance c000001d. Both
were continued NOT_HANDLED and the process naturally exited3221225501.
This is consistent with breakpoint context advancement applied to a
RaiseException-generated breakpoint, but source attribution is not yet
established. No context correction, instruction mutation or fallback was
applied. The original FAILED receipt is not relabeled because dump capture
worked.

Per-process aggregate totals were3525565 bytes (handled) and5358264 bytes
(breakpoint), including split output, completed dumps and respective server
growth3016890/4740077. The full server file is155911946 bytes including all
standard pre-gates; it is not the single-synthetic bound measurement.
No writer stall or over-limit occurred. Timeout/read-error/over-limit/live
detach ownership behavior remains unverified because those modes did not run.
All original logs/dumps are preserved, untruncated.

Owner result is **FAILED**:
`RuntimeError('Checked command failed: exception-event-breakpoint')`,
with zero restoration errors and complete owner restoration. Independent
verification confirms all33 original baseline hashes/absences, canonical
target/candidate/baseline triples, temporary keys/devnode/Detail.Vector
absence, original CoreApplication/WineDbg settings, both dbghelp copies and
native alias unchanged, strict signature, no runtime or recursive bottle
holders, and no owner/wrapper/caffeinate processes. An initial expanded
verifier incorrectly required the entire Debug key to be absent; read-only
inspection corrected this to its original two permanent exclusion values
and no RelayInclude. That verifier assumption failure was not runtime residue
and no setting was changed to satisfy it.

The consumed2240 hold is released. No next runtime or game cycle is authorized.
The mandatory quality runner is still absent; no overall PASS or first-frame
claim. Failed2230 and actual2210 evidence remain unchanged.

|2240 preserved private evidence|SHA256|
|---|---|
|FAILED owner receipt|`5911e119bcbf97d6fb66d535cec3bc99d4379c76a991e24b09a6b4eb71e33b9a`|
|Independent baseline proof|`85d09fcce8bcc5cdcb8059aa493f9341936f253282335155ad12962d1e7bf312`|
|Expanded independent proof|`e97b1f9559ddf2b48849d7eb4325ae71feb74dd53a92618942b6d2f3c8fdb04e`|
|handled stdout|`48d7667f3be8ef1a0edcae64ddfce8278b60fa7aac6ba3f86386572a0faaf233`|
|handled stderr|`a8ef48e3fd5040a8b06b5d4ad3ac9019e932ceb9a5dc5924421a256f0ad603de`|
|handled dump0|`8af5db45001b412a34c727974f7699a572a0c71eb4fb40bed850353bd2f9d64f`|
|breakpoint stdout|`3183e389e82b4e3760aed86e0c37ce351d130c1baaf55cde2078f9cc7323f91d`|
|breakpoint stderr|`f1731627af1e9395a7ecfec53bd66246ed50a67b68058b16e43dae790bafc4f0`|
|breakpoint dump0|`56b966bc9f80d57caab35c4c056167ddab41a7e0c39b28e7edd8856abc6ceebf`|
|breakpoint dump1|`97cd415b7a2f7c29c2e425a9e415d977c89a6f2ea3507a8850be3cd2d57bfe8e`|

Private evidence root:
`~/xodus-app-tooling/cuphead/cuphead-owned-exception-unit-20261011-2240`.

### Source-only2250: replace unsuitable synthetic breakpoint with owned INT3

After2240 persistence/hold release, the coordinator authorized source-only
attribution and a grounded fixture correction, not a new runtime cycle.
The admitted kernelbase SHA remains
`9308d50feb4f7a702d9935708680a91b60c6e053082164284a809af35e32c73f`.
Frozen bytes at da72 are `e8 b9 15 00 00` (CALL RtlRaiseException), followed
at da77 by `48 81 c4 c8 00 00 00 c3` (stack restore/RET).
`dlls/ntdll/signal_x86_64.c:839-841` captures that CALL's return address into
both context RIP and record ExceptionAddress. This matches actual2240 da77.

`dlls/ntdll/unix/signal_x86_64.c:1750-1753` unconditionally decrements the
application-dispatch context RIP for EXCEPTION_BREAKPOINT. This is verified
in the actual frozen ntdll.so, not only current source:
SHA `e4d34518f2627d73a0035223939ec53bea0dd9829b07238486f51c35cae34422`,
`call_user_exception_dispatcher` at389c0, comparison at38ac1,
`decq %rcx` at38ad4 and saved RIP write at38ad7.
The synthetic RaiseException breakpoint therefore dispatches its application
handler with RIP da76, the final zero byte of the preceding CALL operand,
not an INT3 site. The2240 fixture returned CONTINUE_EXECUTION without fixing
that synthetic context. Resuming at da76 decodes across the stack-restoration
instruction, consistent with the actual later illegal-instruction event da79.
The unchanged controller's NOT_HANDLED disposition is not evidence of an
incorrect debugger resume: the synthetic fixture was unsuitable for a genuine
INT3 continuation gate.

The only repository implementation correction is the fixture: a fixed
AMD64 assembly function contains `INT3; RET`, with owned labels at
fixture RVAs1564/1565 and verified PE bytes `cc c3`. It is not runtime
instruction patching. Its application handler accepts only the breakpoint
record at that exact fixture-owned address, verifies the one-byte opcode and
adjacent resume label, and accepts only context RIP at the trap or immediately
following instruction. It sets its own application CONTEXT RIP to the known
following RET and returns CONTINUE_EXECUTION. Other breakpoint records are
CONTINUE_SEARCH. This narrowly handles the AMD64 INT3 continuation convention
and Wine's documented source-backed one-byte adjustment without changing
the debugger's immutable captured record/context or any product provider.
There is no controller swallowing or target-context rewriting by the debugger.

The controller source and executable are byte-identical to2240. Handled,
unhandled first/second-chance, read-error, deadline, aggregate-size and live
detach checks are retained. The fresh owner's breakpoint assertions now
require an actual event at the printed owned trap, one-byte adjacent resume
site, dump/context/module validation, NOT_HANDLED, own application handler
marker, exactly one first chance/no second chance and normal exit via the
unchanged controller. Synthetic parameters remain required for the actual
RaiseException custom-code modes, not falsely required for a real INT3.

Both AMD64 candidates strictly compile source-only; imports remain checked
for mutation/unverified unwind and the controller pin is unchanged.
All13 local accounting fixtures pass; fresh owner/wrapper syntax and staged
bytes verify. No2250 fixture/controller/owner/Wine execution occurred.
The mandatory quality runner remains absent, actual exit2; no overall PASS.

Fresh candidate directory:
`~/xodus-app-tooling/cuphead/exception-event-candidate-20261011-2250`.
Fresh staged root owner/wrapper are `cuphead-exception2250-{owner,launch}.py`;
proposed unique cycle label
`cuphead-owned-exception-unit-20261011-2250` has no work/log/status files.
Consumed2240 candidate/owner/dumps/receipts are not modified.

|2250 source-only artifact|SHA256|
|---|---|
|exception-event-fixture.c|`f80eb93e7fa3ae24090df8165cc6c8f8afc5d0e7666d44c04ddf4e7a3f4e3620`|
|exception-event-fixture.exe|`7cb8ed07e7ab8f42ed296d70863e75a440f5d0b71d3c1b6d302fd6839e28bfb2`|
|unchanged controller.exe|`558195bb602a130b980e82c4a503e22328624718b17ad8083bd6d6bab843d8bf`|
|source-compile-manifest.json|`2493c76bc8d8167df380157d93e68547b79dea4ff10c3c85a452ffc41a5347d9`|
|cuphead-exception2250-owner.py|`7427f5c216ad5bb7768482e95f7a83b37f3d7bef3033dfba96bea0b0ef5d1d15`|
|cuphead-exception2250-launch.py|`f13ae13df8196f75d1c7bfff2ffa5e83cd09bc29fb592be7e9c2ead48581c091`|
|capture_accounting2250.py|`28add3e42f44916613dddd83f7db87e8380767738c24da10c71bd36c463ed1a1`|

Proposed scope for a future decision remains one exclusive **NO-GAME** cycle:
canonical2195 33 triples, unchanged pinned builtin dbghelp, standard pre-gates
then seven stop-first-failure modes with the real INT3 fixture substitution.
Same18s/20MB live aggregate owner failure thresholds, preserved originals,
expected-negative reasons/boundaries and successful owned cleanup required.
Same independent33/keys/devnode/settings/dbghelp/alias/signature/full runtime
and recursive-holder restoration checks. No alias selection, Detail.Vector
registration, package/game controller, extra provider patch or second cycle.
No2250 runtime hold or game authorization has been granted.

### Actual2250: seven synthetic exception gates pass; restored independently

The coordinator subsequently granted ONE exclusive **NO-GAME2250** cycle
on the exact above artifacts, canonical33 triples and builtin dbghelp.
Fresh paths and all five executable/owner/wrapper/helper pins were verified
before the single Terminal+caffeinate launch. Owner exit0, no error and no
restoration errors. Standard pre-gates and all seven synthetic modes passed;
no package/game controller or alias selection was used.

|Mode|Observed result|Aggregate bytes|
|---|---|---|
|handled|e0425844 first chance, validated90552-byte dump, NOT_HANDLED, application handler, normal exit0/quiet1;12 closes/0 failures|3525988|
|real breakpoint|trap140001564/resume140001565/bytecc; original80000003 address at trap, stopped RIP140001565, validated91418-byte dump, NOT_HANDLED, owned INT3 handler, exit0/quiet1;12 closes/0 failures|3539575|
|unhandled|e0425845 first and second chances NOT_HANDLED; first-chance91418-byte dump validated, natural exit3762444357=e0425845/quiet1;12 closes/0 failures|3529337|
|read-error|actual first-chance custom event/context, injected address1 read fails; exact reason3, zero dumps, owned terminate/continue/detach/quiet all1;11 closes/0 failures|1661868|
|deadline|fixture deadline boundary entered, exact reason1; owned terminate/continue/detach/quiet all1;11 closes/0 failures|1653310|
|aggregate-size|fixture pressure boundary entered, exact reason2 at controller64KiB output threshold; owned terminate/continue/detach/quiet all1;11 closes/0 failures|4050635|
|live detach|custom first chance and validated91418-byte dump; NOT_HANDLED, application handler; actual detach1/quiet1/exit0;13 closes/0 failures|3543933|

Every mode logged the actual loaded
`C:\windows\system32\dbghelp.dll` admitted hash e111a5b1..., and exact startup
recognition once. Every positive dump's actual exception/thread/module
streams correlated with the preserved original event and stopped context.
The real INT3 test therefore verifies the grounded fixture correction; it
does not relabel failed2240 or alter debugger disposition/context.
Negative controller results deliberately remain failed1 with exact reason;
test-process exit0 requires that expected reason and successful owned cleanup.

Live and completed aggregate accounting includes stdout/stderr, dumps and
per-mode server growth. All external18s/20MB thresholds were respected.
Aggregate-size stdout65813 bytes is preserved, including the genuine
controller64KiB threshold overshoot; it is expected-negative evidence, not
truncated output. The full server file168060548 bytes includes standard
pre-gates, not a single-mode20MB violation. No stalled minidump writer or
external18s/20MB breach was forced: those outer emergency thresholds remain
monitored limits, not independently exercised hard-cancellation capability.
Wine's missing CancelCallback limitation and finite polling overshoot remain.

Independent verification confirms all33 baseline hashes/absences and exact
canonical triples, temporary registration/devnode/Detail.Vector absence,
original CoreApplication/WineDbg and two Debug exclusions, both dbghelp copies
and alias unchanged, strict signature, no runtime/recursive bottle holders
or owner/wrapper/caffeinate processes. The entire2240 receipt and all recorded
capture/executable artifacts were rehashed against its failed receipt and
remain unchanged.

|2250 preserved evidence|SHA256|
|---|---|
|Successful no-game owner receipt|`c66b1693eec495032a09b178c76ea42cdc604e4848637a241d2945f17004537e`|
|Independent baseline proof|`f7758fc17a004f443fd3b1dd97928465ee420420b2d2059ad6a0971f72d57312`|
|Expanded independent proof|`66ced7507fe31c763c86a6ca6a818df0ff38d954f4fbd3070b5f7c8e78f2ce2c`|
|handled dump|`5c758e6ae978521274e603faed1cff23b5ab04de1fd31c169e3d7e30f8d37476`|
|real INT3 dump|`eda37058897bf3460970b7b7f106c7f7bfb5f8bc64f5eb0c01b86d5242b12861`|
|unhandled dump|`860f066817a42cbc9041e44d847d9bfd33b1064e8262769349a40489a7b91311`|
|live detach dump|`b4f94ab436134f7a9eb9da3041277c22c854769c471fbd54121b9c2692a82149`|

Evidence root:
`~/xodus-app-tooling/cuphead/cuphead-owned-exception-unit-20261011-2250`.
The single2250 grant is consumed and exclusive hold released after independent
verification. No next runtime/game grant exists. This is successful bounded
synthetic exception admission, not a package diagnostic, guard-entry/private
Vector-return attribution or first-frame claim. Mandatory quality-runner
absence still blocks an overall integration PASS.

### Source-only2260: filtered explicit package diagnostic adapter

The coordinator authorized source-only preparation, not a game/runtime cycle.
`exception-event-package-controller.c` now includes the validated
exception-event engine with a compile-time package adapter. It does not use
the cut hardware header or old package/debug controller. Compiling the
synthetic branch still produces the exact2250 controller SHA558195bb... and
fixture SHA7cb8ed07...; existing2250/failed2240 artifacts are untouched.

The adapter queries the existing registered package/App image, requires its
exact Cuphead.exe bytes, creates it suspended with DEBUG_ONLY_THIS_PROCESS
and the normal `-ServerName:Xodus.Cuphead.App`, then performs explicit
NtXodusActivateRegisteredPackage before resume. It checks launcher and
pre-activation child genuinely unpackaged, activated token's exact package/App
strings, mask3/Flags1/Origin4 (DeveloperUnsigned, not Store), no AppContainer,
launcher still unpackaged and repeat activation denied. Catalog mismatch,
image hash mismatch, missing export, activation/identity or process failure
stops rather than fabricating claims. Existing application exit/window checks
are retained: first-wait status around11s if still running, PID-only window
count/visible/bounds, final actual exit and owned wait/handle results.
Natural app failure is preserved as its actual exit, not product success.

**Exact filter:** all non-startup event records/parameters are logged and
NOT_HANDLED. Dumps are selected for original80000100, every later first-
and second-chance exception after that record, and00001007 even if it arrives
before the stub. At most **eight selected events/dumps**, **2,000,000 bytes
per dump**, **10,000,000 controller stdout+dump bytes**, controller60s.
Selection exhaustion fails reason6 and owned cleanup; failed/oversized dumps
are retained, never discarded to continue into a success-shaped receipt.
Routine module/thread/debug-string events do not generate dumps.
Each selected event preserves its genuine stopped context/record, validated
exception/thread/module streams and an event-indexed live module inventory
(base/SizeOfImage/name, read-only PE headers), plus explicit event/thread/code/
chance-to-dump-file bindings. Module unloads update inventory; the fixed bound
is128 module-load records and128 owned thread handles, with overflow failure.
Original ntdll startup and dbghelp pins remain; loaded twinapi/mrt100_app/alias
files have their existing exact pins checked. No raw stack or unwind is added.
Exception observation alone still cannot prove guard entry or private Vector
slot30/outer-call return.

Fresh2260 owner uses the existing exact-lineage cleanup/restoration harness,
canonical33 triples and successful2250 receipt/expanded-proof identities.
It proposes standard required provider pre-gates followed by **one** explicit
package capture; identical2250 synthetic behaviors are not replayed without
new evidence. Old launch/sample/+file branches are removed from the fresh
owner. Only package stderr uses minimal seh/loaddll (plus error diagnostics);
the separate owned server retains its lineage trace for safe cleanup.
The single package command's external threshold is **75s/20,000,000 bytes**,
polling100ms across stdout/stderr/server delta/live and completed dumps.
Completion is polled before timeout classification. Failed in-flight command
accounting is conservatively rechecked after owned cleanup, including server
growth from command start; completed originals and hashes are retained.
This is a monitored owner threshold with finite overshoot, not a hard writer
cancellation guarantee: Wine still lacks CancelCallback, and stalled-writer
outer termination has not been exercised.2250 proves controller negative
cleanup and live detach, not debugged package activation parity or outer stall.

Read-only fixture discovery found an existing signed graph fixture with App
and Secondary applications, but `Xodus.PackageGraphTest` is **not in the active
Cuphead bottle catalog**. Its static graph executable is not an already
available registered-server/debug fixture for this exact Cuphead adapter.
No registration, resigning, repackaging or fixture runtime was performed to
force parity. The new query/create-debug/activate/resume seam, package-specific
module-inventory/filter/count limits, second-chance dumps and new ADVAPI32/
USER32 loader path therefore remain unexecuted/unverified runtime boundaries.
If a future seam/dependency fails, stop with that evidence, not a loader hack.

Three AMD64 PE32+ candidates strictly compile with empty warning logs;
mutation/unverified-unwind import checks remain. All13 local aggregate/dump
accounting tests pass; owner/wrapper AST, staged pins and fresh work/log/status
absence verify. Mandatory quality command still exits2 (missing runner).
No Windows executable, Wine runtime, package or game was executed for2260.

Candidate directory:
`~/xodus-app-tooling/cuphead/exception-event-candidate-20261011-2260`.
Root owners: `cuphead-exception2260-{owner,launch}.py`, helper
`capture_accounting2260.py`. Unique proposed cycle label:
`cuphead-owned-exception-package-20261011-2260`. Wrapper accepts only
`capture-only`, pins its owner and refuses replay. No2260 hold is granted.

|2260 source-only artifact|SHA256|
|---|---|
|shared exception-event-controller.c|`09cd6d0118db0f4aa350a5e41656032c39cf658e5720fc48efae8bcf7278b7e9`|
|exception-event-package-controller.c|`61cf5b63c435fdbb592b77b92bb474ba1cecb4596d060495351337edc3cd4de5`|
|exception-event-package-controller.exe|`5cd98f9d79c97ee9f4b3b630f62c0cc41fe31dd883cdfa8b4b180dd10271467f`|
|unchanged synthetic controller.exe|`558195bb602a130b980e82c4a503e22328624718b17ad8083bd6d6bab843d8bf`|
|compile_exception_event.py|`41a8c5e4a75097dca912ab7a920d58a15402e5f728c8657eb4c44ae73cbb9b9c`|
|source-compile-manifest.json|`fab8a7ee56126b87d3148ab1bcbab1b2e590480bc24e62edda365b64efb974f1`|
|cuphead-exception2260-owner.py|`d1f7c45dad0290505937ca81ee439d0bd0b354f670df82e7aa769f6f22b8b34a`|
|cuphead-exception2260-launch.py|`0aff59f2d19f39c8630dbfbd0230a236d431a701b15f9bad74bdab11704ab42a`|
|capture_accounting2260.py|`28add3e42f44916613dddd83f7db87e8380767738c24da10c71bd36c463ed1a1`|
|original queried Cuphead.exe|`c4aaae517d4433d576748c2b5d963083deea88df154115d099662f075dc9c21c`|

Proposed admission only: one exclusive targeted game-capture cycle on the
identical2195 33 triples, unchanged read-only builtin dbghelp and ONLY the
previously verified native Vector alias selection/temporary originally-absent
Detail.Vector key from2205/2210. Alias remains SHA5a94b019... outside Git,
vendor and bottle; same temporary server/App activation registrations and
existing standard pre-gate keys/settings are independently restored.
No #295/frame/events/provider patch, package/save change or second launch.
Restore all33 baselines and original keys/devnode/settings, verify dbghelp and
alias unchanged, strict signature/full runtime/recursive holder quiescence
independently before releasing any future hold. Preserve2250 and failed2240/
2210 evidence. If1007 is not a debug event, report that telemetry gap explicitly;
its synthesized code is not the recovered original exception or causality.

### Actual2260: original exception captured; cumulative inventory ceiling fails

One exclusive capture-only2260 cycle was granted on the exact pins above.
All artifact/fresh-path checks and standard pre-gates passed. The single
Terminal+caffeinate launch reached the previously untested package seam:
catalog query S_OK, suspended debug process activation S_OK (pid536),
exact activated mask3/Flags1/Origin4/no AppContainer, launcher NOT_FOUND and
repeat activation ACCESS_DENIED. No alias/provider/package changes beyond
the granted scope. First-wait TIMEOUT was observed at12936ms; windows total0,
visible0, valid1. This is activation/diagnostic evidence, not first-frame proof.

The genuine first-chance80000100 arrived on thread544 (0x220), flags1
(noncontinuable), address/RIP `kernelbase+da77` =6fffff4ada77,
RSP10f440, RCX10f460, RDX0, R8=6ffff6ddc3a2, R9=10f548.
Its two parameters were6ffff6d9d950 (`twinapi.appcore+1dd950`) and
6ffff6ddc3a2 (`twinapi.appcore+21c3a2`). Minimal seh trace records the
failed delay import `GetStringValueForManifestField` immediately before
the WINE_STUB event. The190902-byte dump preserved and validated actual
exception/thread/module correlation. Independent direct stream decoding
confirmed the original record/parameters/RIP/RSP and59 module entries.
The event was continued NOT_HANDLED; no debugger context/record rewriting.

The **first subsequent failure** is controller bookkeeping, not a loader
dependency: module_event's fixed128 array counts cumulative load records,
while UNLOAD marks entries inactive but never reuses them. Actual log contains
129 MODULE records, with repeated bcrypt.dll LOAD/UNLOAD pairs at the same
base; the129th LOAD fails the128-record check, producing event6/reason4.
The controller correctly fails/stops rather than dropping inventory.
Original natural app exit is UNOBSERVED; no later1007 DEBUG_EVENT was captured
before abort. That is a telemetry gap caused by this finite capture failure,
not evidence that1007 is absent/undeliverable or that the original stub is
benign. Guard entry/private Vector return/reason7 causality remain unproved.

Actual owner remains **FAILED**:
`RuntimeError('Checked command failed: exception-event-package')`,
controller exit7/failed1/reason4.148 owned handles close with0 failures;
owned termination/continuation/detach/quiescence all1. Complete owner restore
has zero restoration errors. Package aggregate17107454 bytes consists of
62195 split-output bytes,190902 dump bytes and16854357 server growth.
No75s/20MB or per-dump/controller-size breach/stalled writer occurred.
Full server189023100 bytes includes pre-gates/cleanup. All original artifacts
are preserved untruncated; no success relabel or second launch.

Independent verification confirms canonical33 triples and all original
baseline hashes/absences, temporary server/App/Detail.Vector/Pnp registrations
and devnode absence, original CoreApplication/WineDbg/Debug settings,
both dbghelp copies and alias unchanged, original Cuphead.exe unchanged,
strict signature, no runtime/full recursive bottle holders or owner/wrapper/
caffeinate. Entire2240/2250 receipts and all their captured artifact hashes
remain unchanged. The consumed2260 exclusive hold is released.

|2260 preserved evidence|SHA256|
|---|---|
|FAILED owner receipt|`a628a6a072a7df8cc5bdfdeb0b3643378ccc42841e201de2d4cf0ed62dc4db68`|
|Independent baseline proof|`8fec97ef1593930be3ac6b226dbd8fde347129075d107b248035f7343c5d084f`|
|Expanded independent proof|`cba8c2f7af1942ce0335a49244a47f2bb577f4c61103e452865ed3fe76968c32`|
|Original exception dump|`489102cea18b9bbd694637eacb39e064a80b77ca24cdabcfecb5ed9270d25918`|
|stdout|`8fc9af8229a5d5e4cfe55f1444ee2e98a8716684692e1b008e2367e176c4ecb9`|
|stderr|`ea2189ac9f9689e488d1c0ac9a22d802cb87727ce61f7ed43353d3501489b2ef`|

Evidence root:
`~/xodus-app-tooling/cuphead/cuphead-owned-exception-package-20261011-2260`.
Cheapest proposed correction is source-only inventory-slot reuse keyed by
base, retaining128 simultaneously-live entries, rejecting duplicate live
bases/true overflow, with focused synthetic bookkeeping tests. It is not
an increase of output/event/time limits or provider/debugger subsystem fix.
No next runtime/game grant exists. Missing mandatory quality runner and
unexercised outer-writer hard-cancellation limitations remain.

### Source-only2270: matched-unload inventory reuse and original-exception attribution

The package-only inventory now keys live entries by module base and reuses
the first inactive slot only after a matched unload. The limit remains
128 simultaneously-live images, including the process image. Duplicate
live bases, an unknown/repeated unload and a genuine129th live image fail
explicitly; no capacity, capture/filter/provider or deadline limit increases.
This follows the public LOAD_DLL/UNLOAD_DLL debug-event base-address contract.
Each selected exception first copies its live inventory by value, prints
that event's immutable snapshot, then writes its separate dump. Later reuse
cannot change those earlier names/bases/sizes or dump module entries.

The shared portable bookkeeping helper is exercised by
`test_exception_inventory.c`:1024 matched load/unload cycles, exact128 live,
129th live overflow, duplicate livebase, unknown/double unload, base reuse
with changed name/size, old snapshot preservation and subsequent snapshot
exclusion after unload. A shared finish-load callback also proves a valid
loader-file token is closed exactly once on both admission and rejection;
close failure rejects admission, and NULL/invalid tokens are not closed.
The package loader wrapper uses that same finish helper around the existing
module identity/read checks and real `close_owned`. CREATE_PROCESS's existing
thread-capacity rejection retains its separate once-only file close.
This is a native host test with mock file tokens, not a Windows CloseHandle
runtime assertion.

**VERIFIED source-only:** strict AMD64 PE32+ compilation of all three
diagnostic binaries completes without warnings. Prohibited mutation/
unverified-unwind import checks pass. The synthetic controller remains
exactly558195bb and fixture exactly7cb8ed07; their2250 behavioral evidence
is unchanged, not replayed. Native Clang
`-Wall -Wextra -Werror -std=c11 test_exception_inventory.c`
reports2193 checks, zero failures, three mock closes. All13 local
capture-accounting tests pass. No Windows executable, Wine/package/game
or consumed owner was executed. Mandatory
`python gates\gate_runner.py quality-gate` still returns2 (runner missing);
this is not an overall quality PASS.

Read-only original2260 attribution:

- The two genuine80000100 parameters are inside exact-pinned twinapi.
  Static image bytes at RVA1dd950 decode
  `ext-ms-win-mrmcorer-resmanager-l1-1-0.dll`; RVA21c3a2 decodes
  `GetStringValueForManifestField`. These are disk-image string decodes,
  not a captured-memory string read. Twinapi SHA remains
  `0d3e56c5068633f975bcab528267789f520f82a6d6a50f1330104e7597289b43`.
- All eight dump thread stack descriptors have RVA0; thread544 advertises
  stack start10f438/length3016 but no stored payload. MemoryList contains
  zero ranges. File offset0 is the MDMP header, not stack bytes.
  No raw stack, recovered return-address sequence or unwind is supported.
- Same-run stderr dispatches the fourth handler at6ffffd30faa8. The
  immutable dump maps it to `Cuphead.dll`, base6ffff7e00000/size5730000,
  RVA550faa8; the other three entries map to twinapi+ac8c0 and twice
  twinapi+197e68. `decoded-attempt1/Cuphead.dll` SHA is
  `7d6c5cbf6323451f0456527b667c346a0e16bea2d483fdad5725f4e41258c3fc`.
  Exact pinned image bytes there are `ff25c26952fc`: an indirect JMP to
  IAT RVA1a36470, whose public PE import is
  `mrt100_app.dll!RhpPInvokeExceptionGuard`. The exact-pinned native
  mrt100_app exports that name at8380. This establishes same-run dispatch
  to the guard import thunk, not a captured guard-entry RIP/runtime IAT
  target, handler return, guard internal reason or historical2210 address
  ownership across ASLR. No chain to synthesized1007 or Vector slot30/outer
  return is claimed.1007 remains unobserved before the inventory abort.

Failed2260 receipt, expanded proof, dump and original split captures are
rehashed unchanged. Package debug activation success remains distinct
from diagnostic failure and zero-window/no-first-frame observations.

Fresh unexecuted candidate:
`~/xodus-app-tooling/cuphead/exception-event-candidate-20261011-2270`.
Root files are `cuphead-exception2270-{owner,launch}.py` and
`capture_accounting2270.py`; unique label
`cuphead-owned-exception-package-20261011-2270` has no workdir/log/status.
Their ASTs and staged bytes match local pins. The owner additionally
pins the failed2260 receipt/restoration/dump/split captures before mutation.

|2270 source-only artifact|SHA256|
|---|---|
|Package controller.exe|`fcec3e2e61f0722a7849550e2e484abdcb38841def5a5abad0be7ecdc550a780`|
|Common controller.c|`1a00a485c04354afe2d566941c078969c850d66d86595b89864df8e4ecfa47b5`|
|Inventory helper.h|`3a8416ad94b9cdc3b863d9b42aa723115f186af52dd2dc268a9331c75a8d8695`|
|Inventory native test.c|`e7b6bf788e7b0bfdb181a05331e431a2d5af3e13bad06d4a3bac34b02cac8d90`|
|Compile script|`0999e63258283d1493e47ed17bdf6fadc77306583f2867a3cc54b97ce663fc6c`|
|Compile manifest|`9b93ba4c9b77d4bb070ccfb680ffc3ac11a0139b984e67835915c376249a25c8`|
|Owner|`d4d3771ef2fc7c64598f1201cae9d2f1d0f9e8f1e182b6b7d4be5f19ab8cc589`|
|Wrapper|`38eb6370e6b56057d996b672e235f148a312918035c23a35b4b80a174d0abd29`|
|Accounting helper|`28add3e42f44916613dddd83f7db87e8380767738c24da10c71bd36c463ed1a1`|

Prepared scope, **not granted**: canonical2195's exact33 triples, builtin
dbghelp and only the verified native Vector alias/temporary Detail.Vector
registration, standard pre-gates then one capture-only package process.
Unchanged filters/bounds:80000100 then later first/second chances,
1007 independently;8 selected events,2MB/dump,10MB/controller,60s;
outer75s/20MB aggregate including live/completed dumps and server delta.
Normal unrelated NOT_HANDLED disposition, exact owned cleanup and independent
full restoration/quiescence remain required. No295/frame/events/providers,
package/save mutation or synthetic replay. This source correction's actual
Wine inventory/unload integration is unexecuted; hard-cancelling a stalled
writer remains unexercised. No runtime hold is requested or assumed here.

### Actual2270: inventory advances; original stub and repeated breakpoints captured; bounds fail

One exclusive capture-only2270 cycle was granted on the exact source-only
pins above. Read-only preflight verified canonical33 baseline hashes/absences,
candidate manifest/owner/wrapper/helper pins, Detail.Vector absence,
dbghelp/alias hashes, strict signature and full recursive bottle/runtime
quiescence. One initial AppleScript shell-quoting attempt failed before any
owner artifact/process existed; argument-safe AppleScript then launched
the unique owner once via Terminal/caffeinate. No game replay occurred.

Standard pre-gates passed: CM retry/error35/0, Pnp backend async23/0,
cfgmgr original/augmented1292/1369 tests (one TODO each, zero failures),
enumeration1938/1987 tests (91 TODOs each, zero failures), wintypes1120
tests (49 TODOs, zero failures), nullable50/0, folders96/0,
accessibility33/0, cstub248 tests (two TODOs, zero failures), and the
existing activation/transport/dispatcher/proxy controls.

The explicit package query/create-debug/activation seam succeeds again:
activation status0, exact mask3/Flags1/Origin4, no AppContainer, own
launcher unpackaged and relabel denied. First wait is TIMEOUT at12944ms,
zero total/visible windows. Actual loaded providers are builtin dbghelp,
builtin Wine wintypes and the native alias, with admitted identity checks.
There are162 cumulative module loads, no duplicate/unknown-unload/overflow
inventory error, and immutable selected snapshots contain59 then66 live
modules. This is runtime progress beyond2260's129th cumulative-load abort,
not a test of all128-live/invalid-event cases.

**FAILED owner:** polling observes20,493,144 aggregate bytes, including
20,180,655 server-growth bytes, and records
`Aggregate debug capture bound exceeded: exception-event-package`.
Owned cleanup preserves continued output, eight completed dumps and final
post-cleanup aggregate161,230,032 bytes; no truncation or success relabel.
The first observed owner bound is distinct from the controller's subsequent
eight-selected-event exhaustion: its ninth event fails reason6, exit result
not collected by the already-failed owner. Server final281,988,499 bytes
also includes pre-gates/restoration, not just the diagnostic delta.
This exposes substantial cleanup-output growth; hard outer bound enforcement
or immediate stalled-writer cancellation is not established.

All eight genuine exception dumps validate record/thread/context/fault-module
streams, with independent control/integer register correlation:

|Dump index|Thread/chance|Code|Fault/RIP|RSP|Bytes|
|---|---|---|---|---|---|
|0|540/first|80000100|kernelbase+da77 / same|10f440|190596|
|1|540/first|80000003|SharedLibrary+41cecf / +41ced0|10e260|198100|
|2|540/first|80000003|same / same|10dc70|198406|
|3|540/first|80000003|same / same|10d680|198406|
|4|540/first|80000003|same / same|10d090|198406|
|5|540/first|80000003|same / same|10caa0|198406|
|6|540/first|80000003|same / same|10c4b0|198406|
|7|540/first|80000003|same / same|10bec0|198406|

The original flags1 parameters remain twinapi+1dd950/+21c3a2; stderr
dispatches its three ContinueSearch handlers and fourth Cuphead+550faa8
guard import thunk. Later exceptions have flags0/parameter0, address
6ffff7a6cecf in SharedLibrary base6ffff7650000. Pinned SharedLibrary
SHA `5f0fd3ae5d7840015263ae84fc05b72528d38c346c04f8e26186ebf4c0708161`
has an INT3 at41cecf. Actual stopped RIP is the following41ced0, while
application dispatch reports41cecf; each logged later handler at
mrt100_app+5a70 returns0. This establishes repeated first-chance breakpoint
observations and decreasing stopped RSP, not an unwind, runtime guard-entry
proof or a confirmed1007 causal chain. The original and seven dumped
breakpoints receive normal NOT_HANDLED; the ninth pending event also uses
NOT_HANDLED in cleanup. No1007/second chance/natural app exit is captured
before abort; these are bounded telemetry gaps, not absence conclusions.

Controller cleanup reports terminate/continue/detach/quiet all1,
190 handles closed/zero close failures; final failed1/reason6/startup1/
first9/second0/dumps8/exited0/quiet1. Original app exit is UNOBSERVED.
No first frame or diagnostic-success claim follows.

Independent verification confirms canonical33 triples/baselines,
all temporary registrations/fixture absence, original CoreApplication/
WineDbg/Debug settings, dbghelp/alias/package image unchanged, strict
signature, no runtime/owner/wrapper/caffeinate and no recursive bottle
holders. Prior2240/2250/2260 artifact pins remain unchanged. The2270
grant is consumed and exclusive hold released; no second launch is granted.

|2270 preserved evidence|SHA256|
|---|---|
|FAILED receipt|`32d45bbcdfbb736e177b830db6d387b723226ddfcc94ab8fda6c8e4a6e42764f`|
|Independent baseline proof|`a955926c5cf23e08a5c0f93bb8bc165b32f6116304d98fc501527f20b3e56b53`|
|Expanded independent proof|`a46aeb82e738979828750ccfcfe6b0f3ed3c8962ad944488da01044aed6d7b38`|
|Original80000100 dump|`6cbfdd833b9dd31f187b96dbb1d1369897ca0f09fc989b0367f63d1c0af8321b`|
|First later breakpoint dump|`b9603d45b1cd5b176968b7bbaf2f8d48e14dd39adcdc813cdc84d41bd249b7c1`|
|stdout|`5f803cda8901c643eda143e4bd4d5809882f18234bcdf417b1965c6851fadc1f`|
|stderr|`aaf314373a225924224967ea080f023a5e9c2f61a6809732446de51a5181f75d`|

Private evidence root:
`~/xodus-app-tooling/cuphead/cuphead-owned-exception-package-20261011-2270`.
Cheapest next discriminator is read-only exact SharedLibrary INT3 caller/
mrt100_app+5a70 continuation and admitted Wine breakpoint-context behavior,
plus source-only outer-cleanup output accounting/control attribution.
No swallowing breakpoint/context mutation, guessed API or widened bounds
is justified by this failed capture. Missing quality runner remains.

### Read-only2270 discriminator: debugger-dependent FailFast recursion and delayed bound cleanup

Further debug-game launches are CUT pending this discriminator. No runtime,
context/code modification, swallowed exception or source implementation was
performed. The exact2270 receipt and captured artifact pins are unchanged.

**VERIFIED pinned static control-flow:** SharedLibrary's enclosing native
function spans41cd40-41cfc2. It calls
`api-ms-win-core-debug-l1-1-1!IsDebuggerPresent` at41ce9c, tests EAX at41cecb,
branches to41ced0 if zero at41cecd and otherwise executes INT3 at41cecf.
The admitted kernelbase SHA9308d50f exports IsDebuggerPresent at3b810;
its actual bytes read TEB->PEB->BeingDebugged (PEB+2), matching
`dlls/kernelbase/debug.c:165-170`. This is not a dormant guessed diagnostic.
The attached DEBUG_ONLY_THIS_PROCESS controller changes an explicit branch
in the executing failure path. The actual breakpoint record and dump RIP
prove that branch reached its INT3.

The intended straight-line continuation after debugger handling is41ced0:
initialize EBX=80004005, optionally take an exception HRESULT, or at41cefe
set EBX=ESI+1000. The helper builds its terminal exception record at41cf2c,
sets flags1 and calls RaiseFailFastException at41cf82 (return site41cf88).
Thus the debugger trap precedes generated1007; more breakpoint dumps are
not independent proof of more originating faults.

**VERIFIED static handler identity:** mrt100_app+5a70 is
`jmp [RIP+3f44a]` through local pointer RVA44ec0, whose pinned disk value is
preferred-base+8380 with a DIR64 relocation. It is a local guard thunk,
not another unknown import.8380 is RhpPInvokeExceptionGuard. Its call7f50
is followed by return--1 test83af/83b2; the--1 branch returns EAX0 at840c,
otherwise it takes runtime policy branches and calls32c0 or32e0 at83ea/
8402 before returning0. Both32c0/32e0 set reason7 (ECX) and invoke12a0;
12a0 obtains/calls an indirect failure callback at12bd/12e5. No
unconditional INT3-specific RIP increment/normal41ced0 continuation exists
in this handler. Runtime IAT/local pointer contents and callback target
were not read; disk relocation is not a stopped guard-entry RIP.

**Observed versus hypothesis:** preserved stderr reports later handler
mrt+5a70 returning0; eight stopped records/context snapshots show first
80000100 then seven debugger-dependent INT3 events. After the first
breakpoint, each RSP falls by5f0, with unchanged record address and
stopped RIP41ced0. Application dispatch instead reports41cecf, consistent
with admitted `dlls/ntdll/unix/signal_x86_64.c:1750-1753` decrementing
EXCEPTION_BREAKPOINT RIP before the application dispatcher. The controller
never edits that context and passes NOT_HANDLED. The established frozen
ntdll.so decrement remains the admitted code, not a new build.

**HYPOTHESIS, strongly grounded but not unwound:** unhandled debugger INT3
enters the native PInvoke failure policy again, calling the managed FailFast
helper on a deeper stack and hitting its debugger branch again. The5f0
per-event descent plus guard thunk/control-flow supports nested FailFast
re-entry, rather than eight independent game faults or a simple same-stack
INT3 reexecution. No stack payload/unwind/callback-return trace proves each
edge. This debugger-induced terminal-path recursion is distinct from the
original missing-export80000100 dispatch to Cuphead's guard import thunk;
it does not establish that original failure is benign or fixable by
swallowing it. Further attached-game capture is not the cheapest probe.

**VERIFIED source-only owner audit:** at the20MB observation,
`cuphead-exception2270-owner.py:334-337` raises without first terminating
the active command; its command ExitStack closes Python's file handles,
not child-inherited writer handles. The finally block at1019 starts
11 temporary-key query/delete/query restorations, WineDbg restoration and
CoreApplication restoration before `cleanup_runtime()` at1050. These
ordinary `command()` calls have30s individual deadlines and100ms polling,
but no exception-package aggregate polling once that call has raised.
The actual package process/debugger can keep running during that sequence.

Afterward cleanup_runtime at562-600 first runs a Wine tasklist census,
reads/parses the full server trace/generations, validates native ownership,
then signals admitted clients/explicit launchers, waits up to8s at200ms,
escalates exact survivors and stops the owned server. Registry operations
therefore precede both target control and the ownership refresh. The
100ms active-command polling cadence is not a hard outer bound across
these blocking restoration/census/trace-read phases.

Exact preserved server byte offsets identify the continued writers:
server.log is the owned wineserver's output. Post-bound request prefix0220
is controller native PID69363, Wine pid0234 (verified same-generation
init_first_thread and package census), not the historical0220 pre-gate
generation. Target Cuphead is native PID69377/Wine022c. At the first
post-bound process census, both remain live.0220 contributes89,886,362
post-bound server bytes, notably remote dump module enumeration;
read_process_memory method records contribute71,076,353 bytes overall.
The restoration commands add114 first-thread roots (wrapper/helper chains),
and enum_key_value traces contribute28,970,880 bytes. Controller target
terminate_process(handle0074,exit95) is only at139,267,061 bytes after
the observed bound; detach follows and target/controller terminate.
The owner's cleanup census begins at post-bound129,982,576, after the
bulk of restoration. Total post-bound server growth is139,304,710.
These are file-offset order facts, not wall-clock cancellation latency.

The receipt's `server_growth_after_owned_cleanup`159,485,365 is measured
from the package's initial offset all the way through registry restoration
and owned cleanup, not exclusively game/dump output. Combined final
161,230,032 correctly remains over-limit FAILED, but attribution must
separate continued controller/dump requests from restoration client noise.

**ONE grounded next correction, source-only proposal:** on package
bound/timeout/failure, run a control-first owned abort before any Wine
registry restoration. Freeze the already-admitted generation/native-identity
mapping, reconcile only through a bounded incremental ownership mechanism,
stop the exact owned target/debugger/launcher set under identity checks,
and require quiescence before running registry restoration in a separate
bounded phase. Keep original captures untruncated and mark overshoot FAILED;
preserve the existing safe requirement never to restore provider files while
runtime is live. Do not merely reorder current unbounded full-trace/census
work and call it hard cancellation. A future fresh helper needs local
synthetic live writer/blocked census/identity mismatch/cleanup-order tests,
including post-bound output attribution, before any runtime proposal.
No implementation/next owner/game hold is authorized by this analysis.
The failure path can already be discriminated statically without debugger
attachment; hiding BeingDebugged or handling its trap would change semantics.

### Control-first safety correction and finite feasibility decision

**SOURCE ONLY:** `owned_capture_control.py` supplies incremental generation
admission using the already-established Wine request/reply parent/handle
records, rather than an emergency full-trace read or tasklist census.
Each drain has a2MB backlog budget,64KB line bound and caller deadline;
incomplete/truncated/malformed/unresolved lineage fails closed.
The abort freezes generation/native-PID/start-identity records, validates
the whole admitted live set before signaling, then sends exact-identity
TERM deepest-first, gives0.5s grace and uses exact-identity KILL if needed.
It admits newly observed descendants during control and requires mapped
package-process quiescence before permitting registry restoration.
Unknown identity/reused generation/backlog/deadline failures refuse further
control and block restoration; unrelated roots are never signaled.
No original capture bytes are removed/truncated or relabeled success.

The preserved session artifact `cuphead-control-first-owner.py` wires this
helper into per-command incremental observation and puts abort before the
registry-restoration finally block. A capture launched without identity
admission also blocks restoration. Identity adapter uses one ps probe with
at most0.5s timeout/remaining deadline and rechecks immediately before
signaling. Abort failure records uncertainty instead of falling through
to the old blocking cleanup/census. Provider-file restoration still needs
the existing later whole-bottle quiescence check. This owner has a distinct
source-only label and its main deliberately refuses execution: attached
game capture is CUT, no fresh launchable wrapper/game candidate exists.
Its full Wine integration is unexecuted; AST/order inspection is not a
runtime gate.

15 focused safety tests pass on both Windows and native Mac Python:
real live writer stops before restoration with original overrun preserved;
a real blocked-census subprocess is not waited on; native PID/Wine generation
reuse and identity changes at admission/immediately before signal refuse
signaling; parent/child control precedes restoration; a child born during
grace is reconciled; unrelated roots survive; incomplete/backlogged/truncated
trace and deadline/unresponsive-target cases block restoration. Synthetic
processes are explicitly reaped. Together with13 accounting tests,28
focused Windows tests pass. Native Mac tests are not Wine/game execution.
Required quality runner remains absent/exit2.

**Capability limit:**4s is the cooperative emergency-control deadline,
not an independently proven hard writer-cancellation bound. Per-probe
timeouts and incremental byte caps avoid the known unbounded census/full
trace work, but Python/OS scheduling, filesystem I/O and signal observation
cannot guarantee a strict byte ceiling. A process that cannot be proven/
quiesced leaves an uncertainty receipt and prohibits restoration; there is
no success-shaped fallback. Whole-bottle quiescence remains independent
verification, not a claim inferred from the helper's package set.

|Source-only safety artifact|SHA256|
|---|---|
|Helper|`de3cafd37193d4822112b437a8ca106e390e6a05cad8b3f37b8ca651c481be42`|
|15-test source|`300edc1b4801dce6a2c91246d95b7487ae9a055b5b8ac84f1e2dc467793ceb33`|
|Nonexecutable owner artifact|`7531fa5ffba69ea30bffa67ae001b97586c6f5c8bebe1ca5a8179d461c77a4a3`|

The owner artifact also remains outside Git under
`~/xodus-app-tooling/cuphead/control-first-source-only`; staged pins match.
Consumed2270 receipt/dumps/splits remain byte-identical. No runtime grant
was used and no new binary/provider instrumentation was added.

The same source-owner `command(..., "ordinary-package-activation",
timeout=75, separate_stderr=True)` seam now admits an existing ordinary
activation launcher as the control root and shares the existing completed/
live split+server aggregate accounting and control-first restoration gate.
It does not select/attach a debugger or introduce a capture adapter; normal
activation arguments and exact existing controller identity belong to the
next scoped product-candidate admission. AST checks confirm both running
and completed-output accounting routes include this ordinary command.
The owner stays nonexecutable, no wrapper/launch grant is staged, and this
ordinary Wine integration remains unexecuted.

Fresh ordinary2280 source owners are now prepared at the tooling root:
`cuphead-ordinary2280-{owner,launch}.py`, `capture_accounting2280.py` and
the tested generic helper. Both entrypoints explicitly refuse execution
until the actual MRM product candidate and launch admission are supplied;
the2280 workdir/status/log are absent. The source wrapper pins its owner
and refuses replay. This is preparation, not a ready/granted launch or a
new diagnostic framework.

The existing `package-activation-launch.exe` is frozen unchanged at
`ordinary-activation-candidate-20261011-2280`. Its actual PE has no debugger/
dump/mutation imports; actual CreateProcessW call RVA166d sets flags4
(CREATE_SUSPENDED) at165d, not DEBUG_ONLY_THIS_PROCESS. Source
`package-activation-launch.c` SHA4260b165e9bc275119ce7e44b531739ffe8b853ee7b4bd794b5d4a3046d8a8c3
preserves explicit catalog activation, mask3/Flags1/Origin4 checks,
no-AppContainer/relabel checks, first-wait/window reporting and terminal
wait/exit. It was not rebuilt or executed. Source owner passes its already
established package/App/ntdll/run/server arguments, selects only ordinary
seh/loaddll trace and shares75s/20MB split+server bounds. It preserves
nonzero original app exit as failure rather than controller success.
No debugger/filter/dump requirements survive in this ordinary adapter.

|Fresh ordinary2280 artifact|SHA256|
|---|---|
|Existing ordinary controller.exe|`ff65f4261896777125d02e3712da9f15fe38f2c50662ae959f0d85323d74605d`|
|Source owner|`afd86df694c09387c5fb7913e3ff775624f6a953bf11455d3db79996709755ee`|
|Disabled source wrapper|`54e6e9e06b4200984bf39f18b6fd4592251af96a84da66f6d78a5d593de9523d`|
|Fresh accounting helper|`28add3e42f44916613dddd83f7db87e8380767738c24da10c71bd36c463ed1a1`|

AST checks/staged hashes and28 focused local tests pass. Runtime-only
unverified boundaries remain actual Wine generation-stream admission,
exact-identity control under live Wine load, ordinary activation with the
forthcoming MRM candidate and independent full restoration. Candidate
provider targets/build pins and relevant pre-gates must be incorporated
from that implementation handoff before either entrypoint is enabled.
The Manifest resource API child now exclusively owns MRM contract/
implementation discovery; exact native artifact paths were sent directly,
and this runtime owner does not duplicate that lane or edit its scratch tree.

#### Integrated MRM literal slice: source/full-link only2280

The coordinator approved integration of the Manifest resource child's patch
`83a18d3036a3c6085b01d688c13f8726cadebc4afe72a9daf329d2e87a8b6aa1`.
Its include and contract tests were read first; exact-hash dry-run and
`patch -p1` succeeded in the owned Wine source tree. The integrated include
is byte-identical to the reviewed handoff. No child scratch source was edited.
The patch is preserved as `wine-manifest-resource.patch`, alongside the new
`manifest-resource-integrated-test.c`.

The three exports use WINAPI/LONG and stdcall specs: context queries take
`UINT32, reserved pointer, output pointer`; property takes an opaque pointer,
`UINT32 property, UINT32 *byteCount, buffer, optional UINT32 *flags`.
Only pointers equal to this process's published manifest cache are accepted.
The existing interlocked cache publishes once and frees only losing
candidates; no arbitrary native context layout or release API is invented.
Native kernelbase ignores reserved and preserves context outputs on failure.

The child's genuine native disassembly establishes an eight-byte header of
four WORDs: literal bit `0x100`, total bytes, zero URI bytes, UTF-16 value bytes
including NUL. MrmCoreR+346cb passes a UINT32 byte count, +3498f checks at least
eight bytes, +34999 checks total length, and +34ea0/+3aa69 consumes literal
UTF-16 bytes and divides by two. Its SIZE_T output is required even for
queries. Native kernelbase+105b20 updates byte count/optional flags for short
and successful buffers without copying on short capacity; absent/invalid
properties preserve outputs. These are static ABI/consumer observations,
not a packaged runtime oracle. The child retains the disassembly at
`parallel/failfast/gsv_mrm.asm`.

**Source boundary:** index0 and parsed literal manifest properties only.
Nonzero indices, PRI `ms-resource:` values, file-valued slots, oversized
16-bit payloads and foreign/native context handles are unsupported, with
explicit errors. Missing supported IDs return1168, invalid IDs87. Genuine
native MrmCoreR remains the consumer/provider; no new shim for its export,
PRI fallback, package enumeration or fabricated success was added.

Full `make -j4` links of kernelbase and apisetschema succeeded. The actual PE
adds exactly the three requested exports, removes none and changes no
forwarders relative to the canonical kernelbase candidate. Export RVAs are
application-context26f10, package-context28760 and property30c20. Decoding the
linked v6 schema verifies the exact mapping
`ext-ms-win-mrmcorer-resmanager-l1-1-0 -> mrmcorer.dll`; admitted ntdll's Unix
loader opens the vendor schema, so no ntdll rebuild or bottle schema swap is
proposed. Strict integrated-probe compilation succeeds. The child's mock
contract checks pass under native ASan/UBSan;28 focused safety/accounting tests
also pass. None is Wine integration or game evidence.

Fresh staged files are under
`~/xodus-app-tooling/cuphead/mrm-candidate-20261011-2280`.

|Compiled MRM artifact|SHA256|
|---|---|
|kernelbase.dll|`1fbe8bd25c77b60332cff5a6f4f2ce969b3514edb3e4848997169b5abe6295e4`|
|apisetschema.dll|`f91bed73d425d26761d91a70d291b966b3fb03d4e6217871045fbdee9a3234e9`|
|Integrated test.exe|`2ec01c9638d7800537a92d86e163f18f37d740ac5d152312175d25ff01bea232`|
|Integrated test.c|`be0014c8e9d57c57b8d896a3f203c26837abc28ae27c04c9545a9016759515cd`|
|Unchanged native MrmCoreR.dll, private fixture copy|`6186f981d1aa1e1991697133aa066045097ab52227a240632a0c7d8fd04352be`|
|Full-link log|`ef6ea09c3d3d03d3e245ec0b4677eabe950d42cdb1f779e05f1e11aaf06925bb`|

The proposed target delta is exact: preserve the canonical2195 assertion of
all33 triples before applying the delta; replace only its existing vendor
kernelbase candidate9308d50f... with1fbe8bd2..., retaining baseline
`ca72a76ce5770996cc9cba6989031a79eb1e63b6d9fca90d59eebab22e39191b`;
add one vendor `lib/wine/x86_64-windows/apisetschema.dll` target with candidate
f91bed73... and baseline
`9d20ab9c93439309b0325467e45b80b70e00ad5f484d8dd522c05e8a94b4a0fb`.
That is34 unique mutable targets, not33 unchanged candidates. The bottle
schema stays untouched at the same9d20ab9c... hash. Native MRM is frozen
beside the test outside the bottle/vendor/Git; the game's original
decoded-attempt1 copy is verified unchanged. Native `mrmcorer=n` is the
only added consumer selection.

The integrated no-game test loads the API-set name rather than bypassing
the mapping, prints provider path and the three actual export RVAs, and
expects real unpackaged80073d54/count0/cleared-buffer behavior. Its default
mode has six checks, including all three exports' invalid/unpackaged paths.
Optional `--packaged-literal` tests byte-sized property
query plus native NULL-buffer/short/exact-size string calls against an
explicit expected fixture literal; it assigns no identity or registration.
An existing signed graph fixture is present but no already-bound execution
of this new probe is established without changing its content/activation
seam. Consequently the admission proposes the real unpackaged negative,
not an invented packaged-positive result. Positive mock tests remain labeled
fixtures; Cuphead's literal DisplayName alone is not packaged call evidence.

The fresh source owner verifies canonical33 first, adds the explicit delta,
freezes native provider/test, and runs the integrated no-game call after
required standard pre-gates. It enforces18s/2MB including live server growth,
uses control-first abort even for that command, and admits an ordinary game
only after exact loader/output/quiet evidence. Ordinary75s/20MB accounting,
explicit activation/identity/window/exit checks, no debugger or exception
disposition/context/instruction edits, and restoration requirements are
unchanged. Overshoot, stalled
control or uncertain identity still fails; no hard writer-cancellation claim.

|Updated source-only owner artifact|SHA256|
|---|---|
|Disabled owner, staged|`cb9379c6f2b089f4f8cf414af9f15409bb9b95ed6ad34a389547706f332d15d8`|
|Disabled wrapper, staged|`795fc46cd76343e93fd8e40e4ff1a7311a52a70e71695206a236e485b819625d`|
|Proposed executable owner, CRLF admission conversion|`fa0a8eecd0b0a1b057bc5756397bb98c19792adf2627d965a71ea222b2ca9d2e`|
|Proposed executable wrapper, matching owner pin/removal of refusal|`7d7e691c91b18043238180e39703ad1463966b67e3edee90b9d9ff0fe74d69d0`|

Executable hashes are computed, not enabled/executed: owner refusal would
become `Owner().execute()`, wrapper refusal removed and its owner pin updated,
with existing CRLF bytes preserved. Accounting/control/controller pins remain
those above. AST, staged pins, exact34 scope and absent2280 workdir/log/status
are verified. The next decision is one exclusive no-game plus conditional
ordinary2280 cycle under these exact pins, stopping at first mismatch, with
independent restoration of34/all temporary keys/settings/fixture/provider
identities/signature/full recursive-holder/runtime quiescence. No unchanged
synthetic debugger replay is proposed. No runtime or game is granted or run
here; actual loaded native dependencies, Wine ownership/control, packaged
MRM success and first frame remain untested. Mandatory
`python gates\gate_runner.py quality-gate` again exits2 because the runner
is absent; this remains an integration limitation, not overall PASS.

#### Actual2280: stopped before game; exact-owned recovery verified

The coordinator granted one34-target cycle and the exact enabling hashes
fa0a8eec.../7d7e691c... were verified before the unique Terminal/caffeinate
launch. Canonical33 was checked before the MRM delta. Required standard
pre-gates completed: Pnp devnode35/backend22, cfgmgr1292/1369,
enumeration1938/1987, wintypes1120, nullable50, folders96,
accessibility33, cstub248 and the existing activation/proxy gates all reported
zero failures (existing TODO counts remain). These are no-game results.

The first controlled command failed with
`OwnershipError('Launcher identity changed')`; emergency admission also
failed `Native PID generation/identity changed`. No uncertain signal was
issued. The owner correctly blocked registry/file restoration rather than
calling it success. Its original receipt retains `complete_restoration=false`.
The probe continued briefly and ultimately printed
`FAIL exports_or_mapping error=126`, with no native MRM result or six-check
completion. No ordinary package/game command or temporary Vector/server/
entrypoint registration was reached. This consumed2280; no replay occurred.

The receipt's early snapshot had zero stdout bytes and incomplete stderr.
Original artifacts were not truncated or updated into PASS: a separate
`post-recovery-artifacts.json` records final stdout35 bytes, stderr4272 bytes
and server110,018,401 bytes. The server initial boundary for this command was
108,886,715, so final command split+server aggregate was1,135,993 bytes, below
2MB. The full server includes earlier pre-gates, not just the controlled
command. The failure was identity admission/loader126, not an aggregate
overrun. Actual consumer behavior/output contract was not exercised.

The hold remained through recovery. Persisted `server-identity.json` proved
server88004/start19:48:16/exact wineserver argv. Receipt clients88020 and
88026 retained exact original start times/argv and matched fresh identities;
all live identities were checked before signaling. Recovery sent TERM then
KILL only to these admitted clients, then TERM to the exact server, and
verified no runtime/recursive bottle holder before restoration. Scoped
offline registry edits restored original CoreApplication DllPath and empty
WineDbg and removed the proven temporary Pnp/dispatcher/proxy registrations.
All34 hash-checked file backups were restored; temporary files removed.
No Wine/probe/game was launched for recovery. Original failed receipt and
captures remain unchanged; supplemental recovery is not a successful cycle.

Independent verification passed all34 baselines and the canonical33 subset,
14 registry/fixture absence checks, original settings, unchanged native MRM/
Vector alias/dbghelp/package/bottle schema, strict signature and full runtime/
owner/wrapper/caffeinate/recursive-holder quiescence. The coordinator was
informed that the hold could release. Failed2270 remains unchanged.

|Actual2280 retained evidence|SHA256|
|---|---|
|Original failed receipt|`bd2cf44bc65794cd13af84feee504ddc3085d8490a84e0f514d44b470ebe1011`|
|Supplemental exact-owned recovery|`ee11ce39f731b28d9073964e25b630f345029dbe8bdc0782e1b074cedeffae83`|
|Independent recovery verification|`ec212bdc737ae81fb8d4ee7fed39a34f450c2fa0f0c4d6888daee0dbe5dd70e5`|
|Completed probe stdout|`805ab3955d8932e9eef27cc118ebce14ad4d79acbed380cc5b4e5b62ecb21705`|
|Completed probe stderr|`0f90feb6754021010f1395d1002d9c1938501bcdccb54d597ab38dae4271e099`|
|Preserved server log|`1db8fb0d7a048b0d64f2a12b8d7e35a5bf6ff8cb75edcbbf7f57898e129ee960`|

#### Source-only2290: two grounded corrections, no retry

The bounded identity adapter compared the entire `ps` line, including
mutable R/S state and argv. CrossOver's Perl wrapper explicitly performs
`exec $cmd, @wine_args, @args` at line1245: legitimate same-PID exec changes
argv. Same-run2280 server evidence admits original native launcher88743 at
`init_first_thread` line851628 and shows its winewrapper generation creating
the new probe. No foreign-PID observation exists. The differing bounded
`ps` strings were not retained, so whether state, exec argv or both caused
this exact comparison cannot be asserted. The source model is nevertheless
wrong for a launcher whose documented path execs in place.

The narrow correction uses the genuine SDK's `proc_pidinfo(PROC_PIDTBSDINFO)`
and `proc_bsdinfo` native PID plus kernel start seconds/microseconds, not
mutable state/argv as generation identity. SZOMB is absent; malformed/short/
permission errors are explicit failures. The single bounded subprocess
adapter checks its deadline and format; existing Wine generation/parent
admission and signal-boundary identity recheck stay intact. No PID-only
ownership, broad process-name signaling or exception/product mutation is
introduced. `owned-process-generation.c` compiles strictly against genuine
Darwin headers. Two native synthetic tests pass: stopped owned Perl process
execs `/bin/sleep` at the same PID while birth identity stays unchanged;
distinct children/exited child remain distinct/absent. Existing15 control
tests retain PID-generation/identity-mismatch and fail-closed expectations.
These tests prove native process identity behavior, not Wine cleanup success.

The126 source discriminator found a separate placement bug: admitted
ntdll `find_apiset_dll`3698-3720 constructs `system_dir + mrmcorer.dll`;
`find_dll_file`3910 onward opens the resulting absolute path.2280 had only
an app-local native MRM copy and no system32 provider. Thus its probe's
bare API-set request did not search the app-local copy. The Manifest child
independently confirmed this and distinguishes twinapi's relative delay
import path from this generic LoadLibrary path. No MrmCoreR loader line was
observed; static unresolved imports are possible later blockers, not an
observed additional126 cause or justification for success shims.

The coordinator explicitly selected one additional native target for the
product and probe paths: bottle `drive_c/windows/system32/mrmcorer.dll`,
baseline absent, candidate unchanged6186f981..., selection `mrmcorer=n`,
restore by removal. Fresh disabled2290 source now asserts the original33,
applies the prior kernelbase/schema delta, then this single native target:
exactly35 unique mutable targets. Bottle schema stays unchanged. No
staterepository-cache/mrmdeploy/languageoverlay mappings or guessed exports
were added. The probe now diagnoses each kernelbase export separately,
prints its exact API-set request, reports provider-load failure immediately
and separately reports the native consumer export failure. Its actual
success must load the pinned system32 native provider; an app-local load is
not claimed as mapping success.

The corrected probe strictly compiles, without execution; consumed2280
binary/artifacts stay frozen. Fresh files are under
`mrm-candidate-20261011-2290`, owner/wrapper at tooling root. The native
generation helper stays under `control-first-source-only`. Bounds/checks
remain standard pre-gates then18s/2MB real unpackaged six-check admission,
conditional one ordinary75s/20MB launch, stop-first-mismatch and control-first
cleanup before registry restore. No packaged-positive fixture claim, debug
capture or new diagnostic framework.35-target restoration/provider/alias/
settings/signature/full holder quiescence remains required.

|Source-only2290 correction|SHA256|
|---|---|
|Native generation helper executable|`fc78d00d0492e7559f971a8da6eeb05898cd0c5082feae67624392eaf7cd0bef`|
|Native generation helper source|`f832aa73eed97d11f03636e2f871f0202dca04b7dd44b72a0dda47bf0407f726`|
|Two native regression tests|`e3c8142f6821a1587dc58fb552ee5dbb5fba6f7ccd263951fc45a067a1fc359b`|
|Corrected integrated probe.exe|`9a3f7ee8932d9e2f9010fbec0294dedacbb0f610b4d436f01635d59b98328e75`|
|Corrected integrated probe.c|`5101b5b5a5ffa613c5b7dcbff24542b13590a665af9945460ab4fd8f2f8a9fc7`|
|Disabled2290 owner|`adb51450f87103457731d0ff370fb7eb77b31262d9eb86064d3bd0bcce66c43b`|
|Disabled2290 wrapper|`8ab261f508b000f853caf2972fc717494459c5c7129f69dc98b888ae65f41cd6`|
|Proposed executable2290 owner|`994a257beb8bbde78be6151d8bba84cc670a1ad60ec8e8bb965d77ddb70d985b`|
|Proposed executable2290 wrapper|`c263cb8cb031a9adb1d97276b2e1172dfbab668488ca4a4e2a2dc92a173c2a21`|

AST/staged pins, absent2290 workdir/log/status and absent35th baseline are
verified. Executable hashes are computed for the same explicit refusal/
owner-pin conversion only; not enabled or run. Product kernelbase/schema,
ordinary controller/accounting/control helper/alias pins are unchanged.
No runtime retry granted here. Quality-runner absence remains disclosed.

#### Actual2290: native negative passed, ordinary app still exits4103

The coordinator granted one2290 cycle. Canonical33 was asserted first,
then exactly35 candidates were admitted: kernelbase1fbe8bd2..., vendor
schemaf91bed73..., and bottle system32 `mrmcorer.dll` baseline absent to
genuine native6186f981..., with only `mrmcorer=n` selection. The exact
authorized owner/wrapper enabling hashes were verified before the unique
Terminal/caffeinate run. All prior required no-game pre-gates completed with
zero failures (same fixture TODO counts).

The integrated no-game negative **passed all six checks**. It reported actual
kernelbase RVAs package28760/application26f10/property30c20; requested the
MRM API-set; and `GetModuleFileNameW` returned
`C:\windows\system32\mrmcorer.dll`. The provider's genuine
`GetStringValueForManifestField` export was found. Unpackaged context status
was15700; native consumer returned HRESULT80073d54, count0, first WCHAR0000.
This proves mapping, placement, exports, and the expected unpackaged failure
contract; it is not a packaged-positive call.

Exactly one conditional ordinary Cuphead launch then ran, no debugger,
dumps, 295/frame/events, or other provider delta. Catalog activation and
claims passed (13 checks, zero failures); activated original app had mask3,
Flags1, Origin4. First wait succeeded at1285ms. It exited4103; total and
visible windows were both0; no frame was observed. The controller preserved
the nonzero application exit as failure. Ordinary stdout:

```text
ACTIVATION pass=0 status=00000000 pid=584
CLAIMS activated-original-app status=00000000 mask=3 flags=1 origin=4 package_bytes=104 app_bytes=8
ORIGINAL_APP_FIRST_WAIT=0 ELAPSED_MS=1285
ORIGINAL_APP_WINDOWS total=0 visible=0
ORIGINAL_APP_EXIT=4103
RESULT activation_checks=13 failures=0
```

Ordinary trace shows native system32 `mrmcorer.dll` loaded at
`00006FFFF38E0000`, then Cuphead's already observed
`EXCEPTION_WINE_STUB`80000100: `ntdll.dll.RtlIsMultiSessionSku` at
`ntdll+0x6fcca` (trace reports aborting). The app later loads its original
private `decoded-attempt1\MrmCoreR.dll` separately. The integrated no-game
probe did exercise `GetStringValueForManifestField`; ordinary Cuphead trace
proves provider loading, not that a packaged property call completed.

The three access violations at `kernelbase+0x82eb9` are secondary faults in
Wine's own `failfast_diagnostic` pointer walk, not product exceptions. At that
instruction `failfast_read_words` treats scalar register values7,1007,1 as
pointers and attempts a 16-byte read. Each diagnostic AV unwinds through
`kernelbase+0xf7ea` (`__wine_unwind_trampoline`); `+0x97ea` is not the unwind
target. The diagnostic is called only by the temporary
`RtlCaptureContext`/`failfast_diagnostic` block in
`dlls/kernelbase/debug.c:592-596`. Removing that call preserves the normal
record/context/flags and `NtRaiseException` fail-fast behavior while removing
the speculative reads.

The only observed missing-export event before the original fail-fast in2290 is
`ntdll.dll.RtlIsMultiSessionSku`, reported as `EXCEPTION_WINE_STUB`
`80000100`. It is followed by the diagnostic-generated AVs and then the
original unhandled exception1007 at `0x6ffff7afcf88`. The trace does not prove
that the stub caused exception1007. The corresponding pre-fail-fast tails in
2200 and2210 have the same CoreApplication/CoreWindow/
AccessibilitySettings semi-stubs, WNF/ETW stubs and fixed-scale behavior, but
no `RtlIsMultiSessionSku` WINE_STUB or diagnostic pointer-walk faults. Their
original unhandled exception is also1007. Thus the SKU export is a genuine new
missing API, but its causal relationship to the unchanged fail-fast remains
unproven.

The genuine Windows `RtlIsMultiSessionSku` at
`C:\Windows\System32\ntdll.dll` returns BOOLEAN1 on this host. Its reported
`RtlGetVersion` product type is `VER_NT_WORKSTATION`, with registry
`EditionID=Enterprise` and `ProductSuite=Terminal Server`; this is not a
confirmed non-multisession oracle. Per the requested Wine desktop-SKU
behavior, the source candidate exports the zero-argument BOOLEAN API and
returns FALSE; its focused test checks the FALSE result only when running
under Wine, while accepting either native BOOLEAN value on Windows. This does
not establish behavior for a native non-multisession Windows installation.

Both integrated and ordinary commands remained below their respective
18s/2MB and75s/20MB limits: aggregate totals1,101,632 and4,315,438 bytes.
Control-first reached generation-proven owned-package quiescence before
registry restoration. Owner receipt records exit4103 as failure, no
restoration errors and all35 file restores. Independent verification confirms
canonical33 plus exact35 baseline hashes/absences, removed system32 MRM and
11 temporary registration keys, original CoreApplication/WineDbg settings,
unchanged original Cuphead image/native MRM/Vector alias/dbghelp/bottle
schema, strict signature, and no runtime/recursive bottle holders. The one
grant was consumed and the hold is safe to release; no second launch.

|Actual2290 retained evidence|SHA256|
|---|---|
|Owner receipt, failed original exit preserved|`5e071345a54d71ea6e06529559210cdf2f2a898dce9afc446a75df59ee697f9d`|
|Independent35-target restoration verification|`1279e9bd0975a36de9e0250d0b0880d4361b57e5a34694801323958147bf53c5`|
|MRM negative stdout|`2b9cd9e2d1e9e4ef6cd5a4e191557375e3f791e530fc6b01347cc56abe64bf9e`|
|MRM negative stderr/module trace|`3ab16e6df603c41b76c6a1da05a396b583fcfa484ca381133e6d6b77ab98fbcf`|
|Ordinary activation stdout|`d57f50d40f55f8f1b39d3de6700d822dfd34a666a88ee916115ced054b54c073`|
|Ordinary SEH/module trace|`44c4f9f9e2ff3b0db9eeb89218cfee997e7897d1741fa4f1b583d23ebaa6ddf3`|
|Full server trace|`697b67b4a7ab496f1d686b883b767610a75dae1bbb9db80b8b1734b99a0c7b9d`|

The source-only candidate removes the diagnostic pointer walker and
invocation, preserving the fail-fast exception record/context/flags and
`NtRaiseException` path, and adds the requested `RtlIsMultiSessionSku` FALSE
export for Wine. The candidate patch is
`scripts/macos/compatibility/wine-cuphead-failfast-multisession.patch`
(SHA256 `bd030ee928d684dcd42ab0a079e3ffc0c62a978009dded19dc5cdb7f764538b1`).
Both modified Wine DLLs built and linked; the ntdll PE export table contains
`RtlIsMultiSessionSku`, the kernelbase PE contains no
`FAILFAST_DIAGNOSTIC` strings, and the modified `rtl.c` test translation unit
passes a cross-compiler syntax check. The configured build has no target for
executing the ntdll test suite, so the runtime assertion was not run. No
Cuphead launch or other product runtime was performed. Fresh execution stays
CUT pending a new explicit grant. No alias, debugger, additional provider or
unrelated missing delay-import was introduced. Quality runner remains
absent/exit2.

|Built source-only DLL|SHA256|
|---|---|
|Wine `ntdll.dll`|`8c82df24d37f04e729f9ff8ac11c086e5ef6ed640921b9444fdf61cd7526dcf8`|
|Wine `kernelbase.dll`|`da8b625bbf1a9bfbe9d71c20719a1b8268bb930babbb6f1d4a1e78ea8fb68d74`|

**2300 attempt (not a candidate result):** the unique 2300 owner ran over
plain SSH instead of the required Terminal/osascript GUI session. winemac
`macdrv_init` lacked `sessionHasGraphicAccess`, explorer had no display driver,
and the dispatcher pre-gate failed before any no-game MRM or Cuphead launch.
The candidates were frozen at mode 0444 and copied with that mode, so the owner
could not restore kernelbase/ntdll (EACCES). Both were restored exactly from
receipt backups. Independent verification then confirmed all 35 baselines,
registry/WineDbg state, absent system32 MRM, unchanged pins, no runtime/holders
and the bundle signature. The candidate DLLs are now 0644 and their hashes are unchanged.
Kernelbase resource export RVAs for the MRM gate are package `0x28720`,
application `0x26ed0` and property `0x30ba0`.

**2310 (a79f42b candidate, Terminal launch):** the admitted owner `7e2cb2c1…`
and wrapper `e729d232…` used the same 35-target shape. In the no-game MRM check
all 6 checks passed through the system32 native provider with the new RVAs.
Ordinary activation passed 13 checks (claims mask 3, origin 4). Cuphead then
exited `0xC0000421` after about 1.2 s with zero windows, so there is still no
frame. Compared with 2290, the `RtlIsMultiSessionSku` stub and the 1007/fail-fast
path are gone, and native `mrmcorer`/`RMCLIENT` now load. The new ordered chain:
1. RMCLIENT `HamConnectForExtendedExecution` reports the HAM backend unavailable.
2. CLSID `{96c7a5ef-0e2c-46d7-9bc1-6445c2444d7a}` is not registered. Genuine
   Windows registers it as an in-process `Both` class of `threadpoolwinrt.dll`,
   and its GUID sits beside the native `ActivatableClassId` WRL registration.
   Wine's threadpoolwinrt has no such class.
3. `dispatch_rpc` reports no apartment.
4. `0x80010108` RPC_E_DISCONNECTED repeats.
5. A non-continuable code-4 exception carries `0x8000000E`.
The required interface and semantics of that class remain unidentified. The
owner reported complete restoration, and independent verification
`7d372308…` confirmed 35 baselines, absent system32 MRM/override, keys,
WineDbg, pins, no runtime/holders and the signature. Failed receipt `28a9efa9…`.

**2320–2350:**
- **2320:** the threadpoolwinrt SignalNotifier patch `0cc86c9a…` removed the
  CLSID `{96c7a5ef…}` gap; its Wine conformance test passed. Cuphead still
  exits `0xC0000421` with no window.
- **2330, 2331:** procedural refusals with no launch (trace backlog, then the
  guard). **2332:** a broad RelayFromInclude trace exceeded the 20 MB bound
  before the raise.
- **Dual MrmCoreR load:** caused by a stale bottle registration. The early
  activation script pointed ResourceContext and ResourceManager at the
  package-local `MrmCoreR.dll`; genuine Windows uses System32.
  `scripts/macos/compatibility/winrt_class_registration.py` now resolves
  these system classes to `C:\windows\system32\mrmcorer.dll`. It refuses a
  package fallback when the provider is absent, so a permanent registration
  also needs that provider permanently installed.
- **2340:** a cycle-owned repoint gave a single system32 MrmCoreR, but the
  same fatal recurred. The dual-load hypothesis is falsified.
- **2350:** RelayInclude was limited to RaiseException. The fatal
  `kernelbase.RaiseException(4, 1, 2, …)` returns to `mrmcorer+0x9ABA0`. That
  is the return address of MrmCoreR's generic WIL throw thunk `0x9ab68`.
  Static analysis puts the only arg2=4 reporter call at `0x3d719`, inside the
  ambient ResourceContext get-or-create `0x3d660`. There, a provider
  vtable `+0x30` returns `0x8000000E`, apparently because the ambient-context
  global `0x100cb0` is still unset. The calling frame above `0x3d660` was not
  captured.
- **2360:** the relay added the kernelbase package-identity APIs. Every
  MrmCoreR identity call succeeds (size probe, then 0), so identity is not
  the gap. The fatal is unchanged (`+0x9ABA0`). No load of
  `ext-ms-win-resources-deployment` is logged; that absence is inconclusive.
  Static analysis names its missing `GetInitInfoByPackageFullName` as the
  remaining candidate gap for the ambient-context init `0x42684`.
- The 2360 control-first abort refused with "Incomplete ownership boundary"
  while a live wineserver trace was still being written, so nothing was
  restored. The runtime had already exited. An offline recovery restored 36
  files and 14 registry blocks, and independent verification `060139b8…`
  passed. The abort now re-drains within its window before refusing.
- **2370/2371:** integrated the Manifest child's `GetInitInfoByPackageFullName`
  kernelbase provider and the `ext-ms-win-resources-deployment-l1-1-0`
  api-set mapping (patch `ba8ef8b4…`; kernelbase `14c86783…`, apisetschema
  `e01c6842…`). 2370 stopped at the no-game MRM gate before launch. The gate's
  checks had passed, but it pinned the kernelbase `property_rva`, which the
  rebuild moved, so 2371 reran with the new RVA. In 2371 the fatal is
  unchanged (`mrmcorer+0x9ABA0`, exit `0xC0000421`, 0 windows).
  `GetInitInfoByPackageFullName` and `GetPackagePathByFullName` were never
  called. MrmCoreR (`+0x216d0`) resolved Cuphead and its three framework
  dependencies through `GetStagedPackagePathByFullName2(type 2)`, all
  successfully. The resources-deployment gap is therefore not on this fatal's
  path. The fixed abort quiesced cleanly, and restoration completed.
- **2380–2383:** integrated `corewindow-autocreate-default.patch`
  (`3e08fdd7…`) and `corewindow-null-hwnd.patch` (`0d531beb…`). Both
  applied together, and the windows.ui build is `0983cbf9…`. The contract
  test passed 16/16 under ASan/UBSan.
  - 2380, 2381 and 2382 stopped at the pre-game CoreWindow conformance gate.
    That gate encodes the old opt-in contract and sets the AUTOCREATE
    variables itself. Neither the Unix env nor `--env` can opt it out.
  - 2383 used a 2380 gate variant (`fe9c6edb…`) that asserts the strict NULL
    under `WINE_COREWINDOW_NO_AUTOCREATE=1` and the new default create
    otherwise.
  - 2383 result, with RelayInclude for `RoGetActivationFactory` and
    `RoActivateInstance`: the fatal is unchanged (`mrmcorer+0x9ABA0`, exit
    `0xC0000421`, 0 windows), and Cuphead never queried
    `GetForCurrentThread`.
  - MrmCoreR was loaded directly, with no combase activation before it.
  - Before the fatal, MrmCoreR itself activated, all returning S_OK:
    `CoreWindow`/`ICoreWindowStatic` at `+0x2364b`; an instance at
    `+0x4a65b`, which loaded builtin `system32\windows.ui`; a factory at
    `+0x4bce0`; and `CoreApplication`/`ICoreApplication`
    (`0AACF7A4…`) at `+0x42854`, inside the `0x42684` init. That last
    factory is served by native `twinapi.appcore`. The fatal follows
    immediately.
  - HYPOTHESIS: an `ICoreApplication` call, made with no
    `CoreApplication.Run` view, returns `0x8000000E`.
  - Restoration: 36/36 baselines match for each label, and no Wine
    processes remain.
- **2384–2386 (error-origin relays, same candidate):**
  - 2384 used RelayInclude `ntdll.RtlCaptureStackBackTrace`. Both captures
    before the raise come from `combase+0x28ee0`, which is our
    `trace_illegal_method_call` helper.
  - 2385 stopped at a pre-mutation pin check after a label replace
    corrupted a pinned hash. The bottle was untouched.
  - 2386 used RelayInclude `RoOriginateError(W)`/`RoTransformError(W)`.
    Right after `mrmcorer+0x42854` returns S_OK, native
    `twinapi.appcore+0x7fbfc` originates `0x8000000E` twice, then MrmCoreR
    raises.
  - VERIFIED: the error originates in native `twinapi.appcore`, and
    MrmCoreR only re-raises. Per static RE, twinapi's
    `CoreApplication`-context predicate (`ctx+0x360` current view, state
    ≥3) is unset because no activation host creates the view.
  - Restoration errors: none, and no Wine processes remain.
- **2387 (loader relay):**
  - Native `twinapi.appcore` is activated first from Cuphead
    `SharedLibrary+0x56c322`. Then `mrmcorer` maps as a dependency, and
    twinapi originates `0x80070057` ×7.
  - After that, the builtin RMCLIENT HAM extended-execution call is
    refused.
  - Later, a `SharedLibrary+0x56c322` activation is served by
    `mrmcorer`, leading to the `0x8000000E` fatal.
  - The `mrmcorer` export relay did not fire.
  - HYPOTHESIS: native `CoreApplication::Run` is in progress, but its view
    registration fails under Wine.
- **2388 (class-string and window relay):**
  - On the main thread, `Cuphead.dll+0x2527fc7` calls `RoInitialize(MTA)`.
  - Then `CoreApplication` is activated twice from native twinapi.
  - Decoded `Windows.UI.dll+0x3bad` creates a CoreWindow HWND before
    `mrmcorer` maps.
  - `ResourceContext` is served by `mrmcorer`, which reaches
    `CoreApplication` at `+0x42854`, and the fatal follows.
- **2389 (apartment values; existing `trace+ole`, no scratch build):**
  - At `twinapi+0x460d5`, `twinapi+0x4928d` and `mrmcorer+0x42bdd` the
    main thread is MTA, qualifier 0.
  - A thread created through `shcore+0x61ba` calls `RoInitialize(SINGLETHREADED)`
    from `twinapi+0x8f4d5`.
  - Wine reports that thread as `MAINSTA`/0 at `twinapi+0x170c7`. Native
    reports an application STA as STA/6.
  - HYPOTHESIS: the view thread's apartment report blocks view registration.
    Candidate fix: combase reports an application STA for
    `RoInitialize(SINGLETHREADED)`.
  - Restoration errors: none.
- **2390 (combase application-STA reporting):**
  - Change: `RoInitialize(RO_INIT_SINGLETHREADED)` marks the apartment as an
    application STA. It never becomes the main STA, and
    `CoGetApartmentType` reports STA with qualifier 6.
  - Unchanged: explicit `CoInitializeEx` STA/MTA, and the public model seen
    by init spies.
  - Limits: report-only. True ASTA re-entrancy semantics are not
    implemented, and the change may later need scoping to packaged
    processes.
  - Targeted pre-gate: 13 checks, 0 failures.
  - Runtime: the view thread now reports STA/6 at `twinapi+0x170c7`.
  - The main-thread `0x8000000E` fatal is unchanged. A worker thread newly
    originates `0x8000000B` at `twinapi+0x7fbfc`.
  - Windows 0, no frame. Restoration errors: none.
- **2391 (relay read-out on the ASTA baseline):**
  - The view worker waits only inside our builtin Windows.UI
    `PumpUntilSignaled`, on handle `0x22c`.
  - Main runs the twinapi Run driver. It polls the same `0x22c` with
    timeout 0 at `twinapi+0x97926`, times out, and continues.
- **2392:** invalid as a trace run. The ordinary-run debug assignment
  overwrote the added `+ui` channel, so earlier "no ui fixme" inferences
  from ordinary runs are void.
- **2393:** stopped at the pre-gate. The combase oracle build shifted the
  pinned ordinal-122 RVA, so nothing launched.
- **2394 (Windows.UI method/QI trace plus combase 32-frame stack oracle):**
  - Exit `0xC0000421`. Windows 0, no frame. Restoration errors: none.
  - Cuphead's `ResourceContext` activation and the `0x8000000E` happen on
    main after Run returned: their stacks contain no twinapi Run frames.
  - The seven `0x80070057` originations are inside Run on main
    (`twinapi+0xfc43` → `0x4932c`).
  - twinapi calls into Cuphead only on the view worker
    (`twinapi+0x8f507` → `0x16fef` → `0x18f00`). The worker then pumps
    `0x22c`, the pump returns `S_OK`, and the worker exits.
  - Nothing is enqueued on any builtin dispatcher, and twinapi does not use
    `ICoreDispatcher`.
  - Builtin anomalies seen on main:
    - `GetForCurrentThread` creates a CoreWindow on the MTA main thread.
    - `get_ApplicationViewClientAdapter` returns `E_NOTIMPL`.
    - The late UIContext QI for `{f21e14c1}` returns `E_NOINTERFACE`.
  - Causal impact of these anomalies: unproven (HYPOTHESIS).
- **2395–2397 (relay read-outs of the Run activation step):**
  - Main queries only token classes 1, 2, 8, 9, 25, 29 and 39, so the
    AppContainer/Low-IL token answers stay held.
  - The `0x80070057` source is `CreateWellKnownSid(111)`
    (`WinBuiltinDefaultSystemManagedGroupSid`, `S-1-5-32-581`), which
    returns `ERROR_INVALID_PARAMETER` in Wine.
- **2398 (kernelbase well-known SIDs 108, 109, 111 and 112):**
  - Targeted gate: 37 checks, 0 failures. Restoration errors: none.
  - `CreateWellKnownSid(111)` now succeeds.
  - The next fault is the `sechost` SDDL parse of twinapi's named-object
    SD. It fails on the conditional ACE
    `(XA;;GA;;;IU;(WIN://SYSAPPID Contains "<PFN>"))`, with
    `RPC_S_INVALID_STRING_UUID` (`0x800706A9`). `0x8000000E` still follows.
- **2399 (conditional-ACE SDDL plus a server ACL that accepts it):**
  - `sechost` encodes `XA`/`XD` conditions as `artx` postfix bytecode, and
    the SD tokenizer ignores `:` inside parentheses and quotes.
  - `wineserver` accepts `ACCESS_ALLOWED_CALLBACK` ACEs but never evaluates
    them, so they grant nothing. Deny-callback ACEs are still rejected, and
    access checks are unchanged.
  - Targeted gate: 32 checks, 0 failures. Restoration errors: none.
  - Runtime: the SDDL parses. No `0x80070057`, `0x800706A9` or
    `0x8000000E` appear, and mrmcorer's `ResourceContext` path runs.
  - New terminal fault: WIL fail-fast `0xC0000409` on main.
    - `mrmcorer+0x494d3` gets the builtin's lazily created main-thread
      CoreWindow.
    - It QIs that window for the private
      `{84acbbfe-649c-5ac7-8130-7b6b76db463c}` (present only in native
      Windows.UI, twinapi.appcore, MrmCoreR and Windows.Graphics), and the
      builtin returns `E_NOINTERFACE`.
    - Statically, the interface's slot 6 returns an object whose slot 17
      yields a float that mrmcorer scales into the resource scale
      qualifier (HYPOTHESIS: display scale).
  - Windows 0, no frame.
- 2400 — strict CoreWindow `GetForCurrentThread` (case A):
  - 2399 evidence: the main thread is MTA (`RoInitialize(1)`, apartment
    type 1). No `SetWindow` or ordinal-1500 `CreateCoreWindow` runs on any
    thread. The only CoreWindow HWND was the builtin's lazy one, created
    under `twinapi+0x21324`.
  - Change: set the existing builtin switch
    `WINE_COREWINDOW_NO_AUTOCREATE=1`, only for the ordinary activation.
    Threads without an explicitly created or bound window now get `NULL`,
    which matches native MTA behaviour. No rebuild.
  - Gate: `corewindow-strict-test.exe` `37898c03…` checks that a strict MTA
    thread gets `NULL` with no HWND, and that an existing bound window is
    still returned. Owner `0657c24e…`; restoration complete.
  - Runtime: every `GetForCurrentThread` returns `NULL`, including
    twinapi's and mrmcorer's. The `{84acbbfe}` fail-fast is gone.
  - New terminal fault: `RaiseFailFastException` (exit `0x1007`) at
    `SharedLibrary.dll+0x41cf88` on main.
    - It comes immediately after a successful GIT registration of
      `{e2663f37-2e1b-500c-ad68-c3ed7a8f74c8}` (FTM marshal, hr 0).
    - The managed cause is not visible in the relay.
  - The ASTA view worker stays parked in `PumpUntilSignaled`. Windows 0,
    no frame.
- 2401/2402 — fail-fast oracle (report-only kernelbase `5df387b4…`,
  combase `8d06a4b9…`; return values unchanged):
  - 2401 stopped at the pre-launch MRM gate only because the pinned
    kernelbase export RVAs shifted; the MRM checks passed 6/0.
  - 2402 repinned the RVAs and ran with restoration complete; exit
    `0x1007` as before.
  - Root cause of the fail-fast: inside `CoreApplication.Run` on main,
    twinapi+0x6b842 delay-loads
    `ext-ms-win-com-psmregister-l1-1-0.dll.PsmQueryBackgroundActivationType`.
    - The load fails and raises `EXCEPTION_WINE_STUB`, the only exception
      on main.
    - .NET Native's handler (mrt100 → `SharedLibrary.dll+0x41cf88`) then
      calls `RaiseFailFastException(0x1007)` with no parameters. There is no
      managed exception text.
  - The stub call comes right after a successful `ApplicationService:<hash>`
    named file mapping, created with the ML/NX/NW/NR plus capability SDDL.
  - `Run` never returns. CoreMessaging is never loaded, and no
    broker `CoGetObject` is reached.
- 2403 — stand-in `ext-ms-win-com-psmregister-l1-1-0.dll`:
  - Candidate: a PE DLL, `d5243ade…`, placed as an absent target in
    system32. `PsmQueryBackgroundActivationType` sets `*type = 0` and
    returns `S_OK`, or `E_POINTER` when the output pointer is NULL.
  - Gate: 7/0 checks pass. Restoration completed.
  - VERIFIED: the DLL loads natively and is called from main with the
    (HANDLE)-4 pseudo-token, returning type 0. The psm delay-load failure
    is gone.
  - Next wall: right after that call, twinapi does an `ApplicationService`
    mutant/section probe, then `CreateWellKnownSid(22 = LocalSystem)`, then
    delay-loads `RPCRT4.RpcBindingCreateW`.
    - Wine does not implement that export.
    - The same `EXCEPTION_WINE_STUB` → .NET Native → fail-fast `0x1007`
      chain follows.
  - Of twinapi's rpcrt4 imports, `RpcBindingCreateW` and
    `RpcBindingBind` are the ones Wine is missing. This is the ncalrpc
    client for interface `{5824833b…}`.
- Independent verification: 2340 `cf204f29…`, 2350 `d205e86a…`.
**Finite product checkpoint:** Cuphead still has no observed window/frame.
Pnp/nullable/folders fixes and the Vector oracle removed earlier startup
gaps, but native alias progress is not our distributable Vector support.
The 2290 no-game MRM negative passed and the ordinary trace shows native MRM
provider loading. Its only new observed missing-export event is
`ntdll.dll.RtlIsMultiSessionSku` (`80000100`); its causal relationship to the
original unhandled1007 remains unproven. The repeated `kernelbase+0x82eb9`
faults were generated by the removed failfast diagnostic pointer walk, not
the product. The genuine original exception-to-guard-entry/return/1007
linkage remains unproven.

Read-only inspection now establishes a genuine oracle on this existing
Windows host, OS binaries version10.0.26100.9278, AMD64:

|Genuine Windows system artifact|SHA256|Observed contract surface|
|---|---|---|
|apisetschema.dll|`391a2818fdef14f4a9b6e6ce2a7e7f01d04f943de9ef543ec683927cfe480889`|Actual v6 namespace maps the ext apiset to mrmcorer.dll|
|MrmCoreR.dll|`6186f981d1aa1e1991697133aa066045097ab52227a240632a0c7d8fd04352be`|GetStringValueForManifestField export34610|
|kernelbase.dll|`becad014fb8efa8cb5e314931cca92778ad42c649b12a6909632cacd68af4f40`|Three resource-context exports104b30/100bc0/105b20|

Actual admitted Wine kernelbase SHA9308d50f lacks all three exports
(PE export table verified, not inferred from comments): GetCurrentPackage-
ApplicationResourcesContext, GetCurrentPackageResourcesContext and
GetPackageResourcesProperty. Mapping alone therefore moves the failure
to the genuine provider's private kernelbase imports. **No complete
contract-correct packaged implementation is currently grounded.** Missing
contract is kind2/field1's actual app-manifest property and package
resource-context/lifetime/output/error semantics, not merely the
GetStringValueForManifestField five-argument ABI (already grounded).

**ONE cheapest next product discriminator:** a bounded no-game native
Windows oracle calling the existing genuine provider export exactly as
twinapi does: `(2,1,0x101,WCHAR buffer,SIZE_T* required)`, from an ordinary
unpackaged x64 process. Pin the three system artifacts above, verify actual
loaded provider path, report HRESULT/output count/buffer state, and impose
30s/1MB owned-process bounds. No debugger, PEB/disposition edits, downloaded
game, account/license, registry/package registration or private-context
calls. It can establish genuine missing-package failure/output semantics,
not field1's packaged success value; success semantics still need an
authorized existing signed Windows package or genuine contract reference.
No API call or native oracle was executed, and no grant is assumed.

The smallest ordinary Mac capture plan, only after a grounded product
candidate, needs existing `+seh,+loaddll` split traces, not a debug object
or another capture engine. Admitted source `ntdll/exception.c:199-207,
242-247` already logs WINE_STUB identity/record/context and
`signal_x86_64.c:270-276` logs handler dispatch/return; these traces were
observed in prior ordinary runs. They cannot establish a stopped guard RIP
or unwind and should not be expanded to do so here.

**Recommendation: SMALLEST_VIABLE.** Finish this safety slice, obtain the
single genuine no-game result if granted, then make one ABI/semantics-
grounded MRM repair or PARK Cuphead if packaged context semantics remain
unavailable. Do not run another unchanged launch/full-suite/debugger cycle
or implement a guessed HRESULT shim. The next implementation decision is
blocked on that exact contract, not on more diagnostic machinery.

#### 2210 original-exception telemetry gap and targeted capture-only plan

Further read-only inspection finds exactly one dispatched native exception:
01a8's `EXCEPTION_WINE_STUB` 80000100 at kernelbase+0xda77, after the missing
`GetStringValueForManifestField` delay import. Its record has two stub identity
parameters; three twinapi handlers return ContinueSearch, then a fourth
handler is entered at 0x6ffffd3afaa8. The fourth handler's module ownership is
not established by game.stderr's incomplete module inventory. No return from
that handler is logged before resource-context construction and the eventual
fail-fast. Therefore "recovered missing export" describes later activity, not
proof of a benign fallback or a separate exception.

No dispatch/handler entry for mrt100_app+0x8380 is recorded, and no second
native dispatch precedes the generated 1007 record. The log cannot establish
which original exception (including the earlier stub) enters the PInvoke
guard, nor which twinapi outer caller consumes the successful vector.
`FAILFAST_DIAGNOSTIC` is absent; diagnostics present in the current source tree
must not be assumed to exist in the admitted kernelbase binary.
Both factory returns expose the same private factory pointer 0x238e68.
The next logged public activation is ApplicationView a28d7594, S_OK and
interface 0x238dc8, called by SharedLibrary+0x46c322. This does not identify
the caller at the untraced private slot30 boundary.

The cheapest discriminator is **one capture-only debugger controller**, not
another compatibility API or an unchanged replay. Preparation/admission plan:

| Exact pinned module site | Grounded observation |
| --- | --- |
| twinapi+0x3d20c, then +0x3b50c | Instruction immediately after each slot30 call: EAX HRESULT, vector output at RSP+0x28, bounded stack/unwind caller evidence. |
| mrt100_app+0x8380 (`RhpPInvokeExceptionGuard`) | Original RCX EXCEPTION_RECORD, R8 CONTEXT, R9 DISPATCHER_CONTEXT and handler thread before guard classification. |
| mrt100_app+0x32c0 (`RhpFailFastForPInvokeExceptionCoop`) | Original RCX dispatch control PC, RDX exception record and R8 context, before reason7 conversion. |
| mrt100_app+0x32e0 (`RhpFailFastForPInvokeExceptionPreemp`) | Same exception/context parameters on the preemptive path; keep distinct from generated FailFast. |

Native mrt100_app SHA-256 is
`6eb3c9c626c2514b39301969f318d1b7c839d3efe1a86a90783ec6f78f19e3ec`;
twinapi remains
`0d3e56c5068633f975bcab528267789f520f82a6d6a50f1330104e7597289b43`.
Never reuse 2210 absolute addresses. Resolve module bases from owned debug
events, verify image pins and the exact instructions before arming sites.

The existing controller already creates the exact original executable
suspended and performs explicit package activation before ResumeThread.
A separate compiled diagnostic variant can use public
DEBUG_ONLY_THIS_PROCESS/WaitForDebugEvent/ContinueDebugEvent and x64
GetThreadContext/SetThreadContext debug-register controls. Four hardware
execution slots fit guard/coop/preemptive plus one twinapi return; rotate the
last slot from +0x3d20c to +0x3b50c after its first hit, and install equivalent
controls for each new owned thread. Preserve occupied debug registers by
refusing admission, not overwriting them. Do not patch private binaries/code,
redirect execution, swallow application exceptions or clear FailFast.
For owned breakpoint events only, log the typed records and resume under the
debugger contract; pass original application exceptions as NOT_HANDLED.
Host support for hardware debug registers remains unproven: the synthetic
gate must reject unavailable/ineffective debug-register operations, with no
software-code-patching or guessed interception fallback.

Record first-chance exception code/address/parameters and exact handler
inputs, retain bounded original contexts and up to four chained records.
Use public header sizes and checked ReadProcessMemory, no guessed object
layouts. At guard entry correlate its original record with first-chance
events and report a mismatch or missing record explicitly. At the slot30
return, preserve HRESULT/output and stack PCs; only claim an outer caller
when its unwind or recorded return address actually identifies it.
The normal activation, exit and window observations remain unchanged.

This mechanism is supported by public debug APIs, but **not yet compiled or
runtime-admitted**. It requires a source-only controller implementation and
synthetic no-game tests first: suspended attach/start ordering, new-thread
breakpoint propagation, module-relative resolution, read failures, ordinary
first-chance exception pass-through, hardware-breakpoint resume without
re-execution loops, exact exit accounting and cleanup. If these fail, stop
before any game request.

Only after that gate should a new exact controller pin and hold be requested:
same 33 providers and native alias registration, no extra provider or
compatibility fixes. Propose 60 seconds / 20 MB aggregate capture bound with
minimal +seh/+loaddll, no +file/+module/full relay and no samples. Keep unique
owner paths, Terminal/caffeinate, exact PID/start/lineage ownership,
all-target/registration/fixture/settings restoration and independent
signature/runtime/holder verification. This is a proposal, not a granted
cycle, debugger success or a first-frame claim.

#### Source-only capture accounting correction

The preserved 2210 owner and captures are unchanged. New
`capture_accounting.py` polls the controller, reads completed stdout, then
checks the sum of stdout/stderr/server bytes before classifying ready or
timeout. An aggregate over-limit remains failure even when exit/first-wait
markers are present; missing captures explicitly error and no file is
truncated. Eleven local synthetic tests cover rapid real subprocess exit,
split-output aggregate limits, exact thresholds, stale timeout races,
marker/over-limit precedence, missing files and retained output.

A separate session-artifact owner prototype imports this helper in both
first-wait and later diagnostic loops, keeps parsed exit observations before
raising a genuine aggregate failure, records per-stream/aggregate sizes and
performs final post-cleanup accounting without replacing a prior owner error.
It is syntax-checked source only, still inherits consumed 2210 paths, and
**must not be executed**. Any future unique owner must stage/pin the helper
and controller together and pass its own admission; this does not change
2210's failed receipt or authorize another launch.

Local focused validation: `python -m unittest discover -s
scripts/macos/compatibility -p test_capture_accounting.py -v` passes 11/11;
the owner prototype passes AST parsing without import/execution. The required
`python gates/gate_runner.py quality-gate` was attempted again and exits 2
because this checkout has no `gates/gate_runner.py`. This remains an explicit
mandatory integration blocker, not a quality PASS.

#### Source-only targeted debug-event candidate 2230

The coordinator retained the targeted public-debug-API approach and authorized
implementation/compile only. No Wine process, fixture, package or game was
executed while preparing this candidate. The consumed 2210 accounting
prototype, original logs and failed receipt remain untouched.

Added a shared x64 debug-event capture engine, a synthetic-only controller
which refuses any executable basename except debug-event-fixture.exe, the
fixture, a separate future package-capture controller, and a compile-only
manifest builder. All three executables pass strict compilation
(`-Wall -Wextra -Werror`) as AMD64 PE32+. Their actual imports include public
debug-event/thread-context/process APIs and BCrypt hashing; compile success
does not establish Wine hardware-debug-register support.

The engine resolves image bases from owned CREATE_PROCESS/LOAD_DLL events,
checks exact target file SHA-256, AMD64 headers, image bounds and executable
site memory, and retains the original per-thread debug registers. It refuses
occupied slots. Get/SetThreadContext register readback checks each installation,
including new threads and the fourth-slot rotation. A trap is consumed only
when first-chance SINGLE_STEP, exactly one DR6 hardware bit, no competing
BS/BD/BT condition, matching owned slot, matching RIP and matching exception
address agree. It clears only its debug status and sets RF for that instruction
before continuation. Unrelated first- and second-chance exceptions remain
DBG_EXCEPTION_NOT_HANDLED. The startup breakpoint is separately matched to
the remote ntdll DbgBreakPoint export location and matching local bytes;
an unmatched breakpoint is not silently swallowed.

Typed original EXCEPTION_RECORD chains (maximum four), CONTEXT and
DISPATCHER_CONTEXT are read through checked ReadProcessMemory. Bounded raw
stack qwords and module bases are logged, but are explicitly not an unwind
or proven caller stack. The future package controller selects only the
previously grounded five sites; it does not call private slots or modify
Microsoft binaries. It verifies the catalog-selected Cuphead.exe against
an independently supplied pin before CREATE_SUSPENDED/debug startup and
existing explicit package activation. This separately compiled artifact is
**not included in the no-game owner, not runtime-admitted and not a replacement
for all normal activation/window-observation checks**. Future game integration
must retain those checks and obtain a separate decision.

On capture/read/installation failures and bounds, owned stopped threads have
their original debug registers restored/read back before exact-handle
termination and debugger detach. Cleanup records restoration, termination,
detach and process-handle quiescence independently; none may be substituted
by a generic success. Natural exit remains a debug-event observation.
A distinct synthetic live-detach mode restores registers while all threads
are stopped, continues the identified trap, detaches, then signals its
PID-specific fixture event and verifies exact-handle natural exit.
Timeout and aggregate-log failures cannot be accepted merely because
some unrelated startup failure occurred.

Fresh unexecuted owners are staged outside Git:
`~/xodus-app-tooling/cuphead/cuphead-debug2230-{owner,launch}.py`, with unique
`cuphead-owned-debug-unit-20261011-2230` work/log/status paths and an independently
named `capture_accounting2230.py`. Both pass AST parsing without import or
execution. No new work directory, owner log or owner status exists.

| Source-only candidate / fresh owner | SHA-256 |
| --- | --- |
| Synthetic debug-event controller | `fa23b237ec49aa40805385469e165c156539a7ae1fa497af48cdf5b482b4b71e` |
| Synthetic fixture | `ea4c4db2fe94a1a0e9ae7329c4edc292db740dea0e638ea55b8ff55bbcf0c1d3` |
| Future package capture controller, excluded from no-game execution | `c7a3ee05e140a17a565ea44507b98ee908497164824e525d354862ccfe0b3e64` |
| Source/compile/RVA manifest | `7b4c580aa0aef0a243d617f9e30c0fc39681d3ffbfff7c072afa2b8e673d639d` |
| Fresh no-game owner | `b798f95dee0526ab647b02da11f6a3f556607a2b5b01e0246ce43aa7b834bb05` |
| Fresh no-game launch wrapper | `f5e4227ef79e8f8ffa94d2baf98fed05a79dcddb5a57ed35a963772700009d4b` |
| Unchanged accounting helper, uniquely staged | `28add3e42f44916613dddd83f7db87e8380767738c24da10c71bd36c463ed1a1` |

Candidate directory:
`~/xodus-app-tooling/cuphead/debug-event-candidate-20261011-2230`.
Fixture export RVAs are 0x1564, 0x15d1, 0x162e, 0x168b, 0x16b2,
bound to the exact fixture hash above. No private Microsoft artifact is
copied into Git, vendor or bottle by this source preparation.

**Proposed exclusive no-game scope, not granted or executed:** preserve the
identical admitted 2195 33 target/candidate/baseline triples and unchanged
cfgmgr/enumeration/wintypes pins. The fresh owner derives from the verified
2205 ownership/restoration harness, retains its standard pre-gates and
temporary Pnp/devnode/proxy/dispatcher setup, but replaces the native oracle
phase with eight synthetic modes:

| Mode | Required observation, not yet tested |
| --- | --- |
| positive | Four-slot install/readback, new-thread propagation, all five site hits exactly once after rotation, typed original record/context/dispatch values, application-handled custom exception passed through. |
| detach | Same hits, all live debug registers restored/read back, successful detach, PID-specific fixture release and exact-handle natural exit zero. |
| foreign-step | Unrelated software-raised SINGLE_STEP remains NOT_HANDLED by the controller and is handled by the fixture. |
| unhandled | Custom exception reaches second chance, remains NOT_HANDLED and yields its real exception exit code. |
| read-error | Owned guard hit with invalid record address causes explicit read failure and independently successful cleanup. |
| occupied | Preoccupied hardware slot is refused rather than overwritten; owned-process cleanup completes. |
| timeout | Real deadline failure, not a startup failure, with restored registers, detach/termination/quiescence. |
| log-limit | Fixture debug-string pressure crosses the genuine controller byte bound, not a timeout/startup proxy; same cleanup proof. |

Each synthetic invocation is owner-bounded at 15 seconds / 20 MB aggregate
stdout+stderr+server growth, checked after polling and again at completion.
The controller has its stricter internal limits: normal 10 seconds / 1 MB;
timeout 1.5 seconds; pressure mode 32 KB. The owner stops at first unexpected
result and uses existing exact PID/start/lineage cleanup. The launch wrapper
accepts `unit` only and must be invoked through the established
Terminal/caffeinate mechanism after an exclusive hold decision.

The native Detail.Vector alias is **not selected or executed** by this
proposed synthetic phase, and its key stays absent before/after; its unchanged
artifact identity remains a restoration control. No new Detail.Vector
registration, #295, graphics or events candidate is added. Restore all 33
baselines and prior key/fixture/settings state, then independently verify
signature, full runtime and recursive holder quiescence before release.
No game is included. Hardware debug support and all eight behavioral gates
remain unproven until an authorized no-game cycle; mandatory quality-runner
absence still blocks an overall integration PASS.

The static lane's missing-MRM dependency chain is retained as correlation
context only. GetStringValueForManifestField/WINE_STUB is not established
benign, the fourth handler's owner remains unknown, and deployed
FAILFAST_DIAGNOSTIC availability is not assumed. Successful Vector factories
remove the activation gap but do not prove slot30 return or the outer caller.

#### No-game 2230: hardware execution traps not observed, stopped/restored

The coordinator granted exactly one exclusive no-game cycle on the reported
33-target manifest and source-only candidate/owner pins. The owner ran via
Terminal/caffeinate once. All standard pre-gates passed before the first
synthetic mode: cfgmgr32 1292/1369, enumeration 1938/1987, wintypes 1120 tests,
each zero failures; nullable50, devnode35, async22, plus the existing
proxy/marshaler/dispatcher/resource/registration gates.

**FAILED positive mode:** the fixture's five functions executed, reporting
`touched=5 thread_exit=0`; EXIT_PROCESS_DEBUG_EVENT reports its natural exit0.
However, the diagnostic reports `initial=0 rotated=0 hits=0,0,0,0,0`.
No intended hardware SINGLE_STEP event was observed, although the installation
and Get/SetThreadContext readback paths reported no error. This is not a
successful debugger gate and does not establish effective hardware trapping.
The exact cause (host execution support, context propagation or another
mechanism) remains unproven; it is not evidence that every public debug API
or all hardware debugging is unsupported.

The actual first-chance startup breakpoint is at remote ntdll+0x110a8
(0x6fffffbd10a8), not recognized by the candidate's exact DbgBreakPoint
predicate. It is passed DBG_EXCEPTION_NOT_HANDLED, not blindly consumed.
The custom e0425844 exception is likewise passed NOT_HANDLED; the fixture's
application handler runs afterward. Thus that ordinary first-chance
application handling is observed, but initial-trap recognition, four-slot
hit/context and rotation requirements fail.

The owner stops immediately with
`RuntimeError('Checked command failed: debug-event-positive')`.
The other seven modes (detach, foreign-step, unhandled, read-error, occupied,
timeout, log-limit) are **NOT RUN**. There is no live debug-register
restoration/readback, successful detach, second-chance, forced termination
or expected-negative cleanup proof from this cycle. Positive-mode thread
event/remaining thread handles and the owned process handle follow the
implemented close paths after natural exit, not a runtime handle-leak census.
Do not substitute natural fixture success for the missing gates.

Positive split captures are 1012 bytes stdout / 394292 bytes stderr; the
owner's synthetic aggregate completion check, including server growth,
reports no bound failure. The cumulative server log is 149,558,052 bytes
including all preceding standard gates; it is not the isolated positive
process growth. That growth was checked live but not recorded separately
in the receipt, a remaining accounting-telemetry limitation. The complete
original captures and failed owner receipt are retained without truncation.

Owner restoration reports zero errors. Independent verifier exits0 and
confirms all33 baseline hashes/absences, prior temporary registration and
devnode fixture absence, original WineDbg/CoreApplication settings, strict
signature, no runtime and recursive holder quiescence. An independent
comparison also confirms exact equality of all target/candidate/baseline
triples with2195. Native alias stays unselected, Detail.Vector absent and the
package controller unexecuted. The grant is consumed and exclusive hold
released; no replay, fallback, binary patch or game is authorized.
The original MRM/PInvoke exception attribution remains unresolved.

| Preserved 2230 evidence | SHA-256 |
| --- | --- |
| Failed owner receipt | `23a6d8a65340dc50c5a7d65d2b04720d1cf414497e510bc68c2692d2e65e6d64` |
| Independent restoration proof | `883f62b04e836e8c28ad737e7f037badf8d7e1e403b174c57b5e822de9fbfd05` |
| Positive stdout | `8da0281728c0144ac55a1527ab9436142b76f7ddc284b111be109d03acc05cdc` |
| Positive stderr | `4884fcb34b3412f7696da6dd56cb8dab6722a180acb522ba57edc1bfcd7e33c2` |
| Complete cumulative server capture | `ba14c614cceb6c46805c8b27d7dea5c83fb9b7ddf6611a00579cd4341d5311e1` |

No overall integration PASS: mandatory quality runner remains absent and the
first required synthetic hardware-trap gate failed. Any further action must
first discriminate the unobserved hardware events and startup breakpoint
without speculative compatibility changes or another unchanged game launch.

#### Read-only 2230 diagnosis: Rosetta hardware wall and separate startup predicate bug

The coordinator authorized source-only attribution, not another execution.
Inspection now distinguishes two issues. No candidate bytes or provider
were changed, and the package controller remains unexecuted.

**Startup predicate is a controller bug, not the hardware failure.**
The frozen admitted ntdll.dll (`f8bd19c7606e0f7c506380d1c5c1d8bea477cbfea935f8c6f115de2ae8a2cb42`)
exports DbgBreakPoint at RVA0x110dc, whereas the observed startup INT3 at
RVA0x110a8 is the separate `process_breakpoint` routine. Its frozen bytes
are INT3/RET followed by the handler's context-RIP increment. Source
`dlls/ntdll/loader.c:5286-5287` queries ProcessDebugPort and calls
process_breakpoint; `dlls/ntdll/signal_x86_64.c:1052-1061` defines that
INT3 and exception handler. DbgBreakPoint is separately defined at1119.
Passing the unrecognized startup trap NOT_HANDLED lets Wine's own
process_breakpoint handler continue; it does not explain absent DR traps.
A future exception-only controller must recognize that exact pinned loader
trap independently, not consume arbitrary first-chance breakpoints.

**Hardware register roundtrip is not physical trap support on this host.**
`server/mach.c:278-284` explicitly refuses Rosetta debug-register writes
with STATUS_UNSUCCESSFUL, noting that neither cross-process nor in-process
setting has the desired effect. `server/mach.c:192-197` returns zero debug
registers for translated processes. The actual frozen admitted wineserver
is x86_64 Mach-O on the arm64 host; its `set_thread_context` disassembly
contains the Rosetta-status branch setting c0000001, and its
`get_thread_context` contains the corresponding zero-return branch.
This is binary-grounded, not merely a comment in a different source tree.

`dlls/ntdll/unix/signal_x86_64.c:1147-1156` has the Apple-specific
"Setting debug registers is not supported under Rosetta, faking success"
branch. That exact string is present in frozen admitted ntdll.so
(`e4d34518f2627d73a0035223939ec53bea0dd9829b07238486f51c35cae34422`).
Server suspended/debug context storage at `server/thread.c:2313-2337`
copies submitted registers, explaining a successful immediate readback
without programming a usable hardware execution trap. On resume,
`server/thread.c:2052` attempts system register application; client cached
register copies are not proof of trap delivery.

The retained server trace independently confirms correct fixture addresses:
DR0..3 are 0x140001564, 0x1400015d1, 0x14000162e, 0x14000168b;
DR7=0x55 enables all four local slots with execution type/length fields zero.
Frozen fixture exports/instructions match these addresses. The new worker
is tid0128 / handle004c, entry0x140001744: CREATE_THREAD_DEBUG_EVENT,
GetContext zero, SetContext exact addresses/0x55, GetContext exact readback,
then ContinueDebugEvent, in that order. The earlier process thread uses
handle0048. Thus there is no evidence of a wrong export RVA, data-watchpoint
encoding, wrong thread handle or installation after continuation.
Later debug-event contexts return zero again before reinstall, consistent
with the platform/cache distinction.

Unix delivery source still contains a real SIGTRAP path:
`dlls/ntdll/unix/signal_x86_64.c:2594-2608` converts TRCTRAP to
EXCEPTION_SINGLE_STEP, and1588-1605 refreshes debug registers and sends
the debug event. Its existence cannot make Rosetta generate an execution
trap that was never installed. The syscall hardware-trap ignore branch
at2375-2378 is not implicated by these fixture function entry points.

**Stop this hardware-debugger lane.** Correcting the startup predicate alone
cannot satisfy the four-slot gate on this admitted translated host. No
software breakpoint patch, binary rewrite, trap emulation, debug-subsystem
implementation or replay is proposed. This is a scoped hardware execution
capture wall, not a claim that public exception debug events are unavailable:
2230 actually delivered the custom first-chance exception and natural exit.

The one supported alternative to prepare, if retained by the coordinator,
is an **exception-event-only capture**, without any DR access or code writes.
At each original first-chance event, retain its typed DEBUG_EVENT
EXCEPTION_RECORD, obtain the stopped thread's CONTEXT through public APIs,
and create a bounded exception minidump with controller-local
EXCEPTION_POINTERS (`ClientPointers=FALSE`). Existing
`dlls/dbghelp/minidump.c:390-406,1023` implements those public
MiniDumpWriteDump parameters; `dlls/dbghelp/stack.c:216` supports
StackWalk64 with remote reads. Validate minidump exception/thread/module
streams and any actual unwind separately; raw stack data is not a stackwalk.
Pass original first/second-chance events NOT_HANDLED, preserving the app's
own handlers and genuine exception exit behavior.

This would capture the original fault context/stack for80000100 rather than
only SharedLibrary's synthesized1007. It cannot by itself observe entry to
mrt100_app+8380 or a private slot30 return, so those claims stay unproven.
Pair minimal existing +seh/+loaddll with that dump for handler-address
ownership/correlation; do not infer that the only first-chance exception
caused reason7 merely because it was captured. Preparation would first
need synthetic handled/unhandled exception, exact dump-stream/context,
read-failure, bounds and owned-detach/cleanup gates. No new runtime or
game decision has been granted, and none is requested by this attribution.
| Frozen executed owner | `50904eb36c6359a6ee13cfad538478238bb7ff8d0a41373647fe75f007636405` |
| Frozen executed Terminal wrapper | `32160f9ab71e37dd305f7e2e2686eba99683f94704c67d3cc571cb6f6d5222a7` |

#### Detail.Vector specialization and unsupported factory boundary

Subsequent **read-only static analysis** of the unchanged native twinapi
(`0d3e56c5...`) identifies the helper at RVA 0x3d010. Its x64 descriptor
contains three HSTRINGs for the CoreApplicationView vector/view/iterator
names, followed by the element-interface IID and four collection IIDs.
The 104-byte layout has HSTRING offsets 0/8/16 and GUID offsets
24/40/56/72/88. The first GUID is ICoreApplicationView
`638bb2db-451d-4661-b099-414f34ffb9f1`.

| Returned CoreApplicationView specialization | IID |
| --- | --- |
| IVector | `8af29ecf-6cc4-5a34-99c6-cd9069809253` |
| IVectorView | `de9e16c4-1b7c-5126-b1d8-7cd04f13bd08` |
| IIterable | `32bc12d1-2653-5a41-a55e-88a12af2026a` |
| IIterator | `4f5f6944-264b-5868-809e-c7ac1ac5edad` |

All four match WinRT UUIDv5 generation from their public generic signatures
and `rc(Windows.ApplicationModel.Core.CoreApplicationView;{638bb2db-451d-4661-b099-414f34ffb9f1})`.
The class/default-interface signature and generic signatures were verified
against Microsoft's public [Core projection](https://github.com/microsoft/windows-rs/blob/master/crates/libs/windows/src/Windows/ApplicationModel/Core/mod.rs)
and [collection bindings](https://github.com/microsoft/windows-rs/blob/master/crates/libs/collections/src/bindings.rs).
No observable-vector IID is passed in this descriptor.

The requested `08c77958...` is a distinct private factory interface, not one
of those returned public collection IIDs. On activation success the helper
calls factory slot 30 (+0xf0) through RVA 0xb9758:
RCX is the factory, RDX the descriptor, and R8 the vector output.
Private factory slots 6-29, complete method semantics and creation-error
ownership are not established by this static call shape or the earlier
direct/catalog factory oracles.

The helper preserves failed activation HRESULTs at 0x3d19e and returns them
after cleanup at 0x3d1c1. Its two direct callers, 0x3b1e0 and 0xbfc80, also
preserve and return that failure (0x3b2f7 and 0xbff1a). The first wrapper
eventually calls the returned vector's GetView slot. The second is an
indirect native vtable entry; no public method name is asserted.
2200 did not capture the outer caller stack, so the executed consumer and
its linkage to terminal fail-fast remain unproven.

The existing enumeration vector helper supports the public collection
surface but does not provide this activatable private factory.
Builtin wintypes has no Detail.Vector factory branch. The parked ordinal-295
patch reads registration metadata and creates PropertySet attributes; it
neither registers nor implements Detail.Vector. Registration to the builtin
wintypes alone cannot repair the failure.

The smallest proposed discriminator is a separately authorized no-game
registration control using the existing owned genuine provider under a
distinct native basename, leaving the admitted nullable Wine PropertySet
provider selected. It must check factory identity, the verified slot-30
descriptor, public vector mutation/view/iteration and ownership, same-process
PropertySet selection/lifetime regressions, and restoration. This remains
a runtime-only native diagnostic, not a distributable implementation.
No further runtime operation was performed; unknown private slots were not
filled with guessed generic stubs.

#### Source-only native oracle candidate

`detail-vector-oracle-test.c` strictly compiles as x64 with the existing
compiler/import libraries (`-Wall -Wextra -Werror`, excluding unused
parameters in the reused nullable test/mocks). It includes the unchanged
50-check nullable PropertySet test in the same process. It has **not run**.
Its descriptor and slot-30 offsets have compile-time assertions; slots 6-29
are opaque uncalled storage, not synthesized method implementations.
Typed agile CoreApplicationView test doubles exercise non-null collection
ownership, not real windows or platform activation. Invalid descriptor/output
calls are excluded because their private contract is ungrounded.
Public empty-index E_BOUNDS and unknown-IID QI are separate grounded controls.

The private native artifact remains outside Git. A byte-identical alias is
staged at
`Y:\xodus-app-tooling\cuphead\detail-vector-candidate\xodus-vector-oracle.dll`.
It is 1,501,432 bytes, PE32+ AMD64 (8664/20b), SHA-256
`5a94b019c7190d9ec47f98e0bc28dced70036f1f97e9a84ef9d75340e364158a`;
DllGetActivationFactory is export ordinal 2/RVA 0x3eb20.
The only exact ASCII self-name is the PE export-directory DLL name; the
UTF-16 self-name is in version resources. This does not prove alias loading
works, but no executable fixed-name lookup is established by those strings.
No string, import, export or binary patch is applied.

The delivery plan forces only `xodus-vector-oracle=n` and keeps `wintypes=b`.
The test verifies separate module handles, exact loaded paths and SHA-256
files, the module owning slot 30, and the unchanged Wine wintypes handle/pin
before running the included nullable controls with the alias still loaded.
Any coexistence or required dependency failure stops the candidate, rather
than replacing the admitted Wine provider or adding loader workarounds.

Eager imports include CRT private functions, ordinary Win32 contracts,
ntdll and combase ordinal 90. Current Wine's API-set resolver hashes the
contract family before the final version component; absence of a physical
legacy shim is not a loader-failure finding. The actual host schema maps the
relevant families to ucrtbase, kernelbase, kernel32, ntdll and combase.
Delayed imports additionally include WinRT strings/errors/COM, rpcrt4,
bcryptPrimitives, package APIs, rometadata and oleaut32. Actual process
resolution remains to be observed with loader/module traces under a hold;
metadata/package calls and unexercised delayed imports are not claimed working.

Proposed admission leaves the 33 pinned provider-file targets unchanged.
Only the originally absent HKLM ActivatableClassId key for Detail.Vector
is added temporarily, with DllPath pointing at the staged native alias.
The alias is a pre-existing read-only candidate artifact, not an installed
vendor/bottle replacement. The bounded plan is absent-registration control,
direct factory control, registered catalog control and final absence check;
each process is limited to 40 seconds and 20 MB split captures. Standard
pre-gates precede it, followed by independently verified 33-target/key,
signature and quiescence restoration. No game or ordinal-295 addition is
part of this proposed cycle. No runtime hold has yet been granted.

| Source-only candidate | SHA-256 |
| --- | --- |
| Exact-descriptor test source | `112c0c399915665d421e0c39d8f9d915045d4664eb246f2b9b69a4fe18be35e5` |
| Strictly compiled test executable | `05894fa4c4aa4022f9c6db7071fd618fa381d388fc46b433bcadc01ad61cfb57` |
| Native eager/delayed import manifest | `012c70080d1adb2b0cf88c1069d543e5163e6c9239a8239a6dc2ac74212bb890` |

### Native Detail.Vector oracle 2205: coexistence verified, no game

The coordinator granted exactly one exclusive no-game cycle on the unchanged
33-target manifest and the read-only native alias. The owner enforced the
2195 receipt identity and target/candidate equality before provider swaps,
used Terminal/caffeinate, retained unique executed scripts, and applied
40-second/20 MB bounds to each split oracle capture. Standard pre-gates
preceded the four oracle processes. No game was launched.

| Oracle process | Observed result |
| --- | --- |
| Absent before registration | 5 checks, zero failures; factory CLASSNOTREG/null |
| Direct native export | 44 checks, zero failures; one grounded slot-30 call succeeds |
| Registered catalog | 44 checks, zero failures; one grounded slot-30 call succeeds |
| Absent after removal | 5 checks, zero failures; factory CLASSNOTREG/null |

**VERIFIED runtime:** both positive processes load
`xodus-vector-oracle.dll` as **native**, alongside **builtin**
`C:\windows\system32\wintypes.dll`. Separate handles and exact path/file
SHA-256 checks pass; the module owning the slot-30 method address is the
native alias. The catalog trace resolves Detail.Vector to that alias.
The only temporary catalog addition is its originally absent machine key.
No alias self-load conflict or required dependency failure occurs in the
exercised path; loader traces show builtin ucrtbase, combase, ole32 and
rpcrt4. Unexercised delayed-import behavior remains unknown.

The factory GetIids returns exactly IActivationFactory and the private
08c77958 IID. Their IUnknown identities agree; a balanced AddRef/Release
pair reports 5/4. Each verified descriptor call returns S_OK/non-NULL and
runtime name
`Windows.Foundation.Collections.IVector`1<Windows.ApplicationModel.Core.CoreApplicationView>`.
Public Append/GetAt/IndexOf/SetAt/InsertAt/RemoveAt/Clear, GetView, iterable
and iterator controls pass. Views/iterators retain the required ownership
after releasing the originating interfaces. Mock references settle to
one each, then final Release reaches zero and each destruction count is one.
Empty GetAt returns E_BOUNDS/null. No private slots 6-29 or invalid private
descriptor/output calls were made.

All four processes also pass the **same-process nullable PropertySet
50-check test**, with the admitted Wine wintypes path/pin and handle
unchanged before/after native use. The native alias stays loaded through
the positive-process nullable checks; Wine PropertySet is not replaced.
The largest diagnostic capture is 448,800 bytes, below the bound.

The repeated pre-gates pass: CM retry/error 35/0; async backend-error 23/0
in this cycle; original/augmented cfgmgr 1292/1369 tests, zero failures,
one TODO each; original/augmented enumeration 1938/1987 tests, zero
failures, 91 TODOs each; full wintypes 1120 tests, zero failures, 49 TODOs;
nullable 50/0, folders 96/0, HighContrastChanged 33/0, and cstub 248 tests,
zero failures, two TODOs. Standard transport/dispatcher/proxy controls
also complete successfully.

Owner restoration has no errors. Independent verification confirms all
33 baseline hashes/absences, all temporary keys including Detail.Vector
and synthetic fixture absence, original WineDbg/CoreApplication settings,
deep/strict signature, no Wine runtime and no holders under .wineserver.
All target/candidate/baseline triples match 2195; the alias remains
byte-identical outside Git/vendor/bottle. The exclusive hold is released
and the no-game grant consumed.

This is an authentic native oracle and coexistence result, **not**
a distributable private-interface implementation or actual-game progress.
It does not establish the executing twinapi consumer, fail-fast causality
or a first frame. A further game discriminator requires a separate grant;
registration-295, events and graphics remain outside this candidate.
The missing mandatory quality runner remains an integration limitation.

| 2205 private evidence | SHA-256 |
| --- | --- |
| Receipt | `03687e15b73c486de6af84734e00350f44462c10b27ac6351d37fff1e69a4bc9` |
| Independent restoration proof | `f7758fc17a004f443fd3b1dd97928465ee420420b2d2059ad6a0971f72d57312` |
| Direct oracle stdout | `46428af3a9218425bf0a8b91adb97ded408bc5272e6ae7d4497b0ba63a37d7f5` |
| Direct loader stderr | `d7fae60466ff1e0ac3ccd7632fd8d3cda18ac95eea641f9294ba68c91277af0f` |
| Catalog oracle stdout | `da850b7def1a06470f965b8ed2e51bbd0d0c796d4a976e7cafa1d6858fd99a1e` |
| Catalog loader stderr | `560f7e9b87802bee8ce23a543b6a74282db5b201153d4a23244a1e639e8ab609` |

### One-game native-reference 2210: factory resolved, fail-fast unchanged

After 2205 persistence (`6a394d3`), the coordinator authorized one bounded
Cuphead launch with the identical 33 providers plus only the verified alias
selection/temporary Detail.Vector registration. The owner pinned the 2205
receipt/proof and native alias, retained the standard pre-gates and used
Terminal/caffeinate with unchanged split game captures. No registration-295,
events, graphics or guessed factory implementation was added.

**VERIFIED product delta:** thread 01a8 loads the native alias and its exact
08c77958 factory succeeds twice. The first return is S_OK at twinapi+0x3d199;
the second is S_OK at twinapi+0x3b499. Native descriptor strings are duplicated
for both CoreApplicationView and a subsequent Object vector. The latter
specialization was not part of the synthetic oracle, and its complete
collection semantics are not asserted. These native construction traces and
subsequent ApplicationView activity demonstrate progress beyond the original
CLASSNOTREG boundary; no direct slot-30 return instrumentation is present.
No 80040154 activation return remains in this capture.

The actual game still exits 4103 after 2415 ms, zero total/visible windows.
The original activation controller reports 13 checks and zero failures, not
gameplay success. cuphead.App's native callback is registered, but no invocation
is observed; no RoGetActivatableClassRegistration or swapchain creation is
observed. The original UnityPlayer.log remains 201 bytes of metadata and is
retained as a unique snapshot, unchanged from 2200.

The terminal fingerprint is again reason 1007 at SharedLibrary+0x41cf88 on
the MTA (01a8). The last MTA operation is successful FTM-backed GIT marshaling
of IID e2663f37, returning cookie f101,
then RaiseFailFastException(record 0x10e5a8, NULL context, flags 1).
No intervening HRESULT or e06d7363 exception is captured. The pointed-to
record and managed/native transition caller remain unavailable. The earlier
recovered missing MRM export remains present and is not newly attributed as
the cause. This repeats the earlier terminal boundary despite successful
Detail.Vector activation; an unchanged launch cannot identify its cause.

**FAILED capture receipt:** the owner reaches its existing first-wait/log
bound before polling the controller's final stdout, because server.log crosses
100 MB. Preserved stdout nevertheless contains the actual app exit/window
result. Cleanup yields a final server log of 154,171,367 bytes and diagnostic
stderr of 38,640,360 bytes. The owner error remains
`RuntimeError('Bounded first-wait/log limit')`; it is not relabeled PASS and
the bound was not relaxed or the launch replayed.

Owner restoration completes without errors. Independent verification confirms
all 33 baseline hashes/absences, exact 2195 target/candidate/baseline triples,
Detail.Vector and prior temporary key/fixture absence, original
WineDbg/CoreApplication settings, strict signature, no runtime and full
.wineserver holder quiescence. The native alias is unchanged. The single
grant is consumed and exclusive hold released. No second launch is authorized;
the lane stops unchanged-fingerprint retries pending a discriminating
exception-record/transition-caller observation or other fresh evidence.
This native-reference experiment is not distributable support or a first frame.
Mandatory quality-runner absence remains a reported integration limitation.

| 2210 private evidence | SHA-256 |
| --- | --- |
| Game stdout | `e068dd5e4be8592c06e8d579f75cf70a9620745d9f8c4d518b45ba238130ee53` |
| Game stderr | `49031520ad4ee8904a02bc9c1dee386c7d41d1df3efec2f14bd919218656a6b0` |
| Server log | `e6fd72452e6fa74c69dba4802e9c286ef32ecd9b07d9e01226994d0cc1a97b83` |
| Failed bounded-capture receipt | `75cfd9cc38e1d463823f9ae45b327d00ab9d615e51dc41d934666217ca4c5953` |
| Independent restoration proof | `6de03725d2abde8aff385d446c83c798cebea5fd699510574905c56d469d5d4e` |
| Preserved UnityPlayer.log | `7c396f108b1c46506fed6eda2a1283cf33ffb90695825a8b2b2102c37063d71d` |
