use std::future::Future;
use std::process::ExitCode;
use std::time::Duration;

use serde_json::{Value, json};
use tokio::io::{AsyncWrite, AsyncWriteExt};
use xodus::api::response::{PACKAGE_RESPONSE_LIMIT, ProviderResponseError, request_json};
use xodus::api::xbox::{XboxAuthError, get_xsts_auth_header};
use xodus::models::xbox::XstsResponse;
use xodus::tokens::TokenManager;

use crate::provider_credentials::{CredentialError, ProviderCredentials};

const ENDPOINT: &str = "https://collections.mp.microsoft.com/v7.0/collections/query";
const RELYING_PARTY: &str = "http://mp.microsoft.com/";
const LIVE_RELYING_PARTY: &str = "http://xboxlive.com";
const DEADLINE: Duration = Duration::from_secs(30);
const PAGE_SIZE: usize = 100;

#[derive(Debug)]
enum Failure {
    Market,
    NativeKeychain,
    Credentials(CredentialError),
    Authentication(XboxAuthError),
    BeneficiaryUnavailable,
    IdentityMismatch,
    Provider(ProviderResponseError),
    Response(&'static str),
    ProfileChanged,
    Deadline,
    Output,
}

impl std::fmt::Display for Failure {
    fn fmt(&self, output: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Market => output.write_str("The diagnostic requires explicit test market US"),
            Self::NativeKeychain => {
                output.write_str("Noninteractive native management Keychain is required")
            }
            Self::Credentials(error) => write!(output, "{error}"),
            Self::Authentication(error) => write!(output, "{error}"),
            Self::BeneficiaryUnavailable => output.write_str(
                "The same-attempt Xbox Live XSTS response has no usable beneficiary identity",
            ),
            Self::IdentityMismatch => output
                .write_str("The same-attempt Xbox Live and Store XSTS identities do not match"),
            Self::Provider(error) => write!(output, "{error}"),
            Self::Response(field) => write!(output, "Collections response has malformed {field}"),
            Self::ProfileChanged => output
                .write_str("The saved management profile changed before diagnostic publication"),
            Self::Deadline => output
                .write_str("The complete collections diagnostic exceeded its 30-second deadline"),
            Self::Output => output.write_str("Collections diagnostic output is unavailable"),
        }
    }
}

fn readonly(tokens: &TokenManager) -> Result<TokenManager, Failure> {
    tokens
        .with_noninteractive_management_keychain()
        .and_then(|tokens| tokens.readonly_management_profile())
        .map_err(|_| Failure::NativeKeychain)
}

fn client() -> Result<reqwest::Client, Failure> {
    reqwest::Client::builder()
        .timeout(DEADLINE)
        .connect_timeout(Duration::from_secs(10))
        .redirect(reqwest::redirect::Policy::none())
        .retry(reqwest::retry::never())
        .build()
        .map_err(|_| Failure::Provider(ProviderResponseError::RequestFailed))
}

struct BoundToken {
    store: XstsResponse,
    beneficiary: String,
}

fn bind(live: XstsResponse, store: XstsResponse) -> Result<BoundToken, Failure> {
    let beneficiary = live
        .user_id()
        .ok_or(Failure::BeneficiaryUnavailable)?
        .to_owned();
    get_xsts_auth_header(live.clone()).map_err(Failure::Authentication)?;
    get_xsts_auth_header(store.clone()).map_err(Failure::Authentication)?;
    if live.user_hash() != store.user_hash()
        || (store.has_user_id_claim() && store.user_id() != Some(beneficiary.as_str()))
    {
        return Err(Failure::IdentityMismatch);
    }
    Ok(BoundToken { store, beneficiary })
}

fn request(
    client: &reqwest::Client,
    token: BoundToken,
    market: &str,
) -> Result<reqwest::RequestBuilder, Failure> {
    if market != "US" {
        return Err(Failure::Market);
    }
    let authorization = get_xsts_auth_header(token.store).map_err(Failure::Authentication)?;
    Ok(client
        .post(ENDPOINT)
        .header("Authorization", authorization)
        .header("x-xbl-contract-version", "2")
        .header("MS-CV", format!("{}.0", uuid::Uuid::new_v4().simple()))
        .header("Accept", "application/json")
        .header("Accept-Language", "en-US")
        .header("Content-Type", "application/json")
        .json(&json!({
            "maxPageSize": PAGE_SIZE,
            "excludeDuplicates": true,
            "market": market,
            "validityType": "All",
            "beneficiaries": [{
                "identityType": "xuid",
                "identityValue": token.beneficiary,
                "localTicketReference": "xlib"
            }]
        })))
}

