use std::collections::{BTreeMap, BTreeSet, HashSet};
use std::future::Future;
use std::path::Path;
use std::pin::Pin;
use std::sync::Arc;
use std::time::Duration;

use chrono::{DateTime, Utc};
use tokio::io::{AsyncBufRead, AsyncRead, AsyncReadExt, AsyncWrite, AsyncWriteExt, BufReader};
use tokio::process::{Child, Command};
use tokio::sync::mpsc;
use tokio::task::{AbortHandle, JoinHandle, JoinSet};
use uuid::Uuid;
use xodus::api::displaycatalog::{BoundedCatalogError, find_products_by_id_bounded};
use xodus::models::displaycatalog::DisplayCatalogProductsResponse;
use xodus::models::secrets::Token;
use xodus::tokens::TokenManager;
use xodus::tokens::store::TokenStoreError;

use crate::state::{Store, catalog_key, identifier_valid, now};
use crate::transport::{self, COMMANDS, Line};
use crate::wire::*;

const CORPUS: &str = "observedPublicProducts";
const MAX_WORKERS: usize = 4;
pub const MAX_AUTH_HANDOFF_BYTES: usize = 256 * 1024;

// Private inherited socket payload, never a management stdout frame.
#[derive(serde::Serialize, serde::Deserialize)]
#[serde(tag = "outcome", rename_all = "camelCase", deny_unknown_fields)]
pub enum ConsentHandoff {
    Completed {
        session: Box<xodus::xal::TokenStore>,
    },
    StoreCompleted {
        session: Box<xodus::models::secrets::ManagementStoreSession>,
    },
    Cancelled,
    Failed,
}

#[derive(serde::Serialize, serde::Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ConsentBootstrap {
    pub flow_id: String,
    pub device: Option<xodus::models::secrets::Device>,
    pub device_token: Option<xodus::models::secrets::LegacyToken>,
}

struct ConsentReader {
    flow_id: String,
    task: JoinHandle<Result<ConsentHandoff, WireError>>,
}

async fn read_consent<R: AsyncRead + Unpin>(mut reader: R) -> Result<ConsentHandoff, WireError> {
    let invalid = || {
        WireError::new(
            ErrorCode::AuthInvalid,
            "Native consent did not return a complete bounded private session.",
            true,
        )
    };
    let length = reader.read_u32().await.map_err(|_| invalid())? as usize;
    if length == 0 || length > MAX_AUTH_HANDOFF_BYTES {
        return Err(invalid());
    }
    let mut bytes = vec![0; length];
    reader.read_exact(&mut bytes).await.map_err(|_| invalid())?;
    serde_json::from_slice(&bytes).map_err(|_| invalid())
}

fn spawn_consent(
    executable: &Path,
    flow_id: &str,
    bootstrap: &ConsentBootstrap,
) -> Result<(Child, ConsentReader), WireError> {
    #[cfg(unix)]
    {
        use std::os::fd::OwnedFd;
        let unavailable = || {
            WireError::new(
                ErrorCode::AuthInvalid,
                "Could not open the owned native consent window and private channel.",
                true,
            )
        };
        let bytes = serde_json::to_vec(bootstrap).map_err(|_| unavailable())?;
        if bytes.len() > MAX_AUTH_HANDOFF_BYTES {
            return Err(unavailable());
        }
        let (parent, child) = std::os::unix::net::UnixStream::pair().map_err(|_| unavailable())?;
        parent.set_nonblocking(true).map_err(|_| unavailable())?;
        let mut parent = tokio::net::UnixStream::from_std(parent).map_err(|_| unavailable())?;
        let child = Command::new(executable)
            .arg("management-auth-worker")
            .arg("--flow-id")
            .arg(flow_id)
            .stdin(std::process::Stdio::from(OwnedFd::from(child)))
            .stdout(std::process::Stdio::null())
            .stderr(std::process::Stdio::null())
            .env_remove("XODUS_LOG")
            .env_remove("RUST_LOG")
            .env_remove("GH_TOKEN")
            .env_remove("GITHUB_TOKEN")
            .kill_on_drop(true)
            .spawn()
            .map_err(|_| unavailable())?;
        Ok((
            child,
            ConsentReader {
                flow_id: flow_id.to_owned(),
                task: tokio::spawn(async move {
                    tokio::time::timeout(Duration::from_secs(5), async {
                        parent.write_u32(bytes.len() as u32).await?;
                        parent.write_all(&bytes).await
                    })
                    .await
                    .map_err(|_| {
                        WireError::new(
                            ErrorCode::AuthInvalid,
                            "Native consent bootstrap timed out.",
                            true,
                        )
                    })?
                    .map_err(|_| {
                        WireError::new(
                            ErrorCode::AuthInvalid,
                            "Native consent bootstrap channel failed.",
                            true,
                        )
                    })?;
                    read_consent(parent).await
                }),
            },
        ))
    }
    #[cfg(not(unix))]
    {
        let _ = (executable, flow_id, bootstrap);
        Err(WireError::new(
            ErrorCode::UnsupportedConfiguration,
            "Native consent requires the macOS protected parent channel.",
            false,
        ))
    }
}

pub trait CatalogProvider: Send + Sync {
    fn fetch(
        &self,
        params: ProductParams,
    ) -> Pin<Box<dyn Future<Output = Result<ProductRecord, WireError>> + Send + '_>>;
    fn discovery_supported(&self) -> bool {
        false
    }
    fn discover(
        &self,
        _: DiscoveryParams,
    ) -> Pin<Box<dyn Future<Output = Result<DiscoveryData, WireError>> + Send + '_>> {
        Box::pin(async {
            Err(WireError::new(
                ErrorCode::CapabilityMissing,
                "This provider has no proved public discovery feed.",
                false,
            ))
        })
    }
}

#[derive(Clone)]
pub struct PublicCatalog {
    client: reqwest::Client,
    permits: Arc<tokio::sync::Semaphore>,
}

impl PublicCatalog {
    pub fn new() -> Result<Self, WireError> {
        let client = reqwest::Client::builder()
            .user_agent(concat!("xodus-management/", env!("CARGO_PKG_VERSION")))
            .timeout(Duration::from_secs(30))
            .connect_timeout(Duration::from_secs(10))
            .redirect(reqwest::redirect::Policy::none())
            .build()
            .map_err(|_| {
                WireError::new(
                    ErrorCode::InternalError,
                    "Could not initialize the public catalog client.",
                    false,
                )
            })?;
        Ok(Self {
            client,
            permits: Arc::new(tokio::sync::Semaphore::new(4)),
        })
    }
}

impl CatalogProvider for PublicCatalog {
    fn fetch(
        &self,
        params: ProductParams,
    ) -> Pin<Box<dyn Future<Output = Result<ProductRecord, WireError>> + Send + '_>> {
        Box::pin(async move {
            let _permit = self.permits.acquire().await.map_err(|_| {
                WireError::new(
                    ErrorCode::NetworkUnavailable,
                    "Public catalog request capacity is unavailable.",
                    true,
                )
            })?;
            let response = find_products_by_id_bounded(
                &self.client,
                &params.product_id,
                &params.market,
                &params.language,
            )
            .await
            .map_err(|error| match error {
                BoundedCatalogError::Http(error)
                    if error.status().is_some_and(|status| status.as_u16() == 404) =>
                {
                    WireError::new(
                        ErrorCode::NotFound,
                        "Product is unavailable in this catalog market.",
                        false,
                    )
                }
                BoundedCatalogError::Http(error) if error.is_connect() || error.is_timeout() => {
                    WireError::new(
                        ErrorCode::NetworkUnavailable,
                        "Public catalog connection failed. Retry later.",
                        true,
                    )
                }
                BoundedCatalogError::Http(error)
                    if error.status().is_some_and(|status| {
                        status.is_server_error() || status.as_u16() == 429
                    }) =>
                {
                    WireError::new(
                        ErrorCode::NetworkUnavailable,
                        "Public catalog is temporarily unavailable. Retry later.",
                        true,
                    )
                }
                _ => WireError::new(
                    ErrorCode::PackageUnavailable,
                    "Public catalog returned unsupported or invalid metadata.",
                    false,
                ),
            })?;
            map_product(&params, response)
        })
    }
    fn discovery_supported(&self) -> bool {
        true
    }
    fn discover(
        &self,
        params: DiscoveryParams,
    ) -> Pin<Box<dyn Future<Output = Result<DiscoveryData, WireError>> + Send + '_>> {
        Box::pin(crate::discovery::fetch_page(
            &self.client,
            &self.permits,
            Arc::new(self.clone()),
            params,
        ))
    }
}

pub fn map_product(
    params: &ProductParams,
    response: DisplayCatalogProductsResponse,
) -> Result<ProductRecord, WireError> {
    let invalid = || {
        WireError::new(
            ErrorCode::PackageUnavailable,
            "Public metadata has missing, mismatched or ambiguous product/edition identity.",
            false,
        )
    };
    let product = response.product;
    if product.product_id.as_deref() != Some(&params.product_id) {
        return Err(invalid());
    }
    let localized = product
        .localized_properties
        .iter()
        .find(|property| {
            property
                .language
                .as_deref()
                .is_some_and(|language| language.eq_ignore_ascii_case(&params.language))
        })
        .or_else(|| {
            let (base, _) = params.language.split_once('-')?;
            product.localized_properties.iter().find(|property| {
                property
                    .language
                    .as_deref()
                    .is_some_and(|language| language.eq_ignore_ascii_case(base))
            })
        })
        .ok_or_else(invalid)?;
    let title = localized.product_title.trim();
    if title.is_empty()
        || title.chars().count() > 1024
        || title.chars().any(char::is_control)
        || product.display_sku_availabilities.is_empty()
        || product.display_sku_availabilities.len() > 256
    {
        return Err(invalid());
    }
    let mut editions = BTreeMap::new();
    let mut pc_candidate = false;
    for availability in product.display_sku_availabilities {
        let sku = availability.sku;
        let sku_id = sku.sku_id.ok_or_else(invalid)?;
        if !identifier_valid(&sku_id) {
            return Err(invalid());
        }
        let pc_packages: Vec<_> = sku
            .properties
            .packages
            .iter()
            .filter(|package| {
                package
                    .platform_dependencies
                    .iter()
                    .any(|dependency| dependency.platform_name == "Windows.Desktop")
            })
            .collect();
        let has_pc = !pc_packages.is_empty();
        let candidate = (pc_packages.len() == 1).then(|| pc_packages[0]);
        let package_id = candidate.and_then(|package| package.package_id.clone());
        if package_id.as_ref().is_some_and(|id| !identifier_valid(id)) {
            return Err(invalid());
        }
        let package_version = candidate
            .and_then(|package| package.version.clone())
            .filter(|version| !version.is_empty() && version != "0");
        if package_version
            .as_ref()
            .is_some_and(|version| !identifier_valid(version))
        {
            return Err(invalid());
        }
        pc_candidate |= has_pc;
        let evidence = ProductEvidence {
            product_id: params.product_id.clone(), edition_id: sku_id.clone(),
            entitlement: Entitlement { kind: EntitlementKind::Unknown, source: "notQueried".to_owned(),
                checked_at: None, expires_at: None },
            installability: Installability { kind: if has_pc { InstallabilityKind::Unknown } else {
                InstallabilityKind::Blocked }, package_id, package_version,
                reason: Some(if pc_packages.len() > 1 { "Multiple PC package candidates require explicit selection and authorization." }
                else if has_pc { "Public package identity is not authorization or a verified immutable download version." } else {
                    "This edition has no Windows.Desktop package in the returned public metadata." }.to_owned()) },
            compatibility: Compatibility { kind: CompatibilityKind::Unknown,
                source: "noCertifiedRuntime".to_owned(), checked_at: None, os: None,
                architecture: None, runtime_fingerprint: None },
            installation: Installation { kind: InstallationKind::NotInstalled,
                installation_id: None, package_version: None, runtime_fingerprint: None },
            inventory: InventoryMetadata { completeness: Completeness::Unknown, checked_at: None,
                last_complete_at: None, source: Some("notQueried".to_owned()),
                reason: Some("Consumer PC entitlement enumeration is not established.".to_owned()) },
        };
        if let Some(existing) = editions.get(&sku_id) {
            if serde_json::to_value(existing).map_err(|_| invalid())?
                != serde_json::to_value(&evidence).map_err(|_| invalid())?
            {
                return Err(invalid());
            }
        } else {
            editions.insert(sku_id, evidence);
        }
    }
    Ok(ProductRecord {
        product_id: params.product_id.clone(),
        title: title.to_owned(),
        market: params.market.clone(),
        language: params.language.clone(),
        resolved_language: localized.language.clone(),
        source: "MicrosoftDisplayCatalog:v7.0".to_owned(),
        checked_at: now(),
        freshness: Freshness::Live,
        editions: editions.into_values().collect(),
        pc_catalog_candidate: pc_candidate,
    })
}

