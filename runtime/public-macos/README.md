# Public macOS host build probe

This exact-source overlay removes concrete compilation/link failures in public
Wine `eab69739f15180b96797645ccade44c1c4414980`. It does not use private runtime
code, enable Linux kernel interfaces on macOS, or certify a playable runtime.
Original upstream LGPL notices remain in every changed source file.

`apply_platform_fix.py` requires the exact revision and eleven original source
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
application, dry-run preservation, all-eleven-file updates, repeated/changed
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
Resolved paths containing `:` are refused before prefix creation or process
launch, so a directory name cannot inject unvalidated dyld search paths.

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
The PE helper also requires Wine's actual ntdll export and an explicit absolute
`WINEPREFIX` before certificate access or HTTP. Its trust-write invocation was
executed on native Windows and refused with exit 1 before opening the fixture;
the same executable still passes all three Wine cases.

| Observed PE component | SHA-256 |
|---|---|
| Original TLS check with host refusal | `2b5ad08d4abdde7895e99b029a49a4ed9b7f649700f869e3b8b4cddf1cf82d49` |
| Corrected WinHTTP DLL | `ce7e92f95ef993778c2656fcfe69ff8e862f6b7796ba1ab9885d428cddee37ff` |

All four actual RPS cases still passed after callback extraction. The twenty
Darwin runner checks cover the existing ownership regressions plus callback
completion/failure and explicit dependency-directory guards. Exact selected
native ntdll mapping checks confirm no Wine clients remain after the runs.
Hosted checks cover the native client, runner, TLS-peer shutdown and guarded
public patch application; they do not substitute for these owner-run Wine cases.