fn text<'a>(value: &'a Value, field: &'static str, maximum: usize) -> Result<&'a str, Failure> {
    value
        .as_str()
        .filter(|text| {
            !text.is_empty()
                && text.len() <= maximum
                && text.trim() == *text
                && !text.chars().any(char::is_control)
        })
        .ok_or(Failure::Response(field))
}

fn optional_text<'a>(
    item: &'a serde_json::Map<String, Value>,
    field: &'static str,
    maximum: usize,
) -> Result<Option<&'a str>, Failure> {
    match item.get(field) {
        None | Some(Value::Null) => Ok(None),
        Some(value) => text(value, field, maximum).map(Some),
    }
}

fn parse(response: Value) -> Result<Value, Failure> {
    let object = response.as_object().ok_or(Failure::Response("root"))?;
    let items = object
        .get("items")
        .and_then(Value::as_array)
        .filter(|items| items.len() <= PAGE_SIZE)
        .ok_or(Failure::Response("items"))?;
    let continuation = match object.get("continuationToken") {
        None => "absent",
        Some(Value::Null) => "null",
        Some(Value::String(value)) if value.is_empty() => "empty",
        Some(value) => {
            text(value, "continuationToken", 4096)?;
            "present"
        }
    };
    let mut identity_unresolved = 0;
    let mut status_missing = 0;
    let mut status_null = 0;
    let mut status_value = 0;
    let mut active = 0;
    let mut expired = 0;
    let mut revoked = 0;
    let mut status_other = 0;
    let mut trial_true = 0;
    let mut trial_false = 0;
    let mut trial_unresolved = 0;
    let mut dates_present = 0;
    let mut dates_unresolved = 0;
    for item in items {
        let item = item.as_object().ok_or(Failure::Response("item"))?;
        let lower = optional_text(item, "productId", 128)?;
        let upper = optional_text(item, "ProductId", 128)?;
        if lower
            .zip(upper)
            .is_some_and(|(lower, upper)| lower != upper)
        {
            return Err(Failure::Response("productId"));
        }
        let product = lower.or(upper);
        let sku = optional_text(item, "skuId", 128)?;
        for identity in [product, sku].into_iter().flatten() {
            if !identity
                .bytes()
                .all(|byte| byte.is_ascii_alphanumeric() || byte == b'-')
            {
                return Err(Failure::Response("product/SKU identity"));
            }
        }
        if product.is_none() || sku.is_none() {
            identity_unresolved += 1;
        }
        match item.get("status") {
            None => status_missing += 1,
            Some(Value::Null) => status_null += 1,
            Some(value) => {
                status_value += 1;
                match text(value, "status", 64)? {
                    "Active" => active += 1,
                    "Expired" => expired += 1,
                    "Revoked" => revoked += 1,
                    _ => status_other += 1,
                }
            }
        }
        let trial = match item.get("isTrial") {
            None | Some(Value::Null) => None,
            Some(value) => Some(value.as_bool().ok_or(Failure::Response("isTrial"))?),
        };
        let sku_type = optional_text(item, "skuType", 64)?;
        if trial == Some(false) && sku_type == Some("Trial") {
            return Err(Failure::Response("trial declarations"));
        }
        match trial.or((sku_type == Some("Trial")).then_some(true)) {
            Some(true) => trial_true += 1,
            Some(false) => trial_false += 1,
            None => trial_unresolved += 1,
        }
        for field in ["acquiredDate", "startDate", "endDate"] {
            match optional_text(item, field, 64)? {
                Some(date) => {
                    chrono::DateTime::parse_from_rfc3339(date)
                        .map_err(|_| Failure::Response(field))?;
                    dates_present += 1;
                }
                None => dates_unresolved += 1,
            }
        }
        for field in ["productKind", "purchasedCountry"] {
            optional_text(item, field, 64)?;
        }
    }
    Ok(json!({
        "category": "collectionsDiagnostic",
        "market": "US",
        "coverage": "partial",
        "pages": 1,
        "itemCount": items.len(),
        "unresolvedIdentityCount": identity_unresolved,
        "statusField": {"missing": status_missing, "null": status_null, "value": status_value},
        "statusValues": {"active": active, "expired": expired, "revoked": revoked, "other": status_other},
        "trial": {"true": trial_true, "false": trial_false, "unresolved": trial_unresolved},
        "dateFields": {"present": dates_present, "unresolved": dates_unresolved},
        "continuation": continuation
    }))
}

