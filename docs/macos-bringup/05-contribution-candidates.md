# Initial contribution candidates

Status date: 2026-10-01

These candidates are based on reproduced Apple Silicon behavior. No issue or
pull request should be opened without explicit user approval and maintainer
coordination.

## 1. xgameruntime out-of-tree strict cross-build

- Repository: `xodus-gaming/xgameruntime`
- Related work: PR
  [#18](https://github.com/xodus-gaming/xgameruntime/pull/18)
- Reproduction: Apple Silicon macOS, MinGW-w64 14.0.0, Wine `widl` 11.0 built
  from the matching CrossOver 26.3.0 source.
- Result: compilation reaches 73 of 82 steps, then MinGW rejects conversions
  between the `HANDLE` socket global and Winsock's `SOCKET` type in
  `src/Xodus/IPCLayer.cpp`.
- Scope: cross-toolchain correctness in the new AF_UNIX implementation.
- Likely affected hosts: any strict MinGW build, including macOS-hosted builds.
- Confidence: high.
- Boundary: **HUMAN / CLEAN-ROOM IMPLEMENTATION REQUIRED** under the repository
  contribution policy.
- Review size: small source correction plus compile coverage.

A diagnostic-only build with `-fpermissive` produces the DLL and test
executable. Test discovery succeeds, but executing any of the 42 XThreading
tests exits with code 5 before GoogleTest reports an assertion. The same result
occurs inside the Aqua LaunchAgent, so it is not caused by the SSH GUI session.
The diagnostic artifact must not be used for game execution.

## 2. xgameruntime macOS cross-build documentation or CI

- Repository: `xodus-gaming/xgameruntime`
- Problem: the out-of-tree branch requires Wine's `widl`, its standard IDL
  include directory, MinGW, and `--enable-archs=x86_64` when building Wine
  tools on an arm64 Mac. None of this is discoverable from a README.
- Reproduction: documented in `03-wine-integration-options.md` and automated by
  `scripts/macos/build-widl.sh`.
- Scope: build integration and documentation; no GDK behavior.
- Confidence: high.
- Boundary: coordinate with maintainers before contributing because the
  xgameruntime repository broadly restricts LLM-authored changes.
- Review size: small.

## 3. macOS service launch and Keychain context

- Repository: `xodus-gaming/xodus`
- Problem: `xodus-service` fails with Keychain error `-25308` when started from
  a plain SSH login, but starts correctly in the logged-in Aqua bootstrap
  namespace.
- Reproduction: deterministic on the M5 Max; the successful service creates
  `/tmp/xodus.sock` with mode `0600`, exits cleanly on SIGINT, and removes the
  socket.
- Scope: macOS host portability and service lifecycle, not GDK semantics.
- Confidence: high for the observed behavior; the best upstream product design
  still needs maintainer input.
- Boundary: AI assistance may be acceptable for host integration, subject to
  current maintainer policy.
- Review size: medium if implemented as a supported launchd service; small if
  limited to documentation and diagnostics.

## 4. CrossOver/Xodus Wine AF_UNIX integration

- Repository: `xodus-gaming/wine`, with local integration against CrossOver
  26.3.0 source.
- Reproduction: `scripts/macos/test-wine-af-unix.sh` compiles a minimal Windows
  AF_UNIX client and connects it to a native macOS socket server.
- Result: stock CrossOver 26.3 returns `INVALID_SOCKET` from
  `socket(AF_UNIX, SOCK_STREAM, 0)` with Winsock error `10047`
  (`WSAEAFNOSUPPORT`).
- Existing public source: Xodus Wine commit `6b7313c1bd` adds AF_UNIX support
  and applies cleanly to the matching CrossOver 26.3.0 Wine source.
- Boundary: Wine/platform plumbing; use the existing public patch and preserve
  provenance rather than reimplementing behavior.
- Review size: potentially large because a private CrossOver-compatible Wine
  build and D3DMetal validation are still required.

The public patch applies cleanly to the CrossOver source. A full arm64-host
build then fails because `WineMetalLayer` is excluded by an x86_64 source guard,
while an x86_64-host build under Rosetta cannot use the arm64-only Command Line
Tools `libxcrun`. This narrows the next investigation to supported CrossOver
build tooling rather than AF_UNIX patch conflicts.

## 5. Stale macOS service socket handling

- Repository: `xodus-gaming/xodus`
- Relevant code: `crates/xodus-service/src/main.rs`.
- Observation: the service binds `/tmp/xodus.sock` without first distinguishing
  a live listener from a stale socket left by an unclean shutdown.
- Status: code-review hypothesis only; no stale-socket failure has yet been
  reproduced.
- Boundary: host portability.
- Action: do not implement until reproduced and checked against active service
  PR [#193](https://github.com/xodus-gaming/xodus/pull/193).

## Priority

The first actionable upstream discussion should cover candidates 1 and 2
together because they block a clean macOS build of the active out-of-tree
runtime. Candidate 3 should be raised only after deciding whether Xodus intends
to support launchd or another official macOS service lifecycle.
