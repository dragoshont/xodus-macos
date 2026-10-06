#![cfg(feature = "live")]

use std::future::Future;
use std::pin::Pin;
use std::sync::Arc;
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use xodus::models::displaycatalog::DisplayCatalogProductsResponse;
use xodus::models::secrets::{LegacyToken, Token};
use xodus::models::soap::Timestamp;
use xodus::tokens::{PASSPORT_STS, TokenManager};
use xodus_management::adapter::{
    Backend, CatalogProvider, auth_status, map_product, search, serve,
};
use xodus_management::state::Store;
use xodus_management::wire::*;

fn directory() -> (tempfile::TempDir, std::path::PathBuf) {
    let temporary = tempfile::tempdir().unwrap();
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        std::fs::set_permissions(temporary.path(), std::fs::Permissions::from_mode(0o700)).unwrap();
    }
    let path = temporary.path().canonicalize().unwrap();
    (temporary, path)
}

fn params() -> ProductParams {
    ProductParams {
        product_id: "FIXTURE00001".to_owned(),
        market: "US".to_owned(),
        language: "en-US".to_owned(),
        refresh: Refresh::Network,
    }
}

fn public_response() -> DisplayCatalogProductsResponse {
    serde_json::from_value(serde_json::json!({"Product":{
        "ProductId":"FIXTURE00001",
        "LocalizedProperties":[{"ProductTitle":"Fixture Harbor","Language":"en-US"}],
        "DisplaySkuAvailabilities":[{"Sku":{"SkuId":"fixture-standard","Properties":{
            "Packages":[{"ContentId":"fixture-content","PlatformDependencies":[{"PlatformName":"Windows.Desktop"}]}]}},
            "Availabilities":[]}]
    }})).unwrap()
}

#[test]
fn catalog_pc_visibility_never_becomes_entitlement_or_package_identity() {
    let product = map_product(&params(), public_response()).unwrap();
    assert!(product.pc_catalog_candidate);
    assert_eq!(product.editions[0].edition_id, "fixture-standard");
    assert!(matches!(
        product.editions[0].entitlement.kind,
        EntitlementKind::Unknown
    ));
    assert!(matches!(
        product.editions[0].installability.kind,
        InstallabilityKind::Unknown
    ));
    assert!(product.editions[0].installability.package_id.is_none());
    assert!(matches!(
        product.editions[0].compatibility.kind,
        CompatibilityKind::Unknown
    ));
}

#[test]
fn catalog_artwork_is_selected_language_actual_metadata_and_bad_optional_images_do_not_fail_product()
 {
    let mut response = public_response();
    response.product.localized_properties[0].images = serde_json::json!([
        {"ImagePurpose":"BoxArt","Uri":"//store-images.s-microsoft.com/image/apps.fixture","Width":1080,"Height":1080},
        {"ImagePurpose":"Poster","Uri":"https://private.invalid/PRIVATE_SENTINEL","Width":1440,"Height":2160},
        {"ImagePurpose":"FuturePurpose","Uri":"PRIVATE_SENTINEL"}
    ]);
    let mut other = response.product.localized_properties[0].clone();
    other.language = Some("fr-FR".to_owned());
    other.images = serde_json::json!([{"ImagePurpose":"SuperHeroArt","Uri":"//store-images.s-microsoft.com/image/wrong-locale"}]);
    response.product.localized_properties.insert(0, other);
    let product = map_product(&params(), response).unwrap();
    assert_eq!(product.artwork.len(), 1);
    assert_eq!(product.artwork[0].role, ArtworkRole::BoxArt);
    assert_eq!(product.artwork_status, ArtworkStatus::Available);
    let encoded = serde_json::to_string(&product).unwrap();
    assert!(!encoded.contains("PRIVATE_SENTINEL") && !encoded.contains("wrong-locale"));
    let mut malformed = public_response();
    malformed.product.localized_properties[0].images = serde_json::json!("PRIVATE_SENTINEL");
    assert_eq!(
        map_product(&params(), malformed).unwrap().artwork_status,
        ArtworkStatus::Rejected
    );
    assert_eq!(
        map_product(&params(), public_response())
            .unwrap()
            .artwork_status,
        ArtworkStatus::Absent
    );
}