enum Completion {
    ConsentPrepared {
        request_id: String,
        flow_id: String,
        result: Result<ConsentBootstrap, WireError>,
    },
    AccountCommit {
        flow_id: String,
        result: Result<(), WireError>,
    },
    AccountLogout {
        request_id: String,
        result: Result<AuthData, WireError>,
    },
    AccountStatus {
        request_id: String,
        result: Result<AuthData, WireError>,
    },
    Discovery {
        request_id: String,
        result: Result<DiscoveryData, WireError>,
    },
    Product {
        request_id: String,
        result: Result<ProductRecord, WireError>,
    },
    Job {
        job_id: String,
        result: Result<ProductRecord, WireError>,
    },
}

pub struct Backend {
    pub store: Store,
    provider: Arc<dyn CatalogProvider>,
    tasks: JoinSet<Completion>,
    workers: BTreeMap<String, AbortHandle>,
    tokens: Option<TokenManager>,
    native_auth: bool,
    auth_child: Option<Child>,
    auth_handoff: Option<ConsentReader>,
    auth_flow: Option<AuthFlow>,
    auth_started: Option<std::time::Instant>,
    pending_requests: BTreeSet<String>,
    async_keychain_io: bool,
    account_mutation_pending: bool,
}

impl Backend {
    pub fn new(store: Store, provider: Arc<dyn CatalogProvider>) -> Self {
        Self {
            store,
            provider,
            tasks: JoinSet::new(),
            workers: BTreeMap::new(),
            tokens: None,
            native_auth: xodus::secrets::management_native_keychain_enabled(),
            auth_child: None,
            auth_handoff: None,
            auth_flow: None,
            auth_started: None,
            pending_requests: BTreeSet::new(),
            async_keychain_io: true,
            account_mutation_pending: false,
        }
    }

    fn token_manager(&mut self) -> Result<TokenManager, WireError> {
        if !self.native_auth {
            return Err(WireError::new(
                ErrorCode::UnsupportedConfiguration,
                "Management authentication requires macOS native Keychain without key-chain-file.",
                false,
            ));
        }
        if self.tokens.is_none() {
            xodus::secrets::init_secrets().map_err(|_| {
                WireError::new(
                    ErrorCode::AuthInvalid,
                    "Native Keychain is unavailable. Resolve Keychain access before connecting.",
                    true,
                )
            })?;
            self.tokens = Some(TokenManager::with_management_keychain_and_memory());
        }
        self.tokens.clone().ok_or_else(transport::invalid)
    }

    fn capabilities(&self) -> Vec<Capability> {
        COMMANDS.iter().map(|command| {
            let supported = matches!(*command, "hello" | "product.detail" | "catalog.search" |
                "jobs.enqueue" | "jobs.cancel" | "jobs.retry" | "jobs.snapshot" |
                "events.replay" | "installed.snapshot" | "diagnostics.export") ||
                (*command == "catalog.discover" && self.provider.discovery_supported()) ||
                (self.native_auth && matches!(*command, "auth.begin" | "auth.cancel" | "auth.status" | "auth.logout"));
            let audience = match *command {
                "auth.begin" | "auth.status" | "auth.logout" => Some("Isolated launcher profile; Passport.NET/STS store credential, not entitlement authorization"),
                "inventory.snapshot" => Some("Unproven consumer PC entitlement audience"),
                "install.plan" | "game.update" => Some("http://update.xboxlive.com and www.microsoft.com"),
                "game.launch" => Some("Package license and signed paired runtime"),
                _ => None,
            };
            let reason = if !supported {
                Some(match *command {
                    "inventory.snapshot" =>
                        "Consumer account audience, complete pagination and PC ownership coverage are not proven.",
                    "catalog.discover" => "No proved public PC discovery feed is available in this provider.",
                    "catalog.query" => "The agreed anonymous Microsoft Store query provider is not connected yet.",
                    "game.launch" => "A signed, distributable, exact version-paired runtime is not certified.",
                    "jobs.pause" | "jobs.resume" => "Catalog refresh does not support durable pause.",
                    "auth.begin" | "auth.cancel" | "auth.status" | "auth.logout" =>
                        "Native macOS Keychain is required; plaintext fallback is refused.",
                    _ => "Authorized package planning and verified failure-safe installation are not implemented.",
                }.to_owned())
            } else if *command == "jobs.enqueue" {
                Some("Only catalogRefresh jobs are supported; install jobs remain gated.".to_owned())
            } else if *command == "installed.snapshot" {
                Some("Only management registry scope, not legacy installs or external folders.".to_owned())
            } else if *command == "catalog.discover" {
                Some("Partial public PC GamePass discovery, not ownership/subscription or whole-store text search.".to_owned())
            } else { None };
            Capability { command: (*command).to_owned(), supported,
                audience: audience.map(str::to_owned), reason }
        }).collect()
    }

    fn hello(&self, params: &HelloParams) -> Result<Data, WireError> {
        let capabilities = self.capabilities();
        if params.required_capabilities.iter().any(|required| {
            !capabilities
                .iter()
                .any(|capability| &capability.command == required && capability.supported)
        }) {
            return Err(WireError::new(
                ErrorCode::CapabilityMissing,
                "Required backend capabilities are unavailable. Review negotiated capability reasons.",
                false,
            ));
        }
        Ok(Data::Hello(HelloData {
            protocol: Protocol::default(),
            backend_version: env!("CARGO_PKG_VERSION").to_owned(),
            runtime_fingerprint: None,
            capabilities,
            session_id: self.store.state.session_id.clone(),
            schema: SCHEMA_ID.to_owned(),
            catalog_corpus: CORPUS.to_owned(),
        }))
    }

