# Xodus macOS first-release specification

Status: implementation contract, October 5, 2026. This is not a release or
compatibility certificate.

## Outcome and scope

Deliver the existing native launcher with an official, separately installed
CrossOver dependency, native Microsoft sign-in integration and real Xbox/
Microsoft Store query results. Reinstate the existing CrossOver game regressions.
Keep unsupported ownership, installation and gameplay capabilities explicit.

Reuse the existing SwiftUI/AppKit app, reviewed engine, management protocol and
owners. Do not introduce a new launcher architecture, worker, runtime provider
API, credentials broker or orchestration system. The hardening ancestor is
app `00ba51fd5e48f6298d27a56e75f491f6f697696f`; new requirements need an additive
source commit and its own green CI before packaging.

## Governing decisions

- Official CrossOver is the first-release supported runtime dependency. Wine,
  GPTK and alternative graphics configurations are experimental/preview tracks.
- CrossOver must be installed separately. No commercial binary redistribution,
  automated purchase/download, license inference or CodeWeavers endorsement.
- Actual installation identity, credentials, entitlement and per-game
  compatibility are separate facts. None proves the others.
- Shipping data comes only from actual backend results. Tests may use synthetic
  protocol peers; shipping views must never construct or fall back to demo data.
- Builds have no CPU lease prerequisite. Foreground/input/capture, human
  authentication windows and exact shared mutation targets require coordination.
- Credentials, MFA, passkeys/biometrics, consent and Keychain approval are human
  actions. Stop at a genuine human checkpoint, not at internal code/test phases.

## Reviewed backend contract

Producer: `9ef0f298481fb48840734b538e0f6d22e1c98ff3`, tree
`8b2f7abb54f91e347afe013eee18c93873b111a5`. Negotiate capabilities; a command
being present in the C95 schema does not mean the production adapter supports it.

| Surface | Actual contract and limits |
| --- | --- |
| Public catalog | `catalog.search` searches checked cached products; `catalog.discover` and `catalog.query` provide public discovery/retail queries when advertised. Preserve market, language, source, timestamp, continuation and partial failures. |
| Product details | `product.detail` reports real checked metadata. Catalog presence is not ownership, package authorization or compatibility. |
| Account | Native-bound `auth.begin`, `auth.cancel`, `auth.status`, `auth.logout` report isolated Store-credential flow state. Saved credentials do not grant entitlement authorization. |
| Owned inventory | `inventory.snapshot` exists in the schema but is not advertised by this reviewed production adapter. Show unavailable/auth-required context, not an empty owned account or a fake library. Successful sign-in alone cannot enable it. |
| Installed registry | `installed.snapshot` reports `managementRegistryOnly`; the reviewed adapter currently returns an empty registry. This is not an OS-wide scan and says nothing about existing CrossOver bottles or games. |
| Inspection | `installed.inspect`, when advertised on macOS, is read-only local folder inspection, not ownership or a playable installation. |
| Jobs | Supported enqueue/cancel/retry/snapshot/replay operations expose real catalog-check work. They are not game downloads. Pause/resume must remain unavailable unless advertised. |
| Package/game mutations | Authorized install/package lifecycle and game launch/update/rollback/remove are not implemented in this adapter. Do not enable these from schema presence, a saved credential or an empty registry. |
| Runtime plan | Separate pure `runtime-plan` validates configuration without executing a provider. Output stays `installation=notInspected`, `devicePreflight=notPerformed`, `gameVerification=notVerified`, `launchable=false`. |

Authoritative owned inventory or actual download/install/play requires a
separately specified backend capability and real authenticated results. It is
not part of a UI-only completion claim.

## Requirements and acceptance