#[test]
fn absent_or_mismatched_catalog_id_is_not_fabricated() {
    let mut response = public_response();
    response.product.product_id = None;
    assert_eq!(
        map_product(&params(), response).unwrap_err().code,
        ErrorCode::PackageUnavailable
    );
    let mut response = public_response();
    response.product.display_sku_availabilities[0].sku.sku_id = None;
    assert_eq!(
        map_product(&params(), response).unwrap_err().code,
        ErrorCode::PackageUnavailable
    );
}

#[test]
fn neutral_same_language_is_explicit_but_unrelated_locale_is_rejected() {
    let mut response = public_response();
    response.product.localized_properties[0].language = Some("en".to_owned());
    let product = map_product(&params(), response).unwrap();
    assert_eq!(product.language, "en-US");
    assert_eq!(product.resolved_language.as_deref(), Some("en"));
    let mut response = public_response();
    response.product.localized_properties[0].language = Some("fr".to_owned());
    assert_eq!(
        map_product(&params(), response).unwrap_err().code,
        ErrorCode::PackageUnavailable
    );
}

#[test]
fn bcp47_casing_and_public_package_id_do_not_create_download_authorization() {
    let mut response = public_response();
    response.product.localized_properties[0].language = Some("en-us".to_owned());
    response.product.display_sku_availabilities[0]
        .sku
        .properties
        .packages[0]
        .package_id = Some("fixture-package-id".to_owned());
    response.product.display_sku_availabilities[0]
        .sku
        .properties
        .packages[0]
        .version = Some("0".to_owned());
    let product = map_product(&params(), response).unwrap();
    assert_eq!(
        product.editions[0].installability.package_id.as_deref(),
        Some("fixture-package-id")
    );
    assert!(product.editions[0].installability.package_version.is_none());
    assert!(matches!(
        product.editions[0].installability.kind,
        InstallabilityKind::Unknown
    ));
}

#[test]
fn cached_search_is_scoped_and_revision_pinned() {
    let (_temporary, path) = directory();
    let mut store = Store::open(&path).unwrap();
    store
        .cache_product(map_product(&params(), public_response()).unwrap())
        .unwrap();
    let search_params = SearchParams {
        query: "harbor".to_owned(),
        market: "US".to_owned(),
        language: "en-US".to_owned(),
        platform: Platform::Pc,
        limit: 1,
        cursor: None,
    };
    let results = search(&store, &search_params).unwrap();
    assert_eq!(results.products.len(), 1);
    assert_eq!(results.corpus, "observedPublicProducts");
    assert_eq!(results.completeness, "partial");
    let wrong_market = SearchParams {
        market: "GB".to_owned(),
        ..search_params.clone()
    };
    assert!(search(&store, &wrong_market).unwrap().products.is_empty());
    let mut another = map_product(&params(), public_response()).unwrap();
    another.product_id = "FIXTURE00002".to_owned();
    store.cache_product(another).unwrap();
    let mut paged = search_params;
    paged.cursor = search(&store, &paged).unwrap().next_cursor;
    assert!(paged.cursor.is_some());
    store
        .cache_product(map_product(&params(), public_response()).unwrap())
        .unwrap();
    assert_eq!(
        search(&store, &paged).unwrap_err().code,
        ErrorCode::RevisionConflict
    );
}

