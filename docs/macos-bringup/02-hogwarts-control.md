# Hogwarts Legacy Steam control

Status date: 2026-09-30

Hogwarts Legacy is the Windows-only Steam control. Grounded is no longer the
target because the signed-in Steam account does not own it.

## Proven

- CrossOver trial 26.3.0 is installed in `/Applications/CrossOver.app`.
- CrossOver's bundled runtime reports product build `26.3.0.39832`.
- CrossOver launches into the active Aqua desktop from an SSH-initiated
  `open -a CrossOver`.
- A dedicated 64-bit Windows 10 bottle exists. It retains the legacy name
  `GroundedControl` to avoid moving or duplicating its 76 GB installation.
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
./scripts/macos/create-hogwarts-control.sh
```

The script preserves an existing bottle, requires D3DMetal, downloads the
official Steam installer only when needed, and does not handle account
credentials.

## Windows graphics control

Hogwarts Legacy, Steam application ID `990080`, is owned and Windows-only, so it
is the DirectX 12/D3DMetal control for the new Hogwarts Game Pass target.

The Hogwarts install manifest was created with:

```text
BytesToDownload = 73108753104
```

The download completed successfully with Steam build ID `20773316` and
`StateFlags=4` (`Fully Installed`). Steam reported 73,108,753,104 downloaded
bytes and 76,444,174,839 bytes on disk.

After a clean Windows Steam restart, Steam launched:

```text
HogwartsLegacy.exe
Phoenix\Binaries\Win64\HogwartsLegacy.exe
```

Both processes remained alive for more than two minutes, CrossOver logged
`using d3dmetal as the graphics backend`, and macOS produced no crash report.
The user then visually confirmed that the game opened and reached shader
preparation. This proves a rendered Windows game path and D3DMetal activation.
Actual interactive gameplay still needs confirmation after shader preparation
finishes.

The process remained alive beyond seven minutes. The revision-bound run
manifest is stored on the Mac at:

```text
~/xodus-runs/hogwarts-control-20261001T005911+0300/manifest.txt
```

The game was then closed deliberately by shutting down only the Windows Steam
client. No crash report was produced.

The Metal and DXVK performance HUD variables are enabled for future launches.
The game must be restarted for the overlay to appear.

Windows Steam initially displayed no connectivity after its account session
was replaced by the simultaneously running native macOS Steam client. Closing
native Steam and relaunching only the CrossOver client restored a successful
Steam logon. Native Steam must remain closed while the Windows client downloads
or runs games.

The remaining control gate is confirming interactive gameplay after shader
preparation.
