# Grounded Steam control

Status date: 2026-09-30

The control environment is prepared, but gameplay is not yet validated.

## Proven

- CrossOver trial 26.3.0 is installed in `/Applications/CrossOver.app`.
- CrossOver's bundled runtime reports product build `26.3.0.39832`.
- CrossOver launches into the active Aqua desktop from an SSH-initiated
  `open -a CrossOver`.
- A dedicated 64-bit Windows 10 bottle named `GroundedControl` exists.
- The bottle has:

  ```text
  CX_GRAPHICS_BACKEND=d3dmetal
  ```

- The official Windows Steam installer was downloaded from
  `cdn.akamai.steamstatic.com`.
- Observed SHA-256:

  ```text
  7d3654531c32d941b8cae81c4137fc542172bfa9635f169cb392f245a0a12bcb
  ```

- Windows Steam files were installed under the `GroundedControl` bottle.
- The Windows Steam client has a local login state in that bottle. A separate
  native macOS Steam installation also exists and must not be used as evidence
  for the CrossOver control.

## GUI launch finding

Launching `Steam.exe` directly with the CrossOver Wine wrapper over SSH first
reached the Windows Steam updater but exited with:

```text
failed to initialize update status ui, or create initial window
```

Launching the same executable through the logged-in Aqua bootstrap namespace
resolved this problem. Steam downloaded and installed its complete 236,054 KiB
client update, restarted, verified its installation, connected to Steam's
network, and remained running with `steam.exe` and `steamwebhelper.exe`
processes inside the bottle.

The Mac also has a native Steam client. Native Steam processes must not be
mistaken for the Windows Steam process inside the CrossOver bottle.

Running both clients with the same account caused the CrossOver client to log:

```text
RecvMsgClientLoggedOff('Session Replaced')
```

The native Steam client must remain closed while installing or running games in
the CrossOver Steam client.

## Reproduce the prepared bottle

From the Mac-side repository:

```bash
./scripts/macos/create-grounded-control.sh
```

The script preserves an existing bottle, requires D3DMetal, downloads the
official Steam installer only when needed, and does not handle account
credentials.

## Interim Windows graphics control

The signed-in account does not own Grounded on Steam. Hogwarts Legacy, Steam
application ID `990080`, is owned and Windows-only, so it is the interim
DirectX 12/D3DMetal control. It is larger and cannot remove Grounded-specific
engine uncertainty, but successful gameplay will validate the lower
CrossOver/D3DMetal stack on this exact Mac.

The Hogwarts install manifest was created with:

```text
BytesToDownload = 73108753104
```

Windows Steam initially displayed no connectivity after its account session
was replaced by the simultaneously running native macOS Steam client. Closing
native Steam and relaunching only the CrossOver client restored a successful
Steam logon. Native Steam must remain closed while the Windows client downloads
or runs games.

The full Grounded control still requires legitimate Steam access:

1. Install the Windows edition of Grounded.
2. Launch Grounded with D3DMetal and reach actual gameplay.

Do not begin Game Pass/Xodus runtime diagnosis until this control reaches
gameplay or produces a reproducible CrossOver-specific failure.
