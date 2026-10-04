# Public macOS host build probe

This exact-source overlay removes concrete compilation/link failures in public
Wine `eab69739f15180b96797645ccade44c1c4414980`. It does not use private runtime
code, enable Linux kernel interfaces on macOS, or certify a playable runtime.
Original upstream LGPL notices remain in every changed source file.

`apply_platform_fix.py` requires the exact revision and seven original source
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
application, dry-run preservation, all-seven-file updates, repeated/changed
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
frameworks/libraries. No actual TLS peer or certificate validation was tested.

The complete configured build is still separate from these successful targets.
Its Linux DRM-dependent `amd_ags_x64` extension is explicitly disabled on this
Mac candidate, not made to compile using invented Linux headers. Vulkan,
GStreamer and actual graphics/TLS/gameplay remain unverified. A complete make
result, if obtained, does not establish licensed operation or an approved pair.
