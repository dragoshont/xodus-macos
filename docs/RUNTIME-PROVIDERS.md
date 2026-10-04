# Runtime provider configuration v1

This separate source-only contract exposes all four provider choices: GPTK3,
GPTK4, legitimate user-installed CrossOver and user-selected standalone or
source-built Wine. Selection starts unconfigured. GPTK4 is an individual trial
default, not a product-wide restriction. No runtime, game, device, existing
bottle, save, credential or provider process is inspected or modified here.
Management C95, its 25 operations/capabilities and null runtime fingerprint
remain unchanged; install/play stay gated.

App-consumable source entry is `xodus runtime-plan`, separate from management.
Write exactly one Configuration JSON object to stdin and close the write end
to supply EOF. Exit zero returns exactly one ConfigurationPlan JSON object
plus LF on stdout; invalid input or IO failure returns nonzero, with a static
stderr diagnostic and no valid success plan. The caller must discard output
on nonzero exit and own its cancellation/deadline. Input is byte-bounded even
without EOF; a short input requires EOF, so callers must not keep stdin open.
The route returns before account/log initialization and never executes a
provider, probes a device, reads a runtime path or creates a prefix. Deployment
of a new binary is separately gated; this entry is not present in frozen
authentication engine source943 or any held production artifact.

Canonical schema: `contracts/runtime-providers-v1.schema.json`, namespace
`urn:xodus:runtime-provider-configuration:1`. Neutral configuration/plan and
negative fixtures: `fixtures/runtime-providers-v1.json`. Rust implementation:
`xodus_management::runtime_provider`. Input JSON is 1..16384 UTF-8 bytes;
duplicate, unknown, incompatible, missing nullable and invalid metadata fields
are explicit `INVALID_REQUEST` failures, never an installed/launchable fallback.

Configuration exact keys are `version`, `provider`, `providerVersion`, `engine`
and `graphics`. Engine keys are `kind` (`wine`), `provenance`, `version` and
`artifactSha256`. Graphics keys are `backend`, `provenance`, `version` and
`artifactSha256`. Nullable keys are required and use null when unknown.
Version strings are bounded printable ASCII, not inferred from a provider label;
artifact identities are nullable lowercase SHA256, declared rather than
attested. Provider/package, Wine engine and graphics component versions are
independent. Wine11 with D3DMetal4 is admissible configuration, not GPTK Wine7.7
equivalence or demonstrated compatibility.

| Provider value | Engine provenance | Proposed preset graphics |
| --- | --- | --- |
| `gptk3` | `appleToolkit` | `d3dMetal` / `appleToolkit`, version unknown |
| `gptk4` | `appleToolkit` | `d3dMetal` / `appleToolkit`, version unknown |
| `crossover` | `userInstalledCrossOver` | null/unconfigured, not a guessed renderer |
| `standaloneWine` | `userSelectedWine` or `userSelectedSourceBuild` | null/unconfigured |

Graphics backends are `d3dMetal`, `dxvk`, `wineD3d` or null. Provenance is
`appleToolkit`, `crossOverBundled`, `engineBundled`, `userSelected` or null.
A configured backend requires provenance; a null backend requires all graphics
metadata null. Preset choices are proposals, not observed installation proof.
No commercial Apple redistributability claim, copied CrossOver, licensing
bypass or globally installed provider discovery is included.

`plan` validates real configuration and returns a fresh generation UUID and
safe relative path `runtime-prefixes/v1/CONFIGURATION_SHA256/GENERATION_UUID`.
The configuration identity covers provider/version, engine identity/version/
provenance and graphics identity/version/provenance. The reference hashes the
compact Rust serialization in documented field order; consumers may use the
returned plan and must not assume arbitrary JSON key order hashes identically.
Every plan allocates a new generation, including either component change;
there is no path reuse, migration, prefix creation or save relocation. Persisting
a plan and later reusing its generation requires a separate checked lifecycle,
not a call to this planning function.

Plan fields are `version`, `configuration`, `generationID`,
`prefixRelativePath`, `installation`, `devicePreflight`, `gameVerification`
and `launchable`. The latter four are independently `notInspected`,
`notPerformed`, `notVerified` and false. Configured metadata or declared hashes
never promote these evidence states. Installed component verification,
device/backend preflight, exact per-game/version results, authorized content,
licensed RPS/runtime pairing and supervised launch remain separate release
gates. No trial certificate transfers across an engine/backend generation.
