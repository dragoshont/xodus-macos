# Public macOS host build probe

This exact-source overlay removes concrete compilation/link failures in public
Wine `eab69739f15180b96797645ccade44c1c4414980`. It does not use private runtime
code, enable Linux kernel interfaces on macOS, or certify a playable runtime.
Original upstream LGPL notices remain in every changed source file.

`apply_platform_fix.py` requires the exact revision and ten original source
blobs. Selected checkout/ancestor aliases, source aliases and modified files
are refused before application. Default operation verifies without writing.
The patch input is newline-normalized so Windows transfers work correctly.

```sh
python3 apply_platform_fix.py /absolute/owned/public-wine
python3 apply_platform_fix.py /absolute/owned/public-wine --apply
python3 tests.py /absolute/owned/public-wine
```

The checks clone only the supplied public Git objects into new private temporary
fixtures; they do not read credentials or run Wine. They exercise actual patch
application, dry-run preservation, all-ten-file updates, repeated/changed
source rejection, wrong revision and directory/source alias rejection.

The overlay guards Linux futex and ntsync interfaces, retaining the existing
server-side synchronization path on macOS. Unsupported futex calls return
`ENOSYS`, and an incompatible fsync initialization still fails explicitly.
It guards glibc allocator perturbation and the fork's extra Linux-only early
TEB setup; the original macOS pthread/GS setup remains intact. It corrects an
indirect AT&T assembly branch. Linux-only automatic/performance-core topology
overrides report an explicit error on other platforms; manual mappings and
ordinary macOS CPU discovery remain unchanged.

## Observed native build

An isolated x64 macOS cross-build used the existing arm64 Wine build tools and
`clang -arch x86_64`, not arm64 binaries mislabeled as an x64 host:

```sh
CC='clang -arch x86_64' CXX='clang++ -arch x86_64' \
OBJC='clang -arch x86_64' /absolute/owned/public-wine/configure \
  --build=aarch64-apple-darwin --host=x86_64-apple-darwin \
  --with-wine-tools=/absolute/owned/public-wine-arm-build \
  --disable-tests --without-x --without-wayland --without-vulkan \
  --without-gstreamer --enable-archs=x86_64
make -j2 loader/wine server/wineserver dlls/ntdll/ntdll.so
```

The first configure attempt correctly rejected missing **x64** FreeType.
FreeType 2.14.3 was then built into an owned dependency prefix from its verified
public source archive
`36bc4f1cc413335368ee656c42afca65c5a3987e8768cc28cf11ba775e785a5f`.
The probe uses system zlib and disables optional PNG, bzip2, HarfBuzz and Brotli.
Configure needs explicit matching `FREETYPE_CFLAGS`/`FREETYPE_LIBS`; no global
package or system configuration was changed.

The actual loader/server/ntdll make targets succeeded. All three outputs are
Mach-O x86-64, with these individual observed SHA-256 values:

| Component | SHA-256 |
|---|---|
| Loader | `adb8da9707ee37f1567612032f62970320cd12a057ee3a3dcd258d95b7827ab0` |
| Server | `4bf94583f1ddea04422a9e4e93ecbbd1516f968f5a624279ca52948c9dd8c780` |
| Native ntdll | `10e18deeed67494ad2f885092ad7504e433ed6cfe091df48220137a7d4fb96c9` |

These identify one build, not reproducible-build guarantees. The targets import
only inspected system libraries/frameworks, but configure explicitly reports
**no Schannel support** because matching GnuTLS development files are absent.
Other DLLs, graphics, fonts, TLS, a controlled prefix, the Windows-branch RPS
execution, service/profile pairing and licensed gameplay remain separate gates.
No Wine loader, game or real credential operation was executed for this build.
Do not bundle or advertise these partial outputs as a supported runtime.

## Subsequent TLS dependency build

The missing x64 GnuTLS dependency was subsequently built from verified public
source into the same owned prefix, without global package installation:

| Source | Archive SHA-256 |
|---|---|
| GMP 6.3.0 | `a3c2b80201b89e68616f4ad30bc66aee4927c3ce50e33929ca819d5c43538898` |
| Nettle 4.0 | `3addbc00da01846b232fb3bc453538ea5468da43033f21bb345cb1e9073f5094` |
| libtasn1 4.21.0 | `1d8a444a223cc5464240777346e125de51d8e6abf0b8bac742ac84609167dc87` |
| libunistring 1.4.2 | `e82664b170064e62331962126b259d452d53b227bb4a93ab20040d846fec01d8` |
| GnuTLS 3.8.13 | `ffed8ec1bf09c2426d4f14aae377de4753b53e537d685e604e99a8b16ca9c97e` |

GMP/Nettle use portable C rather than optional assembler. The probe disables
GnuTLS tools, tests, documentation, translation, C++/Guile, IDN and PKCS#11
integrations; these omissions must not be represented as supported features.
The actual GnuTLS dylib is Mach-O x86-64.

