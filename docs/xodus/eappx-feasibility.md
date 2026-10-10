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
