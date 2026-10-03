use serde::{Deserialize, Serialize};

pub const MAX_LINE_BYTES: usize = 1024 * 1024;
pub const SCHEMA_ID: &str = "urn:xodus:management:1.0";

#[derive(Clone, Debug, Deserialize, Serialize, PartialEq, Eq)]
#[serde(deny_unknown_fields)]
pub struct Protocol {
    pub major: u32,
    pub minor: u32,
}

impl Default for Protocol {
    fn default() -> Self {
        Self { major: 1, minor: 0 }
    }
}

#[derive(Clone, Debug, Serialize)]
pub struct Request {
    pub kind: RequestKind,
    pub protocol: Protocol,
    #[serde(rename = "requestID")]
    pub request_id: String,
    #[serde(flatten)]
    pub operation: Operation,
}

impl<'de> Deserialize<'de> for Request {
    fn deserialize<D: serde::Deserializer<'de>>(deserializer: D) -> Result<Self, D::Error> {
        #[derive(Deserialize)]
        #[serde(deny_unknown_fields)]
        struct Envelope {
            kind: RequestKind,
            protocol: Protocol,
            #[serde(rename = "requestID")]
            request_id: String,
            command: String,
            params: serde_json::Value,
        }
        let envelope = Envelope::deserialize(deserializer)?;
        let operation = serde_json::from_value(serde_json::json!({
            "command": envelope.command,
            "params": envelope.params
        }))
        .map_err(serde::de::Error::custom)?;
        Ok(Self {
            kind: envelope.kind,
            protocol: envelope.protocol,
            request_id: envelope.request_id,
            operation,
        })
    }
}
#[derive(Clone, Debug, Deserialize, Serialize)]
pub enum RequestKind {
    #[serde(rename = "request")]
    Request,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(tag = "command", content = "params")]
pub enum Operation {
    #[serde(rename = "hello")]
    Hello(HelloParams),
    #[serde(rename = "auth.begin")]
    AuthBegin(AccountParams),
    #[serde(rename = "auth.cancel")]
    AuthCancel(AuthCancelParams),
    #[serde(rename = "auth.status")]
    AuthStatus(Empty),
    #[serde(rename = "auth.logout")]
    AuthLogout(Empty),
    #[serde(rename = "inventory.snapshot")]
    InventorySnapshot(InventoryParams),
    #[serde(rename = "catalog.search")]
    CatalogSearch(SearchParams),
    #[serde(rename = "catalog.discover")]
    CatalogDiscover(DiscoveryParams),
    #[serde(rename = "product.detail")]
    ProductDetail(ProductParams),
    #[serde(rename = "install.plan")]
    InstallPlan(PlanParams),
    #[serde(rename = "jobs.enqueue")]
    JobsEnqueue(EnqueueParams),
    #[serde(rename = "jobs.pause")]
    JobsPause(JobMutation),
    #[serde(rename = "jobs.resume")]
    JobsResume(JobMutation),
    #[serde(rename = "jobs.cancel")]
    JobsCancel(JobMutation),
    #[serde(rename = "jobs.retry")]
    JobsRetry(JobMutation),
    #[serde(rename = "jobs.snapshot")]
    JobsSnapshot(Empty),
    #[serde(rename = "events.replay")]
    EventsReplay(ReplayParams),
    #[serde(rename = "installed.snapshot")]
    InstalledSnapshot(Empty),
    #[serde(rename = "game.launch")]
    GameLaunch(InstallationMutation),
    #[serde(rename = "game.update")]
    GameUpdate(UpdateParams),
    #[serde(rename = "game.rollback")]
    GameRollback(InstallationMutation),
    #[serde(rename = "game.remove")]
    GameRemove(RemoveParams),
    #[serde(rename = "diagnostics.export")]
    DiagnosticsExport(Empty),
}