#[test]
fn credential_presence_expiry_and_invalid_are_separate_from_session_validity() {
    let tokens = TokenManager::with_memory();
    assert!(matches!(
        auth_status(&tokens).unwrap().state,
        AuthState::SignedOut
    ));
    for (proof, expiration, expected) in [
        (
            "fixture-not-a-real-token",
            "2099-01-01T00:00:00Z",
            AuthState::CredentialPresent,
        ),
        (
            "fixture-not-a-real-token",
            "2000-01-01T00:00:00Z",
            AuthState::Expired,
        ),
        ("", "2099-01-01T00:00:00Z", AuthState::Invalid),
        ("fixture", "invalid", AuthState::Invalid),
    ] {
        tokens
            .save_user_token(
                PASSPORT_STS.to_owned(),
                Token::Legacy(LegacyToken {
                    key_name: Some(PASSPORT_STS.to_owned()),
                    token: proof.to_owned(),
                    binary_secret: None,
                    tpm_key: None,
                    lifetime: Timestamp {
                        id: None,
                        created: "2000-01-01T00:00:00Z".to_owned(),
                        expires: expiration.to_owned(),
                    },
                }),
            )
            .unwrap();
        let status = auth_status(&tokens).unwrap();
        assert_eq!(
            std::mem::discriminant(&status.state),
            std::mem::discriminant(&expected)
        );
        assert!(!status.entitlement_authorized);
        let output = serde_json::to_string(&status).unwrap();
        assert!(!output.contains("fixture-not-a-real-token"));
    }
    tokens.remove_user_credentials().unwrap();
    assert!(matches!(
        auth_status(&tokens).unwrap().state,
        AuthState::SignedOut
    ));
}

struct SlowPublicFixture;
impl CatalogProvider for SlowPublicFixture {
    fn fetch(
        &self,
        product: ProductParams,
    ) -> Pin<Box<dyn Future<Output = Result<ProductRecord, WireError>> + Send + '_>> {
        Box::pin(async move {
            tokio::time::sleep(std::time::Duration::from_secs(60)).await;
            map_product(&product, public_response())
        })
    }
}

async fn send<W: tokio::io::AsyncWrite + Unpin>(
    writer: &mut W,
    id: &str,
    command: &str,
    params: serde_json::Value,
) {
    let frame = serde_json::json!({"kind":"request","protocol":{"major":1,"minor":0},
        "requestID":id,"command":command,"params":params});
    writer
        .write_all(format!("{frame}\n").as_bytes())
        .await
        .unwrap();
}

async fn result<R: tokio::io::AsyncBufRead + Unpin>(reader: &mut R, id: &str) -> serde_json::Value {
    tokio::time::timeout(std::time::Duration::from_secs(5), async {
        loop {
            let mut line = String::new();
            assert_ne!(reader.read_line(&mut line).await.unwrap(), 0);
            let frame: serde_json::Value = serde_json::from_str(&line).unwrap();
            if frame["kind"] == "result" && frame["requestID"] == id {
                break frame;
            }
            assert_eq!(frame["kind"], "event");
        }
    })
    .await
    .unwrap()
}