    async fn dispatch(&mut self, request: &Request) -> Result<Option<Data>, WireError> {
        let data = match &request.operation {
            Operation::Hello(params) => self.hello(params)?,
            Operation::AuthBegin(_) => {
                self.poll_auth()?;
                let tokens = self.token_manager()?;
                if self.account_mutation_pending
                    || self
                        .auth_flow
                        .as_ref()
                        .is_some_and(|flow| matches!(flow.state, AuthFlowState::Pending))
                {
                    return Err(WireError::new(
                        ErrorCode::InvalidTransition,
                        "Finish the active launcher account operation before connecting.",
                        false,
                    ));
                }
                if self.async_keychain_io {
                    self.reserve_worker()?;
                    let tokens = tokens
                        .with_explicit_management_keychain_interaction()
                        .map_err(|_| transport::invalid())?;
                    let flow_id = Uuid::new_v4().to_string();
                    let request_id = request.request_id.clone();
                    self.auth_flow = Some(AuthFlow {
                        flow_id: flow_id.clone(),
                        state: AuthFlowState::Pending,
                        error: None,
                    });
                    self.pending_requests.insert(request_id.clone());
                    self.tasks.spawn(async move {
                        let prepare_id = flow_id.clone();
                        Completion::ConsentPrepared {
                            request_id,
                            flow_id,
                            result: account_work(move || prepare_consent(tokens, prepare_id)).await,
                        }
                    });
                    return Ok(None);
                }
                if self.auth_child.is_some()
                    || self.auth_handoff.is_some()
                    || !matches!(auth_status(&tokens)?.state, AuthState::SignedOut)
                {
                    return Err(WireError::new(
                        ErrorCode::InvalidTransition,
                        "Disconnect existing credentials or finish the current consent before connecting another account.",
                        false,
                    ));
                }
                let executable = std::env::current_exe().map_err(|_| {
                    WireError::new(
                        ErrorCode::AuthInvalid,
                        "Native consent worker executable is unavailable.",
                        false,
                    )
                })?;
                let flow_id = Uuid::new_v4().to_string();
                let bootstrap = prepare_consent(tokens, flow_id.clone())?;
                let (child, handoff) = spawn_consent(&executable, &flow_id, &bootstrap)?;
                self.auth_child = Some(child);
                self.auth_handoff = Some(handoff);
                self.auth_started = Some(std::time::Instant::now());
                self.auth_flow = Some(AuthFlow {
                    flow_id,
                    state: AuthFlowState::Pending,
                    error: None,
                });
                Data::Auth(self.account_status()?)
            }
            Operation::AuthCancel(params) => {
                if self.account_mutation_pending {
                    return Err(WireError::new(
                        ErrorCode::InvalidTransition,
                        "Native Keychain commit/disconnect already started. Respond to its explicit prompt and reconcile status.",
                        false,
                    ));
                }
                self.poll_auth()?;
                if self
                    .auth_flow
                    .as_ref()
                    .is_none_or(|flow| flow.flow_id != params.flow_id)
                {
                    return Err(WireError::new(
                        ErrorCode::NotFound,
                        "Consent flow does not belong to this backend connection.",
                        false,
                    ));
                }
                self.stop_auth().await?;
                if let Some(flow) = &mut self.auth_flow
                    && matches!(flow.state, AuthFlowState::Pending)
                {
                    flow.state = AuthFlowState::Cancelled;
                    flow.error = Some(WireError::new(
                        ErrorCode::AuthCancelled,
                        "Consent cancelled before an approved account session committed.",
                        false,
                    ));
                }
                if self.async_keychain_io {
                    self.reserve_worker()?;
                    let tokens = self.token_manager()?;
                    let request_id = request.request_id.clone();
                    let flow = self.auth_flow.clone();
                    self.pending_requests.insert(request_id.clone());
                    self.tasks.spawn(async move {
                        Completion::AccountStatus {
                            request_id,
                            result: read_account_status(tokens).await.map(|mut status| {
                                status.flow = flow;
                                status
                            }),
                        }
                    });
                    return Ok(None);
                }
                let mut status = auth_status(&self.token_manager()?)?;
                status.flow = self.auth_flow.clone();
                Data::Auth(status)
            }
            Operation::AuthStatus(_) => {
                self.poll_auth()?;
                self.reserve_worker()?;
                let tokens = self.token_manager()?;
                let request_id = request.request_id.clone();
                let flow = self.auth_flow.clone();
                self.pending_requests.insert(request_id.clone());
                self.tasks.spawn(async move {
                    Completion::AccountStatus {
                        request_id,
                        result: read_account_status(tokens).await.map(|mut status| {
                            status.flow = flow;
                            status
                        }),
                    }
                });
                return Ok(None);
            }
            Operation::AuthLogout(_) => {
                if self.account_mutation_pending {
                    return Err(WireError::new(
                        ErrorCode::InvalidTransition,
                        "A native Keychain account mutation is pending. Reconcile it before disconnecting.",
                        false,
                    ));
                }
                self.stop_auth().await?;
                self.auth_flow = None;
                let tokens = self.token_manager()?;
                if self.async_keychain_io {
                    let tokens = tokens
                        .with_explicit_management_keychain_interaction()
                        .map_err(|_| transport::invalid())?;
                    let request_id = request.request_id.clone();
                    self.account_mutation_pending = true;
                    self.pending_requests.insert(request_id.clone());
                    self.tasks.spawn(async move {
                        Completion::AccountLogout { request_id, result: account_work(move || {
                            tokens.remove_user_credentials().map_err(|_| WireError::new(
                                ErrorCode::AuthInvalid, "Launcher disconnect failed. Signed-out state is not confirmed.", true))?;
                            let status = auth_status(&tokens)?;
                            if !matches!(status.state, AuthState::SignedOut) {
                                return Err(WireError::new(ErrorCode::AuthInvalid,
                                    "Launcher credentials changed during disconnect; reconcile account status.", true));
                            }
                            Ok(status)
                        }).await }
                    });
                    return Ok(None);
                }
                tokens.remove_user_credentials().map_err(|_| {
                    WireError::new(
                        ErrorCode::AuthInvalid,
                        "Keychain disconnect failed. Account state is not confirmed signed out.",
                        true,
                    )
                })?;
                let status = auth_status(&tokens)?;
                if !matches!(status.state, AuthState::SignedOut) {
                    return Err(WireError::new(
                        ErrorCode::AuthInvalid,
                        "Credentials changed during disconnect; review account state before retrying.",
                        true,
                    ));
                }
                Data::Auth(status)
            }
            Operation::CatalogSearch(params) => Data::Search(search(&self.store, params)?),
            Operation::CatalogQuery(_) => {
                return Err(WireError::new(
                    ErrorCode::CapabilityMissing,
                    "The agreed public Microsoft Store query contract is not connected to this provider yet.",
                    false,
                ));
            }
            Operation::CatalogDiscover(params) => {
                if params
                    .cursor
                    .as_ref()
                    .is_some_and(|cursor| !crate::discovery::validate_cursor(cursor))
                {
                    return Err(transport::invalid());
                }
                self.reserve_worker()?;
                let provider = self.provider.clone();
                let params = params.clone();
                let request_id = request.request_id.clone();
                self.pending_requests.insert(request_id.clone());
                self.tasks.spawn(async move {
                    Completion::Discovery {
                        request_id,
                        result: provider.discover(params).await,
                    }
                });
                return Ok(None);
            }
            Operation::ProductDetail(params) => match params.refresh {
                Refresh::Cache => {
                    let mut product = self.store.state.catalog.get(&catalog_key(params)).cloned()
                        .ok_or_else(|| WireError::new(ErrorCode::NotFound,
                            "No cached public metadata exists for this product/market/language.", false))?;
                    product.freshness = Freshness::Cached;
                    Data::Product(ProductData { product })
                }
                Refresh::Network => {
                    self.reserve_worker()?;
                    let provider = self.provider.clone();
                    let params = params.clone();
                    let request_id = request.request_id.clone();
                    self.pending_requests.insert(request_id.clone());
                    self.tasks.spawn(async move {
                        Completion::Product {
                            request_id,
                            result: provider.fetch(params).await,
                        }
                    });
                    return Ok(None);
                }
            },
            Operation::JobsEnqueue(EnqueueParams::CatalogRefresh {
                idempotency_key,
                product,
            }) => {
                if !self.store.state.idempotency.contains_key(idempotency_key) {
                    self.reserve_worker()?;
                }
                let (job, created) =
                    self.store
                        .enqueue(&request.request_id, idempotency_key, product.clone())?;
                let job = if created { self.start_job(job)? } else { job };
                Data::Job(JobData {
                    job,
                    watermark: self.store.state.watermark,
                })
            }
            Operation::JobsCancel(mutation) => {
                let job = self
                    .store
                    .state
                    .jobs
                    .get(&mutation.job_id)
                    .cloned()
                    .ok_or_else(|| {
                        WireError::new(ErrorCode::NotFound, "Job does not exist.", false)
                    })?;
                let job = if job.state == JobState::Cancelled {
                    job
                } else {
                    self.store.checked_job(mutation)?;
                    if job.state.terminal() {
                        return Err(WireError::new(
                            ErrorCode::InvalidTransition,
                            "Job already reached a terminal state; cancellation cannot undo completed work.",
                            false,
                        ));
                    }
                    let job = self.store.transition(
                        &mutation.job_id,
                        JobState::Cancelled,
                        Some(WireError::new(
                            ErrorCode::Cancelled,
                            "Public metadata refresh cancelled.",
                            false,
                        )),
                        None,
                    )?;
                    if let Some(worker) = self.workers.remove(&mutation.job_id) {
                        worker.abort();
                    }
                    job
                };
                Data::Job(JobData {
                    job,
                    watermark: self.store.state.watermark,
                })
            }
            Operation::JobsRetry(mutation) => {
                self.reserve_worker()?;
                let job = self.store.retry(mutation)?;
                let job = self.start_job(job)?;
                Data::Job(JobData {
                    job,
                    watermark: self.store.state.watermark,
                })
            }
            Operation::JobsSnapshot(_) => Data::Jobs(JobsData {
                session_id: self.store.state.session_id.clone(),
                watermark: self.store.state.watermark,
                jobs: self.store.state.jobs.values().cloned().collect(),
            }),
            Operation::EventsReplay(params) => Data::Replay(self.store.replay(params)?),
            Operation::InstalledSnapshot(_) => Data::Installed(InstalledData {
                registry_version: Protocol::default(),
                scope: "managementRegistryOnly".to_owned(),
                completeness: Completeness::Complete,
                installations: Vec::new(),
                watermark: self.store.state.watermark,
            }),
            Operation::DiagnosticsExport(_) => Data::Diagnostics(DiagnosticsData {
                report_version: 1,
                backend_version: env!("CARGO_PKG_VERSION").to_owned(),
                protocol: Protocol::default(),
                redacted: true,
                job_count: self.store.state.jobs.len() as u32,
                cached_product_count: self.store.state.catalog.len() as u32,
                runtime_certified: false,
                inventory_authorized: false,
            }),
            Operation::InventorySnapshot(_) => {
                let mut error = WireError::new(
                    ErrorCode::AccessUnknown,
                    "Complete owned-PC inventory is not proven. History/catalog records cannot establish ownership.",
                    false,
                );
                error.details = serde_json::json!({
                    "inventory": {"completeness":"unknown","checkedAt":null,"lastCompleteAt":null,
                        "source":"notQueried","reason":"Consumer audience and complete PC coverage unproven."}
                }).as_object().cloned();
                return Err(error);
            }
            Operation::GameLaunch(_) => {
                return Err(WireError::new(
                    ErrorCode::RuntimeMismatch,
                    "No certified signed paired runtime is available. No process was launched.",
                    false,
                ));
            }
            Operation::InstallPlan(_)
            | Operation::JobsEnqueue(EnqueueParams::Install { .. })
            | Operation::GameUpdate(_)
            | Operation::GameRollback(_)
            | Operation::GameRemove(_) => {
                return Err(WireError::new(
                    ErrorCode::PackageUnavailable,
                    "Authorized verified package lifecycle is not implemented. No installation or save was modified.",
                    false,
                ));
            }
            _ => {
                return Err(WireError::new(
                    ErrorCode::CapabilityMissing,
                    "This operation is unavailable. Review its negotiated capability reason.",
                    false,
                ));
            }
        };
        Ok(Some(data))
    }

    fn reserve_worker(&self) -> Result<(), WireError> {
        if self.tasks.len() >= MAX_WORKERS {
            return Err(WireError::new(
                ErrorCode::LimitExceeded,
                "Four management operations are already active. Retry after a terminal outcome.",
                true,
            ));
        }
        Ok(())
    }

    fn account_status(&mut self) -> Result<AuthData, WireError> {
        let mut status = auth_status(&self.token_manager()?)?;
        status.flow = self.auth_flow.clone();
        Ok(status)
    }

    async fn stop_auth(&mut self) -> Result<(), WireError> {
        if let Some(handoff) = self.auth_handoff.take() {
            handoff.task.abort();
            match handoff.task.await {
                Ok(_) => {}
                Err(error) if error.is_cancelled() => {}
                Err(_) => {
                    return Err(WireError::new(
                        ErrorCode::AuthInvalid,
                        "Private consent receiver failed while stopping the flow.",
                        true,
                    ));
                }
            }
        }
        if let Some(mut child) = self.auth_child.take()
            && child
                .try_wait()
                .map_err(|_| {
                    WireError::new(
                        ErrorCode::AuthInvalid,
                        "Consent worker status is unavailable.",
                        true,
                    )
                })?
                .is_none()
        {
            child.kill().await.map_err(|_| {
                WireError::new(
                    ErrorCode::AuthInvalid,
                    "Consent cancellation failed. Account state must be reconciled.",
                    true,
                )
            })?;
        }
        self.auth_started = None;
        Ok(())
    }

