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

## macOS fork progress

Status as of **October 4, 2026**. This fork is developing a native macOS
launcher backed by Xodus, rather than relying on an unverified Heroic
extra-store plugin mechanism. Heroic remains a useful workflow reference.
The Swift launcher lives in the separate
[xodus-macos-app repository](https://github.com/dragoshont/xodus-macos-app).
Reviewed backend and app work is published on their respective feature
branches; this README is not a claim that it has all been merged or deployed.

| Area | Completed work | Remaining qualification |
| --- | --- | --- |
| Native launcher | Shared live/fixture header with artwork-toned native chrome, navigation, search and no visible app title; native Runtime Settings. | Fresh live visual/resize verification and deployment of the reviewed app. |
| Store authentication | Swift AppKit/WKWebView helper, explicit executable/hash/version binding, bounded private communication and cancellation/cleanup; reviewed direct-notification and continuation-writer fixes. | A matching new native engine/app/helper build and one user-assisted end-to-end Store login. The reported face/fingerprint stall is not diagnosed or certified fixed. |
| Helper preflight | Packaged apps missing both helper and receipt now fail before engine launch with an actionable helper-specific error; unpackaged anonymous checks remain supported. | General engine provenance/admission is still separate: HELLO capabilities and a valid helper receipt do not attest an arbitrary engine. |
| Runtime selection | GPTK 3, GPTK 4, legitimately installed CrossOver and standalone/source-built Wine presets; independent declared engine/graphics versions, provenance and hashes; fresh isolated generation plans. | **Configuration/planning only.** No installation discovery, prefix creation/migration, device preflight or game execution is certified by these plans; `launchable` remains `false`. |
| Application shutdown | Normal Quit fences new work, cancels and joins the planning child through cleanup/reap, and awaits management shutdown. Failed cleanup refuses Quit and preserves ownership for retry. | Live application/game qualification remains distinct from neutral process checks. |
| Public runtime components | Scoped offline transport, Windows RPS, TLS, graphics and async/shim checks with explicit source provenance and isolated fixtures. | These checks do not establish a production account/runtime pair, Store entitlement or general playability. |

### Reviewed source and evidence

- Backend authentication source: `94353b5`; separate pure `runtime-plan`
  implementation: [9ef0f2](https://github.com/dragoshont/xodus-macos/commit/9ef0f298481fb48840734b538e0f6d22e1c98ff3).
  The provider follow-up preserves the authentication boundary.
- App authentication corrections: [6196399](https://github.com/dragoshont/xodus-macos-app/commit/6196399e7390eca854a5c50a1a4356287041c565).
  The exact SDK 27 [hosted run](https://github.com/dragoshont/xodus-macos-app/actions/runs/37208819522)
  passed 562 checks.
- Runtime integration and Quit correction: [43d0d19](https://github.com/dragoshont/xodus-macos-app/commit/43d0d190517a545f856b123749bdec10a885ced1).
  Its exact SDK 27 [hosted run](https://github.com/dragoshont/xodus-macos-app/actions/runs/37212933441)
  passed 668 checks. All 58 raw native build inputs were independently
  matched to immutable Git blobs; release artifact hashes were also verified.
- Helper preflight correction: [6a2103c](https://github.com/dragoshont/xodus-macos-app/commit/6a2103cfd0e2d263918039122990ac9492ec17f3).
  Its separate SDK 27 [hosted run](https://github.com/dragoshont/xodus-macos-app/actions/runs/37226882643)
  passed 674 checks, including six new helper-admission checks.

All three hosted runs passed with zero failures and reproducible SVG output.
The relevant source reviews are closed for their stated scopes. Their
authentication checks use neutral children and detached synthetic WebKit,
not real Microsoft credentials or a completed Store sign-in.

**Current login gate:** an isolated native Release build of the matching
reviewed engine is being prepared for a controlled Mac test. Packaging,
separate signed-copy identities, helper receipts and the actual selected
engine must be verified before opening the app for the user. The previous
installed pair is preserved; its older engine cannot activate the reviewed
Swift-helper flow. No successful login on the new pair is claimed.

For exact source/artifact identities, review dispositions and remaining
gates, see [the architecture and verification ledger](docs/xodus/architecture.md).
Component evidence is recorded separately in
[public RPS](runtime/public-rps/README.md),
[public macOS](runtime/public-macos/README.md) and
[public async](runtime/public-async/README.md).

## Upstream project state

The following summarizes the inherited upstream capabilities, not verified
end-to-end authentication or game compatibility in this macOS launcher.

The project can now login, download packages and obtain licenses for games.

These parts are still quite scattered around.

- [x] Device login
- [x] User login
- [x] XBOX authorization
- [x] MSIXVC download
- [x] On-demand .exe decryption [#50](https://github.com/xodus-gaming/xodus/issues/50)
- [ ] MSIXVC2 support [#53](https://github.com/xodus-gaming/xodus/issues/53)

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
- The inherited standalone CLI login uses wry and tao. The reviewed managed
  macOS launcher path instead requires its explicitly paired Swift helper;
  these are distinct build/integration paths. For the inherited path, consult
  https://docs.rs/wry/latest/wry/#platform-considerations
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