#[cfg(target_os = "macos")]
#[tokio::test]
async fn installed_snapshot_reads_real_registry_without_provider_or_account_calls_and_recovers_transport_errors()
 {
    use sha2::{Digest, Sha256};
    use xodus_management::staging::{Manifest, ManifestFile, StagingStore};
    let (_temporary, path) = directory();
    let bytes = b"local transaction fixture";
    let mut staging = StagingStore::open(&path).unwrap();
    let transaction = staging
        .prepare(
            Manifest {
                product_id: "fixture-product".to_owned(),
                edition_id: "fixture-edition".to_owned(),
                package_id: "fixture-package".to_owned(),
                package_version: "fixture-version".to_owned(),
                files: vec![ManifestFile {
                    path: "game.bin".to_owned(),
                    bytes: bytes.len() as u64,
                    sha256: Sha256::digest(bytes)
                        .iter()
                        .map(|byte| format!("{byte:02x}"))
                        .collect(),
                }],
            },
            None,
            0,
            0,
        )
        .unwrap();
    staging
        .write_file(
            &transaction.transaction_id,
            "game.bin",
            bytes.as_slice(),
            || false,
        )
        .unwrap();
    staging.verify(&transaction.transaction_id).unwrap();
    let installed = staging.commit(&transaction.transaction_id).unwrap();
    drop(staging);
    let registry = path.join("registry.json");
    let before = std::fs::read(&registry).unwrap();
    let backend = Backend::new(Store::open(&path).unwrap(), Arc::new(SlowPublicFixture));
    let (client, server) = tokio::io::duplex(65536);
    let (server_read, server_write) = tokio::io::split(server);
    let task = tokio::spawn(serve(backend, BufReader::new(server_read), server_write));
    let (read, mut write) = tokio::io::split(client);
    let mut read = BufReader::new(read);
    send(
        &mut write,
        "hello",
        "hello",
        serde_json::json!({"client":"fixture","clientVersion":"1"}),
    )
    .await;
    assert_eq!(result(&mut read, "hello").await["ok"], true);
    for id in ["first", "second"] {
        send(&mut write, id, "installed.snapshot", serde_json::json!({})).await;
        let frame = result(&mut read, id).await;
        assert_eq!(frame["ok"], true);
        assert_eq!(frame["data"]["scope"], "managementRegistryOnly");
        assert_eq!(frame["data"]["completeness"], "complete");
        assert_eq!(frame["data"]["watermark"], 0);
        let records = frame["data"]["installations"].as_array().unwrap();
        assert_eq!(records.len(), 1);
        assert_eq!(records[0]["installationID"], installed.installation_id);
        assert_eq!(records[0]["revision"], 1);
        assert_eq!(records[0]["health"], "notVerified");
        assert!(
            records[0]
                .as_object()
                .unwrap()
                .contains_key("runtimeFingerprint")
        );
        assert!(records[0]["runtimeFingerprint"].is_null());
        assert_eq!(std::fs::read(&registry).unwrap(), before);
    }
    std::fs::write(&registry, b"PRIVATE_SENTINEL corrupt local registry").unwrap();
    send(
        &mut write,
        "corrupt",
        "installed.snapshot",
        serde_json::json!({}),
    )
    .await;
    let failed = result(&mut read, "corrupt").await;
    assert_eq!(failed["ok"], false);
    assert_eq!(failed["error"]["code"], "REGISTRY_RECOVERY_REQUIRED");
    assert!(!failed.to_string().contains("PRIVATE_SENTINEL"));
    assert!(failed.get("data").is_none());
    send(
        &mut write,
        "responsive",
        "jobs.snapshot",
        serde_json::json!({}),
    )
    .await;
    assert_eq!(
        result(&mut read, "responsive").await["data"]["watermark"],
        0
    );
    std::fs::write(&registry, &before).unwrap();
    let writer = StagingStore::open(&path).unwrap();
    send(
        &mut write,
        "busy",
        "installed.snapshot",
        serde_json::json!({}),
    )
    .await;
    let busy = result(&mut read, "busy").await;
    assert_eq!(busy["error"]["code"], "STATE_LOCKED");
    assert_eq!(busy["error"]["retryable"], true);
    drop(writer);
    send(
        &mut write,
        "after-busy",
        "installed.snapshot",
        serde_json::json!({}),
    )
    .await;
    assert_eq!(
        result(&mut read, "after-busy").await["data"]["installations"][0]["revision"],
        1
    );
    drop(write);
    drop(read);
    task.await.unwrap().unwrap();
    assert_eq!(std::fs::read(&registry).unwrap(), before);
}