Cross-configure requires explicit `PKG_CONFIG=/opt/homebrew/bin/pkg-config` and
`PKG_CONFIG_LIBDIR` pointing only at the matching owned prefix. The subsequent
Wine configure actually accepts the GnuTLS header and cipher symbol and defines
`SONAME_LIBGNUTLS`; the previous no-Schannel warning is gone. Native secur32 and
the PE ntdll, kernelbase, kernel32, ucrtbase, ws2_32 and patched xgameruntime
targets then compile. Cross resource tools and their public locale data must
also be built, rather than borrowed from an installed/private runtime.

`tls_dependency_checks.c` checks the real dependency's minimum version,
SHA-256 known answer and default TLS priorities **offline**, without accessing
accounts, opening network connections or executing Wine:

```sh
clang -arch x86_64 -std=c11 -Wall -Wextra -Werror -pedantic \
  -I/absolute/owned/dependencies/include tls_dependency_checks.c \
  -L/absolute/owned/dependencies/lib -lgnutls \
  -o /absolute/owned/output/tls-dependency-checks
/absolute/owned/output/tls-dependency-checks
```

All three checks passed in the actual x64 executable. Inspection confirms its
GnuTLS dependency and GMP/Nettle/libtasn1/libunistring dependencies are x64
Mach-O libraries in the owned prefix, with remaining imports in system
frameworks/libraries. At that stage, no actual TLS peer or certificate
validation was tested.

The complete configured build is still separate from these successful targets.
Its Linux DRM-dependent `amd_ags_x64` extension is explicitly disabled on this
Mac candidate, not made to compile using invented Linux headers. Vulkan,
GStreamer and actual graphics/TLS/gameplay remain unverified. A complete make
result, if obtained, does not establish licensed operation or an approved pair.

The full build exposed another genuine feature-guard failure: `win32u/opengl.c`
referenced EGL-only framebuffer functions even when EGL was not compiled.
The overlay now keeps framebuffer detection/target unwrapping behind the
existing EGL feature guard and uses ordinary native drawables without EGL.
The EGL framebuffer/scaling path remains unchanged when enabled; absent EGL
does not provide that emulation. The actual non-EGL OpenGL object compiles, the
EGL-enabled branch passes a separate real-header compiler syntax check, and all
six application/refusal checks pass with the eighth exact source blob.
These are compile/application checks, not rendered graphics or gameplay proof.

The shared media-converter header also now explicitly includes `pthread.h`
for its `pthread_mutex_t` member instead of relying on transitive headers.
This fixes actual winedmo compilation on macOS even with GStreamer disabled;
it does not enable GStreamer or validate media playback.

## Complete configured build result

`make -j2 all` subsequently succeeded for the configured public x64 candidate.
The canonical loader, server, native ntdll, native Mac driver and native
secur32 are actual x64 Mach-O binaries; the build contains 613 PE DLL files,
including the patched xgameruntime. The final configure reports only the
expected cross-tool naming warning, not missing Schannel.

This candidate additionally uses `--disable-loader64`: the fork's optional
duplicate legacy loader has a broken macOS plist dependency. The canonical
`loader/wine` remains enabled; no `wine64` alias is advertised. The disabled
Linux DRM AMD extension, Vulkan, GStreamer and optional TLS integrations
remain explicit limitations, not working features.

| Final observed component | SHA-256 |
|---|---|
| Canonical loader | `ea89187cb5aebca27d23c3258be80f6ba227ae72ef80706b51a287e4d2c19b17` |
| Server | `f0177c5ebec2af86cf07fee16b0af18442db85db40f41c4e6c2f9b40e85a3cbb` |
| Native ntdll | `c6108ebe9b105aebc2373a7923bc04c00c094d94258ba8bee875341d71a94288` |
| Native Mac driver | `524583953de59da68d7e0c8af3f8df5e52747e31ad9ea8793457d3a8ed9a2041` |
| Native secur32 | `b0efbacaa927056db3e029a82b68cf7fa02bfad1a8f6ecbeed5f3870bf9b44c0` |
| Patched PE xgameruntime | `de32abfaac587ea36eb843a825a7825b1f4db3271c8bef0caaeebd9955710390` |

These final outputs differ from the earlier isolated component builds. No
Wine loader, Windows-branch RPS exchange, prefix, graphics, TLS peer, game or
real credentials were executed to obtain this result. A successful complete
**configured build** is not a supported runtime pair or licensed-gameplay
certificate, and this candidate is not wired to the app's Play capability.

## Subsequent isolated Windows client execution