async fn read_using<A, AF, Q, QF>(
    tokens: &TokenManager,
    credentials: &ProviderCredentials,
    authenticate: A,
    query: Q,
) -> Result<Value, Failure>
where
    A: FnOnce() -> AF,
    AF: Future<Output = Result<BoundToken, Failure>>,
    Q: FnOnce(BoundToken) -> QF,
    QF: Future<Output = Result<Value, Failure>>,
{
    credentials
        .verify_current(tokens)
        .await
        .map_err(Failure::Credentials)?;
    let token = authenticate().await?;
    credentials
        .verify_current(tokens)
        .await
        .map_err(Failure::Credentials)?;
    let response = query(token).await?;
    credentials
        .verify_current(tokens)
        .await
        .map_err(Failure::Credentials)?;
    parse(response)
}

async fn publish<W: AsyncWrite + Unpin>(
    tokens: &TokenManager,
    credentials: &ProviderCredentials,
    summary: Value,
    writer: &mut W,
) -> Result<(), Failure> {
    let mut bytes = serde_json::to_vec(&summary).map_err(|_| Failure::Output)?;
    bytes.push(b'\n');
    credentials
        .verify_current(tokens)
        .await
        .map_err(Failure::Credentials)?;
    let witness = credentials
        .publication_witness()
        .ok_or(Failure::ProfileChanged)?;
    if !tokens.management_publication_current(&witness) {
        return Err(Failure::ProfileChanged);
    }
    writer
        .write_all(&bytes)
        .await
        .map_err(|_| Failure::Output)?;
    writer.flush().await.map_err(|_| Failure::Output)
}

async fn diagnostic(market: &str) -> Result<(), Failure> {
    if market != "US" {
        return Err(Failure::Market);
    }
    if !xodus::secrets::management_native_keychain_enabled() {
        return Err(Failure::NativeKeychain);
    }
    xodus::secrets::init_secrets().map_err(|_| Failure::NativeKeychain)?;
    let tokens = readonly(&TokenManager::with_management_keychain_and_memory())?;
    let credentials = ProviderCredentials::read(&tokens)
        .await
        .map_err(Failure::Credentials)?;
    let client = client()?;
    let summary = read_using(
        &tokens,
        &credentials,
        || async {
            let user = xodus::api::xbox::authenticate_saved_user(
                &client,
                credentials.device.clone(),
                credentials.user.clone(),
                credentials.account.username.clone(),
            )
            .await
            .map_err(Failure::Authentication)?;
            let live = xodus::api::xbox::request_xsts_token(
                &client,
                user.token.clone(),
                LIVE_RELYING_PARTY,
            )
            .await
            .map_err(|error| Failure::Authentication(error.into()))?;
            live.user_id().ok_or(Failure::BeneficiaryUnavailable)?;
            get_xsts_auth_header(live.clone()).map_err(Failure::Authentication)?;
            let store = xodus::api::xbox::request_xsts_token(&client, user.token, RELYING_PARTY)
                .await
                .map_err(|error| Failure::Authentication(error.into()))?;
            bind(live, store)
        },
        |token| async {
            request_json::<Value>(request(&client, token, market)?, PACKAGE_RESPONSE_LIMIT)
                .await
                .map_err(Failure::Provider)?
                .require_success()
                .map_err(Failure::Provider)
        },
    )
    .await?;
    publish(&tokens, &credentials, summary, &mut tokio::io::stdout()).await
}

pub async fn run(market: &str) -> ExitCode {
    let result = within_deadline(diagnostic(market), DEADLINE).await;
    xodus::secrets::destroy_secrets();
    match result {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("Collections diagnostic failed: {error}.");
            ExitCode::FAILURE
        }
    }
}

async fn within_deadline(
    work: impl Future<Output = Result<(), Failure>>,
    deadline: Duration,
) -> Result<(), Failure> {
    tokio::time::timeout(deadline, work)
        .await
        .unwrap_or(Err(Failure::Deadline))
}