    fn poll_auth(&mut self) -> Result<(), WireError> {
        let expired = self
            .auth_started
            .is_some_and(|started| started.elapsed() > Duration::from_secs(600));
        if expired {
            if let Some(handoff) = self.auth_handoff.take() {
                handoff.task.abort();
            }
            if let Some(flow) = &mut self.auth_flow
                && matches!(flow.state, AuthFlowState::Pending)
            {
                flow.state = AuthFlowState::Failed;
                flow.error = Some(WireError::new(
                    ErrorCode::AuthExpired,
                    "Native consent exceeded its ten-minute deadline.",
                    true,
                ));
            }
        }
        let Some(child) = &mut self.auth_child else {
            return Ok(());
        };
        if expired {
            child.start_kill().map_err(|_| {
                WireError::new(
                    ErrorCode::AuthInvalid,
                    "Expired consent worker could not be cancelled.",
                    true,
                )
            })?;
        }
        let Some(exit) = child.try_wait().map_err(|_| {
            WireError::new(
                ErrorCode::AuthInvalid,
                "Consent worker status is unavailable.",
                true,
            )
        })?
        else {
            return Ok(());
        };
        self.auth_child = None;
        if exit.success() && self.auth_handoff.is_some() && !expired {
            return Ok(());
        }
        if let Some(handoff) = self.auth_handoff.take() {
            handoff.task.abort();
        }
        self.auth_started = None;
        if let Some(flow) = &mut self.auth_flow
            && matches!(flow.state, AuthFlowState::Pending)
        {
            if exit.code() == Some(2) {
                flow.state = AuthFlowState::Cancelled;
                flow.error = Some(WireError::new(
                    ErrorCode::AuthCancelled,
                    "The native consent window was closed before authorization completed.",
                    false,
                ));
            } else {
                flow.state = AuthFlowState::Failed;
                flow.error = Some(WireError::new(
                    if expired {
                        ErrorCode::AuthExpired
                    } else {
                        ErrorCode::AuthInvalid
                    },
                    "Consent did not produce an approved nonempty XboxLive session. No package authorization is implied.",
                    true,
                ));
            }
        }
        Ok(())
    }

    async fn complete_auth(
        &mut self,
        flow_id: String,
        result: Result<ConsentHandoff, WireError>,
    ) -> Result<(), WireError> {
        if self.auth_flow.as_ref().is_none_or(|flow| {
            flow.flow_id != flow_id || !matches!(flow.state, AuthFlowState::Pending)
        }) {
            return Ok(());
        }
        let result = if self
            .auth_started
            .is_some_and(|started| started.elapsed() > Duration::from_secs(600))
        {
            Err(WireError::new(
                ErrorCode::AuthExpired,
                "Native consent exceeded its ten-minute deadline.",
                true,
            ))
        } else {
            result
        };
        self.stop_auth().await?;
        let outcome = match result {
            Ok(ConsentHandoff::StoreCompleted { session }) => {
                if session.flow_id != flow_id || !session.valid() {
                    Err(WireError::new(
                        ErrorCode::AuthInvalid,
                        "Native consent did not return matching complete unexpired store proof.",
                        false,
                    ))
                } else {
                    if self.async_keychain_io {
                        let tokens = self.token_manager()?;
                        self.account_mutation_pending = true;
                        self.tasks.spawn(async move {
                            Completion::AccountCommit { flow_id, result: account_work(move || {
                                if !matches!(auth_status(&tokens)?.state, AuthState::SignedOut) {
                                    return Err(WireError::new(ErrorCode::AuthInvalid,
                                        "Launcher credentials changed during consent. Disconnect explicitly before retrying.", false));
                                }
                                tokens.save_management_store_session(*session).map_err(|_| WireError::new(
                                    ErrorCode::AuthInvalid, "Launcher Keychain commit failed; review profile status.", true))
                            }).await }
                        });
                        return Ok(());
                    }
                    self.token_manager().and_then(|tokens| {
                        if !matches!(auth_status(&tokens)?.state, AuthState::SignedOut) {
                            return Err(WireError::new(ErrorCode::AuthInvalid,
                                "Launcher credentials changed during consent. Disconnect explicitly before retrying.", false));
                        }
                        tokens.save_management_store_session(*session).map_err(|_| WireError::new(
                            ErrorCode::AuthInvalid, "Launcher Keychain commit failed; review profile status.", true))
                    })
                }
            }
            Ok(ConsentHandoff::Completed { session }) => {
                if !xal_session_valid(&session) {
                    Err(WireError::new(
                        ErrorCode::AuthInvalid,
                        "Native consent returned an empty, expired or unsupported session.",
                        false,
                    ))
                } else {
                    self.token_manager().and_then(|tokens| {
                        if !matches!(auth_status(&tokens)?.state, AuthState::SignedOut) {
                            return Err(WireError::new(ErrorCode::AuthInvalid,
                                "Account credentials changed during consent. Disconnect explicitly before retrying.", false));
                        }
                        tokens.save_xal_user_session(flow_id, *session).map_err(|_| WireError::new(
                            ErrorCode::AuthInvalid, "Native Keychain session commit failed; review account state.", true))
                    })
                }
            }
            Ok(ConsentHandoff::Cancelled) => Err(WireError::new(
                ErrorCode::AuthCancelled,
                "The native consent window closed before authorization completed.",
                false,
            )),
            Ok(ConsentHandoff::Failed) => Err(WireError::new(
                ErrorCode::AuthInvalid,
                "Native Microsoft/XboxLive consent failed. No package authorization is implied.",
                true,
            )),
            Err(error) => Err(error),
        };
        self.finish_auth(outcome);
        Ok(())
    }

    fn finish_auth(&mut self, outcome: Result<(), WireError>) {
        if let Some(flow) = &mut self.auth_flow {
            match outcome {
                Ok(()) => {
                    flow.state = AuthFlowState::Completed;
                    flow.error = None;
                }
                Err(error) => {
                    flow.state = if error.code == ErrorCode::AuthCancelled {
                        AuthFlowState::Cancelled
                    } else {
                        AuthFlowState::Failed
                    };
                    flow.error = Some(error);
                }
            }
        }
    }
    fn start_job(&mut self, job: Job) -> Result<Job, WireError> {
        let job = self
            .store
            .transition(&job.job_id, JobState::Running, None, None)?;
        let provider = self.provider.clone();
        let product = job.product.clone();
        let job_id = job.job_id.clone();
        let handle = self.tasks.spawn(async move {
            Completion::Job {
                job_id,
                result: provider.fetch(product).await,
            }
        });
        self.workers.insert(job.job_id.clone(), handle);
        Ok(job)
    }
}

async fn account_work<T: Send + 'static>(
    work: impl FnOnce() -> Result<T, WireError> + Send + 'static,
) -> Result<T, WireError> {
    let unavailable = || {
        WireError::new(
            ErrorCode::AuthInvalid,
            "Launcher account worker is unavailable. Reconcile account state before retrying.",
            true,
        )
    };
    let (sender, receiver) = tokio::sync::oneshot::channel();
    std::thread::Builder::new()
        .name("xodus-account".to_owned())
        .spawn(move || {
            let _ = sender.send(work());
        })
        .map_err(|_| unavailable())?;
    receiver.await.map_err(|_| unavailable())?
}

fn prepare_consent(tokens: TokenManager, flow_id: String) -> Result<ConsentBootstrap, WireError> {
    if !matches!(auth_status(&tokens)?.state, AuthState::SignedOut) {
        return Err(WireError::new(
            ErrorCode::InvalidTransition,
            "Disconnect existing launcher credentials before connecting another account.",
            false,
        ));
    }
    let device = match tokens.get_device_license() {
        Ok(device) => Some(device),
        Err(TokenStoreError::NotFound) => None,
        Err(_) => {
            return Err(WireError::new(
                ErrorCode::AuthInvalid,
                "Launcher device credentials could not be read; they were not replaced.",
                true,
            ));
        }
    };
    let device_token = match tokens.get_device_sts_token() {
        Ok(Token::Legacy(token)) => Some(token),
        Err(TokenStoreError::NotFound) => None,
        _ => {
            return Err(WireError::new(
                ErrorCode::AuthInvalid,
                "Launcher device proof could not be read or is unsupported; it was not replaced.",
                true,
            ));
        }
    };
    Ok(ConsentBootstrap {
        flow_id,
        device,
        device_token,
    })
}

async fn read_account_status(tokens: TokenManager) -> Result<AuthData, WireError> {
    let unavailable = || {
        let mut error = WireError::new(
            ErrorCode::AuthInvalid,
            "Launcher Keychain read is unavailable. No approval was requested or credentials replaced.",
            true,
        );
        error.details = serde_json::json!({"category":"credentialStoreUnavailable",
            "action":"Review Keychain availability or use explicitly initiated account consent."})
        .as_object()
        .cloned();
        error
    };
    let (sender, receiver) = tokio::sync::oneshot::channel();
    std::thread::Builder::new()
        .name("xodus-account-read".to_owned())
        .spawn(move || {
            let _ = sender.send(auth_status(&tokens));
        })
        .map_err(|_| unavailable())?;
    tokio::time::timeout(Duration::from_secs(2), receiver)
        .await
        .map_err(|_| unavailable())?
        .map_err(|_| unavailable())?
}

pub fn auth_status(tokens: &TokenManager) -> Result<AuthData, WireError> {
    let keychain_error = || {
        let mut error = WireError::new(
            ErrorCode::AuthInvalid,
            "Stored account credentials could not be read. Review native Keychain access.",
            true,
        );
        error.details = serde_json::json!({"category":"credentialStoreUnavailable",
            "action":"Review native Keychain permission or availability; do not replace stored credentials automatically."})
            .as_object().cloned();
        error
    };
    let invalid_status = || AuthData {
        state: AuthState::Invalid,
        credential_store: "macOSKeychain".to_owned(),
        audience: None,
        expires_at: None,
        entitlement_authorized: false,
        flow: None,
    };
    match tokens.get_management_store_session() {
        Ok(Some(session)) => {
            let state = if !session.structurally_valid() {
                AuthState::Invalid
            } else if session.valid() {
                AuthState::CredentialPresent
            } else {
                AuthState::Expired
            };
            return Ok(AuthData {
                state,
                credential_store: "macOSKeychain".to_owned(),
                audience: Some(xodus::tokens::PASSPORT_STS.to_owned()),
                expires_at: session
                    .tokens
                    .get(xodus::tokens::PASSPORT_STS)
                    .and_then(|token| {
                        let Token::Legacy(token) = token else {
                            return None;
                        };
                        let user_expires =
                            DateTime::parse_from_rfc3339(&token.lifetime.expires).ok()?;
                        let device_expires =
                            DateTime::parse_from_rfc3339(&session.device_token.lifetime.expires)
                                .ok()?;
                        Some(user_expires.min(device_expires).to_rfc3339())
                    }),
                entitlement_authorized: false,
                flow: None,
            });
        }
        Ok(None) => {}
        Err(TokenStoreError::Serde(_) | TokenStoreError::InvalidCredential) => {
            return Ok(invalid_status());
        }
        Err(_) => return Err(keychain_error()),
    }
    let proof = match tokens.get_xal_user_session() {
        Ok(proof) => proof,
        Err(TokenStoreError::Serde(_)) => return Ok(invalid_status()),
        Err(_) => return Err(keychain_error()),
    };
    if let Some(proof) = proof {
        let session = proof.session;
        let state = if !xal_session_structurally_valid(&session) {
            AuthState::Invalid
        } else if !xal_session_valid(&session) {
            AuthState::Expired
        } else {
            AuthState::CredentialPresent
        };
        return Ok(AuthData {
            state,
            credential_store: "macOSKeychain".to_owned(),
            audience: Some("http://xboxlive.com".to_owned()),
            expires_at: session
                .authorization_token
                .as_ref()
                .map(|token| token.not_after.to_rfc3339()),
            entitlement_authorized: false,
            flow: None,
        });
    }
    let (state, expires_at, audience) = match tokens.get_user_sts_token() {
        Err(TokenStoreError::NotFound) => match tokens.get_user() {
            Err(TokenStoreError::NotFound) => (AuthState::SignedOut, None, None),
            Ok(_) => (AuthState::Invalid, None, None),
            Err(TokenStoreError::Serde(_)) => return Ok(invalid_status()),
            Err(_) => return Err(keychain_error()),
        },
        Err(TokenStoreError::Serde(_)) => return Ok(invalid_status()),
        Err(_) => return Err(keychain_error()),
        Ok(Token::Legacy(token)) if !token.token.trim().is_empty() => {
            match DateTime::parse_from_rfc3339(&token.lifetime.expires) {
                Ok(expires) if expires > Utc::now() => (
                    AuthState::CredentialPresent,
                    Some(expires.to_rfc3339()),
                    Some(xodus::tokens::manager::PASSPORT_STS.to_owned()),
                ),
                Ok(expires) => (AuthState::Expired, Some(expires.to_rfc3339()), None),
                Err(_) => (AuthState::Invalid, None, None),
            }
        }
        Ok(_) => (AuthState::Invalid, None, None),
    };
    Ok(AuthData {
        state,
        credential_store: "macOSKeychain".to_owned(),
        audience,
        expires_at,
        entitlement_authorized: false,
        flow: None,
    })
}

