---
name: mac-observe
description: Use whenever you need to see or observe the remote Mac (xodus-mac): screenshots of Xodus or any app window, full-display captures, UX comparisons, visual acceptance, and (when added) logs or diagnostics. There is exactly one tool for this, Xodus Observe; use and extend it, never create another capture/observer/recorder helper.
---

# Mac observe: the single remote-Mac observability tool

**One tool rule:** all remote-Mac observation goes through **Xodus Observe**
(`~/Applications/Xodus Observe.app`, bundle id `ro.hont.xodus.observe`).
Source is in `tools/mac-observe/`. To observe something new (logs, process state, window lists, video),
**add a subcommand** to `XodusObserve.swift` and `observe.py`. Do **not** build another helper app.
Every new helper needs its own Screen Recording grant, and earlier ones piled up and were forgotten
(Xodus Design Capture, Diablo Observer, Gameplay Recorder, NMS Observer, Private Diagnostics, XodusCapture).
Those are retired.

## Use

From Windows, at the repository root:

```powershell
.\tools\mac-observe\observe.ps1 capture window Xodus xodus-home -Pull <session-files-dir>
.\tools\mac-observe\observe.ps1 capture window TV tv-home -Pull <dir>        # Apple TV app
.\tools\mac-observe\observe.ps1 capture window Games games-home -Pull <dir>  # Apple Games app
.\tools\mac-observe\observe.ps1 capture display desktop -Pull <dir>
```

Then `view` the pulled PNG. The window must be on screen; bring it forward first
(`open -a <App>` over ssh) if needed. The status line prints `ok WxH` or `error <reason>`.

## Why it is built this way (don't relearn these)

- **Plain `screencapture` over ssh or Terminal fails** with "could not create image from window/display".
  TCC attributes ssh-spawned processes to `sshd-keygen-wrapper`, which has no grant.
  The tool is therefore launched with `open -n` (LaunchServices), so TCC attributes it to the app itself.
- **The grant is tied to the code signature's designated requirement.** Rebuilds must be signed with
  the same developer certificate (SHA1 `6C5F1CD832A2B2842686245B4DEF219BB8B465B2`) and identifier.
  An ad-hoc signature (`codesign -s -`) or a different identity silently breaks the grant.
  The log shows `tccd: Failed to match existing code requirement`. Toggling the switch does **not**
  fix that; only removing and re-adding the entry does.
- **Signing only works from the GUI Terminal session** (`osascript … Terminal do script`);
  over ssh the keychain fails with `errSecInternalComponent`. `observe.py install` handles this.
- **Binary transfer:** never use PowerShell `>` redirection for PNGs (it corrupts the bytes).
  `observe.ps1 -Pull` copies binary-safe.
- The app writes `<out>.png.status` and self-terminates after 20 seconds, so a hung capture never blocks.

## Install or rebuild (rare)

```powershell
.\tools\mac-observe\observe.ps1 install
```

It must print `designated => identifier "ro.hont.xodus.observe" and certificate leaf = H"6c5f1cd8…"`.
The first install only needs a one-time user grant: System Settings → Privacy & Security →
Screen & System Audio Recording → + → `~/Applications/Xodus Observe.app`. To check the grant
without asking the user, run a capture; `error screen recording not granted` means it is missing.

## Extending

Add a `case` in `XodusObserve.main` (for example `("logs", "show")` or `("windows", "list")`) and
the matching branch in `observe.py`. Keep every output written to `~/xodus-app-tooling/observe/`
with a `.status` file. Rebuild with `install`; the certificate signature keeps the existing grant valid.
Never record credentials, tokens, account identifiers, CDN URLs or licence data in outputs.