#[derive(Clone, Debug, Default, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Empty {}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct HelloParams {
    pub client: String,
    pub client_version: String,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub required_capabilities: Vec<String>,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct AccountParams {
    pub account_scope: String,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct AuthCancelParams {
    #[serde(rename = "flowID")]
    pub flow_id: String,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct InventoryParams {
    pub account_scope: String,
    pub market: String,
    pub refresh: Refresh,
}

#[derive(Clone, Debug, Deserialize, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub enum Refresh {
    Cache,
    Network,
}

#[derive(Clone, Debug, Deserialize, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct ProductParams {
    #[serde(rename = "productID")]
    pub product_id: String,
    pub market: String,
    pub language: String,
    pub refresh: Refresh,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct SearchParams {
    pub query: String,
    pub market: String,
    pub language: String,
    pub platform: Platform,
    pub limit: u32,
    pub cursor: Option<String>,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct DiscoveryParams {
    pub market: String,
    pub language: String,
    pub limit: u32,
    pub cursor: Option<String>,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
pub enum Platform {
    #[serde(rename = "pc")]
    Pc,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct PlanParams {
    #[serde(rename = "productID")]
    pub product_id: String,
    #[serde(rename = "editionID")]
    pub edition_id: String,
    pub architecture: Architecture,
    pub language: String,
    pub market: String,
    pub destination: String,
    pub experimental_consent: bool,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
pub enum Architecture {
    #[serde(rename = "arm64")]
    Arm64,
    #[serde(rename = "x86_64")]
    X86_64,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(tag = "kind", rename_all = "camelCase", deny_unknown_fields)]
pub enum EnqueueParams {
    CatalogRefresh {
        #[serde(rename = "idempotencyKey")]
        idempotency_key: String,
        product: ProductParams,
    },
    Install {
        #[serde(rename = "idempotencyKey")]
        idempotency_key: String,
        #[serde(rename = "planID")]
        plan_id: String,
        #[serde(rename = "planDigest")]
        plan_digest: String,
    },
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct JobMutation {
    #[serde(rename = "jobID")]
    pub job_id: String,
    pub expected_revision: u64,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct ReplayParams {
    #[serde(rename = "sessionID")]
    pub session_id: String,
    pub after_sequence: u64,
    pub limit: u32,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct InstallationMutation {
    #[serde(rename = "installationID")]
    pub installation_id: String,
    pub expected_revision: u64,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct UpdateParams {
    #[serde(rename = "installationID")]
    pub installation_id: String,
    pub expected_revision: u64,
    #[serde(rename = "planID")]
    pub plan_id: String,
    pub plan_digest: String,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct RemoveParams {
    #[serde(rename = "installationID")]
    pub installation_id: String,
    pub expected_revision: u64,
    pub preserve_saves: bool,
    pub confirmed: bool,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct WireError {
    pub code: ErrorCode,
    pub message: String,
    pub retryable: bool,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub details: Option<serde_json::Map<String, serde_json::Value>>,
}

#[derive(Clone, Debug, Deserialize, Serialize, PartialEq, Eq)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum ErrorCode {
    ProtocolMismatch,
    CapabilityMissing,
    AuthCancelled,
    AuthExpired,
    AuthInvalid,
    InventoryPartial,
    AccessUnknown,
    AccessRevoked,
    PackageAmbiguous,
    PackageUnavailable,
    PlanExpired,
    PlanChanged,
    InsufficientSpace,
    NetworkUnavailable,
    IntegrityFailed,
    RuntimeMismatch,
    UnsupportedConfiguration,
    RevisionConflict,
    EventsExpired,
    Cancelled,
    RegistryRecoveryRequired,
    LaunchFailed,
    InternalError,
    InvalidRequest,
    UnknownCommand,
    HelloRequired,
    DuplicateRequest,
    NotFound,
    InvalidTransition,
    IdempotencyConflict,
    StateLocked,
    LimitExceeded,
}

impl WireError {
    pub fn new(code: ErrorCode, message: &str, retryable: bool) -> Self {
        Self {
            code,
            message: message.to_owned(),
            retryable,
            details: None,
        }
    }
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(tag = "kind", rename_all = "camelCase")]
pub enum ResultFrame {
    Result {
        protocol: Protocol,
        #[serde(rename = "requestID")]
        request_id: String,
        #[serde(flatten)]
        outcome: Outcome,
    },
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(untagged)]
pub enum Outcome {
    Success { ok: True, data: Data },
    Failure { ok: False, error: WireError },
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(try_from = "bool", into = "bool")]
pub struct True;

impl TryFrom<bool> for True {
    type Error = &'static str;
    fn try_from(value: bool) -> Result<Self, Self::Error> {
        if value {
            Ok(Self)
        } else {
            Err("expected true")
        }
    }
}
impl From<True> for bool {
    fn from(_: True) -> Self {
        true
    }
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(try_from = "bool", into = "bool")]
pub struct False;
impl TryFrom<bool> for False {
    type Error = &'static str;
    fn try_from(value: bool) -> Result<Self, Self::Error> {
        if !value {
            Ok(Self)
        } else {
            Err("expected false")
        }
    }
}
impl From<False> for bool {
    fn from(_: False) -> Self {
        false
    }
}

impl ResultFrame {
    pub fn new(request_id: String, result: Result<Data, WireError>) -> Self {
        let outcome = match result {
            Ok(data) => Outcome::Success { ok: True, data },
            Err(error) => Outcome::Failure { ok: False, error },
        };
        Self::Result {
            protocol: Protocol::default(),
            request_id,
            outcome,
        }
    }
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(untagged)]
pub enum Data {
    Hello(HelloData),
    Auth(AuthData),
    Search(SearchData),
    Discovery(DiscoveryData),
    Product(ProductData),
    Job(JobData),
    Jobs(JobsData),
    Replay(ReplayData),
    Installed(InstalledData),
    Diagnostics(DiagnosticsData),
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct HelloData {
    pub protocol: Protocol,
    pub backend_version: String,
    pub runtime_fingerprint: Option<String>,
    pub capabilities: Vec<Capability>,
    #[serde(rename = "sessionID")]
    pub session_id: String,
    pub schema: String,
    pub catalog_corpus: String,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct Capability {
    pub command: String,
    pub supported: bool,
    pub audience: Option<String>,
    pub reason: Option<String>,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct AuthData {
    pub state: AuthState,
    pub credential_store: String,
    pub audience: Option<String>,
    pub expires_at: Option<String>,
    pub entitlement_authorized: bool,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub flow: Option<AuthFlow>,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct AuthFlow {
    #[serde(rename = "flowID")]
    pub flow_id: String,
    pub state: AuthFlowState,
    pub error: Option<WireError>,
}
#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub enum AuthFlowState {
    Pending,
    Completed,
    Cancelled,
    Failed,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub enum AuthState {
    SignedOut,
    CredentialPresent,
    Expired,
    Invalid,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct ProductData {
    pub product: ProductRecord,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct ProductRecord {
    #[serde(rename = "productID")]
    pub product_id: String,
    pub title: String,
    pub market: String,
    pub language: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub resolved_language: Option<String>,
    pub source: String,
    pub checked_at: String,
    pub freshness: Freshness,
    pub editions: Vec<ProductEvidence>,
    pub pc_catalog_candidate: bool,
}

#[derive(Clone, Debug, Deserialize, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub enum Freshness {
    Live,
    Cached,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct SearchData {
    pub products: Vec<ProductRecord>,
    pub corpus: String,
    pub completeness: String,
    pub next_cursor: Option<String>,
    pub cache_revision: u64,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct DiscoveryData {
    pub corpus: String,
    pub completeness: String,
    pub source: String,
    pub checked_at: String,
    pub freshness: Freshness,
    pub corpus_revision: String,
    pub products: Vec<ProductRecord>,
    pub failures: Vec<DiscoveryFailure>,
    pub next_cursor: Option<String>,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct DiscoveryFailure {
    #[serde(rename = "productID")]
    pub product_id: String,
    pub error: WireError,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct ProductEvidence {
    #[serde(rename = "productID")]
    pub product_id: String,
    #[serde(rename = "editionID")]
    pub edition_id: String,
    pub entitlement: Entitlement,
    pub installability: Installability,
    pub compatibility: Compatibility,
    pub installation: Installation,
    pub inventory: InventoryMetadata,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct Entitlement {
    pub kind: EntitlementKind,
    pub source: String,
    pub checked_at: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub expires_at: Option<String>,
}
#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub enum EntitlementKind {
    Purchase,
    Subscription,
    None,
    Unknown,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct Installability {
    pub kind: InstallabilityKind,
    #[serde(rename = "packageID", skip_serializing_if = "Option::is_none")]
    pub package_id: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub package_version: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub reason: Option<String>,
}
#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub enum InstallabilityKind {
    Downloadable,
    Blocked,
    Unknown,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct Compatibility {
    pub kind: CompatibilityKind,
    pub source: String,
    pub checked_at: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub os: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub architecture: Option<Architecture>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub runtime_fingerprint: Option<String>,
}
#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub enum CompatibilityKind {
    Verified,
    Experimental,
    Unsupported,
    Unknown,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct Installation {
    pub kind: InstallationKind,
    #[serde(rename = "installationID", skip_serializing_if = "Option::is_none")]
    pub installation_id: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub package_version: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub runtime_fingerprint: Option<String>,
}
#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub enum InstallationKind {
    NotInstalled,
    Installed,
    Updating,
    Broken,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct InventoryMetadata {
    pub completeness: Completeness,
    pub checked_at: Option<String>,
    pub last_complete_at: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub source: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub reason: Option<String>,
}
#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub enum Completeness {
    Complete,
    Partial,
    Unknown,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct Job {
    #[serde(rename = "jobID")]
    pub job_id: String,
    pub revision: u64,
    pub kind: JobKind,
    pub state: JobState,
    #[serde(rename = "requestID")]
    pub request_id: String,
    pub created_at: String,
    pub updated_at: String,
    pub product: ProductParams,
    pub attempt: u32,
    pub error: Option<WireError>,
}
#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub enum JobKind {
    CatalogRefresh,
}

#[derive(Clone, Debug, Deserialize, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub enum JobState {
    Queued,
    Running,
    Completed,
    Failed,
    Cancelled,
}

impl JobState {
    pub fn terminal(&self) -> bool {
        matches!(self, Self::Completed | Self::Failed | Self::Cancelled)
    }
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct JobData {
    pub job: Job,
    pub watermark: u64,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct JobsData {
    #[serde(rename = "sessionID")]
    pub session_id: String,
    pub watermark: u64,
    pub jobs: Vec<Job>,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct Event {
    pub kind: EventKind,
    pub protocol: Protocol,
    #[serde(rename = "sessionID")]
    pub session_id: String,
    pub sequence: u64,
    #[serde(rename = "requestID")]
    pub request_id: String,
    #[serde(rename = "jobID")]
    pub job_id: String,
    pub revision: u64,
    pub event: EventName,
    pub data: Job,
}
#[derive(Clone, Debug, Deserialize, Serialize)]
pub enum EventKind {
    #[serde(rename = "event")]
    Event,
}
#[derive(Clone, Debug, Deserialize, Serialize)]
pub enum EventName {
    #[serde(rename = "job.changed")]
    JobChanged,
}
#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct ReplayData {
    #[serde(rename = "sessionID")]
    pub session_id: String,
    pub watermark: u64,
    pub events: Vec<Event>,
    pub has_more: bool,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct InstalledData {
    pub registry_version: Protocol,
    pub scope: String,
    pub completeness: Completeness,
    pub installations: Vec<InstallationRecord>,
    pub watermark: u64,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct InstallationRecord {
    #[serde(rename = "installationID")]
    pub installation_id: String,
    pub revision: u64,
    #[serde(rename = "productID")]
    pub product_id: String,
    #[serde(rename = "editionID")]
    pub edition_id: String,
    #[serde(rename = "packageID")]
    pub package_id: String,
    pub package_version: String,
    pub package_digest: String,
    pub runtime_fingerprint: String,
    pub managed_root: String,
    pub save_policy: String,
    pub health: String,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct DiagnosticsData {
    pub report_version: u32,
    pub backend_version: String,
    pub protocol: Protocol,
    pub redacted: bool,
    pub job_count: u32,
    pub cached_product_count: u32,
    pub runtime_certified: bool,
    pub inventory_authorized: bool,
}
