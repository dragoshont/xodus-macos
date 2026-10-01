# First Game Pass run

Status date: 2026-10-01

## Highest proven milestone

```text
M0: host Xodus builds on Mac
```

The full native workspace passes formatting, Clippy, tests, and release build on
the Apple Silicon host.

The native `xodus-service` also passes a controlled service smoke test when
started in the logged-in Aqua session:

```text
socket: /tmp/xodus.sock
mode:   srw-------
owner:  current macOS user
exit:   clean after SIGINT
cleanup: socket removed
```

This validates host service startup and socket lifecycle, but it does not
advance the Game Pass milestone beyond M0 because no Xbox user entitlement or
title package was exercised.

The Steam graphics control is partially proven with Hogwarts Legacy:

- Windows Steam download completed;
- Steam launch and ownership checks completed;
- both Hogwarts Windows executables remained running;
- CrossOver confirmed the D3DMetal backend;
- no macOS crash report appeared;
- rendered gameplay still requires human observation.

## macOS Keychain constraint

Starting `xodus-service` from a plain SSH session failed before socket creation:

```text
Keychain error -25308: User interaction is not allowed
```

Starting the identical binary through the allowlisted Aqua LaunchAgent
succeeded. Xodus operations that initialize or access Apple Keychain should
therefore run in the logged-in GUI bootstrap namespace, even when initiated
remotely.

A dedicated `com.xodus.service` LaunchAgent is installed. Remote start and stop
requests were verified: the service entered the running state, created the
mode-`0600` socket, handled SIGINT, and removed the socket.

## Not attempted

Microsoft's public UK display catalog confirms that product
`9MT5NJ5W7B8Z` is Hogwarts Legacy and includes an x64 `MSIXVC` package allowed
on `Windows.Desktop`. All catalog, download, license, and run commands must use
market `GB`.

The Xodus login UI currently hardcodes `en-US`. This affects login-page locale,
while the package/catalog and license commands accept an explicit market.
Do not change authentication behavior in this AI-assisted branch; pass
`--market GB` to the relevant package operations.

The following were intentionally not attempted while the user was unavailable:

- Microsoft/Xbox device login;
- Game Pass library enumeration;
- Hogwarts Legacy entitlement selection;
- Hogwarts Legacy package download;
- license retrieval;
- encrypted executable preparation;
- Game Pass executable launch.

No credentials, authentication payloads, or license material were captured.

The Xodus device identity can be provisioned from the Aqua session, but no
stored user identity existed at the initial snapshot. The user subsequently
completed Xodus's Microsoft login successfully. The first UK package dry-run
then blocked on macOS Keychain approval for reading the stored user tokens; the
user must choose **Always Allow** on that one-time prompt before unattended
catalog/package operations can continue.

The user approved permanent Keychain access and the UK dry-run succeeded.
Entitled package metadata:

```text
Product: 9MT5NJ5W7B8Z
ContentID: c1084505-abc1-4c27-b3fe-ab7040a5f302
Base: WarnerBros.Interactive.PHX_1.0.16.0_x64__ktmk1xygcecda.msixvc
Base size: 95,473,455,104 bytes
Updates: four XSP files
Market: GB
```

The dry-run selected all files and redacted every CDN URL. No data was
downloaded by the probe.

The resumable Xodus streaming/extraction path is now running through the
dedicated Aqua LaunchAgent `com.xodus.hogwarts-stream`:

```text
Destination: ~/Games/Xodus/HogwartsLegacy-Xbox
Parallel jobs: 8
Market: GB
```

This command automatically selects the MSIXVC base package, acquires the
license, and downloads/extracts required segments. Progress is available with:

```bash
./scripts/macos/hogwarts-xbox-status.sh
```

## Next experiment

With the user present:

1. Launch the Xodus login flow through the Aqua LaunchAgent.
2. Complete Microsoft device authentication directly in the presented UI.
3. Confirm that the account has the PC Game Pass Hogwarts Legacy entitlement
   in the UK catalog.
4. Record the title/package identifier without recording account identifiers.
5. Download or stream the package.
6. Verify license acquisition and the macOS `prepare()` path.
7. Attempt launch with the pinned runtime architecture and report the first
   failing milestone from M1 through M12.