#[cfg(test)]
mod tests {
    use super::*;
    use tokio::io::{AsyncReadExt, AsyncWriteExt};
    use tokio::net::TcpListener;
    use xodus::tokens::store::TokenBackend;

    fn auth(xid: Option<&str>) -> XstsResponse {
        let mut claim = json!({"uhs": "12345"});
        if let Some(xid) = xid {
            claim["xid"] = json!(xid);
        }
        serde_json::from_value(json!({
            "NotAfter": "2099-01-01T00:00:00Z",
            "Token": "PRIVATE_SENTINEL",
            "DisplayClaims": {"xui": [claim]}
        }))
        .unwrap()
    }

    fn page() -> Value {
        json!({
            "items": [{
                "productId": "NEUTRALPRODUCT",
                "skuId": "0001",
                "status": "Active",
                "isTrial": false,
                "acquiredDate": "2026-01-01T00:00:00Z",
                "unknownExtension": "PRIVATE_SENTINEL"
            }],
            "continuationToken": "PRIVATE_SENTINEL"
        })
    }

    fn bound() -> Result<BoundToken, Failure> {
        bind(auth(Some("123")), auth(None))
    }

    #[test]
    fn exact_consumer_request_has_same_context_beneficiary_and_no_filters_or_paging() {
        assert_eq!(RELYING_PARTY, "http://mp.microsoft.com/");
        assert_eq!(LIVE_RELYING_PARTY, "http://xboxlive.com");
        let request = request(&client().unwrap(), bound().unwrap(), "US")
            .unwrap()
            .build()
            .unwrap();
        assert_eq!(request.method(), reqwest::Method::POST);
        assert_eq!(request.url().as_str(), ENDPOINT);
        assert!(request.url().query().is_none());
        assert_eq!(request.headers()["x-xbl-contract-version"], "2");
        assert_eq!(
            request.headers()["Authorization"],
            "XBL3.0 x=12345;PRIVATE_SENTINEL"
        );
        assert_eq!(request.headers()["Accept"], "application/json");
        assert_eq!(request.headers()["Accept-Language"], "en-US");
        assert_eq!(request.headers()["Content-Type"], "application/json");
        let body: Value =
            serde_json::from_slice(request.body().unwrap().as_bytes().unwrap()).unwrap();
        assert_eq!(
            body,
            json!({
                "maxPageSize": 100, "excludeDuplicates": true, "market": "US", "validityType": "All",
                "beneficiaries": [{"identityType": "xuid", "identityValue": "123", "localTicketReference": "xlib"}]
            })
        );
        assert_eq!(PACKAGE_RESPONSE_LIMIT, 4 * 1024 * 1024);
        assert_eq!(DEADLINE, Duration::from_secs(30));
    }

    #[test]
    fn missing_noncanonical_or_ambiguous_beneficiary_never_builds_request() {
        for xid in [
            None,
            Some(""),
            Some("0"),
            Some("0123"),
            Some("-1"),
            Some("PRIVATE_SENTINEL"),
        ] {
            assert!(matches!(
                bind(auth(xid), auth(None)),
                Err(Failure::BeneficiaryUnavailable)
            ));
        }
        let mut token = serde_json::to_value(auth(Some("123"))).unwrap();
        let claim = token["DisplayClaims"]["xui"][0].clone();
        token["DisplayClaims"]["xui"]
            .as_array_mut()
            .unwrap()
            .push(claim);
        assert!(matches!(
            bind(serde_json::from_value(token).unwrap(), auth(None)),
            Err(Failure::BeneficiaryUnavailable)
        ));
        assert!(matches!(
            request(&client().unwrap(), bound().unwrap(), "TW"),
            Err(Failure::Market)
        ));
    }

