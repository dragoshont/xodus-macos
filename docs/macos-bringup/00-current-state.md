# Xodus macOS current state

Snapshot date: 2026-09-30

This is the starting revision record for the Grounded macOS bring-up. Data was
read from public GitHub repository metadata and the local fork before any
runtime integration work.

## Source revisions

| Repository | Default branch | Commit |
| --- | --- | --- |
| `xodus-gaming/xodus` | `main` | `a3afa0569332e32ce2677c0edc643ef85477ee3e` |
| `xodus-gaming/xgameruntime` | `main` | `791710510d9ba0746bbd60754215eb321800e4f0` |
| `xodus-gaming/wine` | `bleeding-edge` | `eab69739f15180b96797645ccade44c1c4414980` |
| `xodus-gaming/Proton` | `xodus/bleeding-edge` | `7c0b435495814349735c913fde78da906aecea52` |
| `xodus-gaming/xgameruntime-docs` | `main` | `7090008692569aa6081e30dfdd9de58237741e24` |
| `xodus-gaming/.github` | `main` | `ae13b61ce23f68e376e6f0562d5a690e41bd1587` |
| `microsoft/libHttpClient` | `main` | `7ead73f6389271c2dc2cb10cdafcc587b899bd8a` |
| `volcmen/forza-motorsport-linux` | `main` | `022fc3bdcfab4c73abc5ec518b04335fcd4299bd` |

The Windows checkout and the initial Mac checkout both started at the current
Xodus upstream commit `a3afa0569332e32ce2677c0edc643ef85477ee3e`.

## Active work that affects the plan

### Core Xodus

- [#193](https://github.com/xodus-gaming/xodus/pull/193), `feat/peer`:
  active Tower-based service refactor. Avoid binding new macOS IPC work to
  interfaces this PR replaces.
- [#196](https://github.com/xodus-gaming/xodus/pull/196), `main`: replaces the
  macOS `hdiutil` preparation mechanism with `diskutil`. Reproduce current
  behavior and review this PR before changing package preparation.
- [#197](https://github.com/xodus-gaming/xodus/pull/197): improves failure
  handling in the run preparation path and may affect diagnostic work.
- [#198](https://github.com/xodus-gaming/xodus/pull/198), `streaming2`: active
  package streaming work.
- [#201](https://github.com/xodus-gaming/xodus/pull/201): device-authenticated
  download proof of concept.
- [#188](https://github.com/xodus-gaming/xodus/pull/188) and
  [#190](https://github.com/xodus-gaming/xodus/pull/190): current download and
  service design documentation.

### xgameruntime

- [#18](https://github.com/xodus-gaming/xgameruntime/pull/18),
  `oot-xodus-afunix`: active out-of-tree native Windows/AF_UNIX IPC work. This
  is the leading candidate for the first CrossOver integration experiment.
- Branch `oot-cpp`: `c5e6ac17b13996ad2338b1f9e3dbffc5e4d4d3b1`.
- Branch `xasync`: `bfecf584a92eedc0ea357693036ff01b01b2763c`.
- Branch `xtaskqueue`: `90ba72778771e12b6951bce51fca2e1dd3ddc81c`.
- Issues
  [#11](https://github.com/xodus-gaming/xgameruntime/issues/11),
  [#12](https://github.com/xodus-gaming/xgameruntime/issues/12),
  [#21](https://github.com/xodus-gaming/xgameruntime/issues/21),
  [#22](https://github.com/xodus-gaming/xgameruntime/issues/22),
  [#23](https://github.com/xodus-gaming/xgameruntime/issues/23), and
  [#24](https://github.com/xodus-gaming/xgameruntime/issues/24) remain open.

Microsoft `libHttpClient` changed on 2026-09-29 with a Task Queue race-condition
fix. Any XTaskQueue/XAsync comparison must pin and record the exact Microsoft
revision rather than relying on an older behavioral summary.

## Mac baseline

- macOS 27.0.1, build `26A434`
- Apple Silicon `arm64`, hardware identifier `Mac17,7`
- logged-in Aqua user available
- approximately 1.5 TiB free on the system volume
- Rosetta 2 installed
- Apple Command Line Tools 27.0 installed
- Apple Clang 21.0.0
- Homebrew 7.0.7
- Git 2.56.0
- Rust/Cargo 1.98.0 installed through rustup
- protobuf 36.2
- CMake 4.4.3
- Ninja 1.13.2
- pkg-config 3.0.7
- Node.js 26.10.0 and npm 11.19.1
- Playwright 1.63.0
- Microsoft Edge 154.0.4258.48
- CrossOver 26.3.0 trial
- tmux 3.7c, ShellCheck 0.11.0, and GitHub CLI 2.102.0

Microsoft Edge launches from SSH into the active Aqua session, exposes CDP only
on `127.0.0.1:9222`, and accepts a real Playwright `connectOverCDP()` session.
CrossOver launches from SSH and its bundled runtime reports build
`26.3.0.39832`.

The allowlisted `com.xodus.remote-launch` user LaunchAgent is installed and
successfully processes both `steam-grounded` and `edge-cdp` requests without
requiring sudo for routine launches.

The following native checks pass on the Mac:

```text
cargo fmt --check --all
cargo clippy --workspace
cargo test
cargo build --release --workspace
```

Validation logs are stored under:

```text
~/xodus-runs/native-validation-20260930T201151Z
```

## Immediate decisions

1. Remove the temporary passwordless sudo policy after unattended setup.
2. Complete CrossOver first-run/trial prompts if they remain.
3. Establish the Steam Grounded plus D3DMetal control before modifying runtime
   integration.
4. Evaluate CrossOver plus out-of-tree xgameruntime before considering a custom
   Wine build.

No xgameruntime or proprietary API behavior implementation should be generated
by an AI agent. Runtime findings must remain evidence, tests based on permitted
sources, or a human clean-room implementation brief.
