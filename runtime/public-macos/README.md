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
