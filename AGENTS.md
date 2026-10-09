# AGENTS.md



## Wine compatibility: upstream first

For every actually reached missing or incomplete Wine API:

1. Resolve the real Windows API and ABI first. A private ordinal must not be
   identified using Wine's different export numbering.
2. Before writing a new implementation, check current upstream Wine source,
   exports and related tests at an identified commit, then relevant wine-staging
   patches, WineHQ bugs, Proton, CodeWeavers source drops and ReactOS COM/WinRT
   code. Use a bounded separate research agent for fork research when requested;
   keep the product lane on the reached API. Record commits/patch names and
   source links; search summaries or an exported name alone are not proof.
3. If upstream has the required behavior, reuse or narrowly backport it with
   its necessary dependencies and tests into the matched experimental runtime.
   Do not replace the whole CrossOver engine just to obtain one API.
4. If upstream is absent, a stub, or still lacks the reached behavior, implement
   that behavior against measured native Windows semantics. No success-shaped
   placeholders, fabricated identity, authentication or service results.
5. Build and exercise the actual implementation, then immediately retry the
   original Xbox app. Keep support changes bounded to the reached prerequisite;
   consolidation and unrelated APIs remain deferred.
6. Preserve the upstream decision with the existing prerequisite evidence and
   report the original app's milestone ladder. Passing helper controls is not
   evidence that the Xbox window, sign-in or library works.

<!-- architrave:begin -->
<!-- Managed by Architrave's Python installer; edit the kit, not this block. -->
## Architrave

Read `architrave.config.json`. Use the `architrave` skill/agent for non-trivial
work; its small core is the canonical mission/worker/stop contract.
Work directly for cheap single-lane changes. Host sessions, context, worktrees,
permissions, lifecycle and model settings remain host-owned.

Ground in this repository; reproduce rather than reinvent. A knowledge profile
uses docs/scripts/schemas/tests, never an invented UI lane. UI uses configured
design source/map/tokens and platform guidance; backend uses its real contract.
Load those packs only for the relevant task.

Mutation defaults to deny. Never expose secrets, bypass human checkpoints,
manually edit Run state, blindly replay uncertain side effects or mistake
worker completion/compile for product PASS. Durable recovery and evidence use
`harness/architrave_runtime.py`; details in `knowledge/runtime-v2.md` on demand.
Run targeted checks first; required gates must pass before completion.
Consult architrave:cto at start and on stall inline; schedule the smallest
demonstrable product slice, not harness ceremony. Keep Run/worktree/key artifacts
private and ignored. No duplicate transcripts, plans or status files.
<!-- architrave:end -->
