<p align="center"><img width="128" src="assets/Icon/Icon.ico" /></p>
<h1 align="center">Xodus</h1>
<p align="center">The great gaming migration to Linux</p>
<p align="center">
    <a href="https://discord.gg/ZG774FK4tq">
        <img src="https://img.shields.io/discord/1123890623586504714?logo=discord&style=for-the-badge&color=red&label=Game+Launchers+Reverse+Engineering" alt="Discord" />
    </a>
</p>

> [!CAUTION]
> This is an unofficial project - use at your own risk. It is not affiliated with, endorsed by, or sponsored by Microsoft or XBOX; all trademarks, product names, and company names or logos mentioned herein are the property of their respective owners.

## Current state of the project

The project can now login, download packages and obtain licenses for games.

These parts are still quite scattered around.

- [x] Device login
- [x] User login
- [x] XBOX authorization
- [x] MSIXVC download
- [x] On-demand .exe decryption [#50](https://github.com/xodus-gaming/xodus/issues/50)
- [ ] MSIXVC2 support [#53](https://github.com/xodus-gaming/xodus/issues/53)

## macOS standalone evaluation results

Snapshot: 2026-10-05, Apple M5 Max, macOS 27.0.1. These observations concern
the acquired **Xbox-PC editions**, not substituted Steam or native Mac builds.
The evaluation used isolated prefixes and private compatibility components
that are **not included in this public repository**. It is not a claim that a
fresh checkout can reproduce these results or automatically configure these
games.

Standalone Wine 11 r17 was built from published Wine sources, separately from
the commercial CrossOver installation. Wine Staging 11.18 is a distinct
distribution. Apple GPTK 4.0b2 is an evaluation renderer with separate license
terms; availability does not imply unrestricted commercial redistribution.
Existing commercial CrossOver results remain comparison controls, not
standalone-runtime passes.

In a separate user-directed quick regression on 2026-10-05, the preserved
CrossOver 26.3 controls opened the same Xbox-PC editions of NMS, Hollow Knight
and Hogwarts. The user confirmed NMS and Hogwarts working; Hollow's full-size
menu, keyboard response and normal quit were directly observed. These quick
checks do not add standalone passes or certify new gameplay/save/relaunch
milestones. Original bottles and saves were preserved.

| Xbox-PC game / build | Standalone route actually exercised | Farthest verified result | Still incomplete or blocked |
|---|---|---|---|
| Hollow Knight / 1.5.12620.0 | Wine 11 r17 + Apple GPTK 4.0b2, dependency-complete profile | Full-size language UI, real keyboard confirmation, and an in-world scene with player HUD | Controlled movement and exact save/full-quit/relaunch verification remain pending |
| Hogwarts Legacy / 1.0.16.0 | Wine 11 r17 + Apple GPTK 4.0b2 | Shader preparation, character creation, opening cinematic and opening world with objective HUD | Controlled movement/combat, normal shutdown and exact save/relaunch remain unverified |
| No Man's Sky / 7.5.0.0 | Two Wine 11 r17/Vulkan profiles and two Wine Staging 11.18/Vulkan profiles | Four actual startup attempts hit the same startup fault. The final, single search-order correction removed the previous driver-path mismatch warning but still exited with status 5 | No standalone gameplay. No live process remained for a window or mapped-library capture on the final trial; disappearance of the warning is not proof of the actual mapped driver or a crash fix |
| Hades / 1.0.38246.0 | Wine 11 r17 + Apple GPTK 4.0b2 | Full-size title screen and verified keyboard delivery | Genuine user initialization fails on unsupported legacy configuration handling; no playable menu or save/relaunch |
| Balatro / 1.0.5.0 | Wine 11 r17/OpenGL and separate Wine Staging 11.18/OpenGL | Both actual trials exited with status 0 | No captured game frame or gameplay; exit status 0 is not a pass |
| Grounded / 2.4.438.0 | No standalone launch yet | Acquired package residency and original manifest verified | Rendering API, startup, authentication, controls and saves remain untested |

The No Man's Sky native prerequisite result establishes Vulkan instance
creation, discovery of the M5 Max and exposure of `wideLines` through the
selected driver. It does **not** establish Wine interoperability or game
compatibility or prove that a game process loads that same driver. The Staging
trial exposed a library-selection mismatch despite its explicit driver manifest;
actual loaded-library evidence matters, not just configuration intent.
The installed `GroundedControl` bottle is an old Hogwarts Steam
control; its name is not evidence that Grounded works.

For NMS, the working Xodus/CrossOver control and final standalone profile have
identical private adapter/legacy-companion/Windows.Web binaries and IPC bridge
source, matching Windows 10 build 19045 settings, and matching inspected
Direct3D/Mac-driver registry values. Both load Wine-provided C++ and Vulkan
bridge DLLs, but those binaries differ between distributions. These comparisons
rule out several simple setup differences; they do not establish the remaining
crash's root cause. The next useful diagnostic is the faulting call stack and
device-initialization return values, not another unchanged launch.

These variations inform profile selection, rather than a single universal
launch configuration:

- Keep Wine distribution, graphics backend/version, dependency bundle and
  prefix identity separate. The complete reference dependency profile took
  Hollow Knight further than the incomplete generic profile.
- Verify the actual loader contract. CrossOver's Vulkan selection variables
  are not interchangeable with Wine Staging's Vulkan loader and driver-manifest
  selection.
- Preserve valid publisher manifests. A legacy `configVersion="0"` manifest
  can legitimately omit `MSAAppId`; inventing an identity is not a fix.
- Record rendering, input, controlled gameplay and exact save/relaunch as
  separate milestones. Catalogue membership, package residency, a live
  process or successful exit alone proves none of them.

Dynamic configuration and catalog-wide compatibility remain goals, not
completed features. Existing accounts, bottles and saves are not imported into
these fresh profiles or replaced to manufacture a passing result.

## FAQ

**Q: What is Xodus**  
Xodus aims to bring XBOX PC games to Linux and possibly Mac devices.

**Q: When can I play my Minecraft Bedrock?**  
While Xodus is quickly maturing, there is still a lot of work to support it from Wine standpoint to provide necessary XBOX Services to games.  
_TL;DR_ soon<sup>tm</sup>

**Q: How to get involved?**  
Start by joining our Discord or review any open GitHub issues .

**Q: What games will be supported?**  
We hope to manage to support most of the catalog, the limitation is the game has to be GDK and in MSIXVC format.  
So far `Gears of War 4` is a prominent unsupported title for the time being.

**Q: Will XBOX Backward Compatibility on PC work?**  
While Xodus is capable of downloading and running those titles. It's possible these games will work only after additional patches to wine, dxvk or vkd3d-proton.

## Building

For the Apple Silicon remote-development environment used by the macOS
bring-up, see [macOS remote development setup](docs/macos-bringup/01-remote-development.md).

The project structure is as follows.

```
.
├── msixvc - [rlib] common rlib crate for utilities for parsing MSIXVC and XSP files
├── xodus - [rlib] common rlib crate that contains core xodus functionality, API calls abstractions and utilities
├── xodus-cli - [bin] CLI currently used for iterating over new xodus features
└── xodus-service - [bin] service process exposing a xodus.sock for IPC communication, it takes care of xgameruntime.dll integration.
```

> [!NOTE]
> xodus-service aims to become a main point of integration. All xodus clients will connect to it to interact with games and XBOX services.

### Prerequisites

- Rust version 1.98 or later
- Right now CLI relies on wry and tao to show a login page. Consult https://docs.rs/wry/latest/wry/#platform-considerations
- xodus-service relies on `protoc` to compile `proto/` definitions make sure to install it for your platform

### Running

Building all crates in release mode

```bash
cargo build --release --workspace
```

Running cli in debug

```
cargo run -- --help
```

Running xodus-service in debug

```
cargo run --bin xodus-service
```

Debug and profile `xodus-cli` or `xodus-service` with [tokio-console]([tokio-console](https://github.com/tokio-rs/console))

```
RUSTFLAGS="--cfg tokio_unstable" cargo run --features tokio_console 
```

> [!WARNING]
> For better performance when decrypting MSIXVC files, the `aes` and `ssse3` features are enabled on `x86_64`,
> and the `aes` feature is enabled on `aarch64`. This means that the program will crash with an illegal instruction
> error when running on a CPU which doesn't support those instructions.
>
> See https://en.wikipedia.org/wiki/AES_instruction_set for a list of compatible CPUs (every processor from
> 2011 onwards should be supported).

### CLI Usage

```
Usage: xodus-cli <COMMAND>

Commands:
  download    Download msixvc or xsp files for a given game
  license     Dump CIKs for use with XvdTool
  extract     Extract locally stored msixvc file
  login       
  streaming   Download and extract the game through streaming algorithm
  clep        Generate or decrypt base64-encoded CLEP challenge data
  sp-license  Decode SPLicenseBlock
  help        Print this message or the help of the given subcommand(s)

Options:
  -h, --help     Print help
  -V, --version  Print version
```

## Special Thanks

- [XvdTool.Streaming](https://github.com/LukeFZ/XvdTool.Streaming) and [CikExtractor](https://github.com/LukeFZ/CikExtractor) by LukeFZ