fn xal_session_structurally_valid(session: &xodus::xal::TokenStore) -> bool {
    use xodus::xal::oauth2::TokenResponse;
    session.app_params.client_id == "000000004424da1f"
        && session.sandbox_id == "RETAIL"
        && !session.live_token.access_token().secret().trim().is_empty()
        && session
            .user_token
            .as_ref()
            .is_some_and(|token| !token.token.trim().is_empty())
        && session
            .authorization_token
            .as_ref()
            .is_some_and(|token| !token.token.trim().is_empty())
}

pub fn xal_session_valid(session: &xodus::xal::TokenStore) -> bool {
    xal_session_structurally_valid(session)
        && session
            .user_token
            .as_ref()
            .is_some_and(|token| token.not_after > Utc::now())
        && session
            .authorization_token
            .as_ref()
            .is_some_and(|token| token.not_after > Utc::now())
}

pub fn search(store: &Store, params: &SearchParams) -> Result<SearchData, WireError> {
    let query = params.query.trim().to_lowercase();
    // Cursor carries a stable scope checksum, not user data or a path.
    let scope = format!(
        "{}:{}:{}:{}",
        query, params.market, params.language, params.limit
    );
    let checksum = scope.bytes().fold(0xcbf29ce484222325u64, |hash, byte| {
        (hash ^ u64::from(byte)).wrapping_mul(0x100000001b3)
    });
    let offset = if let Some(cursor) = &params.cursor {
        let parts: Vec<_> = cursor.split(':').collect();
        if parts.len() != 3 {
            return Err(transport::invalid());
        }
        let revision = parts[0].parse::<u64>().map_err(|_| transport::invalid())?;
        let cursor_scope = parts[1].parse::<u64>().map_err(|_| transport::invalid())?;
        if revision != store.state.cache_revision || cursor_scope != checksum {
            return Err(WireError::new(
                ErrorCode::RevisionConflict,
                "Search scope or public cache changed. Restart this scoped search.",
                true,
            ));
        }
        parts[2]
            .parse::<usize>()
            .map_err(|_| transport::invalid())?
    } else {
        0
    };
    let mut matching: Vec<_> = store
        .state
        .catalog
        .values()
        .filter(|record| {
            record.market == params.market
                && record.language == params.language
                && record.pc_catalog_candidate
                && (record.title.to_lowercase().contains(&query)
                    || record.product_id.to_lowercase().contains(&query))
        })
        .cloned()
        .collect();
    matching.sort_by(|left, right| {
        left.title
            .cmp(&right.title)
            .then(left.product_id.cmp(&right.product_id))
    });
    if offset > matching.len() {
        return Err(transport::invalid());
    }
    let more = matching.len() > offset.saturating_add(params.limit as usize);
    let products = matching
        .into_iter()
        .skip(offset)
        .take(params.limit as usize)
        .map(|mut record| {
            record.freshness = Freshness::Cached;
            record
        })
        .collect();
    Ok(SearchData {
        products,
        corpus: CORPUS.to_owned(),
        completeness: "partial".to_owned(),
        next_cursor: more.then(|| {
            format!(
                "{}:{}:{}",
                store.state.cache_revision,
                checksum,
                offset + params.limit as usize
            )
        }),
        cache_revision: store.state.cache_revision,
    })
}

async fn write_frame<W: AsyncWrite + Unpin>(
    writer: &mut W,
    frame: &impl serde::Serialize,
) -> Result<(), WireError> {
    let bytes = serde_json::to_vec(frame).map_err(|_| {
        WireError::new(
            ErrorCode::InternalError,
            "Could not encode a management frame.",
            false,
        )
    })?;
    if bytes.len() > MAX_LINE_BYTES {
        return Err(WireError::new(
            ErrorCode::LimitExceeded,
            "Response exceeds the frame limit. Request a smaller replay page.",
            true,
        ));
    }
    writer.write_all(&bytes).await.map_err(|_| {
        WireError::new(
            ErrorCode::InternalError,
            "Management output pipe is unavailable.",
            false,
        )
    })?;
    writer.write_all(b"\n").await.map_err(|_| {
        WireError::new(
            ErrorCode::InternalError,
            "Management output pipe is unavailable.",
            false,
        )
    })?;
    writer.flush().await.map_err(|_| {
        WireError::new(
            ErrorCode::InternalError,
            "Management output pipe is unavailable.",
            false,
        )
    })
}

async fn write_result<W: AsyncWrite + Unpin>(
    writer: &mut W,
    id: String,
    result: Result<Data, WireError>,
) -> Result<(), WireError> {
    let frame = ResultFrame::new(id.clone(), result);
    match write_frame(writer, &frame).await {
        Err(error) if error.code == ErrorCode::LimitExceeded => {
            write_frame(writer, &ResultFrame::new(id, Err(error))).await
        }
        result => result,
    }
}

pub async fn serve<R, W>(mut backend: Backend, reader: R, mut writer: W) -> Result<(), WireError>
where
    R: AsyncBufRead + Unpin + Send + 'static,
    W: AsyncWrite + Unpin,
{
    let (sender, mut input) = mpsc::channel(1);
    let input_task = tokio::spawn(async move {
        let mut reader = reader;
        loop {
            let line = transport::read_line(&mut reader).await;
            let terminal = matches!(&line, Ok(Line::Eof | Line::Truncated) | Err(_));
            if sender.send(line).await.is_err() || terminal {
                break;
            }
        }
    });
    let input_guard = input_task.abort_handle();
    let mut result = serve_loop(&mut backend, &mut input, &mut writer).await;
    input_guard.abort();
    backend.tasks.abort_all();
    while backend.tasks.join_next().await.is_some() {}
    for request_id in std::mem::take(&mut backend.pending_requests) {
        let error = WireError::new(
            if backend.account_mutation_pending {
                ErrorCode::AuthInvalid
            } else {
                ErrorCode::Cancelled
            },
            if backend.account_mutation_pending {
                "Transport closed during a started native account mutation. Reconnect and reconcile; cancellation is not confirmed."
            } else {
                "Transport closed before the request completed."
            },
            true,
        );
        let written = write_result(&mut writer, request_id, Err(error.clone())).await;
        if result.is_ok() {
            result = written.and(Err(error));
        }
    }
    let cancelled = backend.stop_auth().await;
    result.and(cancelled)?;
    if backend.account_mutation_pending {
        return Err(WireError::new(
            ErrorCode::AuthInvalid,
            "Native account mutation started before transport closed. Reconnect and reconcile its outcome.",
            true,
        ));
    }
    Ok(())
}