#[tokio::test]
async fn live_transport_cancel_snapshot_and_replay_without_network_or_credentials() {
    let (_temporary, path) = directory();
    let backend = Backend::new(Store::open(&path).unwrap(), Arc::new(SlowPublicFixture));
    let (client, server) = tokio::io::duplex(65536);
    let (server_read, server_write) = tokio::io::split(server);
    let task = tokio::spawn(serve(backend, BufReader::new(server_read), server_write));
    let (read, mut write) = tokio::io::split(client);
    let mut read = BufReader::new(read);
    send(&mut write, "before", "jobs.snapshot", serde_json::json!({})).await;
    assert_eq!(
        result(&mut read, "before").await["error"]["code"],
        "HELLO_REQUIRED"
    );
    send(
        &mut write,
        "hello",
        "hello",
        serde_json::json!({"client":"fixture","clientVersion":"1"}),
    )
    .await;
    let hello = result(&mut read, "hello").await;
    assert_eq!(hello["ok"], true);
    assert!(hello["data"]["runtimeFingerprint"].is_null());
    let session = hello["data"]["sessionID"].as_str().unwrap().to_owned();
    send(
        &mut write,
        "enqueue",
        "jobs.enqueue",
        serde_json::json!({
        "kind":"catalogRefresh","idempotencyKey":"fixture-key","product":params()}),
    )
    .await;
    let enqueued = result(&mut read, "enqueue").await;
    let id = enqueued["data"]["job"]["jobID"]
        .as_str()
        .unwrap()
        .to_owned();
    let revision = enqueued["data"]["job"]["revision"].as_u64().unwrap();
    send(
        &mut write,
        "cancel",
        "jobs.cancel",
        serde_json::json!({"jobID":id,"expectedRevision":revision}),
    )
    .await;
    assert_eq!(
        result(&mut read, "cancel").await["data"]["job"]["state"],
        "cancelled"
    );
    send(
        &mut write,
        "repeat-cancel",
        "jobs.cancel",
        serde_json::json!({"jobID":id,"expectedRevision":revision}),
    )
    .await;
    assert_eq!(
        result(&mut read, "repeat-cancel").await["data"]["job"]["state"],
        "cancelled"
    );
    send(
        &mut write,
        "snapshot",
        "jobs.snapshot",
        serde_json::json!({}),
    )
    .await;
    let snapshot = result(&mut read, "snapshot").await;
    assert_eq!(snapshot["data"]["jobs"][0]["state"], "cancelled");
    send(
        &mut write,
        "replay",
        "events.replay",
        serde_json::json!({
        "sessionID":session,"afterSequence":0,"limit":1000}),
    )
    .await;
    let replay = result(&mut read, "replay").await;
    let sequences: Vec<_> = replay["data"]["events"]
        .as_array()
        .unwrap()
        .iter()
        .map(|event| event["sequence"].as_u64().unwrap())
        .collect();
    assert_eq!(sequences, vec![1, 2, 3]);
    send(
        &mut write,
        "launch",
        "game.launch",
        serde_json::json!({
        "installationID":"fixture","expectedRevision":1}),
    )
    .await;
    assert_eq!(
        result(&mut read, "launch").await["error"]["code"],
        "RUNTIME_MISMATCH"
    );
    write.shutdown().await.unwrap();
    assert!(task.await.unwrap().is_ok());
    let reopened = Store::open(&path).unwrap();
    assert_eq!(reopened.state.jobs[&id].state, JobState::Cancelled);
}

#[tokio::test]
async fn half_closed_input_cancels_correlated_pending_request_and_returns_failure() {
    let (_temporary, path) = directory();
    let backend = Backend::new(Store::open(&path).unwrap(), Arc::new(SlowPublicFixture));
    let (client, server) = tokio::io::duplex(65536);
    let (server_read, server_write) = tokio::io::split(server);
    let task = tokio::spawn(serve(backend, BufReader::new(server_read), server_write));
    let (read, mut write) = tokio::io::split(client);
    let mut read = BufReader::new(read);
    send(
        &mut write,
        "hello",
        "hello",
        serde_json::json!({"client":"fixture","clientVersion":"1"}),
    )
    .await;
    assert!(result(&mut read, "hello").await["ok"].as_bool().unwrap());
    send(
        &mut write,
        "pending",
        "product.detail",
        serde_json::to_value(params()).unwrap(),
    )
    .await;
    write.shutdown().await.unwrap();
    assert_eq!(
        result(&mut read, "pending").await["error"]["code"],
        "CANCELLED"
    );
    assert_eq!(task.await.unwrap().unwrap_err().code, ErrorCode::Cancelled);
}
