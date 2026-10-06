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

Status as of **October 6, 2026**. This fork is developing a native macOS
launcher backed by Xodus, rather than relying on an unverified Heroic
extra-store plugin mechanism. Heroic remains a useful workflow reference.
The Swift launcher lives in the separate
[xodus-macos-app repository](https://github.com/dragoshont/xodus-macos-app).
Reviewed backend and app work is published on their respective feature
branches; this README is not a claim that it has all been merged or deployed.

| Area | Completed work | Remaining qualification |
| --- | --- | --- |
| Native launcher | The source-reviewed gamer-facing cleanup `b82ac96` is installed after exact-source shipping CI passed. Main-screen status walls are reduced, errors have scoped recovery copy, technical facets are collapsed, and unsupported Library capabilities are stated honestly. The signed engine was preserved byte-for-byte. | Live screenshots are blocked by macOS Screen Recording permission. Visual, keyboard, accessibility and resize acceptance remain unverified; no privacy prompt was approved or bypassed. |
| Store authentication | Installed `f30f1b18`/`d00a8b97` reads the saved Microsoft session after native Keychain approval. One authenticated Halo package read returned exact `verified:true`, without another Microsoft sign-in, credential copying or reset. The app is reopened and working. | This proves authentication and the checked package API, not ownership, entitlement or game execution. A different locally self-signed engine build may need its own native Keychain approval; identical signing requirements alone do not provide prompt-free updates. |
| Owned and local games | Public Xbox catalog/search and bounded selected-folder structural inspection exist. | Complete Xbox PC-owned inventory is unsupported by the current integration. `installed.snapshot` currently returns a constant empty list, not a Mac installation scan; inspected folders remain unverified and not launchable. Catalog, achievements, Game Pass availability and folder markers are not ownership or playability evidence. |
| Shipping pair admission | Fixed bundled engine/helper paths and stage-generated compiled identities; missing approval or changed files fail before management, authentication mutations or runtime planning. | Controlled local operator approval only, not general Developer ID distribution attestation. HELLO alone grants no trust. |
| Account presentation | Toolbar, Account, Settings and Library share freshness-aware account state. Failed status checks remove saved-account claims while retaining the safety snapshot; a pending flow does not claim that a Microsoft window opened. | Source-side UI changes do not establish live authentication, PC ownership or entitlement. |
| Runtime selection | CrossOver, GPTK 3, GPTK 4 and standalone Wine configuration/planning foundations. **First-release dependency: a genuine, separately installed official CrossOver copy.** Other runtime tracks remain experimental, not supported first-release alternatives. | **Configuration/planning only.** No installation discovery, prefix creation/migration, device preflight or game execution is certified by these plans; `launchable` remains `false`. No CodeWeavers endorsement is implied. |
| Application shutdown | Normal Quit fences new work, cancels and joins the planning child through cleanup/reap, and awaits management shutdown. Failed cleanup refuses Quit and preserves ownership for retry. | Live application/game qualification remains distinct from neutral process checks. |
| Public runtime components | Scoped offline transport, Windows RPS, TLS, graphics and async/shim checks with explicit source provenance and isolated fixtures. | These checks do not establish a production account/runtime pair, Store entitlement or general playability. |
| Preserved CrossOver controls | NMS and Hogwarts directly user-confirmed working; Hollow Knight reached its menu and quit normally. | Bounded existing controls only, not launcher-driven play, new-save coverage or certification of other configurations. |

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
- Freshness-aware account presentation: [8412f2c](https://github.com/dragoshont/xodus-macos-app/commit/8412f2c813ee8e29da6d1f19a847a157f21d4115).
  Its exact SDK 27 [hosted run](https://github.com/dragoshont/xodus-macos-app/actions/runs/37288191441)
  passed 689 checks, including fourteen new actual neutral-session regressions
  and a live/fixture separation check. Authentication mutation predicates and
  lifecycle methods remain unchanged.

- Production-hardening source: [00ba51f](https://github.com/dragoshont/xodus-macos-app/commit/00ba51fd5e48f6298d27a56e75f491f6f697696f),
  tree `9869d8ac428bd3f9ed2fa7418579097614066036`.
  Its exact SDK 27 [hosted run](https://github.com/dragoshont/xodus-macos-app/actions/runs/37314629896)
  passed shipping and development builds, 697 existing/expanded neutral checks,
  23 shipping assertions, 22 portable packaging checks, forbidden-entry checks
  and SVG reproducibility. The coordinator independently verified the run
  identity and reviewed the final delta against `8412f2c`.
- CrossOver-first UI: [95af7c0](https://github.com/dragoshont/xodus-macos-app/commit/95af7c0e07d84ef85672d3d1ea184955f97536bf),
  tree `a407e75dc6b9ad7cb411d6ece87f3095518b1e1c`.
  Its separate [SDK 27 run](https://github.com/dragoshont/xodus-macos-app/actions/runs/37318275541)
  passed 736 core/management/presentation/native/private-host checks,
  27 shipping checks and 22 portable packaging checks, plus forbidden-entry,
  resource and SVG checks. Approved CrossOver app signature/metadata detection
  runs off the UI thread; only unset profiles default to verified CrossOver.
  Explicit alternatives remain selected and require Experimental acknowledgement.
  Library, Account and Settings share dependency status without license or
  gameplay claims. The coordinator reviewed this focused delta independently.
- Final packaging-compatible source: [9be674d](https://github.com/dragoshont/xodus-macos-app/commit/9be674d28edaa79f376cc79592a09bedcf3cd6b7),
  tree `3c951506850fe534ecec4e6e628eb768ab0135b7`.
  Exact [SDK 27 run](https://github.com/dragoshont/xodus-macos-app/actions/runs/37323168881)
  passed **795 checks with zero failures**, plus shipping-entry/resource checks
  and SVG reproducibility. Follow-ups fixed Mac Python pin-file writing,
  deterministic neutral shutdown observation and native SwiftPM resource layout
  without weakening production authentication or cleanup.

All four earlier hosted runs passed with zero failures and reproducible SVG output.
The relevant source reviews are closed for their stated scopes. Their
authentication checks use neutral children and detached synthetic WebKit,
not real Microsoft credentials or a completed Store sign-in.

**Earlier release baseline:** the exact `9ef0f2` native arm64 Release engine is
built: unsigned SHA-256
`ca86296dfdab23c63c7ff7c5428f9e2feb2f9b4bf6757072a64dbe7695c34d06`,
21,315,008 bytes. Its adjacent provenance SHA-256 is
`d72edb61fb45b0c5f9e91343d4e68ee25e2345ea0df9a0eeb7b4f13ae9d36f6d`,
46,234 bytes. Both were independently rechecked, including exact source,
Release profile and native-Keychain features.

Final source `9be674d` now has a verified native staged package. The coordinator
checked 55 raw Git inputs, exact generated pins, the canonical helper receipt,
all 21 packaged files, protocol/provider resources and deep signature.
Its separately signed engine also passed a real anonymous Halo Store query:
one resolved PC product, four explicit metadata failures, unknown ownership
and compatibility, with continuation not followed. No authentication method
or helper execution was requested by that smoke.

**Current installed pair:** app
[f30f1b18](https://github.com/dragoshont/xodus-macos-app/commit/f30f1b1855c279cfe2c1c3391c5042f53cf76df7)
and backend
[d00a8b97](https://github.com/dragoshont/xodus-macos/commit/d00a8b97501a2ce1045d579e62568c5feb017ca8).
On October 6 the app owner observed a saved Microsoft session after human
sign-in, then verified that it remained saved after a normal app restart.
The subsequent close/retry update also preserved the saved session without
opening another browser or requesting credentials.
Those observations used the unchanged `397dd02` engine. After installing
the new engine, the single authenticated read returned `credentialUnavailable`.
The credential was not deleted: Mac inspection identified changed ad-hoc CLI
code identities and, at that stage, no available signing identity as the Keychain
access boundary. A local self-signed identity has since been created through an
approved CLI import: its Keychain key is permanent, nonextractable and
signing-only. Temporary newly generated key, PKCS12 and passphrase files were
removed; the existing private keys and Microsoft credential were not exported.
Signing failed with `errSecInternalComponent`; read-only inspection confirmed
the imported key's partition ACL permits only its importer, not `codesign`.
The user completed the scoped Terminal permission command; the new signer and
the known unused Xodus orphan key were the two approved targets. No global
certificate trust change was made. Actual signatures on both independently
built engines now pass strict verification and have identical requirements:
the fixed CLI identifier and the same certificate leaf, without a build hash.
The supervisor reported a user-directed reset of saved Xodus credentials;
the stable-signed reference subsequently saved a fresh login and read it after
restart. A newer engine with the same designated requirement could not read
that item with native interaction disabled. Apple Keychain independently
checks partitions: a local non-Apple signer may use a per-binary `cdhash`
partition even when its designated requirement is stable.
The installed update restores normal interactive foreground status reads.
The user must handle any native access prompt; anonymous startup and bounded
provider verification do not trigger permission dialogs. Human approval may
need to be repeated for a different engine build. On October 6 at 15:20, the updated engine returned `credentialPresent` and
one authenticated Halo package read returned exact `verified:true`.
The same saved profile and admitted signed binary were used, with no new
Microsoft login, credential copying, deletion or reset. Xodus was reopened
after the read. This closes current-binary login reuse and authenticated API
access, not complete owned-library, entitlement, installation or gameplay
qualification; signature equality alone remains insufficient.
Earlier observations establish persistence across the same engine identity, not authenticated provider use,
authoritative owned inventory or launcher-driven download/install/update/play.
The backend retains validated failed-exchange inputs only in Keychain for a
bounded retry window of at most five minutes and never extends their original
expiry. Credentials and sign-in/MFA/consent/Keychain prompts remain human-only.

For exact source/artifact identities, review dispositions and remaining
gates, see [the architecture and verification ledger](docs/xodus/architecture.md).
The [first-release specification](docs/xodus/release-v1-spec.md) defines the
CrossOver prerequisite, native authentication, real-data boundaries and
requirement-to-test acceptance contract used for ongoing development.
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