| ID | Requirement | Required acceptance evidence |
| --- | --- | --- |
| RT-01 | Detect an official CrossOver installation read-only from approved bundle locations, bounded metadata and an Apple-anchored approved publisher/bundle signature requirement. Reject absent, malformed, linked/untrusted or identity-mismatched candidates. | Neutral cases for genuine identity, absence, wrong identifier/signer, malformed metadata and verification failure. Real approved publisher pin has a recorded trust source; never approve a candidate's self-reported identity. |
| RT-02 | For a new/unset runtime profile, prefer verified installed CrossOver. If absent/unverified, show a blocking gameplay prerequisite with official installation information and Settings access. | Tests for new/unset installed and absent profiles; no fabricated installed/default success or provider execution. |
| RT-03 | Preserve an existing explicit runtime choice. Label alternatives Experimental/Preview and require explicit acknowledgement before using the experimental configuration. Reset acknowledgement when the relevant selection changes. | Persisted alternative survives startup/reconnect; planning/use is fenced before acknowledgement; changing selection invalidates acknowledgement. No silent rewrite of user configuration. |
| RT-04 | Library, Account, Settings and runtime-plan presentation agree on dependency status while keeping installation, account, entitlement and playability distinct. | Actual presentation tests for absent/verified/unverified CrossOver and experimental selections. Public browsing and sign-in must not falsely require a gameplay-ready installation. |
| AU-01 | Packaged native sign-in uses only the approved compiled engine/helper pair and bounded private channel. No developer override, arbitrary helper or HELLO-based trust. | Shipping positive/negative admission, changed-file prelaunch/mutation checks and exact final package identities. |
| AU-02 | Report genuine pending, cancellation, timeout, stage error, status refresh, logout and reconnect outcomes; keep outstanding-writer, cancellation and Quit fences intact. | Actual session/client tests with neutral protocol peers covering each outcome and cleanup. No prompt automation or fabricated success. |
| AU-03 | Stale account snapshots never drive saved-account icons/copy, ownership or expiry advice. Pending state does not assert that a window opened. | Saved-to-denied, expired-to-denied, HELLO-only, unknown-pending and fresh-recovery regressions remain green. |
| AU-04 | Open the genuine Microsoft sign-in window only after verified package/engine admission and explicit foreground ownership. Stop for human credentials/MFA/consent/Keychain approval. | Bounded app-owned window/process observation and sanitized terminal/cleanup result. Real successful credential commit is a separate human-assisted acceptance gate. |
| DA-01 | Discover/search/detail show actual backend catalog responses, never fixture fallback. Keep scope and continuation truthful. | Neutral transport/UI tests plus an anonymous real supported catalog/query smoke against the packaged engine; no auth/credential-store call in anonymous smoke. |
| DA-02 | Owned-library content requires an advertised authoritative operation and real authenticated ownership results. Otherwise display unavailable/auth-required/unchecked context correctly. | Capability-missing and auth-required tests; protocol-boundary authenticated cases do not certify a real production ownership implementation. |
| DA-03 | Installed/version/playability presentation reflects actual registry scope and runtime capability evidence. Empty management registry is not "no games installed on this Mac." | Scoped empty registry, inspected-but-not-playable and missing capability tests. No bottle/prefix discovery or invented installation. |
| DA-04 | Activity displays actual job progress, cancellation/retry/recovery and replay results. Unsupported mutation controls stay unavailable. | Real-protocol job snapshot/event/cancel/retry/reconnect tests; distinguish catalog checks from downloads and incomplete/failed work from success. |
| DA-05 | Every surface has truthful loading, empty, partial failure, offline, stopped and auth-required states. Late startup or stale responses cannot replace a newer scope. | Loading-versus-empty, query generation, continuation partial-failure, cancellation and reconnect tests. |
| SH-01 | Fixture/demo views, data, artwork, resources and executable test entry points are physically excluded from shipping builds. No failure path activates them. | Shipping compilation, forbidden-argument/resource assertions and actual fresh initial-state tests; retain nonshipping test tooling. |
| UX-01 | Preserve native Apple Games-inspired composition, keyboard/focus, semantic states and accessibility. Use real product art only with established provenance; missing art has a neutral truthful fallback. | Existing UI checks plus bounded packaged keyboard/focus/resize/accessibility observation when foreground is available. No synthetic promotional game artwork. |
| RG-01 | Reinstate the preserved official CrossOver regression controls: Xbox-PC No Man's Sky first, then existing Hollow Knight/Hogwarts controls within their authorized scope. | Existing game owner records exact runtime/game/control identities, launch/render/input result and cleanup; no transfer of certification between games or runtime generations. |
| RL-01 | Package immutable final green source in a new isolated stage, using externally approved engine/provenance and separately signed copies. Verify generated compiler pins, helper receipt, C95/provider resources, source inputs and deep signature. | Final launcher/helper/engine hashes and sizes, receipt and resource checks; original sealed engine remains unchanged. |
| RL-02 | Atomically replace only the approved app target after validation, preserve rollback bytes and user data, and automatically restore the old bundle on failed deployment verification. | Before/after bundle identities, install path, mutation/rollback receipt and inert/anonymous smokes. Do not terminate an active human game/auth process to replace files. |