    #[test]
    fn dual_rp_binding_rejects_conflicting_or_malformed_store_identity_and_hash() {
        for xid in [
            Some("0"),
            Some("0123"),
            Some("456"),
            Some("PRIVATE_SENTINEL"),
        ] {
            assert!(matches!(
                bind(auth(Some("123")), auth(xid)),
                Err(Failure::IdentityMismatch)
            ));
        }
        for xid in [None, Some("123")] {
            assert_eq!(
                bind(auth(Some("123")), auth(xid)).unwrap().beneficiary,
                "123"
            );
        }
        let mut token = serde_json::to_value(auth(None)).unwrap();
        token["DisplayClaims"]["xui"][0]["uhs"] = json!("67890");
        assert!(matches!(
            bind(auth(Some("123")), serde_json::from_value(token).unwrap()),
            Err(Failure::IdentityMismatch)
        ));
        for field in ["Token", "NotAfter"] {
            let mut token = serde_json::to_value(auth(None)).unwrap();
            token[field] = if field == "Token" {
                json!("")
            } else {
                json!("2000-01-01T00:00:00Z")
            };
            assert!(matches!(
                bind(auth(Some("123")), serde_json::from_value(token).unwrap()),
                Err(Failure::Authentication(_))
            ));
        }
    }

    #[test]
    fn unknown_extensions_and_statuses_never_escape_or_become_ownership() {
        let mut response = page();
        response["items"][0]["status"] = json!("PRIVATE_SENTINEL");
        let output = parse(response).unwrap();
        assert_eq!(output["itemCount"], 1);
        assert_eq!(output["statusValues"]["other"], 1);
        assert_eq!(output["statusValues"]["active"], 0);
        assert_eq!(output["coverage"], "partial");
        assert_eq!(output["continuation"], "present");
        let output = output.to_string();
        for forbidden in [
            "PRIVATE_SENTINEL",
            "NEUTRALPRODUCT",
            "skuId",
            "owned",
            "subscription",
            "account",
        ] {
            assert!(!output.contains(forbidden));
        }
    }

    #[test]
    fn absent_null_fields_remain_unresolved_without_active_or_trial_defaults() {
        let output =
            parse(json!({"items": [{}, {"status": null, "isTrial": null, "skuId": null}]}))
                .unwrap();
        assert_eq!(output["itemCount"], 2);
        assert_eq!(output["unresolvedIdentityCount"], 2);
        assert_eq!(
            output["statusField"],
            json!({"missing": 1, "null": 1, "value": 0})
        );
        assert_eq!(output["statusValues"]["active"], 0);
        assert_eq!(
            output["trial"],
            json!({"true": 0, "false": 0, "unresolved": 2})
        );
        assert_eq!(output["dateFields"], json!({"present": 0, "unresolved": 6}));
    }

    #[test]
    fn continuation_shape_never_claims_completion_even_on_empty_page() {
        for (value, shape) in [
            (Value::Null, "null"),
            (json!(""), "empty"),
            (json!("PRIVATE_SENTINEL"), "present"),
        ] {
            let output = parse(json!({"items": [], "continuationToken": value})).unwrap();
            assert_eq!(output["continuation"], shape);
            assert_eq!(output["coverage"], "partial");
            assert_eq!(output["itemCount"], 0);
        }
        assert_eq!(
            parse(json!({"items": []})).unwrap()["continuation"],
            "absent"
        );
        for value in [
            json!(12),
            json!({}),
            json!(["fixture"]),
            json!("\n"),
            json!("x".repeat(4097)),
        ] {
            assert!(matches!(
                parse(json!({"items": [], "continuationToken": value})),
                Err(Failure::Response("continuationToken"))
            ));
        }
    }

    #[test]
    fn malformed_items_or_selected_fields_fail_instead_of_dropping_rows() {
        for response in [
            json!(null),
            json!([]),
            json!({}),
            json!({"items": null}),
            json!({"items": {}}),
            json!({"items": [null]}),
            json!({"items": vec![json!({}); 101]}),
        ] {
            assert!(matches!(parse(response), Err(Failure::Response(_))));
        }
        for (field, bad) in [
            ("productId", json!(12)),
            ("productId", json!("")),
            ("productId", json!("a/b")),
            ("skuId", json!(false)),
            ("status", json!({})),
            ("status", json!("")),
            ("isTrial", json!("false")),
            ("skuType", json!([])),
            ("acquiredDate", json!("bad")),
            ("startDate", json!(true)),
            ("endDate", json!("x".repeat(65))),
            ("productKind", json!(12)),
            ("purchasedCountry", json!({})),
        ] {
            let mut response = page();
            response["items"][0][field] = bad;
            let error = parse(response).unwrap_err();
            assert!(matches!(error, Failure::Response(_)));
            assert!(!error.to_string().contains("PRIVATE_SENTINEL"));
        }
        let mut response = page();
        response["items"][0]["ProductId"] = json!("DIFFERENT");
        assert!(parse(response).is_err());
        let mut response = page();
        response["items"][0]["skuType"] = json!("Trial");
        assert!(parse(response).is_err());
        assert_eq!(
            parse(json!({"items": vec![json!({}); 100]})).unwrap()["itemCount"],
            100
        );
    }

