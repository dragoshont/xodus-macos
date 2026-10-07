# Xbox PC application API inventory and delivery specification

Updated: October 6, 2026. This is the spec-driven development inventory, not a
list of guessed endpoints or a claim that the application is complete.

## Product contract

Build the approved native macOS Figma flows with real, correctly classified
Xbox/Microsoft Store data. **Library means verified PC ownership or current PC
subscription access, not recently played history.** Store availability,
entitlement, installation and Mac runtime compatibility are separate facts.

The previous TitleHub integration returns real Xbox activity, including console
titles. It must not populate the owned-PC Library, its count, featured game or
Continue Playing shelf. A PC activity tag or title-name search does not establish
a PC Store edition or ownership. Recent activity may exist only in a clearly
separate scope. No placeholder games or simulated progress enter shipping.

## Evidence levels

| Level | Meaning |
| --- | --- |
| Live service | Our bounded legitimate request returned an actual result. |
| Official-client observation | The installed Xbox PC app performed the operation and its request/response semantics were verified with safe evidence. |
| Source-backed | A directly inspected implementation or official contract identifies the operation; live coverage may be missing. |
| Unverified | A required capability whose real endpoint, local interface or semantics have not been established. Do not invoke a guessed substitute. |

An endpoint's existence is not evidence that our account can use it. A successful
token exchange is not an entitlement grant. Public package metadata does not
prove authoritative payload integrity or expanded installation size.

## Required operation inventory

Unknown routes remain explicitly unknown. The internal Xodus management methods
are adapters we implement; they are **not names of Xbox app network endpoints**.

