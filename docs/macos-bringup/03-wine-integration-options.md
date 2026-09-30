# Wine and xgameruntime integration options

Status date: 2026-10-01

## Decision

Start with **Option A: CrossOver Wine plus out-of-tree xgameruntime**, using
xgameruntime PR
[#18](https://github.com/xodus-gaming/xgameruntime/pull/18) pinned at:

```text
8dd2aa0044c0fc72d600fbe80f3b1e201e3181fb
```

This is the smallest path that preserves the verified CrossOver/D3DMetal stack
and can reach the current Xodus host service without first rebuilding Wine.

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
| A. CrossOver + out-of-tree xgameruntime | High | Low | High | High | Start here |
| B. CrossOver source + Xodus Wine patchset | Medium/low | Medium/high | Medium | High if patches stay small | Only if A proves a missing Wine hook |
| C. Xodus Wine + external D3DMetal/GPTK | Low | High | Unknown | Medium | Avoid initially |
| D. Wait for upstream out-of-tree maturity | No immediate launch | None | N/A | High | Stop condition if PR #18 is actively changing incompatibly |

## Option A experiment

1. Pin CrossOver 26.3 and xgameruntime PR #18 revisions.
2. Build only the public out-of-tree runtime and its tests.
3. Create a disposable CrossOver test bottle or back up the control bottle.
4. Install/override `xgameruntime.dll` without modifying CrossOver binaries.
5. Start native `xodus-service` and prove `/tmp/xodus.sock` ownership and mode.
6. Run a minimal open test executable that initializes the runtime and connects
   to the service.
7. Only after that, invoke an entitled Game Pass package.

## Stop conditions

Stop Option A if:

- it requires a proprietary Microsoft runtime binary;
- CrossOver lacks the Windows AF_UNIX behavior used by PR #18;
- the PR needs a Wine core hook not present in CrossOver;
- current upstream work supersedes the pinned branch;
- progress requires generating GDK semantic implementation code with an LLM.

If CrossOver is missing only a small public Wine patch, move to Option B and
produce a precise patch-delta report before building a custom CrossOver Wine.