## Implementation and test order

1. Implement RT-01 through RT-04 as one bounded additive app slice; map
   requirements to named tests and run fresh SDK 27 CI.
2. Reuse existing auth/catalog/registry/job code for AU/DA/SH requirements.
   Add only demonstrable gaps and focused tests; do not invent missing APIs.
3. Independently review each coherent source delta, then package and deploy
   the final green immutable source under RL-01/RL-02.
4. Run anonymous catalog and inert package checks. Coordinate the genuine
   native sign-in window after the game lane releases foreground.
5. Report the human login and live UI gates separately. The existing game
   owner may run RG-01 concurrently with builds, but owns foreground serially.

## Current evidence and open gates

- `00ba51f` passed exact hosted run `37314629896`: shipping/development builds,
  original/expanded neutral checks, shipping admission/resource/argument checks
  and SVG reproducibility. It is the hardening ancestor, not the CrossOver-first
  release candidate.
- The reviewed native Release engine and provenance have independently checked
  exact SHA-256/size/ownership/profile/features; immutable identities are in the
  architecture ledger. HELLO is not artifact attestation.
- The existing game owner supplied a user-approved official installation
  baseline: `/Applications/CrossOver.app`, observed version `26.3`, build `26.3.0.39832`,
  signed identifier `com.codeweavers.CrossOver`, publisher team `9C6B7X7Z8E`.
  Deep strict signature verification passed and Gatekeeper accepted its
  notarized Developer ID. RT-01 pins the identifier/team and Apple Developer ID
  certificate chain, rather than trusting candidate metadata. This is scoped
  trust from the approved installation, not a separately retrieved vendor team
  allowlist, license check or compatibility claim.
- The coordinator independently matched that installation to the fixed
  identifier/team/Apple certificate requirement without executing CrossOver or
  inspecting games, prefixes or licensing.
- DA-01 has real engine network-to-wire evidence: one anonymous
  `catalog.query` for `Halo`, US/en-US, limit 5, returned live `Halo Infinite`
  (`9PP5G1F0C2B6`) and four explicit unresolved metadata failures on
  October 5 at 13:30:00 UTC. The result is partial; continuation was not followed.
  Entitlement, installability, compatibility and inventory remain unknown.
  Only HELLO and the query ran; the isolated child exited and was reaped.
  This is not yet a packaged shipping-UI acceptance result.
- NMS was confirmed working directly by the user on the preserved commercial
  CrossOver control. The other two quick control opens remain pending; neither
  this observation nor custom graphics control transfers gameplay certification.
- Native successful human login, accessible live packaged UI and authenticated
  owned inventory/download/install/play are not yet certified.

Completion reports must distinguish implemented source, green neutral tests,
verified package, deployed bundle, observed human authentication and per-game
runtime results. A spec, build, synthetic test or configured provider is never
substituted for the corresponding live product evidence.
