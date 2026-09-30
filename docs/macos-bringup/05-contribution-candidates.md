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

## 4. Stale macOS service socket handling

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
