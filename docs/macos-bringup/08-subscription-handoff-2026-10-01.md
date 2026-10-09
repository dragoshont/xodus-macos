# Xodus macOS Hogwarts Game Pass continuation handoff

Handoff date: 2026-10-01

This document is the starting point for continuing the work under a different
subscription or agent session. The Mac is the authoritative machine. Read this
document, `06-experiment-ledger.md`, and
`07-xuser-ticket-clean-room-brief.md` before changing or rebuilding anything.

## Executive status

Hogwarts Legacy from the UK Xbox/Game Pass catalog is **not yet playable**.

The package is fully downloaded, licensed, extracted, decrypted at launch, and
started as a Windows WinGDK process under a private CrossOver Wine runtime with
D3DMetal. The runtime now successfully completes:

- Windows AF_UNIX transport to the native Xodus service;
- XGameRuntime initialization;
- serialized XTaskQueue creation;
- asynchronous XUser dispatch;
- ECDSA/RSA key generation;
- Schannel HTTPS requests;
- Xbox endpoint download;
- WinRT JSON object and array parsing;
- endpoint and signature-policy enumeration.

The first remaining blocker is the public xgameruntime function:

```c
static HRESULT get_rps_tickets(
    BOOLEAN allowUi,
    char **userTicket,
    char **deviceTicket
);
```

It is an explicit stub in public branch `origin/xuser` at commit `44d97de`.
No implementation was found in upstream branches or public indexed forks.

The native Xodus service already exposes the required token pair through the
public `MSA_TOKEN_REQUEST`/`MSA_TOKEN_RESPONSE` XML protocol over
`/tmp/xodus.sock`. Connecting that response to XUser is XUser/GDK semantic
work and must be implemented by a human under the project's clean-room and LLM
rules. The exact contract is in `07-xuser-ticket-clean-room-brief.md`.

No Hogwarts process or staging RAM disk is currently active.

## First actions under the new subscription

1. Authenticate GitHub Desktop on the Mac.
2. Add the existing repository:

   ```text
   /Users/dragoshont/src/xodus-macos
   ```

3. Confirm the branch and revision:

   ```bash
   cd ~/src/xodus-macos
   git branch --show-current
   git log -3 --oneline
   git status --short
   ```

   Expected:

   ```text
   branch: dragoshont-macos-handoff-plan
   head:   c93c989 docs: record XUser clean-room boundary
   status: clean
   ```

4. Join the Xodus Discord:

   ```text
   https://discord.gg/ZG774FK4tq
   ```

5. Coordinate with the maintainers before writing XUser code. Suggested
   message:

   > I am testing Hogwarts Legacy PC Game Pass on an M5 Max Mac using Xodus,
   > CrossOver/D3DMetal, and an AF_UNIX-capable private Wine build. The stack
   > now passes XThreading/task queues, asynchronous XUser dispatch, key
   > generation, Schannel TLS, and endpoint JSON parsing. The current blocker
   > is the public `origin/xuser` `get_rps_tickets()` stub. The native service
   > already exposes `MSA_TOKEN_REQUEST`/`MSA_TOKEN_RESPONSE` over
   > `/tmp/xodus.sock`. Is someone already implementing this bridge, or can a
   > human clean-room contributor take it? Branch
   > `dragoshont-macos-handoff-plan`, commit `c93c989`.

6. Do not start a new package download. The complete package is already
   present.

## Authoritative repository

```text
Mac checkout:  /Users/dragoshont/src/xodus-macos
GitHub fork:   https://github.com/dragoshont/xodus-macos.git
Branch:        dragoshont-macos-handoff-plan
Latest pushed commit: c93c989
```

Important commits:

```text
c93c989 docs: record XUser clean-room boundary
4a5d78a docs: add macOS experiment ledger
4aaf391 feat: stage decrypted macOS executables
c85119c docs: record Xodus Wine macOS blocker
b92896e feat: stream UK Hogwarts Game Pass package
cb687eb feat: automate UK Hogwarts package probe
a758801 docs: record patched Wine integration boundary
35dd336 test: characterize macOS runtime bridge
```