| ID | Product operation | Current evidence | Required investigation or acceptance |
| --- | --- | --- | --- |
| API-01 | Saved Microsoft account and Xbox service authentication | Live service: existing Exodus saved proofs work; foreground Keychain access and relying-party-specific Xbox exchanges are implemented. | Compare the legitimate desktop client's audiences/scopes and renewal behavior without exporting credentials. Preserve the working reference. |
| API-02 | Purchased Xbox/Microsoft Store PC library | **Unverified.** TitleHub is not this inventory. | Observe the real Xbox PC app's owned-library flow; establish route/local interface, identity, paging, product/SKU identity, purchased/free access semantics and removals. |
| API-03 | Active PC subscription access | **Unverified.** Public Game Pass catalog presence is not account access. | Identify account-specific PC subscription state, applicable editions, expiry/removal and any household/shared-access semantics from the actual client. |
| API-04 | PC Store availability and edition mapping | Source-backed and live public metadata: DisplayCatalog v7 selected products/SKUs/packages. | Resolve the exact edition and Windows.Desktop package. Do not map Xbox title IDs or names into a Store identity by inference. Console-only products must not enter PC shelves. |
| API-05 | Browse and text search | Live public Store queries/artwork, partial results and explicit title-name search routing. | Confirm the desktop app's browse/search source, scope, continuation and regional behavior. Public results remain candidates, not owned games. |
| API-06 | Product details and artwork | Live public metadata and actual bounded image decoding. | Preserve locale/market, actual BoxArt/Poster/hero roles and safe canonical image origins. No borrowed or synthesized artwork. |
| API-07 | Base package metadata | Source-backed existing `get_packages_verified`; one bounded authenticated Halo package check succeeded. | Establish actual desktop request shape, applicable package/version/content identities and dependency selection. That earlier check did not acquire a license or install a game. |
| API-08 | Architecture/language/framework applicability | Direct public model inspection and anonymous official metadata confirm plural `Architectures`, `Languages`, framework/hardware/platform declarations and format fields. | Validate selected-package declarations; missing, malformed, mismatched and unresolved requirements differ. A declaration is not proof that CrossOver satisfies it. |
| API-09 | License/key acquisition | Source-backed existing content-license POST at `licensing.mp.microsoft.com/v7.0/licenses/content`; current helper requests `Rude`, `needKey:true`, `keyOnly:true`. No new live issuance is admitted. | Observe legitimate-client behavior and freeze exact target/consent. Assess concurrency effects. Potentially sent interrupted issuance is outcome-unknown and cannot be blindly replayed. |
| API-10 | Download sources and authoritative integrity | Private source-selection primitives exist. Update `FileHash`/`HashOfHashes` meaning remains unproven for the supported PC payload. | Establish algorithm, encoding, byte coverage, source binding, redirects and expiry. Public AppxBundle SHA256 does not prove MSIXVC semantics. |
| API-11 | Format/layout/space requirements | Existing MSIXVC primitives are incomplete qualification, not an installation certificate. Public MSIXVC install-size values observed were nonpositive. | Establish authenticated layout, encryption/key boundary, padded file sizes, dependencies, scratch and peak space. Revise consent explicitly if pre-key planning cannot be trustworthy. |
| API-12 | Download/install jobs and cancellation | Internal management jobs currently implement catalog refresh; staging primitives exist separately. | Observe actual client stages and measured progress, then implement our own owned staged transaction, bounded cancellation and crash reconciliation. Never rename catalog jobs as downloads. |
| API-13 | Installed games and health | Frozen source-only StagingStore registry reader and strict consumer preparation; current shipping engine still has the old placeholder snapshot. | Inspect the Windows client's Gaming Services/local inventory interface separately. Mac managed records come from our committed registry, not a machine-wide scan or Xbox ownership history. |
| API-14 | Update/repair/rollback | Reusable local staging/recovery primitives; no complete game update driver. | Observe full-version/dependency selection, license requirements, running-game conflicts and recovery. First usable implementation uses verified full-base replacement; delta updates stay deferred. |
| API-15 | Uninstall and save retention | No complete managed removal implementation or per-game save policy. | Establish owned paths, confirmation, save separation, tombstones and restart recovery. Preserve external games and user bottles. |
| API-16 | Launch/runtime/license integration | Official CrossOver baseline and historical manual controls; management runtime planning is not gameplay. | Establish actual Xbox PC launch/license/Gaming Services requirements and an exact legitimate CrossOver pairing. Verify render/input/exit for one supported installed game. |
| API-17 | Continue Playing | Real activity dates exist, but no launcher-managed playable installation is verified. | Populate only from an actual installed, launch-qualified game and relevant activity. History alone cannot enable Play or a Continue Playing shelf. |
| API-18 | Failure/reconnect/account change | Existing generation/profile/read-budget fences and fixed errors are implemented. | Test the real inventory's account changes, offline/cache labeling, revoked access, interrupted work and safe retry without success-shaped empty fallbacks. |

### Already established routes, with narrow meaning

- Public product lookup:
  `https://displaycatalog.mp.microsoft.com/v7.0/products/{productID}` with the
  actual requested market/language and device family. This is catalog metadata,
  not an account's collection.
- Played history:
  `https://titlehub.xboxlive.com/users/xuid(<private-XUID>)/titles/titlehistory/decoration/image`
  with a bounded item limit and the Xbox relying party `http://xboxlive.com`.
  XUID stays private. This operation is deliberately classified as activity.
- Existing package reads use the separate `update.xboxlive.com` relying party.
  Exact request serialization is grounded in the fork's `package.rs` and
  upstream implementation; this inventory does not invent additional routes.
- Content-license issuance is a side effect, not an owned-library query. Its
  source-backed endpoint above is not permission to call it.

Do not add an ownership endpoint string to this document until its actual
client/source evidence is identified. Publisher-only collection APIs, public
Store discovery, achievements and installed-directory markers are not
interchangeable consumer inventory sources.

### Identified inventory leads: not admitted calls

The backend investigation identified these concrete references. None has been
queried for this account or accepted as the modern Xbox PC Library source.