async fn serve_loop<W: AsyncWrite + Unpin>(
    backend: &mut Backend,
    input: &mut mpsc::Receiver<std::io::Result<Line>>,
    writer: &mut W,
) -> Result<(), WireError> {
    let mut negotiated = false;
    let mut seen = HashSet::new();
    let mut invalid_sequence = 0u64;
    let mut auth_poll = tokio::time::interval(Duration::from_millis(250));
    loop {
        let previous = backend.store.state.watermark;
        tokio::select! {
            line = input.recv() => {
                let line = match line {
                    Some(Ok(Line::Eof)) | None => return Ok(()),
                    Some(Err(_)) => return Err(WireError::new(ErrorCode::InternalError,
                        "Management input pipe is unavailable.", false)),
                    Some(Ok(line)) => line,
                };
                invalid_sequence += 1;
                let fallback = format!("invalid-{invalid_sequence}");
                let (id, request) = match line {
                    Line::Frame(bytes) => {
                        let id = serde_json::from_slice::<serde_json::Value>(&bytes).ok()
                            .and_then(|value| value["requestID"].as_str().map(str::to_owned))
                            .filter(|value| identifier_valid(value) && !value.starts_with("invalid-"))
                            .unwrap_or(fallback);
                        (id, transport::parse(&bytes))
                    }
                    Line::Oversized => (fallback, Err(WireError::new(ErrorCode::InvalidRequest,
                        "Input frame exceeds the 1 MiB limit.", false))),
                    Line::Truncated => {
                        write_result(writer, fallback, Err(WireError::new(ErrorCode::InvalidRequest,
                            "Input ended before LF framing; the request was not executed.", false))).await?;
                        return Err(transport::invalid());
                    }
                    Line::Eof => return Ok(()),
                };
                if seen.len() >= 4096 {
                    write_result(writer, id, Err(WireError::new(ErrorCode::LimitExceeded,
                        "Connection request limit reached. Reconnect and reconcile durable snapshots.", true))).await?;
                    return Ok(());
                }
                if !seen.insert(id.clone()) {
                    write_result(writer, id, Err(WireError::new(ErrorCode::DuplicateRequest,
                        "Request ID was already used on this connection. Use a fresh ID and durable idempotency key.", false))).await?;
                    continue;
                }
                let result = match request {
                    Err(error) => Err(error),
                    Ok(request) if !negotiated && !matches!(request.operation, Operation::Hello(_)) =>
                        Err(WireError::new(ErrorCode::HelloRequired, "Negotiate hello before other commands.", false)),
                    Ok(request) if negotiated && matches!(request.operation, Operation::Hello(_)) =>
                        Err(WireError::new(ErrorCode::InvalidTransition, "Hello was already negotiated on this connection.", false)),
                    Ok(request) => {
                        let is_hello = matches!(request.operation, Operation::Hello(_));
                        let result = backend.dispatch(&request).await;
                        if is_hello && result.is_ok() { negotiated = true; }
                        result
                    }
                };
                let fatal = result.as_ref().err().is_some_and(|error|
                    error.code == ErrorCode::RegistryRecoveryRequired);
                match result {
                    Ok(Some(data)) => write_result(writer, id, Ok(data)).await?,
                    Ok(None) => {},
                    Err(error) => write_result(writer, id, Err(error)).await?,
                }
                if fatal { return Err(WireError::new(ErrorCode::RegistryRecoveryRequired,
                    "State persistence failed. Stop and reconcile before retrying mutations.", false)); }
            },
            completion = backend.tasks.join_next(), if !backend.tasks.is_empty() => {
                match completion {
                    Some(Ok(Completion::ConsentPrepared { request_id, flow_id, result })) => {
                        backend.pending_requests.remove(&request_id);
                        let result = if backend.auth_flow.as_ref().is_none_or(|flow|
                            flow.flow_id != flow_id || !matches!(flow.state, AuthFlowState::Pending)) {
                            Err(WireError::new(ErrorCode::AuthCancelled,
                                "Account preparation was cancelled before native sign-in started.", false))
                        } else {
                            result.and_then(|bootstrap| {
                                let executable = std::env::current_exe().map_err(|_| WireError::new(
                                    ErrorCode::AuthInvalid, "Native consent executable is unavailable.", false))?;
                                let (child, handoff) = spawn_consent(&executable, &flow_id, &bootstrap)?;
                                backend.auth_child = Some(child);
                                backend.auth_handoff = Some(handoff);
                                backend.auth_started = Some(std::time::Instant::now());
                                Ok(Data::Auth(AuthData { state: AuthState::SignedOut,
                                    credential_store: "macOSKeychain".to_owned(), audience: None, expires_at: None,
                                    entitlement_authorized: false, flow: backend.auth_flow.clone() }))
                            })
                        };
                        if let Err(error) = &result
                            && backend.auth_flow.as_ref().is_some_and(|flow|
                                flow.flow_id == flow_id && matches!(flow.state, AuthFlowState::Pending)) {
                            backend.finish_auth(Err(error.clone()));
                        }
                        write_result(writer, request_id, result).await?;
                    },
                    Some(Ok(Completion::AccountCommit { flow_id, result })) => {
                        backend.account_mutation_pending = false;
                        if backend.auth_flow.as_ref().is_none_or(|flow|
                            flow.flow_id != flow_id || !matches!(flow.state, AuthFlowState::Pending)) {
                            return Err(WireError::new(ErrorCode::AuthInvalid,
                                "Native commit completed outside its reserved flow. Reconcile launcher account state.", true));
                        }
                        backend.finish_auth(result);
                    },
                    Some(Ok(Completion::AccountLogout { request_id, result })) => {
                        backend.account_mutation_pending = false;
                        backend.pending_requests.remove(&request_id);
                        write_result(writer, request_id, result.map(Data::Auth)).await?;
                    },
                    Some(Ok(Completion::AccountStatus { request_id, result })) => {
                        backend.pending_requests.remove(&request_id);
                        let result = result.map(Data::Auth);
                        write_result(writer, request_id, result).await?;
                    },
                    Some(Ok(Completion::Discovery { request_id, result })) => {
                        backend.pending_requests.remove(&request_id);
                        let result = result.and_then(|page| {
                            backend.store.cache_discovery_products(&page.products)?;
                            Ok(Data::Discovery(page))
                        });
                        let fatal = result.as_ref().err().is_some_and(|error|
                            error.code == ErrorCode::RegistryRecoveryRequired);
                        write_result(writer, request_id, result).await?;
                        if fatal { return Err(WireError::new(ErrorCode::RegistryRecoveryRequired,
                            "Discovery cache persistence failed. Reconcile before retrying.", false)); }
                    },
                    Some(Ok(Completion::Product {request_id, result})) => {
                        backend.pending_requests.remove(&request_id);
                        let result = result.and_then(|product| {
                            backend.store.cache_product(product.clone())?;
                            Ok(Data::Product(ProductData { product }))
                        });
                        let fatal = result.as_ref().err().is_some_and(|error|
                            error.code == ErrorCode::RegistryRecoveryRequired);
                        write_result(writer, request_id, result).await?;
                        if fatal { return Err(WireError::new(ErrorCode::RegistryRecoveryRequired,
                            "State persistence failed. Reconcile before retrying.", false)); }
                    },
                    Some(Ok(Completion::Job {job_id, result})) => {
                        backend.workers.remove(&job_id);
                        if backend.store.state.jobs.get(&job_id).is_some_and(|job| !job.state.terminal()) {
                            match result {
                                Ok(product) => {
                                    match backend.store.transition(&job_id, JobState::Completed, None, Some(product)) {
                                        Ok(_) => {}
                                        Err(error) if error.code == ErrorCode::LimitExceeded => {
                                            backend.store.transition(&job_id, JobState::Failed, Some(error), None)?;
                                        }
                                        Err(error) => return Err(error),
                                    }
                                },
                                Err(error) => { backend.store.transition(&job_id, JobState::Failed, Some(error), None)?; },
                            }
                        }
                    },
                    Some(Err(error)) if error.is_cancelled() => {},
                    Some(Err(_)) => return Err(WireError::new(ErrorCode::InternalError,
                        "Metadata worker failed unexpectedly; restart and reconcile job state.", false)),
                    None => {},
                }
            },
            completion = async { (&mut backend.auth_handoff.as_mut().unwrap().task).await },
                if backend.auth_handoff.is_some() => {
                let handoff = backend.auth_handoff.take().unwrap();
                let result = completion.map_err(|_| WireError::new(ErrorCode::AuthInvalid,
                    "Private native consent receiver failed.", true)).and_then(|result| result);
                backend.complete_auth(handoff.flow_id, result).await?;
            },
            _ = auth_poll.tick(), if backend.auth_child.is_some() || backend.auth_handoff.is_some() => {
                backend.poll_auth()?;
            }
        }
        for event in backend
            .store
            .state
            .events
            .iter()
            .filter(|event| event.sequence > previous)
        {
            write_frame(writer, event).await?;
        }
    }
}

pub async fn run(directory: &Path, protocol: u32) -> std::process::ExitCode {
    std::panic::set_hook(Box::new(|_| {
        eprintln!("Management backend stopped unexpectedly; no raw diagnostic data was emitted.")
    }));
    let result = if protocol != 1 {
        Err(WireError::new(
            ErrorCode::ProtocolMismatch,
            "Only --protocol 1 is supported.",
            false,
        ))
    } else {
        match (Store::open(directory), PublicCatalog::new()) {
            (Ok(store), Ok(provider)) => {
                serve(
                    Backend::new(store, Arc::new(provider)),
                    BufReader::new(tokio::io::stdin()),
                    tokio::io::stdout(),
                )
                .await
            }
            (Err(error), _) | (_, Err(error)) => Err(error),
        }
    };
    match result {
        Ok(()) => std::process::ExitCode::SUCCESS,
        Err(error) => {
            let _ =
                write_result(&mut tokio::io::stdout(), "invalid-0".to_owned(), Err(error)).await;
            eprintln!(
                "Management transport failed. Review the typed error and reconcile durable state."
            );
            std::process::ExitCode::FAILURE
        }
    }
}

#[cfg(all(test, unix))]
mod auth_lifecycle_tests {
    use super::*;

    fn backend() -> (tempfile::TempDir, Backend) {
        use std::os::unix::fs::PermissionsExt;
        let temporary = tempfile::tempdir().unwrap();
        std::fs::set_permissions(temporary.path(), std::fs::Permissions::from_mode(0o700)).unwrap();
        let path = temporary.path().canonicalize().unwrap();
        let mut backend = Backend::new(
            Store::open(&path).unwrap(),
            Arc::new(PublicCatalog::new().unwrap()),
        );
        backend.native_auth = true;
        backend.async_keychain_io = false;
        backend.tokens = Some(TokenManager::with_management_backend(Arc::new(
            xodus::tokens::backend::MemoryBackend::default(),
        )));
        (temporary, backend)
    }

    fn own_worker(backend: &mut Backend) -> u32 {
        let child = Command::new("/bin/sleep")
            .arg("60")
            .kill_on_drop(true)
            .spawn()
            .unwrap();
        let pid = child.id().unwrap();
        backend.auth_child = Some(child);
        backend.auth_flow = Some(AuthFlow {
            flow_id: "fixture-flow".to_owned(),
            state: AuthFlowState::Pending,
            error: None,
        });
        pid
    }

    fn no_owned_process(pid: u32) {
        let result = std::process::Command::new("/bin/ps")
            .args(["-p", &pid.to_string(), "-o", "pid="])
            .output()
            .unwrap();
        assert!(result.stdout.is_empty());
    }

    fn fixture_session() -> xodus::xal::TokenStore {
        let token = serde_json::json!({
            "IssueInstant":"2026-01-01T00:00:00Z", "NotAfter":"2099-01-01T00:00:00Z",
            "Token":"fixture-not-a-credential", "DisplayClaims":{"xui":[{"uhs":"fixture"}]}
        });
        serde_json::from_value(serde_json::json!({
            "app_params":{"client_id":"000000004424da1f", "title_id":"704208617",
                "auth_scopes":["service::user.auth.xboxlive.com::MBI_SSL"],
                "redirect_uri":"https://login.live.com/oauth20_desktop.srf"},
            "client_params":xodus::xal::client_params::CLIENT_WINDOWS(),
            "sandbox_id":"RETAIL", "live_token":{"access_token":"fixture-not-a-credential",
                "token_type":"bearer","expires_in":3600},
            "user_token":token, "authorization_token":token
        }))
        .unwrap()
    }

    fn fixture_store_session() -> xodus::models::secrets::ManagementStoreSession {
        let token = xodus::models::secrets::LegacyToken {
            key_name: Some(xodus::tokens::PASSPORT_STS.to_owned()),
            token: "<EncryptedData Id=\"fixture\" xmlns=\"http://www.w3.org/2001/04/xmlenc#\" Type=\"http://www.w3.org/2001/04/xmlenc#Element\"><EncryptionMethod Algorithm=\"http://www.w3.org/2001/04/xmlenc#tripledes-cbc\"/><KeyInfo><KeyName>http://Passport.NET/STS</KeyName></KeyInfo><CipherData><CipherValue>Zml4dHVyZQ==</CipherValue></CipherData></EncryptedData>".to_owned(),
            binary_secret: Some(format!("BAAA{}AA==", "AAAA".repeat(1364))),
            tpm_key: None,
            lifetime: xodus::models::soap::Timestamp { id: None,
                created: "2026-01-01T00:00:00Z".to_owned(), expires: "2099-01-01T00:00:00Z".to_owned() },
        };
        xodus::models::secrets::ManagementStoreSession {
            flow_id: "fixture-flow".to_owned(),
            user: xodus::models::secrets::User {
                puid: "fixture".to_owned(),
                username: "fixture".to_owned(),
            },
            tokens: std::collections::HashMap::from([(
                xodus::tokens::PASSPORT_STS.to_owned(),
                Token::Legacy(token.clone()),
            )]),
            device: xodus::models::secrets::Device {
                puid: "fixture".to_owned(),
                hwid: "fixture".to_owned(),
                device_id: "fixture".to_owned(),
                username: "fixture".to_owned(),
                password: "fixture-not-a-password".to_owned(),
                splicense: "fixture-not-a-license".to_owned(),
            },
            device_token: token,
        }
    }

