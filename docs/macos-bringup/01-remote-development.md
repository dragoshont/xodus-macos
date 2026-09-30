# macOS remote development setup

This document records the reproducible setup for using an Apple Silicon Mac as
a remote Xodus build and test host. It intentionally excludes passwords,
private keys, authentication tokens, license keys, and proprietary binaries.

## Connection

The Windows SSH client uses the alias `xodus-mac` with:

- key-based authentication;
- a dedicated key at `~/.ssh/id_ed25519_xodus_mac`;
- `IdentitiesOnly yes`;
- SSH keepalives for long-running commands.

Verify the connection from Windows:

```powershell
ssh xodus-mac "whoami; sw_vers; uname -m"
```

The current Mac-side checkout is:

```text
~/src/xodus-macos
```

The Windows GitHub CLI has separate credentials for the enterprise Copilot host
and the user's personal `github.com` account. The Copilot process injects
`GH_HOST` and `GH_TOKEN` for the enterprise host. For an explicit personal
GitHub CLI operation from that process, clear only those process-local
overrides:

```powershell
Remove-Item Env:GH_TOKEN,Env:GH_HOST -ErrorAction SilentlyContinue
gh auth status --hostname github.com
```

Do not remove or overwrite the enterprise credential.

The Mac must remain logged into an Aqua desktop session for reliable GUI game
launches. After a reboot, log into the Mac locally once before attempting a
CrossOver launch over SSH.

Long-running commands should use `caffeinate` and write output to a
revision-bound run directory. Do not leave unnamed background processes that
cannot be inspected or stopped.

Use the checked-in Windows wrapper for routine operations:

```powershell
.\scripts\windows\invoke-mac.ps1 doctor
.\scripts\windows\invoke-mac.ps1 fmt
.\scripts\windows\invoke-mac.ps1 clippy
.\scripts\windows\invoke-mac.ps1 test
.\scripts\windows\invoke-mac.ps1 build
.\scripts\windows\invoke-mac.ps1 edge-cdp
.\scripts\windows\invoke-mac.ps1 overlay-on
.\scripts\windows\invoke-mac.ps1 overlay-off
.\scripts\windows\invoke-mac.ps1 overlay-all-on
.\scripts\windows\invoke-mac.ps1 overlay-all-off
.\scripts\windows\invoke-mac.ps1 launch-crossover
.\scripts\windows\invoke-mac.ps1 launch-steam
.\scripts\windows\invoke-mac.ps1 quit-native-steam
.\scripts\windows\invoke-mac.ps1 quit-windows-steam
.\scripts\windows\invoke-mac.ps1 xgameruntime-smoke
.\scripts\windows\invoke-mac.ps1 xodus-service-smoke
.\scripts\windows\invoke-mac.ps1 xodus-login
.\scripts\windows\invoke-mac.ps1 start-xodus-service
.\scripts\windows\invoke-mac.ps1 stop-xodus-service
.\scripts\windows\invoke-mac.ps1 install-hogwarts
.\scripts\windows\invoke-mac.ps1 launch-hogwarts
.\scripts\windows\invoke-mac.ps1 hogwarts-status
.\scripts\windows\invoke-mac.ps1 build-widl
.\scripts\windows\invoke-mac.ps1 prepare-steam-control
.\scripts\windows\invoke-mac.ps1 bootstrap-status
```

Prepare the isolated Steam control bottle on the Mac with:

```bash
./scripts/macos/create-hogwarts-control.sh
```

## GUI launch bridge

A plain SSH session does not belong to the active Aqua bootstrap namespace.
CrossOver itself can open through Launch Services, but Windows Steam's updater
could not create its initial window when Wine was invoked directly from SSH.

Install the allowlisted per-user LaunchAgent once:

```bash
./scripts/macos/install-gui-launcher.sh
```

The helper accepts only:

- `crossover`;
- `quit-native-steam`;
- `steam-control`;
- `install-hogwarts`, fixed to Steam application ID `990080`;
- `xgameruntime-smoke`, fixed to the public PR #18 diagnostic test binary;
- `edge-cdp`, optionally with a local port.

It does not execute arbitrary request content. SSH creates a mode-600 request
file, and the LaunchAgent processes it inside the logged-in Aqua session.
Runtime logs are stored under `~/Library/Logs/XodusRemote`.

The overlay defaults to enabled for helper-launched games:

- D3DMetal receives `MTL_HUD_ENABLED=1`;
- DXVK receives `DXVK_HUD=fps,frametimes,gpuload,memory`.

The Metal HUD reports FPS, CPU/GPU frame timing, and memory metrics supported by
the active Metal runtime. Restart an already-running game after changing the
overlay state.