The retained adversarial reviewer closed the dependency-path finding after
`861740ae8f873f08864f8d7e8dc98b0ad9cc7d0b`; both real colon-directory regressions
fail against the preceding source and refuse before launch with the correction.
Exact-head hosted [client/runner/peer checks](https://github.com/dragoshont/xodus-macos/actions/runs/37175609564)
and [ten-source overlay checks](https://github.com/dragoshont/xodus-macos/actions/runs/37175609551)
passed. All findings R01-R16 are closed in the retained review ledger.
That closure covers the reviewed components, not the remaining end-to-end
account, package, installation and gameplay journey.

This proves synthetic loopback HTTPS validation in the selected public
candidate, not Microsoft endpoint coverage, account authorization, a complete
runtime/shim/service pair, rendered graphics or licensed gameplay. No account,
real game or private runtime was used; neither native trust configuration nor
the launcher's Account flow was changed.
Play remains gated, and this candidate is not a certified distribution.

## Actual Windows offscreen graphics checks

`windows_graphics_smoke.c` is an original PE client using only its own hidden
32x32 window, WGL contexts and explicitly allocated RGBA8 framebuffer storage.
It creates a legacy context and, in a separate invocation, requests a
forward-compatible core context of at least OpenGL 3.2 and verifies its profile.
Each context clears its own framebuffer and reads one of its own pixels.
All four channels must match `[64, 128, 191, 255]` within one byte.
Missing entry points, incomplete storage, initialization/operation errors and
resource-release failures reject the check. Errors are not cleared to obtain
a passing result.

The retained adversarial review of source commit
`4e53e4b25be3aba24b0574e35c7ed6363159b61c` found no significant issue in
this bounded delta or its evidence limits; R01-R16 remain closed.
Exact-source hosted [client checks](https://github.com/dragoshont/xodus-macos/actions/runs/37178856214)
and [eleven-source overlay checks](https://github.com/dragoshont/xodus-macos/actions/runs/37178856223)
both passed. The reviewer reran the seven mocked Python checks, not the actual
GPU cases. Review closure does not certify the remaining product journey.

The first real execution exposed a launch-context prerequisite: SSH's security
session returned attributes `0x5020`, without `sessionHasGraphicAccess` (`0x10`).
An owned transient current-user GUI job returned `0x6030`. The checker now
refuses missing graphics access before creating a prefix or launching Wine.
Running as the same UID is not a substitute for the graphical security session;
the failed `launchctl asuser` attempt was not escalated or bypassed.
The normal native launcher inherits its graphical login session.

The proper session then exposed genuine public OpenGL initialization defects.
The overlay queries profile masks only for OpenGL 3.2 or newer, initializes
legacy profile state, and queries `GL_TEXTURE_BINDING_2D`, not the texture
target. Raw legacy drawables without split-framebuffer support no longer receive
unsupported split bindings. Default-buffer restoration removes unused trailing
draw buffers and uses the single-buffer API when appropriate; a `GL_BACK`
selection is not submitted with the driver's full eight-buffer capacity.
Unused framebuffer-surface gamma shaders stay behind the existing EGL feature
boundary. Both EGL-enabled source branches pass strict actual-compiler syntax
checks; their execution and gamma/scaling behavior were not tested.

The hidden window's native default framebuffer remained unavailable
(`GL_INVALID_FRAMEBUFFER_OPERATION`, `0x506`). The passing check therefore
explicitly targets newly allocated **offscreen** storage. It never shows or
activates a window, swaps its buffers, uses a desktop DC or reads another
application's pixels. This is not window-presentation evidence.
Temporary error-consuming diagnostic checkpoints were removed and the native
libraries rebuilt before the normal, uninstrumented checks passed.

From the configured public build directory, after applying the overlay and
rebuilding `dlls/win32u/all` and `dlls/opengl32/all`:

```sh
"$tools/tools/winegcc/winegcc" -b x86_64-w64-mingw32 \
  --wine-objdir . --winebuild "$tools/tools/winebuild/winebuild" \
  -std=c11 -Wall -Wextra -Werror -pedantic -D__WINE_PE_BUILD \
  -isystem "$source/include" -isystem "$source/include/msvcrt" \
  "$probe/windows_graphics_smoke.c" -lopengl32 -lgdi32 -luser32 \
  -lkernel32 -lucrtbase -o "$root/xodus-windows-graphics-smoke.exe"

# Developer check from a graphical login session, not an SSH audit session.
python3 -B "$probe/check_windows_graphics.py" "$root" \
  "$root/public-wine-build-macos-x64/loader/wine" \
  "$root/public-wine-build-macos-x64/server/wineserver" \
  "$root/xodus-windows-graphics-smoke.exe" \
  --library-directory "$root/public-host-dependencies/x64/lib"
python3 -B "$probe/test_graphics_session.py" -v
```

Both actual legacy/core cases passed the byte threshold. Seven native tests
cover session admission/refusal, both exact outcomes, wrong/missing mode
markers and process-failure propagation. Twenty existing ownership checks,
two TLS-peer checks and all six eleven-source application/refusal checks pass.
All four actual RPS cases and three actual HTTPS cases still pass after the
OpenGL changes. Exact selected native ntdll mapping checks find no remaining
Wine clients, and owned transient GUI jobs are removed only after they exit.

| Observed unsigned graphics component | SHA-256 |
|---|---|
| Original PE check | `51b38697a6e5afdf4814aa7f65f2a79172f202fbe7561c424e6be04f5b378e64` |
| Native win32u | `ea41be9d342ecaf1388d6f8f7505859042b14a345bd1be84e39e9b09655e72de` |
| Native opengl32 | `601e82ccea5a5970581c3cd7241ed1be9ac5de7f68eb7ec04f3e9b1a16b42c0b` |

These are owned x64 artifacts from the configured public candidate, not
reproducible-build guarantees or a certified runtime. Hosted tests cover the
source guards and session/callback logic, not actual Wine GPU execution.
Window presentation, production graphics coverage, current Store authorization,
entitled installation, runtime/service pairing, licensed gameplay and signed
distribution remain separate gates. No credentials, real game, private runtime,
host graphics configuration or launcher Account flow was used or changed.

## Subsequent public gaming async boundary

The separate [`../public-async`](../public-async/README.md) candidate adds the
pinned public Microsoft XAsync/XTaskQueue core to the development shim.
Actual tests now reach the gaming COM interface and its controlled RPS user-add
failure path; they are no longer solely standalone-client evidence. This is
still not successful Store/account authorization, licensed gameplay, a supported
service/runtime pair or signed distribution. The shared owned Wine lifecycle
and the existing graphics limitations remain unchanged.

## Owned default drawable and buffer-swap API

The graphics helper additionally accepts `--present` and `--present-core`.
These explicitly create and show only an owned 32x32 `WS_EX_NOACTIVATE`
tool window at `HWND_BOTTOM`, using `SWP_NOACTIVATE`. The public Mac driver's
corresponding nonactivating-panel path does not request application activation.
A bounded message pump and guest foreground/active-window assertions reject
observed activation. The original hidden offscreen modes remain separate.

Each default-drawable case requires a complete framebuffer zero, the selected
double-buffered RGBA8 format, an owned `GL_BACK` pixel matching
`[64, 128, 191, 255]` within one byte, successful `SwapBuffers`, no GL error and
normal resource release. There is no `GL_FRONT`/desktop/compositor capture or
fallback from failed default storage to passing offscreen storage.
This establishes an owned default drawable and buffer-swap **API** outcome,
not physical presentation, broader production compatibility or gameplay.

`foreground_pid.swift` reads only the native foreground application's PID,
without names, application contents, screenshots, accessibility access or
consent prompts. `check_graphics_foreground.py` runs both offscreen cases and
both default-drawable cases using the unchanged reviewed prefix lifecycle.
Native PID snapshots before and after each owned process, including bootstrap,
must match; invalid/unavailable metadata or a changed PID rejects the check.
The original process failure remains in the exception chain if the final
snapshot also fails. Snapshot comparison is **not continuous** evidence that
no intermediate activation occurred, and the checker never restores focus.
The selected metadata helper and its ancestors through the owned root must be
non-aliased, current-user-owned and not writable by other users.

Build the updated PE source using the existing strict command above, changing
only its output to `$root/xodus-windows-default-drawable-v1.exe`. Run the
following only from the normal graphical login session:

```sh
xcrun swiftc "$probe/foreground_pid.swift" -o "$root/foreground-pid-check"
python3 -B "$probe/check_graphics_foreground.py" \
  --foreground-helper "$root/foreground-pid-check" "$root" \
  "$root/public-wine-build-macos-x64/loader/wine" \
  "$root/public-wine-build-macos-x64/server/wineserver" \
  "$root/xodus-windows-default-drawable-v1.exe" \
  --library-directory "$root/public-host-dependencies/x64/lib"
(cd "$probe" && python3 -B -m unittest \
  test_graphics_session test_default_drawable test_graphics_foreground -v)
```

The updated PE source compiled with the actual public Win64 toolchain. A normal
current-user GUI job passed both default-drawable cases; a subsequent four-mode
run also passed the original offscreen cases and all native snapshots.
All four cases passed again using the immutable archived binaries; the
separate `public-default-drawable-v1-evidence` archive preserves that final
run and its independent cleanup evidence.
Each completed job has a numerical zero exit, no active job PID, and is removed
only after exit. Independent selected native ntdll mapping checks return
status 1, no PIDs and no stderr. SSH admission still refuses before Wine startup.
Twenty-three native admission/outcome/foreground checks pass on the Mac; Windows
runs twenty-one and explicitly skips the two POSIX filesystem fixtures.
Hosted checks compile the Swift collector and exercise these guards, not GPUs.

| Observed unsigned component | SHA-256 |
|---|---|
| Updated original PE helper | `9f2ce44810ab00ff1929ff94751923f7a3a56b6cca29bae377237f01a6ba3b05` |
| Metadata-only native collector | `9095bfe92207ef3d0de8a406095f59bcaf3a5249a609eced1f7cc23bda7e7fef` |

These identify the owned `artifacts/public-default-drawable-v1` binaries, not
reproducible-build guarantees or a distribution. Their original GPL sources,
license and actual logs are preserved with the binaries. The captured bootstrap
log includes working-directory diagnostics and the x64-only candidate's missing
WOW64 `rundll32` startup diagnostic; this does not establish 32-bit support.
Earlier immutable offscreen and async artifacts are unchanged. No personal
account, package, service/profile pairing, real game, private runtime, approved
app/engine replacement or host graphics configuration was used or changed.
Play and the remaining licensed journey stay gated. The same retained review
of `da6f50c423f5cb29df1a3dcd02ca0f6dd0188baf` identified R18: lexical helper
containment admitted a sibling path spelled with `..`. The correction preserves
symlink refusal, canonicalizes both existing paths before containment and
checks canonical ancestry. Its real-filesystem regression requires both direct
and traversing sibling paths to fail before runner/helper execution.
The new traversal regression fails against the exact published old function
and passes with the correction; no helper is executed in either fixture.
The same retained report 37 closes R18 at
`1acf8939375fc4d4774e8be1deeb3da0508f148f`, with no significant issue in the
correction. Exact-head hosted [native checks](https://github.com/dragoshont/xodus-macos/actions/runs/37184632412)
and [public-source overlay checks](https://github.com/dragoshont/xodus-macos/actions/runs/37184632429)
pass. All R01-R18 scoped findings are closed; this does not approve unpublished
runtime-pair fixtures or the remaining authorized installation/licensed journey.