    #[tokio::test]
    async fn store_consent_commits_only_active_complete_proof_and_reports_cached_not_authorized() {
        let (_temporary, mut backend) = backend();
        let pid = own_worker(&mut backend);
        let session = fixture_store_session();
        assert!(session.valid());
        backend
            .complete_auth(
                "fixture-flow".to_owned(),
                Ok(ConsentHandoff::StoreCompleted {
                    session: Box::new(session),
                }),
            )
            .await
            .unwrap();
        let status = backend.account_status().unwrap();
        assert!(matches!(status.state, AuthState::CredentialPresent));
        assert_eq!(
            status.audience.as_deref(),
            Some(xodus::tokens::PASSPORT_STS)
        );
        assert!(!status.entitlement_authorized);
        assert!(matches!(
            status.flow.unwrap().state,
            AuthFlowState::Completed
        ));
        no_owned_process(pid);
    }

    #[tokio::test]
    async fn cancelled_foreign_and_expired_store_sessions_never_write_credentials() {
        for failure in ["foreign", "cancelled", "expired", "bad-proof"] {
            let (_temporary, mut backend) = backend();
            let pid = own_worker(&mut backend);
            let mut session = fixture_store_session();
            match failure {
                "foreign" => session.flow_id = "foreign".to_owned(),
                "cancelled" => backend.auth_flow.as_mut().unwrap().state = AuthFlowState::Cancelled,
                "expired" => {
                    backend.auth_started =
                        Some(std::time::Instant::now() - Duration::from_secs(601))
                }
                "bad-proof" => session.device_token.binary_secret = None,
                _ => unreachable!(),
            }
            backend
                .complete_auth(
                    "fixture-flow".to_owned(),
                    Ok(ConsentHandoff::StoreCompleted {
                        session: Box::new(session),
                    }),
                )
                .await
                .unwrap();
            assert!(
                backend
                    .tokens
                    .as_ref()
                    .unwrap()
                    .get_management_store_session()
                    .unwrap()
                    .is_none()
            );
            backend.stop_auth().await.unwrap();
            no_owned_process(pid);
        }
    }

    #[test]
    fn store_status_distinguishes_expiry_and_sanitizes_malformed_cached_dates() {
        use xodus::tokens::store::TokenBackend;
        let memory = Arc::new(xodus::tokens::backend::MemoryBackend::default());
        let tokens = TokenManager::with_management_backend(memory.clone());
        let mut session = fixture_store_session();
        session.device_token.lifetime.expires = "2000-01-01T00:00:00Z".to_owned();
        memory
            .set(
                "management-store-user",
                &serde_json::to_vec(&session).unwrap(),
            )
            .unwrap();
        let expired = auth_status(&tokens).unwrap();
        assert!(matches!(expired.state, AuthState::Expired));
        assert!(!expired.entitlement_authorized);
        session.device_token.lifetime.expires = "fixture-not-a-date-or-public-output".to_owned();
        memory
            .set(
                "management-store-user",
                &serde_json::to_vec(&session).unwrap(),
            )
            .unwrap();
        let invalid = auth_status(&tokens).unwrap();
        assert!(matches!(invalid.state, AuthState::Invalid));
        assert!(invalid.expires_at.is_none());
        assert!(!serde_json::to_string(&invalid).unwrap().contains("fixture"));
    }

    #[tokio::test]
    async fn only_active_parent_flow_can_commit_complete_session_to_memory_facade() {
        let (_temporary, mut backend) = backend();
        let pid = own_worker(&mut backend);
        let session = fixture_session();
        assert!(xal_session_valid(&session));
        let bytes = serde_json::to_vec(&ConsentHandoff::Completed {
            session: Box::new(session),
        })
        .unwrap();
        let frame = [
            (bytes.len() as u32).to_be_bytes().as_slice(),
            bytes.as_slice(),
        ]
        .concat();
        let outcome = read_consent(frame.as_slice()).await.unwrap();
        backend
            .complete_auth("fixture-flow".to_owned(), Ok(outcome))
            .await
            .unwrap();
        assert!(matches!(
            backend.auth_flow.as_ref().unwrap().state,
            AuthFlowState::Completed
        ));
        let proof = backend
            .tokens
            .as_ref()
            .unwrap()
            .get_xal_user_session()
            .unwrap()
            .unwrap();
        assert_eq!(proof.flow_id, "fixture-flow");
        let request = Request {
            kind: RequestKind::Request,
            protocol: Protocol::default(),
            request_id: "fixture".to_owned(),
            operation: Operation::AuthCancel(AuthCancelParams {
                flow_id: "fixture-flow".to_owned(),
            }),
        };
        let Some(Data::Auth(status)) = backend.dispatch(&request).await.unwrap() else {
            panic!("auth result required");
        };
        assert!(matches!(status.state, AuthState::CredentialPresent));
        assert!(matches!(
            status.flow.unwrap().state,
            AuthFlowState::Completed
        ));
        no_owned_process(pid);
    }

    #[tokio::test]
    async fn private_handoff_rejects_empty_oversized_truncated_and_invalid_payloads() {
        for bytes in [
            0u32.to_be_bytes().to_vec(),
            ((MAX_AUTH_HANDOFF_BYTES + 1) as u32).to_be_bytes().to_vec(),
            [4u32.to_be_bytes().as_slice(), b"{}"].concat(),
            [2u32.to_be_bytes().as_slice(), b"{}"].concat(),
        ] {
            assert!(matches!(
                read_consent(bytes.as_slice()).await,
                Err(WireError {
                    code: ErrorCode::AuthInvalid,
                    ..
                })
            ));
        }
        let bytes = serde_json::to_vec(&ConsentHandoff::Cancelled).unwrap();
        let frame = [
            (bytes.len() as u32).to_be_bytes().as_slice(),
            bytes.as_slice(),
        ]
        .concat();
        assert!(matches!(
            read_consent(frame.as_slice()).await.unwrap(),
            ConsentHandoff::Cancelled
        ));
    }

    #[tokio::test]
    async fn cancelled_and_foreign_private_handoffs_cannot_promote_credentials() {
        let (_temporary, mut backend) = backend();
        let pid = own_worker(&mut backend);
        let (parent, _peer) = tokio::net::UnixStream::pair().unwrap();
        backend.auth_handoff = Some(ConsentReader {
            flow_id: "fixture-flow".to_owned(),
            task: tokio::spawn(read_consent(parent)),
        });
        backend
            .complete_auth(
                "foreign-flow".to_owned(),
                Ok(ConsentHandoff::Completed {
                    session: Box::new(fixture_session()),
                }),
            )
            .await
            .unwrap();
        assert!(backend.auth_child.is_some());
        assert!(backend.auth_handoff.is_some());
        let request = Request {
            kind: RequestKind::Request,
            protocol: Protocol::default(),
            request_id: "fixture".to_owned(),
            operation: Operation::AuthCancel(AuthCancelParams {
                flow_id: "fixture-flow".to_owned(),
            }),
        };
        backend.dispatch(&request).await.unwrap();
        backend
            .complete_auth(
                "fixture-flow".to_owned(),
                Ok(ConsentHandoff::Completed {
                    session: Box::new(fixture_session()),
                }),
            )
            .await
            .unwrap();
        assert!(matches!(
            backend.auth_flow.as_ref().unwrap().state,
            AuthFlowState::Cancelled
        ));
        assert!(backend.auth_handoff.is_none());
        assert!(
            backend
                .tokens
                .as_ref()
                .unwrap()
                .get_xal_user_session()
                .unwrap()
                .is_none()
        );
        no_owned_process(pid);
    }

    #[tokio::test]
    async fn window_cancellation_and_consent_deadline_remain_typed_terminal_outcomes() {
        let (_temporary, mut backend) = backend();
        let pid = own_worker(&mut backend);
        backend
            .complete_auth("fixture-flow".to_owned(), Ok(ConsentHandoff::Cancelled))
            .await
            .unwrap();
        assert!(matches!(
            backend.auth_flow.as_ref().unwrap().state,
            AuthFlowState::Cancelled
        ));
        no_owned_process(pid);
        let pid = own_worker(&mut backend);
        backend.auth_started = Some(std::time::Instant::now() - Duration::from_secs(601));
        backend
            .complete_auth("fixture-flow".to_owned(), Ok(ConsentHandoff::Failed))
            .await
            .unwrap();
        assert_eq!(
            backend
                .auth_flow
                .as_ref()
                .unwrap()
                .error
                .as_ref()
                .unwrap()
                .code,
            ErrorCode::AuthExpired
        );
        assert!(
            backend
                .tokens
                .as_ref()
                .unwrap()
                .get_xal_user_session()
                .unwrap()
                .is_none()
        );
        no_owned_process(pid);
    }

    #[test]
    fn native_capabilities_refuse_plaintext_fallback_configuration() {
        let (_temporary, mut backend) = backend();
        backend.native_auth = xodus::secrets::management_native_keychain_enabled();
        let capabilities = backend.capabilities();
        for command in ["auth.begin", "auth.cancel", "auth.status", "auth.logout"] {
            assert_eq!(
                capabilities
                    .iter()
                    .find(|capability| capability.command == command)
                    .unwrap()
                    .supported,
                backend.native_auth
            );
        }

        if !backend.native_auth {
            assert_eq!(
                backend.token_manager().err().unwrap().code,
                ErrorCode::UnsupportedConfiguration
            );
        }
    }

    #[test]
    fn corrupt_credentials_are_invalid_but_store_permission_failure_stays_explicit() {
        use xodus::tokens::backend::MemoryBackend;
        use xodus::tokens::store::TokenBackend;
        struct ReadFailure {
            corrupt: bool,
        }
        impl TokenBackend for ReadFailure {
            fn get(&self, _: &str) -> Result<Option<Vec<u8>>, TokenStoreError> {
                if self.corrupt {
                    Ok(Some(b"{}".to_vec()))
                } else {
                    Err(TokenStoreError::Io(std::io::Error::new(
                        std::io::ErrorKind::PermissionDenied,
                        "fixture-secret-not-for-output",
                    )))
                }
            }
            fn set(&self, _: &str, _: &[u8]) -> Result<(), TokenStoreError> {
                unreachable!()
            }
            fn remove(&self, _: &str) -> Result<(), TokenStoreError> {
                unreachable!()
            }
        }
        let corrupt = TokenManager::new(
            Arc::new(ReadFailure { corrupt: true }),
            Arc::new(MemoryBackend::default()),
        );
        let status = auth_status(&corrupt).unwrap();
        assert!(matches!(status.state, AuthState::Invalid));
        assert!(!status.entitlement_authorized);
        let inaccessible = TokenManager::new(
            Arc::new(ReadFailure { corrupt: false }),
            Arc::new(MemoryBackend::default()),
        );
        let error = auth_status(&inaccessible).unwrap_err();
        assert_eq!(error.code, ErrorCode::AuthInvalid);
        assert_eq!(
            error.details.as_ref().unwrap()["category"],
            "credentialStoreUnavailable"
        );
        assert!(
            !serde_json::to_string(&error)
                .unwrap()
                .contains("fixture-secret")
        );
    }

