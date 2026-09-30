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

## GUI launch finding

Launching `Steam.exe` directly with the CrossOver Wine wrapper over SSH reached
the Windows Steam updater but exited with:

```text
failed to initialize update status ui, or create initial window
```

This does not establish a Wine or Steam compatibility failure. CrossOver itself
opens correctly in the logged-in Aqua session, so the next experiment is to
launch Steam from the CrossOver GUI and determine whether the issue is specific
to the raw SSH Wine invocation.

The Mac also has a native Steam client. Native Steam processes must not be
mistaken for the Windows Steam process inside the CrossOver bottle.

## Reproduce the prepared bottle

From the Mac-side repository:

```bash
./scripts/macos/create-grounded-control.sh
```

The script preserves an existing bottle, requires D3DMetal, downloads the
official Steam installer only when needed, and does not handle account
credentials.

## Next manual gate

1. Open CrossOver on the Mac desktop.
2. Select the `GroundedControl` bottle.
3. Launch its Windows Steam installation.
4. Sign into Steam without sharing credentials or session data.
5. Install the Windows edition of Grounded.
6. Launch Grounded with D3DMetal and reach actual gameplay.

Do not begin Game Pass/Xodus runtime diagnosis until this control reaches
gameplay or produces a reproducible CrossOver-specific failure.