After the source review, the canonical public loader/server actually executed
an original Windows console client in a new private prefix. Four synthetic-peer
checks passed twice: successful fragmented RPS framing, malformed XML, expired
tickets and a stalled-response deadline. Three actual path/permission guard
refusals also passed. The subsequent ownership correction adds PID-bound
socket readiness and Wine's client-killing shutdown; exact native ntdll mapping
checks confirm the corrected runs leave no selected background clients.
See [the client checks](../public-rps/README.md) for source, commands,
scope and the exact PE executable hash.

This exercises the client's actual Winsock DOS/ACP-to-Unix mapping, not a live
gaming-shim/scoped-broker account exchange. No real credentials, consent, license,
package or game was involved. Runtime/service version pairing, graphics and
licensed gameplay remain open; Play must remain gated.

## Actual Windows HTTPS certificate checks

`windows_tls_smoke.c` is an original PE console client using actual WinHTTP and
Crypt32 through the public Wine loader, native Schannel and owned x64 GnuTLS.
`check_windows_tls.py` reuses the reviewed RPS runner's private-prefix lifecycle;
it does not create a second server-ownership implementation. The default RPS
callback still executes its original four cases.

The first actual run exposed missing dynamic-library resolution despite a
successful GnuTLS configure check. The shared runner now accepts an explicit
`--library-directory` below its owned root. The selected directory and its
ancestors up to that root must be non-aliased, current-user-owned and not
group/world writable. Only the owned Wine children's
`DYLD_FALLBACK_LIBRARY_PATH` is set; no global library configuration or private
runtime is used. No implicit dependency-prefix discovery is performed.

The next run independently reproduced a public WinHTTP error-propagation bug:
the handshake succeeded and certificate verification reported invalid CA
`12045`, but the failed branch returned generic secure-channel error `12157`.
The tenth guarded source change preserves the actual verification error.
Handshake failures without a more specific result still return the original
secure-channel error. No validation policy, certificate-ignore flag or trusted
success path is weakened.

From the configured public build directory, after applying the exact-source
overlay and rebuilding `dlls/winhttp/all`:

```sh
tools=/absolute/owned/public-wine-tools
source=/absolute/owned/public-wine
probe=/absolute/owned/runtime/public-macos
root=/absolute/owned/output

"$tools/tools/winegcc/winegcc" -b x86_64-w64-mingw32 \
  --wine-objdir . --winebuild "$tools/tools/winebuild/winebuild" \
  -std=c11 -Wall -Wextra -Werror -pedantic -D__WINE_PE_BUILD \
  -isystem "$source/include" -isystem "$source/include/msvcrt" \
  "$probe/windows_tls_smoke.c" -lwinhttp -lcrypt32 -lkernel32 -lucrtbase \
  -o "$root/xodus-windows-tls-smoke.exe"

python3 -B "$probe/check_windows_tls.py" "$root" \
  "$root/public-wine-build-macos-x64/loader/wine" \
  "$root/public-wine-build-macos-x64/server/wineserver" \
  "$root/xodus-windows-tls-smoke.exe" \
  --library-directory "$root/public-host-dependencies/x64/lib"
python3 -B "$probe/test_tls_peer.py"
```

The loopback fixture generates a one-day RSA-2048 localhost certificate and
owner-only private key in the new private prefix. Its single non-daemon HTTP
worker uses TLS 1.2 or newer; shutdown interrupts its owned active connection
before a bounded join. Two real socket regressions cover stalled TLS handshakes
and incomplete HTTP headers, without Wine or host trust-store writes.

Three actual Windows cases passed: unknown CA rejected with `12045`, explicitly
trusted owned certificate accepted with HTTP 200 and exact bounded body, and
wrong hostname rejected with `12038`. The helper imports only the generated
public DER into the private Wine prefix's current-user registry ROOT store,
never the native macOS trust configuration. Proxy use, authentication, cookies
and redirects are disabled; certificate-ignore flags are never enabled.
A separate native TLS request controls fixture readiness, not Windows validation.

| Observed PE component | SHA-256 |
|---|---|
| Original TLS check | `4cd7da9b607d17bb52d17ad46a919d214329a73fe28d93abefff66c3732b015d` |
| Corrected WinHTTP DLL | `ce7e92f95ef993778c2656fcfe69ff8e862f6b7796ba1ab9885d428cddee37ff` |

All four actual RPS cases still passed after callback extraction. The eighteen
Darwin runner checks cover the existing ownership regressions plus callback
completion/failure and explicit dependency-directory guards. Exact selected
native ntdll mapping checks confirm no Wine clients remain after the runs.
Hosted checks cover the native client, runner, TLS-peer shutdown and guarded
public patch application; they do not substitute for these owner-run Wine cases.

This proves synthetic loopback HTTPS validation in the selected public
candidate, not Microsoft endpoint coverage, account authorization, a complete
runtime/shim/service pair, rendered graphics or licensed gameplay. No account,
real game or private runtime was used; neither native trust configuration nor
the launcher's Account flow was changed.
Play remains gated, and this candidate is not a certified distribution.