The Windows worktree may be behind or contain duplicate uncommitted copies of
Mac-authored changes. Do not make it authoritative. Work from the Mac checkout
or fetch the Mac/GitHub branch first.

## Mac host

```text
Host:       m5.hont.ro
LAN IP:     192.168.1.122
User:       dragoshont
macOS:      27.0.1, build 26A434
Hardware:   Apple Silicon Mac17,7
Rosetta:    installed
```

The Windows control machine has SSH alias:

```text
xodus-mac
```

Temporary passwordless sudo remains enabled at:

```text
/etc/sudoers.d/xodus-automation
```

Remove it only when explicitly desired:

```bash
sudo rm -f /etc/sudoers.d/xodus-automation
sudo -k
```

## Installed user applications

```text
/Applications/CrossOver.app
/Applications/Discord.app
/Applications/GitHub Desktop.app
/Applications/Microsoft Edge.app
```

Discord and GitHub Desktop are installed but authentication may still need to
be completed interactively by the user.

No Discord MCP server was found in GitHub Agent Finder. The only result was a
Claude-specific plugin, which was intentionally not installed.

## Hogwarts Game Pass package

```text
Market:       GB
Product ID:   9MT5NJ5W7B8Z
Content ID:   c1084505-abc1-4c27-b3fe-ab7040a5f302
Package:      WarnerBros.Interactive.PHX_1.0.16.0_x64__ktmk1xygcecda.msixvc
Destination:  /Users/dragoshont/Games/Xodus/HogwartsLegacy-Xbox
Executable:   Phoenix/Binaries/WinGDK/HogwartsLegacy.exe
```

Extraction evidence:

```text
541 files
113 directories
approximately 93.7 GiB extracted
final cache: .xodus-streaming.msixvc
```

The package manifest says:

```text
Identity: WarnerBros.Interactive.PHX
Version:  1.0.16.0
RequiresXboxLive: false
```

Do not record or share CDN URLs, license payloads, Keychain records, tokens, or
account identifiers.

## Native Xodus service and launch agents

Installed user LaunchAgents:

```text
com.xodus.remote-launch
com.xodus.service
com.xodus.hogwarts-stream
```

The streaming agent is no longer needed for download; extraction is complete.

Service socket:

```text
/tmp/xodus.sock
mode 0600
```

Keychain-dependent Xodus operations must run in the logged-in Aqua namespace.
Plain SSH processes receive Keychain error `-25308`.

Useful commands:

```bash
cd ~/src/xodus-macos

# Reinstall/restart the GUI request dispatcher
./scripts/macos/install-gui-launcher.sh

# Start/restart the native service
launchctl kickstart -k "gui/$(id -u)/com.xodus.service"

# Trigger the current Hogwarts experiment
./scripts/macos/trigger-gui.sh xodus-hogwarts-launch
```

Remote logs:

```text
~/Library/Logs/XodusRemote
```

## macOS decrypted package overlay

Relevant repository file:

```text
crates/xodus-cli/src/commands/run.rs
```

Stock CrossOver does not support `WINE_DLL_FILE_MAP`. The macOS path therefore:

1. Creates a case-insensitive RAM disk.
2. Writes decrypted encrypted PE files at their real package-relative paths.
3. Symlinks unencrypted package content.
4. Launches the staged WinGDK executable through normal Wine.
5. Cleans up the RAM disk after the wrapper exits.

An orphaned Wine child can keep the RAM disk mounted. If necessary:

```bash
ps -axo pid,command | grep -E 'xodus-cli run|HogwartsLegacy.exe'
mount | grep -E '/private/var/folders/.*/\.tmp'
```

Terminate only the identified PIDs, unmount the exact mount point, then detach
the exact `/dev/diskN` device. Never use broad process-name kills or recursive
cleanup.

## CrossOver and graphics

```text
CrossOver version: 26.3.0 / build 26.3.0.39832
Bottle:            GroundedControl
Backend:           D3DMetal
```