    #[tokio::test]
    async fn missing_identity_stops_before_collection_query() {
        let (manager, _) = crate::package::tests::management_profile();
        let tokens = manager.readonly_management_profile().unwrap();
        let credentials = ProviderCredentials::read_neutral(&tokens).unwrap();
        let result = read_using(
            &tokens,
            &credentials,
            || async { bind(auth(None), auth(None)) },
            |_| async { panic!("Missing beneficiary must not issue a query") },
        )
        .await;
        assert!(matches!(result, Err(Failure::BeneficiaryUnavailable)));
    }

    #[tokio::test]
    async fn profile_fences_cover_auth_query_and_publication_without_persistence() {
        for boundary in ["before", "auth", "query", "publication", "unchanged"] {
            let (manager, memory) = crate::package::tests::management_profile();
            let original = memory.get("management-store-user").unwrap().unwrap();
            let tokens = manager.readonly_management_profile().unwrap();
            let credentials = ProviderCredentials::read_neutral(&tokens).unwrap();
            let change = || {
                let (mut session, _) = manager.management_store_snapshot().unwrap();
                session.user.username = "changed@example.invalid".to_owned();
                // Independent manager changes persistent fingerprint without sharing an epoch.
                TokenManager::with_management_backend(memory.clone())
                    .save_management_store_session(session)
                    .unwrap();
            };
            if boundary == "before" {
                change();
            }
            let result = read_using(
                &tokens,
                &credentials,
                || async {
                    assert_ne!(boundary, "before");
                    if boundary == "auth" {
                        change();
                    }
                    bound()
                },
                |_| async {
                    assert_ne!(boundary, "auth");
                    if boundary == "query" {
                        change();
                    }
                    Ok(page())
                },
            )
            .await;
            let mut output = Vec::new();
            let result = match result {
                Ok(summary) => {
                    if boundary == "publication" {
                        change();
                    }
                    publish(&tokens, &credentials, summary, &mut output).await
                }
                Err(error) => Err(error),
            };
            if boundary == "unchanged" {
                assert!(result.is_ok());
                assert_eq!(
                    memory.get("management-store-user").unwrap().unwrap(),
                    original
                );
                assert_eq!(output.last(), Some(&b'\n'));
                assert!(
                    !String::from_utf8(output)
                        .unwrap()
                        .contains("PRIVATE_SENTINEL")
                );
            } else {
                assert!(matches!(
                    result,
                    Err(Failure::Credentials(CredentialError::ProfileChanged))
                ));
                assert!(output.is_empty());
            }
        }
    }

    #[tokio::test]
    async fn provider_failure_is_not_empty_success_and_writer_failure_is_explicit() {
        let (manager, _) = crate::package::tests::management_profile();
        let tokens = manager.readonly_management_profile().unwrap();
        let credentials = ProviderCredentials::read_neutral(&tokens).unwrap();
        let result = read_using(
            &tokens,
            &credentials,
            || async { bound() },
            |_| async {
                Err(Failure::Provider(ProviderResponseError::HttpRejected {
                    status: 403,
                }))
            },
        )
        .await;
        assert!(matches!(
            result,
            Err(Failure::Provider(ProviderResponseError::HttpRejected {
                status: 403
            }))
        ));
        let (mut writer, reader) = tokio::io::duplex(64);
        drop(reader);
        assert!(matches!(
            publish(&tokens, &credentials, parse(page()).unwrap(), &mut writer).await,
            Err(Failure::Output)
        ));
    }

    #[tokio::test]
    async fn total_deadline_covers_auth_and_publication_not_only_collection_http() {
        for boundary in ["auth", "publication"] {
            let (manager, _) = crate::package::tests::management_profile();
            let tokens = manager.readonly_management_profile().unwrap();
            let credentials = ProviderCredentials::read_neutral(&tokens).unwrap();
            let work = async {
                let summary = read_using(
                    &tokens,
                    &credentials,
                    || async {
                        if boundary == "auth" {
                            std::future::pending::<()>().await;
                        }
                        bound()
                    },
                    |_| async { Ok(page()) },
                )
                .await?;
                let (mut writer, _reader) = tokio::io::duplex(1);
                publish(&tokens, &credentials, summary, &mut writer).await
            };
            assert!(matches!(
                within_deadline(work, Duration::from_millis(25)).await,
                Err(Failure::Deadline)
            ));
        }
    }

