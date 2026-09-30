# Wine and xgameruntime integration options

Status date: 2026-10-01

## Initial decision

Start with **Option A: CrossOver Wine plus out-of-tree xgameruntime**, using
xgameruntime PR
[#18](https://github.com/xodus-gaming/xgameruntime/pull/18) pinned at:

```text
8dd2aa0044c0fc72d600fbe80f3b1e201e3181fb
```

This is the smallest path that preserves the verified CrossOver/D3DMetal stack
and can reach the current Xodus host service without first rebuilding Wine.

## Option A result

Stock CrossOver 26.3 does not provide the Windows AF_UNIX behavior required by
PR #18. The repository's `scripts/macos/test-wine-af-unix.sh` builds a minimal
Windows client, starts a native Unix-domain socket server, and attempts a
`PING`/`PONG` exchange through CrossOver. The Windows `socket(AF_UNIX, ...)`
call returned `INVALID_SOCKET` with Winsock error `10047`
(`WSAEAFNOSUPPORT`; test exit code 11).

Therefore Option A is blocked before xgameruntime or GDK semantics are involved.
The next implementation path is **Option B: apply the already-public Xodus Wine
AF_UNIX patch to the matching CrossOver source and build a private local
runtime**.

Xodus Wine commit:

```text
6b7313c1bd !7650: Add support for AF_UNIX sockets
```

The patch changes seven Wine files and applies cleanly to the CrossOver 26.3.0
source tree. This result is only an applicability check; no patched CrossOver
runtime has been installed into CrossOver.

An isolated source build is reproducible with:

```bash
./scripts/macos/build-crossover-wine-af-unix.sh
```

The script never modifies `/Applications/CrossOver.app`.

An isolated full build was attempted. An arm64-host build reached
`dlls/winemac.drv` and failed because the published CrossOver source declares
`WineMetalLayer` only for an x86_64 host. An x86_64 build under Rosetta could
not start Apple's compiler because the installed Command Line Tools package
contains an arm64-only `libxcrun`. Producing a compatible private runtime
therefore needs the appropriate full Xcode/universal host toolchain or
CodeWeavers' supported build environment; it is not a one-file drop-in build.

Further iteration removed both of those configure blockers:

- native arm64 Clang can cross-compile x86_64 macOS host binaries;
- bundled FreeType was built as an x86_64 dylib;
- an x86_64-host CrossOver Wine configuration completed;
- the Wine loader, wineserver, ntdll, ws2_32, wineboot, and required core
  console DLLs/programs compiled with commit `6b7313c1bd` applied.

The remaining full-build failure is an unrelated CrossOver Vulkan source
assumption (`SONAME_LIBVULKAN` while configured without Vulkan).

A transport-only mixed-runtime experiment starts the patched loader and server
against the disposable stock bottle, but patched `ws2_32.dll` faults during
`PROCESS_ATTACH`:

```text
NtRaiseException: Exception frame is not in stack limits
```

This proves that swapping only ws2_32/server/ntdll into a stock CrossOver
bottle is not a safe integration mechanism. The next experiment needs a
coherent private Wine runtime build, including CrossOver's expected graphics
and compatibility modules, rather than mixed binaries.

Do not implement or upstream GDK semantic behavior from this AI-assisted
workspace. Runtime work here is limited to public-source build integration,
reproduction, diagnostics, and human implementation briefs.

## Current seam

Current Xodus main:

- binds an AF_UNIX listener at `/tmp/xodus.sock` on macOS;
- restricts the socket to mode `0600`;
- removes the socket only during graceful shutdown;
- launches the configured Wine executable with `WINE_DLL_FILE_MAP`.

The AF_UNIX xgameruntime PR:

- detects the Wine host OS;
- explicitly handles Darwin;
- maps `/tmp` to the Wine path `Z:\tmp`;
- uses Windows AF_UNIX sockets to connect to `Z:\tmp\xodus.sock`;
- builds a Windows PE `xgameruntime.dll` out of tree;
- includes XThreading/XAsync/XTaskQueue work and tests.

This is the first architecture with a direct, visible macOS host-service path:

```text
Windows game in CrossOver
        |
        v
out-of-tree xgameruntime.dll
        |
        v
Wine Windows AF_UNIX
        |
        v
/tmp/xodus.sock
        |
        v
native arm64 xodus-service
```

## Build prerequisites discovered

PR #18 currently requires:

- CMake 3.20+;
- Ninja;
- `x86_64-w64-mingw32-gcc`, `g++`, and `windres`;
- Wine's `widl` host tool;
- initialized `libxml2` and GoogleTest submodules;
- a Wine executable for cross-compiled test discovery.

The project deliberately rejects MinGW's `widl` as unsupported. The Mac has
CrossOver Wine but CrossOver 26.3 does not ship a `widl` executable. The next
build experiment therefore needs:

1. Homebrew `mingw-w64` for the PE toolchain.
2. A native Wine `widl` built from public Wine/Xodus Wine source, without
   replacing the verified CrossOver runtime.
3. CMake configured with CrossOver's Wine executable as the test emulator.

Do not bypass the project's `widl` check by renaming MinGW's implementation.

## Build evidence

The Mac now has MinGW-w64 14.0.0. A native Wine IDL compiler was built from
CodeWeavers' matching CrossOver 26.3.0 source archive:

```text
archive SHA-256:
ac99c8ca4b3848f3e81784135f023df266b61c2345726ea55a50b3e030dd6872

widl:
~/.local/xodus-wine-tools/bin/widl

widl version:
11.0
```

Wine configure required:

```text
--enable-archs=x86_64
```

so the arm64 Mac host uses the installed x86_64 MinGW PE toolchain rather than
requiring an unavailable ARM64 Windows compiler. Wine's standard IDL directory
also had to be supplied to `widl` so imports such as `unknwn.idl` and
`propidl.idl` resolve.

Reproduce the pinned source download, checksum verification, tools-only Wine
configuration, `widl` build, and include-path wrapper with:

```bash
./scripts/macos/build-widl.sh
```

The unchanged PR #18 source then built through 73 of 82 steps and stopped at
`src/Xodus/IPCLayer.cpp` because the socket global is declared as `HANDLE` but
passed to Winsock APIs that require `SOCKET`. A diagnostic-only
`-fpermissive` build produced:

```text
build/windows-x64-diagnostic/bin/xgameruntime.dll
build/windows-x64-diagnostic/bin/test_xgameruntime.exe
```

Those artifacts must not be used for a game run. The permissive build only
confirmed that the socket type mismatch is the strict-build blocker.

All 42 XThreading tests start under CrossOver but exit with code 5 before
GoogleTest records an assertion result. The same result occurs in the Aqua
LaunchAgent context, ruling out the SSH GUI namespace as the cause. This
remains a separate runtime/test-harness blocker.

## Public-code observations requiring human review

PR #18's Darwin socket path is directionally correct, but its current
`NormalizeUnixPathToWine()` allocation and termination arithmetic should be
reviewed before relying on it. The function allocates from the Unix string
length, prepends `Z:`, and then writes a terminator beyond that original length.
This is evidence from open source, not proprietary behavior.

The same source declares its socket as `HANDLE` while using Winsock APIs that
require `SOCKET`. MinGW rejects these conversions in strict C++ mode. This
needs an upstream human review rather than an AI-authored runtime patch.

Any fix intended for xgameruntime upstream is:

```text
HUMAN / CLEAN-ROOM IMPLEMENTATION REQUIRED
```

The native Xodus service also fails to remove a stale socket before binding.
That is host portability rather than GDK semantics, but active service PR #193
must be reviewed before changing it.

## Options

| Option | First-launch speed | Divergence | D3DMetal confidence | Upstream value | Decision |
| --- | --- | --- | --- | --- | --- |
| A. CrossOver + out-of-tree xgameruntime | High | Low | High | High | Blocked: stock CrossOver lacks required AF_UNIX |
| B. CrossOver source + Xodus Wine patchset | Medium/low | Medium/high | Medium | High if patches stay small | Next experiment |
| C. Xodus Wine + external D3DMetal/GPTK | Low | High | Unknown | Medium | Avoid initially |
| D. Wait for upstream out-of-tree maturity | No immediate launch | None | N/A | High | Stop condition if PR #18 is actively changing incompatibly |

## Option A experiment

1. Pin CrossOver 26.3 and xgameruntime PR #18 revisions.
2. Build only the public out-of-tree runtime and its tests.
3. Create a disposable CrossOver test bottle or back up the control bottle.
4. Run `scripts/macos/test-wine-af-unix.sh` to verify the exact transport
   needed by the runtime.
5. Install/override `xgameruntime.dll` without modifying CrossOver binaries.
6. Start native `xodus-service` and prove `/tmp/xodus.sock` ownership and mode.
7. Run a minimal open test executable that initializes the runtime and connects
   to the service.
8. Only after that, invoke the entitled Hogwarts Legacy Game Pass package.

## Stop conditions

Stop Option A if:

- it requires a proprietary Microsoft runtime binary;
- CrossOver lacks the Windows AF_UNIX behavior used by PR #18;
- the PR needs a Wine core hook not present in CrossOver;
- current upstream work supersedes the pinned branch;
- progress requires generating GDK semantic implementation code with an LLM.

If CrossOver is missing only a small public Wine patch, move to Option B and
produce a precise patch-delta report before building a custom CrossOver Wine.