The bottle retains the old `GroundedControl` name for compatibility.

Persistent overlays:

```text
MTL_HUD_ENABLED=1
DXVK_HUD=fps,frametimes,gpuload,memory
```

The Steam Hogwarts Windows build already proved that the hardware and
D3DMetal path reach visible shader preparation.

## Private CrossOver Wine

Official CrossOver source archive SHA-256:

```text
ac99c8ca4b3848f3e81784135f023df266b61c2345726ea55a50b3e030dd6872
```

Source:

```text
~/src/crossover-wine-xodus-afunix-26.3.0
```

Build:

```text
~/src/build/crossover-wine-xodus-full-26.3.0-x86_64
```

Public Xodus Wine commits applied:

```text
6b7313c1bd  AF_UNIX support
183d5d90b6  file-map/memfd support
```

Private wrapper committed in this repository:

```text
scripts/macos/private-xodus-crossover-wine.sh
```

It uses:

```text
private loader/server
private builtin ws2_32
private builtin bcrypt
private builtin secur32
private builtin windows.web
native/builtin composite xgameruntime
CrossOver GPTK/D3DMetal libraries
CrossOver bundled x86_64 GnuTLS
```

Isolated validations:

```text
AF_UNIX_PRIVATE_WINE_OK
BCryptGenerateKeyPair(2048) = 0x00000000
WinHTTP Xbox endpoint request = HTTP 200
```

### Private Wine build changes not committed to the Xodus repository

The generated private build configuration was updated to enable the public
GnuTLS backend:

```text
HAVE_GNUTLS_CIPHER_INIT=1
SONAME_LIBGNUTLS="libgnutls.30.dylib"
```

GnuTLS headers were installed through Homebrew. The x86_64 runtime loads
CrossOver's bundled:

```text
/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/lib64/libgnutls.30.dylib
```

Rebuilt Unix companions:

```text
build/dlls/bcrypt/bcrypt.so
build/dlls/secur32/secur32.so
```

Matching private PE DLLs are installed in the bottle, with backups:

```text
bcrypt.dll.before-xodus-private
secur32.dll.before-xodus-private
```

### Public Wine JSON backport

Wine master revision:

```text
6d1b09405774c4f234ed3fa0088a9706deb7ad49
```

Backported public files:

```text
dlls/windows.web/json_value.c
dlls/windows.web/json_object.c
dlls/windows.web/json_array.c
dlls/windows.web/private.h
dlls/windows.web/main.c
dlls/windows.web/Makefile.in
```

A private generic `IVector<IJsonValue*>` interface was added to JsonArray for
`GetAt`, `get_Size`, `IndexOf`, `Append`, and `GetMany`; unsupported mutation
methods remain explicit `E_NOTIMPL` stubs.

The original directory is retained as:

```text
~/src/crossover-wine-xodus-afunix-26.3.0/
  dlls/windows.web.before-wine-6d1b094
```

## Composite xgameruntime

Primary public runtime:

```text
Source: ~/src/xgameruntime-pr18
Commit: 8dd2aa0044c0fc72d600fbe80f3b1e201e3181fb
Build:  ~/src/xgameruntime-pr18/build/windows-x64/bin/xgameruntime.dll
```

Private uncommitted modifications:

```text
src/Xodus/IPCLayer.cpp
  HANDLE socket global corrected to Winsock SOCKET

src/main.cpp
  XThreading handled by PR #18
  unknown interfaces delegated to xgameruntime_legacy.dll
  initialization/uninitialization delegated to the fallback

src/System/Threading/ThreadPool.cpp
  generic COM MTA initialization around task-pool callbacks
```

Fallback runtime:

```text
Public source branch: xodus-gaming/xgameruntime origin/xuser
Public commit:        44d97de
Build output:
  ~/src/build/xodus-wine-macos-x86_64/
    dlls/xgameruntime/x86_64-windows/xgameruntime.dll
```

The fallback's XThreading queries delegate to the primary PR #18 DLL.

Bottle installation:

```text
.../drive_c/windows/system32/xgameruntime.dll
.../drive_c/windows/system32/xgameruntime_legacy.dll
```

Do not upstream the composite or external private experiments as-is.

## Current clean-room blocker

Public XUser flow now reaches:

```text
get_rps_tickets(
    allowUi = FALSE,
    userTicket,
    deviceTicket
) -> E_NOTIMPL
```

The existing native service protocol is:

```text
socket: /tmp/xodus.sock
magic:  0x58445358
request type:  3 / MSA_TOKEN_REQUEST
response type: 4 / MSA_TOKEN_RESPONSE
```

Request:

```rust
MSATokenRequest {
    client_id,
    allow_ui,
    msa_full_trust,
}
```

Response:

```rust
MSATokenResponse {
    token,
    expiry,
    device_rps,
    device_expiry,
}
```

The human implementation must return independently allocated UTF-8
`userTicket` and `deviceTicket` strings because the current caller frees both
with `free()`.

Do not implement this with an LLM. Coordinate with Xodus maintainers and follow
the clean-room brief in `07-xuser-ticket-clean-room-brief.md`.

## Clean-room and AI restrictions

The xgameruntime repository says:

- reverse engineering driven by LLMs is not allowed;
- LLM-assisted code will generally be rejected;
- contributors should coordinate on Discord or a GitHub issue before coding;
- documentation assistance is permitted.

Allowed continuation work for an AI assistant:

- documentation;
- host/macOS integration;
- build diagnostics;
- reproducing behavior from existing public code;
- generic Wine/platform fixes with public provenance;
- tests using synthetic data;
- preparing a human implementation brief.

Not allowed:

- inventing XUser/GDK semantics;
- tracing proprietary Microsoft runtime behavior;
- deriving behavior from disassembly or decompilation;
- writing the `get_rps_tickets()` implementation;
- logging or sharing authentication material.

## Suggested continuation sequence

1. Authenticate GitHub Desktop and Discord.
2. Coordinate with the Xodus team.
3. Have a human implement the ticket bridge.
4. Review it for:
   - exact frame validation;
   - explicit HRESULT failures;
   - allocation ownership;
   - no token logging;
   - no disk persistence;
   - correct `allow_ui` behavior.
5. Rebuild the fallback xgameruntime DLL.
6. Install it as `xgameruntime_legacy.dll`.
7. Start `com.xodus.service` in Aqua.
8. Trigger `xodus-hogwarts-launch`.
9. Use only:

   ```text
   WINEDEBUG=+xgameruntime,+gdkc
   ```

10. Stop at the next unimplemented GDK semantic boundary and update the
    experiment ledger.

## Repository validation

The native Xodus workspace has passed:

```text
cargo fmt --check --all
cargo clippy --workspace
cargo test
cargo build --release --workspace
```

The current repository wrapper passes ShellCheck.

## Authentication and safety

- Xodus Microsoft login is stored in macOS Keychain.
- The user approved **Always Allow** for the Xodus Keychain access.
- Never export Keychain entries into a handoff.
- Never paste full XUser traces into Discord.
- Never share XML token responses.
- Market must remain `GB`.
- The Xodus project is unofficial and not affiliated with Microsoft/Xbox.

## Key documents

```text
docs/macos-bringup/00-current-state.md
docs/macos-bringup/01-remote-development.md
docs/macos-bringup/02-hogwarts-control.md
docs/macos-bringup/03-wine-integration-options.md
docs/macos-bringup/04-first-gamepass-run.md
docs/macos-bringup/05-contribution-candidates.md
docs/macos-bringup/06-experiment-ledger.md
docs/macos-bringup/07-xuser-ticket-clean-room-brief.md
docs/macos-bringup/08-subscription-handoff-2026-10-01.md
```

## Final state at handoff

```text
Repository head: c93c989
Repository status: clean before adding this handoff
Hogwarts process: not running
Staging RAM disk: not mounted
Package download: complete
Game working interactively: no
Current blocker: human clean-room get_rps_tickets bridge
Discord app: installed
GitHub Desktop: installed
```