    #[tokio::test]
    async fn actual_streamed_four_mib_ceiling_accepts_boundary_and_rejects_oversize() {
        for mode in ["exact", "declaredOver", "chunkedOver"] {
            let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
            let address = listener.local_addr().unwrap();
            let server = tokio::spawn(async move {
                let (mut socket, _) = listener.accept().await.unwrap();
                let mut buffer = [0; 4096];
                assert!(socket.read(&mut buffer).await.unwrap() > 0);
                if mode == "declaredOver" {
                    socket.write_all(format!(
                        "HTTP/1.1 200 OK\r\nContent-Length: {}\r\nConnection: close\r\n\r\n",
                        PACKAGE_RESPONSE_LIMIT + 1).as_bytes()).await.unwrap();
                } else {
                    let overhead = br#"{"items":[],"padding":""}"#.len();
                    let payload = format!(
                        r#"{{"items":[],"padding":"{}"}}"#,
                        "x".repeat(
                            PACKAGE_RESPONSE_LIMIT - overhead + usize::from(mode == "chunkedOver")
                        )
                    );
                    let headers = if mode == "exact" {
                        format!(
                            "HTTP/1.1 200 OK\r\nContent-Length: {}\r\nConnection: close\r\n\r\n",
                            payload.len()
                        )
                    } else {
                        format!(
                            "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n{:x}\r\n",
                            payload.len()
                        )
                    };
                    socket.write_all(headers.as_bytes()).await.unwrap();
                    socket.write_all(payload.as_bytes()).await.unwrap();
                    if mode == "chunkedOver" {
                        // The bounded reader may already have closed the over-limit stream.
                        let _ = socket.write_all(b"\r\n0\r\n\r\n").await;
                    }
                }
            });
            let result = request_json::<Value>(
                client().unwrap().get(format!("http://{address}/synthetic")),
                PACKAGE_RESPONSE_LIMIT,
            )
            .await
            .and_then(|response| response.require_success());
            if mode == "exact" {
                assert_eq!(parse(result.unwrap()).unwrap()["itemCount"], 0);
            } else {
                assert!(matches!(
                    result,
                    Err(ProviderResponseError::TooLarge {
                        limit: PACKAGE_RESPONSE_LIMIT
                    })
                ));
            }
            server.await.unwrap();
        }
    }

    #[tokio::test]
    async fn diagnostic_http_client_does_not_follow_redirects() {
        let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
        let address = listener.local_addr().unwrap();
        let server = tokio::spawn(async move {
            let (mut socket, _) = listener.accept().await.unwrap();
            let mut buffer = [0; 4096];
            socket.read(&mut buffer).await.unwrap();
            socket.write_all(format!(
                "HTTP/1.1 302 Found\r\nLocation: http://{address}/forbidden\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{{}}"
            ).as_bytes()).await.unwrap();
            drop(socket);
            assert!(
                tokio::time::timeout(Duration::from_millis(50), listener.accept())
                    .await
                    .is_err()
            );
        });
        let result = request_json::<Value>(
            client().unwrap().get(format!("http://{address}/synthetic")),
            PACKAGE_RESPONSE_LIMIT,
        )
        .await
        .unwrap()
        .require_success();
        assert!(matches!(
            result,
            Err(ProviderResponseError::HttpRejected { status: 302 })
        ));
        server.await.unwrap();
    }

    #[test]
    fn actual_no_ui_first_readonly_last_clone_preserves_shared_epoch_and_blocks_writes() {
        let (manager, _) = crate::package::tests::management_profile();
        let (_, stamp) = manager.management_store_snapshot().unwrap();
        let witness = stamp.publication_witness();
        let tokens = readonly(&manager).unwrap();
        assert!(tokens.management_publication_current(&witness));
        let (session, _) = manager.management_store_snapshot().unwrap();
        assert!(tokens.save_management_store_session(session).is_err());
        assert!(!manager.management_publication_current(&witness));
    }
}