`overlay-all-on` also writes these settings into every existing CrossOver
bottle, preserving one backup per bottle with the suffix
`.before-xodus-overlay`. This makes the overlay available to manual CrossOver
launches as well as LaunchAgent-triggered launches.

The enabled variables are:

```text
MTL_HUD_ENABLED=1
DXVK_HUD=fps,frametimes,gpuload,memory
```

These variables were written to every existing CrossOver bottle. The original
configuration for each bottle is retained as
`cxbottle.conf.before-xodus-overlay`.

A separate `com.xodus.service` user LaunchAgent keeps `xodus-service` in the
same unlocked Keychain/Aqua context while allowing the GUI request processor to
continue accepting launch commands. Install it with:

```bash
./scripts/windows/invoke-mac.ps1 build
ssh xodus-mac "cd ~/src/xodus-macos && ./scripts/macos/install-xodus-service-agent.sh"
```

Or directly on the Mac:

```bash
cd ~/src/xodus-macos
./scripts/macos/install-xodus-service-agent.sh
```

After installation, no sudo is needed for normal GUI launches:

```bash
./scripts/macos/trigger-gui.sh steam-control
./scripts/macos/trigger-gui.sh edge-cdp 9222
```

## Bootstrap

Apple Command Line Tools must be installed first:

```bash
xcode-select --install
```

After installation, clone this repository on the Mac and run:

```bash
./scripts/macos/bootstrap.sh
```

To install CrossOver through Homebrew as part of the bootstrap:

```bash
./scripts/macos/bootstrap.sh --with-crossover
```

To also install the xgameruntime cross-compilation prerequisites:

```bash
./scripts/macos/bootstrap.sh --with-crossover --with-runtime-toolchain
```

The script installs:

- Homebrew under `/opt/homebrew`;
- Git;
- protobuf compiler;
- CMake;
- Ninja;
- pkg-config;
- jq;
- Node.js;
- tmux;
- ShellCheck;
- GitHub CLI;
- Microsoft Edge;
- Playwright without downloading a bundled Chromium browser;
- rustup;
- Rust 1.98.0 with Clippy and rustfmt;
- optionally, the Homebrew CrossOver cask.

The optional runtime toolchain adds MinGW-w64, modern Bison, and FreeType.

CrossOver is proprietary software. Installation does not provide a license or
bundle CrossOver, D3DMetal, Game Porting Toolkit, or game files with Xodus.
Complete any licensing and first-run prompts directly in the CrossOver UI.

## Browser automation

Browser automation uses Microsoft Edge rather than Chrome. Launch Edge with a
localhost-only Chrome DevTools Protocol endpoint:

```bash
./scripts/macos/launch-edge-cdp.sh
```

From Windows, forward that endpoint through SSH:

```powershell
ssh -N -L 9222:127.0.0.1:9222 xodus-mac
```

Playwright can connect with `chromium.connectOverCDP()` to
`http://127.0.0.1:9222`. The bootstrap suppresses Playwright's bundled browser
download so automation does not silently switch to Chrome or Chromium.

## Health check

From the repository root on the Mac:

```bash
./scripts/macos/doctor.sh
```

The doctor reports system, toolchain, Rosetta, CrossOver, disk, GUI session, and
repository revision information without reading credentials.

## Temporary unattended sudo

For the initial supervised bootstrap only, the machine owner may create:

```text
/etc/sudoers.d/xodus-automation
```

with this exact content:

```sudoers
dragoshont ALL=(ALL) NOPASSWD: ALL
```

Validate it before ending the authenticated sudo session:

```bash
sudo chmod 440 /etc/sudoers.d/xodus-automation
sudo visudo -cf /etc/sudoers.d/xodus-automation
```

This grants broad root access and must be temporary. Remove it when unattended
setup is complete:

```bash
sudo rm -f /etc/sudoers.d/xodus-automation
sudo -k
```

Verify removal:

```bash
if sudo -n true 2>/dev/null; then
    echo "WARNING: passwordless sudo is still enabled"
else
    echo "Passwordless sudo is disabled"
fi
```

## Build validation

Run from the repository root:

```bash
cargo fmt --check --all
cargo clippy --workspace
cargo test
cargo build --release --workspace
```

Do not begin Wine or Game Pass integration until the native macOS checks pass.

## Security boundaries

- Never commit SSH private keys, Xbox tokens, XSTS tokens, device credentials,
  content keys, game binaries, Microsoft runtime binaries, or CrossOver/
  D3DMetal binaries.
- Do not expose SSH directly to the public internet.
- Use legitimate Steam and Xbox/Game Pass entitlements.
- Remove temporary passwordless sudo after bootstrap.
- Keep runtime/API work within the upstream Xodus clean-room and AI policies.