    #[tokio::test]
    async fn account_status_remains_dispatchable_while_discovery_waits_and_eof_cancels_it() {
        use tokio::io::AsyncBufReadExt;
        struct PendingDiscovery;
        impl CatalogProvider for PendingDiscovery {
            fn fetch(
                &self,
                _: ProductParams,
            ) -> Pin<Box<dyn Future<Output = Result<ProductRecord, WireError>> + Send + '_>>
            {
                Box::pin(std::future::pending())
            }
            fn discovery_supported(&self) -> bool {
                true
            }
            fn discover(
                &self,
                _: DiscoveryParams,
            ) -> Pin<Box<dyn Future<Output = Result<DiscoveryData, WireError>> + Send + '_>>
            {
                Box::pin(std::future::pending())
            }
        }
        let (_temporary, mut backend) = backend();
        backend.provider = Arc::new(PendingDiscovery);
        let (client, server) = tokio::io::duplex(65536);
        let (server_read, server_write) = tokio::io::split(server);
        let task = tokio::spawn(serve(backend, BufReader::new(server_read), server_write));
        let (client_read, mut client_write) = tokio::io::split(client);
        let mut reader = BufReader::new(client_read);
        for (id, command, params) in [
            (
                "hello",
                "hello",
                serde_json::json!({"client":"fixture","clientVersion":"1"}),
            ),
            (
                "discover",
                "catalog.discover",
                serde_json::json!({"market":"US","language":"en-US","limit":8,"cursor":null}),
            ),
            ("status", "auth.status", serde_json::json!({})),
        ] {
            let request = serde_json::json!({"kind":"request","protocol":{"major":1,"minor":0},
                    "requestID":id,"command":command,"params":params});
            client_write
                .write_all(format!("{request}\n").as_bytes())
                .await
                .unwrap();
        }
        for id in ["hello", "status"] {
            let mut line = String::new();
            tokio::time::timeout(Duration::from_secs(2), reader.read_line(&mut line))
                .await
                .unwrap()
                .unwrap();
            let result: serde_json::Value = serde_json::from_str(&line).unwrap();
            assert_eq!(result["requestID"], id);
            assert_eq!(result["ok"], true);
            if id == "status" {
                assert_eq!(result["data"]["state"], "signedOut");
            }
        }
        client_write.shutdown().await.unwrap();
        let mut line = String::new();
        reader.read_line(&mut line).await.unwrap();
        let result: serde_json::Value = serde_json::from_str(&line).unwrap();
        assert_eq!(result["requestID"], "discover");
        assert_eq!(result["error"]["code"], "CANCELLED");
        assert_eq!(task.await.unwrap().unwrap_err().code, ErrorCode::Cancelled);
    }

    #[tokio::test]
    async fn pending_parent_commit_does_not_block_public_work_or_acknowledge_false_cancellation() {
        use xodus::tokens::store::TokenBackend;
        struct WaitingWrite {
            memory: xodus::tokens::backend::MemoryBackend,
            release: std::sync::Mutex<std::sync::mpsc::Receiver<()>>,
        }
        impl TokenBackend for WaitingWrite {
            fn get(&self, key: &str) -> Result<Option<Vec<u8>>, TokenStoreError> {
                self.memory.get(key)
            }
            fn set(&self, key: &str, value: &[u8]) -> Result<(), TokenStoreError> {
                self.release
                    .lock()
                    .unwrap()
                    .recv_timeout(Duration::from_secs(5))
                    .unwrap();
                self.memory.set(key, value)
            }
            fn remove(&self, key: &str) -> Result<(), TokenStoreError> {
                self.memory.remove(key)
            }
        }
        let (_temporary, mut backend) = backend();
        backend.async_keychain_io = true;
        let (release, receiver) = std::sync::mpsc::channel();
        backend.tokens = Some(TokenManager::with_management_backend(Arc::new(
            WaitingWrite {
                memory: xodus::tokens::backend::MemoryBackend::default(),
                release: std::sync::Mutex::new(receiver),
            },
        )));
        let pid = own_worker(&mut backend);
        backend
            .complete_auth(
                "fixture-flow".to_owned(),
                Ok(ConsentHandoff::StoreCompleted {
                    session: Box::new(fixture_store_session()),
                }),
            )
            .await
            .unwrap();
        assert!(backend.account_mutation_pending);
        let request = Request {
            kind: RequestKind::Request,
            protocol: Protocol::default(),
            request_id: "fixture-public".to_owned(),
            operation: Operation::DiagnosticsExport(Empty {}),
        };
        assert!(matches!(
            backend.dispatch(&request).await.unwrap(),
            Some(Data::Diagnostics(_))
        ));
        let cancel = Request {
            request_id: "fixture-cancel".to_owned(),
            operation: Operation::AuthCancel(AuthCancelParams {
                flow_id: "fixture-flow".to_owned(),
            }),
            ..request
        };
        assert_eq!(
            backend.dispatch(&cancel).await.unwrap_err().code,
            ErrorCode::InvalidTransition
        );
        release.send(()).unwrap();
        let Completion::AccountCommit { result, .. } =
            backend.tasks.join_next().await.unwrap().unwrap()
        else {
            panic!("parent commit completion required");
        };
        result.unwrap();
        assert!(
            backend
                .tokens
                .as_ref()
                .unwrap()
                .get_management_store_session()
                .unwrap()
                .is_some()
        );
        no_owned_process(pid);
    }

    #[tokio::test]
    async fn slow_read_only_account_lookup_does_not_block_public_actor_and_has_a_deadline() {
        use tokio::io::AsyncBufReadExt;
        use xodus::tokens::store::TokenBackend;
        struct Slow {
            first: std::sync::atomic::AtomicBool,
        }
        impl TokenBackend for Slow {
            fn get(&self, _: &str) -> Result<Option<Vec<u8>>, TokenStoreError> {
                if !self.first.swap(true, std::sync::atomic::Ordering::SeqCst) {
                    std::thread::sleep(Duration::from_millis(2100));
                }
                Ok(None)
            }
            fn set(&self, _: &str, _: &[u8]) -> Result<(), TokenStoreError> {
                panic!("read-only status must not write")
            }
            fn remove(&self, _: &str) -> Result<(), TokenStoreError> {
                panic!("read-only status must not remove")
            }
        }
        let (_temporary, mut backend) = backend();
        backend.tokens = Some(TokenManager::with_management_backend(Arc::new(Slow {
            first: std::sync::atomic::AtomicBool::new(false),
        })));
        let (client, server) = tokio::io::duplex(8192);
        let (server_read, mut server_write) = tokio::io::split(server);
        let task = tokio::spawn(async move {
            serve(backend, BufReader::new(server_read), &mut server_write).await
        });
        let (client_read, mut client_write) = tokio::io::split(client);
        let mut reader = BufReader::new(client_read);
        let frames = [
            serde_json::json!({"kind":"request","protocol":{"major":1,"minor":0},"requestID":"hello",
                "command":"hello","params":{"client":"fixture","clientVersion":"1"}}),
            serde_json::json!({"kind":"request","protocol":{"major":1,"minor":0},"requestID":"status",
                "command":"auth.status","params":{}}),
            serde_json::json!({"kind":"request","protocol":{"major":1,"minor":0},"requestID":"public",
                "command":"diagnostics.export","params":{}}),
        ];
        for frame in frames {
            client_write
                .write_all(format!("{frame}\n").as_bytes())
                .await
                .unwrap();
        }
        for id in ["hello", "public"] {
            let mut line = String::new();
            tokio::time::timeout(Duration::from_millis(500), reader.read_line(&mut line))
                .await
                .unwrap()
                .unwrap();
            let result: serde_json::Value = serde_json::from_str(&line).unwrap();
            assert_eq!(result["requestID"], id);
            assert_eq!(result["ok"], true);
        }
        let mut line = String::new();
        tokio::time::timeout(Duration::from_secs(3), reader.read_line(&mut line))
            .await
            .unwrap()
            .unwrap();
        let result: serde_json::Value = serde_json::from_str(&line).unwrap();
        assert_eq!(result["requestID"], "status");
        assert_eq!(
            result["error"]["details"]["category"],
            "credentialStoreUnavailable"
        );
        client_write.shutdown().await.unwrap();
        task.await.unwrap().unwrap();
        tokio::time::sleep(Duration::from_millis(150)).await;
    }

    #[tokio::test]
    async fn cancel_rejects_foreign_flow_and_waits_for_owned_child() {
        let (_temporary, mut backend) = backend();
        let pid = own_worker(&mut backend);
        let mut request = Request {
            kind: RequestKind::Request,
            protocol: Protocol::default(),
            request_id: "fixture".to_owned(),
            operation: Operation::AuthCancel(AuthCancelParams {
                flow_id: "stale-flow".to_owned(),
            }),
        };
        assert_eq!(
            backend.dispatch(&request).await.unwrap_err().code,
            ErrorCode::NotFound
        );
        assert!(backend.auth_child.is_some());
        request.operation = Operation::AuthCancel(AuthCancelParams {
            flow_id: "fixture-flow".to_owned(),
        });
        let Some(Data::Auth(status)) = backend.dispatch(&request).await.unwrap() else {
            panic!("auth result required");
        };
        assert!(matches!(status.state, AuthState::SignedOut));
        assert!(matches!(
            status.flow.unwrap().state,
            AuthFlowState::Cancelled
        ));
        assert!(backend.auth_child.is_none());
        no_owned_process(pid);
    }

    #[tokio::test]
    async fn eof_kills_owned_consent_process_without_keychain_or_browser() {
        let (_temporary, mut backend) = backend();
        let pid = own_worker(&mut backend);
        serve(
            backend,
            BufReader::new(std::io::Cursor::new(Vec::<u8>::new())),
            tokio::io::sink(),
        )
        .await
        .unwrap();
        no_owned_process(pid);
    }

    #[tokio::test]
    async fn logout_stops_pending_flow_before_removing_user_credentials() {
        let (_temporary, mut backend) = backend();
        let pid = own_worker(&mut backend);
        let tokens = backend.tokens.as_ref().unwrap().clone();
        tokens
            .save_device_token(
                xodus::tokens::PASSPORT_STS.to_owned(),
                Token::Compact("fixture-device".to_owned()),
            )
            .unwrap();
        let request = Request {
            kind: RequestKind::Request,
            protocol: Protocol::default(),
            request_id: "fixture".to_owned(),
            operation: Operation::AuthLogout(Empty {}),
        };
        let Some(Data::Auth(status)) = backend.dispatch(&request).await.unwrap() else {
            panic!("auth result required");
        };
        assert!(matches!(status.state, AuthState::SignedOut));
        assert!(status.flow.is_none());
        assert!(backend.auth_flow.is_none());
        assert!(tokens.get_device_sts_token().is_ok());
        no_owned_process(pid);
    }
}
