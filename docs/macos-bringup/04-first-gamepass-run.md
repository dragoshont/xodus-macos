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

## Not attempted

The following were intentionally not attempted while the user was unavailable:

- Microsoft/Xbox device login;
- Game Pass library enumeration;
- Grounded entitlement selection;
- Grounded package download;
- license retrieval;
- encrypted executable preparation;
- Game Pass executable launch.

No credentials, authentication payloads, or license material were captured.

## Next experiment

With the user present:

1. Launch the Xodus login flow through the Aqua LaunchAgent.
2. Complete Microsoft device authentication directly in the presented UI.
3. Confirm that the account has the PC Game Pass Grounded entitlement.
4. Record the title/package identifier without recording account identifiers.
5. Download or stream the package.
6. Verify license acquisition and the macOS `prepare()` path.
7. Attempt launch with the pinned runtime architecture and report the first
   failing milestone from M1 through M12.