| Lead | Evidence | Boundary |
| --- | --- | --- |
| Consumer collection query | Pinned community client [`threesecond/my-xbox-library@472e6ac`](https://github.com/threesecond/my-xbox-library/blob/472e6ac91d2801ed3d210b26be079d3664693a11/library_sync.py#L264-L291) implements POST `https://collections.mp.microsoft.com/v7.0/collections/query`, Xbox XSTS RP `http://mp.microsoft.com/`, private XUID beneficiary and top-level continuation token. | Source-backed only, not verified desktop-client traffic or our saved-auth compatibility. Do not copy plaintext token/raw-response storage, default missing status to Active, infer ownership from Install/Buy actions, or assume an empty intermediate page completes the collection. |
| Legacy Xbox Inventory | [Official inventory GET contract](https://learn.microsoft.com/en-us/gaming/gdk/docs/reference/live/rest/uri/marketplace/uri-inventoryget?view=gdk-2604) documents `https://inventory.xboxlive.com/users/me/inventory` and paging. | Modern complete PC Store/SKU coverage is unproven. Its documented licensing RP is distinct from TitleHub; time-window availability does not prove PC Store applicability. |
| Partner Store collections | [Official publisher query](https://learn.microsoft.com/en-us/gaming/gdk/docs/store/commerce/service-to-service/microsoft-store-apis/xstore-v9-query-for-products?view=gdk-2604) and service authentication contracts. | Publisher/business-partner coverage and required product/SKU filters are not a generic consumer-library API. |
| Store versus player account | [Official PC account-mismatch guidance](https://learn.microsoft.com/en-us/gaming/gdk/docs/store/commerce/pc-specific-considerations/xstore-handling-mismatched-store-accounts?view=gdk-2604). | The Xbox player account and Microsoft Store purchasing account can differ. Player history/XSTS identity cannot silently become Store ownership. |

Next evidence must identify the legitimate desktop client's operation, intended
Store/player account relationship, complete pagination, acquisition/status/
expiry and subscription/bundle semantics, and exact same-SKU PC package mapping.
Include a user-known owned but never-played PC control and a console-only
exclusion control. An HTTP-looking relying-party identifier is not permission
to use insecure HTTP transport.

## Installed Windows reference

Read-only package discovery confirmed this machine has:

| Component | Installed version |
| --- | --- |
| Microsoft.GamingApp | 2609.1001.16.0 |
| Microsoft.GamingServices | 38.117.27001.0 |
| Microsoft.WindowsStore | 22608.1401.5.0 |

These are useful legitimate reference clients. Their presence does not prove
which account is signed in or authorize a purchase, license issuance or install.
Start with package manifests, public code/models and safe diagnostic metadata.
Observe one named UI operation at a time. Distinguish HTTPS services from local
Gaming Services calls; do not pretend every requirement is a REST endpoint.

### Read-only installed-client findings

The accessible manifests declare separate GameCatalog, PlatformStore account,
Store/licensing/package queue/content access and Xbox authentication interfaces.
Those declarations are not proof that a specific Library action invokes them.

Bounded inspection of the one shipped `Bundle\index.bundle` file
(19,506,864 bytes; SHA256
`038a60a67cec41055fc2e6d75525eb87a4828f7ee05150f869b0e8905475a8ca`)
found fixed static references to `collections.mp.microsoft.com`,
`purchase.mp.microsoft.com`, catalog/TitleHub services, and
`getOwnedGamesCollection`, `fetchCollectionData`, `getGameEntitlements` and
edition-entitlement operations. It did **not** establish a versioned collection
route, request method/body, relying party, paging or account binding. Separate
hostname and operation strings must not be combined into an invented call.
The community v7 lead is strengthened, not admitted, by these references.

After explicit user approval, the installed `Microsoft.Xbox.App` entry was
activated and routed to the package's legitimate legacy `XboxPcApp.exe`.
Bounded accessibility inspection identified a pre-install content/feature
selection popup, not the owned-PC Library. Its required base game, optional
features, storage requirement and Next button do not establish ownership or a
completed install. Next was not invoked during that inspection. After the
user directed autonomous continuation, one exact observed `CloseButton` Invoke
dismissed the popup without changing features or advancing installation.

The actual My Library / My games view was then reached. Its Access filter
separates **Owned** and **In Game Pass** from the independent Play state filters
**Installed** and **Installable**. Owned with Installed selected showed
No Results Found; removing that play-state restriction showed eight visible
Owned rows. The Platform selector defaults to All Games and separately offers
XBOX Games and Added from device. Selecting XBOX Games plus Owned and Installable
retained those eight visible rows. This is reference UI evidence, not a paged
total, a technical product/SKU join, license issuance or Mac playability proof.

Permission, session access and the popup are no longer the observation blocker.
No operation-correlated collection method/body, purchasing-account binding,
paging, acquisition/status semantics or same-SKU PC coverage was established.
HTTPS socket observations establish none of those facts.
No authenticated collection query, private-cache read, binary/source export,
TLS interception, certificate/policy change or credential dump was performed.
The next implementation is a bounded development-only diagnostic for the
source-backed consumer v7 collection query: one page with the existing readonly
saved-profile fences and exact token audience. It must not default status to
Active, turn item presence into purchase/subscription ownership, publish a
shipping capability or claim complete coverage. Source qualification and actual
transport acceptance are separate gates. The UI observations do not prove the
installed Xbox client uses that v7 route.

No credential/token dump, root-certificate installation, TLS weakening,
security-policy change, package modification or undocumented endpoint probing
is approved by this specification. A capture requiring such a change needs
specific approval; encrypted traffic alone cannot establish request-body
semantics. Retain only redacted operation/field/enum/count/status evidence.

## Figma-to-data acceptance matrix

Authority: [approved Figma file](https://www.figma.com/design/5iQu716UFImHjRxJkf0t8V)
and the app's native `DESIGN.md`/tokens/platform constitution. Its invented
fixture names and evidence illustrate composition, not production facts.

| Screen | Node | Required truthful data |
| --- | --- | --- |
| Library | 3:115 | API-02/03/04/13/17: purchased/subscription PC inventory, real local/playable state and correctly scoped counts. No TitleHub substitution. |
| Browse | 3:365 | API-04/05/06: actual public PC catalog and artwork; availability is not account access. |
| Search | 3:288 | API-04/05/06: actual scoped candidates, continuation and partial/empty/error states. |
| Product detail | 3:248 | API-04/06/07/08: selected real product/SKU and independent availability/access/runtime facts. |
| Install review | 3:2 | API-07/08/09/10/11: trustworthy package plan, storage and explicit license-operation consent. |
| Downloads | 3:70 | API-12/14/18: measured actual jobs/progress/cancel/recovery, not simulated percentages. |
| Onboarding | 3:187 | API-01/18: real saved/unknown/pending/failed/cancelled account state. |
| Blocked | 3:208 | Specific actual missing entitlement/package/format/runtime prerequisite, with a useful action. |
| Download error | 3:321 | API-10/12/18: actual transport/integrity/job error and preserved prior installation/saves. |

## Delivery sequence and completion rule

1. Correct the current Library/activity meaning without discarding real data or
   inventing owned-PC results.
2. Establish API-02/03/04 from the legitimate Windows reference and documented
   sources; freeze a supported inventory contract and exact negative cases.
3. Implement that real PC Library using the approved composition. Purchase,
   subscription, installed and runtime states remain independent.
4. Resolve one legitimate selected PC edition. Qualify package integrity/layout
   and the precise license/storage operations before any live install.
5. Deliver staged install, genuine local state/jobs, save-safe update/remove and
   actual CrossOver play as a vertical slice, then integrate all nine flows.

Each slice requires source qualification plus its actual product result.
Completing codecs, fixtures, a reserved DTO, CI or a preview is not completion
of the application. The full status remains in the
[delivery ledger](delivery-ledger.md); unsupported operations stay explicitly
blocked rather than simulated.
