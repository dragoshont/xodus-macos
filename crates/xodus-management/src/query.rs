use std::collections::{BTreeMap, HashSet};
use std::sync::Arc;

use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use tokio::sync::Semaphore;

use crate::adapter::CatalogProvider;
use crate::discovery::{FEED_TIMEOUT, PAGE_TIMEOUT, resolve_product_ids};
use crate::state::now;
use crate::transport::{invalid, product_valid};
use crate::wire::*;

pub const CORPUS: &str = "publicMicrosoftStoreSearch";
pub const SOURCE: &str = "MicrosoftStoreEdge:v9.0/searchResults";
const HOST: &str = "storeedgefd.dsx.mp.microsoft.com";
const INITIAL_PATH: &str = "/v9.0/pages/searchResults";
const NEXT_PATH: &str = "/v9.0/search";
const MAX_SOURCE_BYTES: usize = 1024 * 1024;
const MAX_SOURCE_CARDS: usize = 100;
const MAX_URI_BYTES: usize = 4096;
const MAX_CURSOR_BYTES: usize = 16384;

#[derive(Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
struct Cursor {
    version: u8,
    scope: String,
    page: String,
    offset: usize,
    revision: Option<String>,
}

struct SourcePage {
    ids: Vec<String>,
    next: Option<reqwest::Url>,
}

fn metadata() -> WireError {
    WireError::new(
        ErrorCode::PackageUnavailable,
        "Public Microsoft Store search returned unsupported or invalid source metadata.",
        false,
    )
}

fn network() -> WireError {
    WireError::new(
        ErrorCode::NetworkUnavailable,
        "Public Microsoft Store search timed out or is temporarily unavailable. Retry later.",
        true,
    )
}

fn digest(values: impl IntoIterator<Item = impl AsRef<str>>) -> String {
    let mut hash = Sha256::new();
    for value in values {
        hash.update(value.as_ref().as_bytes());
        hash.update([0]);
    }
    crate::staging::digest_hex(&hash.finalize())
}

fn scope(params: &QueryParams) -> String {
    digest([CORPUS, &params.market, &params.language, &params.query])
}

fn initial_url(params: &QueryParams) -> reqwest::Url {
    let mut url =
        reqwest::Url::parse("https://storeedgefd.dsx.mp.microsoft.com/v9.0/pages/searchResults")
            .expect("static public search URL is valid");
    url.query_pairs_mut().extend_pairs([
        ("market", params.market.as_str()),
        ("locale", params.language.as_str()),
        ("deviceFamily", "windows.desktop"),
        ("query", params.query.as_str()),
        ("mediaType", "games"),
    ]);
    url
}

fn validate_url(url: &reqwest::Url, params: &QueryParams) -> Result<(), WireError> {
    if url.as_str().len() > MAX_URI_BYTES
        || url.scheme() != "https"
        || url.host_str() != Some(HOST)
        || url.port().is_some()
        || !url.username().is_empty()
        || url.password().is_some()
        || url.fragment().is_some()
        || !matches!(url.path(), INITIAL_PATH | NEXT_PATH)
    {
        return Err(metadata());
    }
    let mut pairs = BTreeMap::new();
    for (key, value) in url.query_pairs() {
        if pairs.insert(key.into_owned(), value.into_owned()).is_some() {
            return Err(metadata());
        }
    }
    for (key, expected) in [
        ("market", params.market.as_str()),
        ("locale", params.language.as_str()),
        ("deviceFamily", "windows.desktop"),
        ("query", params.query.as_str()),
        ("mediaType", "games"),
    ] {
        if pairs.remove(key).as_deref() != Some(expected) {
            return Err(metadata());
        }
    }
    if url.path() == NEXT_PATH
        && (pairs
            .remove("productFamilies")
            .is_some_and(|family| family != "games")
            || pairs.remove("facets").as_deref() != Some("false")
            || !pairs.remove("cursor").is_some_and(|value| {
                !value.is_empty()
                    && value.len() <= MAX_URI_BYTES
                    && !value.chars().any(char::is_control)
            }))
    {
        return Err(metadata());
    }
    if !pairs.is_empty() {
        return Err(metadata());
    }
    Ok(())
}

fn decode_cursor(params: &QueryParams) -> Result<Cursor, WireError> {
    let Some(encoded) = &params.cursor else {
        return Ok(Cursor {
            version: 1,
            scope: scope(params),
            page: initial_url(params).to_string(),
            offset: 0,
            revision: None,
        });
    };
    let hex = encoded.strip_prefix("q1-").ok_or_else(invalid)?;
    if encoded.len() > MAX_CURSOR_BYTES
        || hex.is_empty()
        || !hex.len().is_multiple_of(2)
        || !hex
            .bytes()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
    {
        return Err(invalid());
    }
    let bytes = (0..hex.len())
        .step_by(2)
        .map(|offset| u8::from_str_radix(&hex[offset..offset + 2], 16).map_err(|_| invalid()))
        .collect::<Result<Vec<_>, _>>()?;
    let cursor: Cursor = serde_json::from_slice(&bytes).map_err(|_| invalid())?;
    if cursor.version != 1
        || cursor.offset >= MAX_SOURCE_CARDS
        || cursor.revision.as_ref().is_some_and(|value| {
            value.len() != 64
                || !value
                    .bytes()
                    .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
        })
        || (cursor.offset > 0) != cursor.revision.is_some()
    {
        return Err(invalid());
    }
    if cursor.scope != scope(params) {
        return Err(WireError::new(
            ErrorCode::RevisionConflict,
            "The query, market or language changed. Restart search without a cursor.",
            false,
        ));
    }
    let url = reqwest::Url::parse(&cursor.page).map_err(|_| invalid())?;
    validate_url(&url, params).map_err(|_| invalid())?;
    if url.as_str() != cursor.page {
        return Err(invalid());
    }
    Ok(cursor)
}

fn encode_cursor(cursor: &Cursor) -> Result<String, WireError> {
    let bytes = serde_json::to_vec(cursor).map_err(|_| metadata())?;
    let result = format!("q1-{}", crate::staging::digest_hex(&bytes));
    if result.len() > MAX_CURSOR_BYTES {
        return Err(metadata());
    }
    Ok(result)
}

fn parse_source(
    bytes: &[u8],
    url: &reqwest::Url,
    params: &QueryParams,
) -> Result<SourcePage, WireError> {
    if bytes.len() > MAX_SOURCE_BYTES {
        return Err(metadata());
    }
    let root: serde_json::Value = serde_json::from_slice(bytes).map_err(|_| metadata())?;
    let candidates: Vec<_> = if url.path() == INITIAL_PATH {
        root.as_array()
            .ok_or_else(metadata)?
            .iter()
            .filter_map(|item| item.get("Payload"))
            .filter(|payload| payload.get("SearchResults").is_some())
            .collect()
    } else {
        vec![root.get("Payload").ok_or_else(metadata)?]
    };
    if candidates.len() != 1 {
        return Err(metadata());
    }
    let payload = candidates[0];
    let cards = payload
        .get("SearchResults")
        .and_then(serde_json::Value::as_array)
        .ok_or_else(metadata)?;
    if cards.len() > MAX_SOURCE_CARDS {
        return Err(metadata());
    }
    let mut seen = HashSet::new();
    let ids = cards
        .iter()
        .map(|card| {
            let id = card
                .get("ProductId")
                .and_then(serde_json::Value::as_str)
                .ok_or_else(metadata)?;
            if card
                .get("ProductFamilyName")
                .and_then(serde_json::Value::as_str)
                != Some("Games")
                || !product_valid(&ProductParams {
                    product_id: id.to_owned(),
                    market: params.market.clone(),
                    language: params.language.clone(),
                    refresh: Refresh::Network,
                })
                || !seen.insert(id.to_owned())
            {
                return Err(metadata());
            }
            Ok(id.to_owned())
        })
        .collect::<Result<Vec<_>, _>>()?;
    let next = match payload.get("NextUri") {
        Some(serde_json::Value::Null) => None,
        Some(serde_json::Value::String(uri)) => {
            if uri.len() > MAX_URI_BYTES || uri.is_empty() || uri.chars().any(char::is_control) {
                return Err(metadata());
            }
            let next = url.join(uri).map_err(|_| metadata())?;
            validate_url(&next, params)?;
            let server_cursor = |page: &reqwest::Url| {
                page.query_pairs()
                    .find(|(key, _)| key == "cursor")
                    .map(|(_, value)| value.into_owned())
            };
            if next.path() != NEXT_PATH
                || server_cursor(url)
                    .is_some_and(|current| server_cursor(&next).as_ref() == Some(&current))
            {
                return Err(metadata());
            }
            Some(next)
        }
        _ => return Err(metadata()),
    };
    if ids.is_empty() && next.is_some() {
        return Err(metadata());
    }
    Ok(SourcePage { ids, next })
}

async fn fetch_source(
    client: &reqwest::Client,
    permits: &Semaphore,
    url: &reqwest::Url,
    params: &QueryParams,
) -> Result<SourcePage, WireError> {
    validate_url(url, params)?;
    let _permit = permits.acquire().await.map_err(|_| network())?;
    let mut response = client
        .get(url.clone())
        .send()
        .await
        .map_err(|_| network())?;
    if !response.status().is_success() {
        return Err(
            if response.status().is_server_error() || response.status().as_u16() == 429 {
                network()
            } else {
                metadata()
            },
        );
    }
    if response
        .content_length()
        .is_some_and(|size| size > MAX_SOURCE_BYTES as u64)
    {
        return Err(metadata());
    }
    let mut bytes = Vec::new();
    while let Some(chunk) = response.chunk().await.map_err(|_| network())? {
        if chunk.len() > MAX_SOURCE_BYTES - bytes.len() {
            return Err(metadata());
        }
        bytes.extend_from_slice(&chunk);
    }
    parse_source(&bytes, url, params)
}

fn page_bounds(
    source: &SourcePage,
    params: &QueryParams,
    cursor: &Cursor,
) -> Result<(usize, Option<String>), WireError> {
    let revision = digest(
        [cursor.scope.as_str(), cursor.page.as_str()]
            .into_iter()
            .chain(source.ids.iter().map(String::as_str)),
    );
    if cursor
        .revision
        .as_ref()
        .is_some_and(|value| value != &revision)
    {
        return Err(WireError::new(
            ErrorCode::RevisionConflict,
            "Public Store source IDs changed during pagination. Restart search without a cursor.",
            true,
        ));
    }
    if !(1..=16).contains(&params.limit)
        || cursor.offset > source.ids.len()
        || (!source.ids.is_empty() && cursor.offset == source.ids.len())
    {
        return Err(invalid());
    }
    let end = (cursor.offset + params.limit as usize).min(source.ids.len());
    let next = if end < source.ids.len() {
        Some(encode_cursor(&Cursor {
            version: 1,
            scope: cursor.scope.clone(),
            page: cursor.page.clone(),
            offset: end,
            revision: Some(revision),
        })?)
    } else {
        source
            .next
            .as_ref()
            .map(|page| {
                encode_cursor(&Cursor {
                    version: 1,
                    scope: cursor.scope.clone(),
                    page: page.to_string(),
                    offset: 0,
                    revision: None,
                })
            })
            .transpose()?
    };
    Ok((end, next))
}

async fn resolve_page(
    provider: Arc<dyn CatalogProvider>,
    source: SourcePage,
    params: QueryParams,
    cursor: Cursor,
    deadline: tokio::time::Instant,
) -> Result<QueryData, WireError> {
    let (end, next_cursor) = page_bounds(&source, &params, &cursor)?;
    let (products, failures) = resolve_product_ids(
        provider,
        &source.ids[cursor.offset..end],
        &params.market,
        &params.language,
        deadline,
        true,
    )
    .await?;
    let page = QueryData {
        corpus: CORPUS.to_owned(),
        completeness: "partial".to_owned(),
        source: SOURCE.to_owned(),
        checked_at: now(),
        freshness: Freshness::Live,
        query: params.query,
        products,
        failures,
        next_cursor,
    };
    if page.products.is_empty() && !source.ids.is_empty() {
        let mut error = WireError::new(
            ErrorCode::PackageUnavailable,
            "No PC product metadata resolved for this query page. Review per-product failures or continue browsing.",
            page.failures.iter().any(|failure| failure.error.retryable),
        );
        error.details = serde_json::to_value(&page)
            .map_err(|_| metadata())?
            .as_object()
            .cloned();
        return Err(error);
    }
    Ok(page)
}

pub async fn fetch_page(
    client: &reqwest::Client,
    permits: &Semaphore,
    provider: Arc<dyn CatalogProvider>,
    params: QueryParams,
) -> Result<QueryData, WireError> {
    let deadline = tokio::time::Instant::now() + PAGE_TIMEOUT;
    let cursor = decode_cursor(&params)?;
    let url = reqwest::Url::parse(&cursor.page).map_err(|_| invalid())?;
    let source = tokio::time::timeout(FEED_TIMEOUT, fetch_source(client, permits, &url, &params))
        .await
        .map_err(|_| network())??;
    resolve_page(provider, source, params, cursor, deadline).await
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::future::Future;
    use std::pin::Pin;
    use std::sync::atomic::{AtomicUsize, Ordering};
    use std::time::Duration;

    fn params() -> QueryParams {
        QueryParams {
            query: " Halo ".to_owned(),
            market: "US".to_owned(),
            language: "en-US".to_owned(),
            limit: 8,
            cursor: None,
        }
    }

    fn next_url(params: &QueryParams) -> reqwest::Url {
        let mut next = initial_url(params);
        next.set_path(NEXT_PATH);
        next.query_pairs_mut().extend_pairs([
            ("productFamilies", "games"),
            ("cursor", "fixture-server-cursor"),
            ("facets", "false"),
        ]);
        next
    }

    fn source(count: usize, params: &QueryParams) -> SourcePage {
        SourcePage {
            ids: (1..=count)
                .map(|index| format!("FIXTURE{index:05}"))
                .collect(),
            next: Some(next_url(params)),
        }
    }

    fn source_bytes(count: usize, next: Option<&str>, initial: bool) -> Vec<u8> {
        let payload = serde_json::json!({"Index":-1,"SearchResults": (1..=count).map(|index|
            serde_json::json!({"ProductId":format!("FIXTURE{index:05}"),
                "ProductFamilyName":"Games","TypeTag":"app","Title":"Not an identity"}))
            .collect::<Vec<_>>(), "NextUri":next});
        let root = if initial {
            serde_json::json!([{"Payload":{"Variant":"ignored"}},
            {"Payload":payload}])
        } else {
            serde_json::json!({"Payload":payload})
        };
        serde_json::to_vec(&root).unwrap()
    }

    #[test]
    fn initial_array_and_next_object_accept_app_cards_and_negative_index() {
        let params = params();
        let initial = initial_url(&params);
        let next = next_url(&params);
        let first = parse_source(
            &source_bytes(20, Some(next.as_str()), true),
            &initial,
            &params,
        )
        .unwrap();
        assert_eq!(first.ids.len(), 20);
        assert_eq!(first.next, Some(next.clone()));
        let following = parse_source(&source_bytes(20, None, false), &next, &params).unwrap();
        assert_eq!(following.ids.len(), 20);
        assert!(following.next.is_none());
        let later = next.to_string().replace("&productFamilies=games", "");
        validate_url(&reqwest::Url::parse(&later).unwrap(), &params).unwrap();
        assert_eq!(
            initial
                .query_pairs()
                .find(|(key, _)| key == "query")
                .unwrap()
                .1,
            " Halo "
        );
    }

    #[test]
    fn source_shape_and_identity_limits_never_default_to_empty_success() {
        let params = params();
        let url = initial_url(&params);
        for bytes in [
            b"[]".to_vec(),
            b"{}".to_vec(),
            source_bytes(MAX_SOURCE_CARDS + 1, None, true),
            vec![b' '; MAX_SOURCE_BYTES + 1],
        ] {
            assert!(parse_source(&bytes, &url, &params).is_err());
        }
        for mutation in [
            "missingID",
            "duplicateID",
            "wrongFamily",
            "missingNext",
            "missingResults",
        ] {
            let mut root: serde_json::Value =
                serde_json::from_slice(&source_bytes(2, None, true)).unwrap();
            let payload = &mut root[1]["Payload"];
            match mutation {
                "missingID" => {
                    payload["SearchResults"][0]
                        .as_object_mut()
                        .unwrap()
                        .remove("ProductId");
                }
                "duplicateID" => {
                    payload["SearchResults"][1]["ProductId"] = serde_json::json!("FIXTURE00001")
                }
                "wrongFamily" => {
                    payload["SearchResults"][0]["ProductFamilyName"] = serde_json::json!("Apps")
                }
                "missingNext" => {
                    payload.as_object_mut().unwrap().remove("NextUri");
                }
                _ => {
                    payload.as_object_mut().unwrap().remove("SearchResults");
                }
            }
            assert!(parse_source(&serde_json::to_vec(&root).unwrap(), &url, &params).is_err());
        }
        assert!(
            parse_source(
                &source_bytes(0, Some(next_url(&params).as_str()), true),
                &url,
                &params
            )
            .is_err()
        );
        assert!(
            parse_source(&source_bytes(0, None, true), &url, &params)
                .unwrap()
                .ids
                .is_empty()
        );
    }

    #[test]
    fn next_uri_refuses_foreign_hosts_routes_scopes_and_ambiguous_parameters() {
        let params = params();
        let initial = initial_url(&params);
        let valid = next_url(&params).to_string();
        for bad in [
            valid.replace("https:", "http:"),
            valid.replace(HOST, "attacker.invalid"),
            valid.replace(HOST, &format!("{HOST}:8443")),
            valid.replace(HOST, &format!("user@{HOST}")),
            valid.replace(NEXT_PATH, "/v9.0/purchase"),
            valid.replace("market=US", "market=GB"),
            valid.replace("locale=en-US", "locale=fr-FR"),
            valid.replace("query=+Halo+", "query=Halo"),
            valid.replace("windows.desktop", "xbox"),
            valid.replace("productFamilies=games", "productFamilies=apps"),
            valid.replace("facets=false", "facets=true"),
            format!("{valid}&market=US"),
            format!("{valid}&url=https%3A%2F%2Fattacker.invalid"),
            format!("{valid}#fragment"),
        ] {
            assert!(
                parse_source(&source_bytes(2, Some(&bad), true), &initial, &params).is_err(),
                "untrusted next URI was accepted"
            );
        }
    }

    #[test]
    fn repeated_logical_server_cursor_refuses_optional_order_and_encoding_variants() {
        let params = params();
        let current = next_url(&params);
        let omitted = current.to_string().replace("&productFamilies=games", "");
        let encoded = current
            .to_string()
            .replace("fixture-server-cursor", "%66ixture-server-cursor");
        let mut reordered = current.clone();
        let pairs = current
            .query_pairs()
            .map(|(key, value)| (key.into_owned(), value.into_owned()))
            .collect::<Vec<_>>();
        reordered.set_query(None);
        reordered
            .query_pairs_mut()
            .extend_pairs(pairs.into_iter().rev());
        for repeated in [omitted.clone(), encoded, reordered.to_string()] {
            let error = parse_source(&source_bytes(20, Some(&repeated), false), &current, &params)
                .err()
                .unwrap();
            assert_eq!(error.code, ErrorCode::PackageUnavailable);
        }
        let distinct = omitted.replace("fixture-server-cursor", "new-fixture-cursor");
        let page =
            parse_source(&source_bytes(20, Some(&distinct), false), &current, &params).unwrap();
        assert_eq!(
            page.next
                .unwrap()
                .query_pairs()
                .find(|(key, _)| key == "cursor")
                .unwrap()
                .1,
            "new-fixture-cursor"
        );
    }

    #[test]
    fn client_pages_preserve_all_twenty_positions_before_following_next_route() {
        let mut params = params();
        let source = source(20, &params);
        for (start, expected_end) in [(0, 8), (8, 16), (16, 20)] {
            let cursor = decode_cursor(&params).unwrap();
            assert_eq!(cursor.offset, start);
            assert_eq!(
                reqwest::Url::parse(&cursor.page).unwrap().path(),
                INITIAL_PATH
            );
            let (end, next) = page_bounds(&source, &params, &cursor).unwrap();
            assert_eq!(end, expected_end);
            params.cursor = next;
        }
        let following = decode_cursor(&params).unwrap();
        assert_eq!(following.offset, 0);
        assert!(following.revision.is_none());
        assert_eq!(
            reqwest::Url::parse(&following.page).unwrap().path(),
            NEXT_PATH
        );
        assert_eq!(page_bounds(&source, &params, &following).unwrap().0, 8);
    }

    #[test]
    fn opaque_cursor_is_scope_revision_and_structure_checked() {
        let params = params();
        let index = source(20, &params);
        let (_, next) = page_bounds(&index, &params, &decode_cursor(&params).unwrap()).unwrap();
        let scoped = QueryParams {
            cursor: next,
            ..params
        };
        for change in ["query", "market", "language"] {
            let mut foreign = scoped.clone();
            match change {
                "query" => foreign.query = "Halo".to_owned(),
                "market" => foreign.market = "GB".to_owned(),
                _ => foreign.language = "en-GB".to_owned(),
            }
            assert_eq!(
                decode_cursor(&foreign).err().unwrap().code,
                ErrorCode::RevisionConflict
            );
        }
        let cursor = decode_cursor(&scoped).unwrap();
        let mut changed = source(20, &scoped);
        changed.ids.swap(0, 1);
        assert_eq!(
            page_bounds(&changed, &scoped, &cursor).unwrap_err().code,
            ErrorCode::RevisionConflict
        );
        for mutation in ["version", "offset", "revision", "page", "extra"] {
            let mut payload = serde_json::to_value(&cursor).unwrap();
            match mutation {
                "version" => payload["version"] = serde_json::json!(2),
                "offset" => payload["offset"] = serde_json::json!(100),
                "revision" => payload["revision"] = serde_json::Value::Null,
                "page" => payload["page"] = serde_json::json!("https://attacker.invalid/"),
                _ => payload["token"] = serde_json::json!("fixture-not-a-token"),
            }
            let encoded = format!(
                "q1-{}",
                crate::staging::digest_hex(&serde_json::to_vec(&payload).unwrap())
            );
            let bad = QueryParams {
                cursor: Some(encoded),
                ..scoped.clone()
            };
            assert_eq!(
                decode_cursor(&bad).err().unwrap().code,
                ErrorCode::InvalidRequest
            );
        }
    }

    struct Lookup {
        pending: bool,
        active: Arc<AtomicUsize>,
        peak: Arc<AtomicUsize>,
    }
    impl Lookup {
        fn new(pending: bool) -> Self {
            Self {
                pending,
                active: Arc::new(AtomicUsize::new(0)),
                peak: Arc::new(AtomicUsize::new(0)),
            }
        }
    }
    struct Active(Arc<AtomicUsize>);
    impl Drop for Active {
        fn drop(&mut self) {
            self.0.fetch_sub(1, Ordering::SeqCst);
        }
    }
    impl CatalogProvider for Lookup {
        fn fetch(
            &self,
            params: ProductParams,
        ) -> Pin<Box<dyn Future<Output = Result<ProductRecord, WireError>> + Send + '_>> {
            Box::pin(async move {
                let active = self.active.fetch_add(1, Ordering::SeqCst) + 1;
                let _active = Active(self.active.clone());
                self.peak.fetch_max(active, Ordering::SeqCst);
                if self.pending {
                    std::future::pending::<()>().await;
                }
                if params.product_id == "FIXTURE00002" {
                    return Err(network());
                }
                let platform = if params.product_id == "FIXTURE00003" {
                    "Xbox.One"
                } else {
                    "Windows.Desktop"
                };
                crate::adapter::map_product(&params, serde_json::from_value(serde_json::json!({"Product":{
                    "ProductId":params.product_id,
                    "LocalizedProperties":[{"ProductTitle":"Fixture Harbor","Language":"en"}],
                    "DisplaySkuAvailabilities":[{"Sku":{"SkuId":"fixture-standard","Properties":{
                        "Packages":[{"PlatformDependencies":[{"PlatformName":platform}]}]}},"Availabilities":[]}]
                }})).unwrap())
            })
        }
    }

    #[tokio::test]
    async fn mixed_page_echoes_exact_query_and_exposes_console_and_network_failures() {
        let params = params();
        let mut source = source(4, &params);
        source.next = None;
        let page = resolve_page(
            Arc::new(Lookup::new(false)),
            source,
            params.clone(),
            decode_cursor(&params).unwrap(),
            tokio::time::Instant::now() + PAGE_TIMEOUT,
        )
        .await
        .unwrap();
        assert_eq!(page.query, " Halo ");
        assert_eq!(page.products.len(), 2);
        assert_eq!(page.failures.len(), 2);
        assert_eq!(page.failures[0].error.code, ErrorCode::NetworkUnavailable);
        assert_eq!(page.failures[1].error.code, ErrorCode::PackageUnavailable);
        assert!(
            page.products
                .iter()
                .all(|product| product.pc_catalog_candidate
                    && product.resolved_language.as_deref() == Some("en")
                    && matches!(
                        product.editions[0].entitlement.kind,
                        EntitlementKind::Unknown
                    ))
        );
    }

    #[tokio::test]
    async fn actual_source_zero_succeeds_but_all_failed_attempts_return_details() {
        let params = params();
        let empty = SourcePage {
            ids: vec![],
            next: None,
        };
        let page = resolve_page(
            Arc::new(Lookup::new(false)),
            empty,
            params.clone(),
            decode_cursor(&params).unwrap(),
            tokio::time::Instant::now() + PAGE_TIMEOUT,
        )
        .await
        .unwrap();
        assert!(page.products.is_empty() && page.failures.is_empty() && page.next_cursor.is_none());
        let failed = SourcePage {
            ids: vec!["FIXTURE00002".to_owned(), "FIXTURE00003".to_owned()],
            next: None,
        };
        let error = resolve_page(
            Arc::new(Lookup::new(false)),
            failed,
            params.clone(),
            decode_cursor(&params).unwrap(),
            tokio::time::Instant::now() + PAGE_TIMEOUT,
        )
        .await
        .unwrap_err();
        assert_eq!(error.code, ErrorCode::PackageUnavailable);
        assert!(error.retryable);
        let details = error.details.unwrap();
        assert_eq!(details["query"], " Halo ");
        assert_eq!(details["products"].as_array().unwrap().len(), 0);
        assert_eq!(details["failures"].as_array().unwrap().len(), 2);
    }

    #[tokio::test]
    async fn zero_source_pages_complete_through_serve_without_cache_mutation_or_disconnect() {
        use crate::adapter::{Backend, serve};
        use crate::state::Store;
        use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
        struct EmptyQuery;
        impl CatalogProvider for EmptyQuery {
            fn fetch(
                &self,
                _: ProductParams,
            ) -> Pin<Box<dyn Future<Output = Result<ProductRecord, WireError>> + Send + '_>>
            {
                Box::pin(async { Err(metadata()) })
            }
            fn query_supported(&self) -> bool {
                true
            }
            fn query(
                &self,
                params: QueryParams,
            ) -> Pin<Box<dyn Future<Output = Result<QueryData, WireError>> + Send + '_>>
            {
                Box::pin(async move {
                    Ok(QueryData {
                        corpus: CORPUS.to_owned(),
                        completeness: "partial".to_owned(),
                        source: SOURCE.to_owned(),
                        checked_at: now(),
                        freshness: Freshness::Live,
                        query: params.query,
                        products: vec![],
                        failures: vec![],
                        next_cursor: None,
                    })
                })
            }
        }
        let temporary = tempfile::tempdir().unwrap();
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            std::fs::set_permissions(temporary.path(), std::fs::Permissions::from_mode(0o700))
                .unwrap();
        }
        let root = temporary.path().canonicalize().unwrap();
        let store = Store::open(&root).unwrap();
        let revision = store.state.cache_revision;
        let bytes = std::fs::read(root.join("management.json")).unwrap();
        let backend = Backend::new(store, Arc::new(EmptyQuery));
        let (client, server) = tokio::io::duplex(65536);
        let (server_read, server_write) = tokio::io::split(server);
        let task = tokio::spawn(serve(backend, BufReader::new(server_read), server_write));
        let (client_read, mut writer) = tokio::io::split(client);
        let mut reader = BufReader::new(client_read);
        writer.write_all(b"{\"kind\":\"request\",\"protocol\":{\"major\":1,\"minor\":0},\"requestID\":\"hello\",\"command\":\"hello\",\"params\":{\"client\":\"fixture\",\"clientVersion\":\"1\"}}\n").await.unwrap();
        let mut line = String::new();
        reader.read_line(&mut line).await.unwrap();
        assert_eq!(
            serde_json::from_str::<serde_json::Value>(&line).unwrap()["ok"],
            true
        );
        let params = params();
        let terminal = encode_cursor(&Cursor {
            version: 1,
            scope: scope(&params),
            page: next_url(&params).to_string(),
            offset: 0,
            revision: None,
        })
        .unwrap();
        for (index, cursor) in [None, Some(terminal)].into_iter().enumerate() {
            let identifier = format!("zero-{index}");
            let request = serde_json::json!({"kind":"request","protocol":{"major":1,"minor":0},
                "requestID":identifier,"command":"catalog.query","params":QueryParams {cursor,..params.clone()}});
            writer
                .write_all(format!("{request}\n").as_bytes())
                .await
                .unwrap();
            line.clear();
            tokio::time::timeout(Duration::from_secs(2), reader.read_line(&mut line))
                .await
                .unwrap()
                .unwrap();
            let frame: serde_json::Value = serde_json::from_str(&line).unwrap();
            assert_eq!(frame["requestID"], identifier);
            assert_eq!(frame["ok"], true);
            assert_eq!(frame["data"]["products"], serde_json::json!([]));
            assert_eq!(frame["data"]["failures"], serde_json::json!([]));
            assert!(frame["data"]["nextCursor"].is_null());
            assert_eq!(std::fs::read(root.join("management.json")).unwrap(), bytes);
            let request = serde_json::json!({"kind":"request","protocol":{"major":1,"minor":0},
                "requestID":format!("after-{index}"),"command":"catalog.search","params":{
                "query":"","market":"US","language":"en-US","platform":"pc","limit":8,"cursor":null}});
            writer
                .write_all(format!("{request}\n").as_bytes())
                .await
                .unwrap();
            line.clear();
            reader.read_line(&mut line).await.unwrap();
            let frame: serde_json::Value = serde_json::from_str(&line).unwrap();
            assert_eq!(frame["ok"], true);
            assert_eq!(frame["requestID"], format!("after-{index}"));
            assert_eq!(frame["data"]["cacheRevision"], revision);
            assert_eq!(std::fs::read(root.join("management.json")).unwrap(), bytes);
        }
        writer.shutdown().await.unwrap();
        task.await.unwrap().unwrap();
    }

    #[tokio::test]
    async fn exhausted_deadline_has_one_visible_failure_per_attempt_not_empty_success() {
        let mut params = params();
        params.limit = 16;
        let error = resolve_page(
            Arc::new(Lookup::new(true)),
            source(20, &params),
            params.clone(),
            decode_cursor(&params).unwrap(),
            tokio::time::Instant::now(),
        )
        .await
        .unwrap_err();
        let details = error.details.unwrap();
        assert_eq!(details["failures"].as_array().unwrap().len(), 16);
        assert!(details["nextCursor"].is_string());
    }

    #[tokio::test]
    async fn exact_product_byte_budget_exposes_every_overflow_without_oversized_frame() {
        struct LargeLookup;
        impl CatalogProvider for LargeLookup {
            fn fetch(
                &self,
                params: ProductParams,
            ) -> Pin<Box<dyn Future<Output = Result<ProductRecord, WireError>> + Send + '_>>
            {
                Box::pin(async move {
                    let mut lookup = params.clone();
                    lookup.product_id = "FIXTURE99999".to_owned();
                    let mut product = Lookup::new(false).fetch(lookup).await?;
                    product.product_id = params.product_id.clone();
                    let mut evidence = product.editions[0].clone();
                    evidence.product_id = params.product_id;
                    evidence.installability.reason = Some("x".repeat(512));
                    product.editions = (0..256)
                        .map(|index| {
                            let mut edition = evidence.clone();
                            edition.edition_id = format!("fixture-edition-{index:03}");
                            edition
                        })
                        .collect();
                    Ok(product)
                })
            }
        }
        let params = params();
        let mut source = source(8, &params);
        source.next = None;
        let page = resolve_page(
            Arc::new(LargeLookup),
            source,
            params.clone(),
            decode_cursor(&params).unwrap(),
            tokio::time::Instant::now() + PAGE_TIMEOUT,
        )
        .await
        .unwrap();
        let size = serde_json::to_vec(&page.products[0]).unwrap().len();
        assert_eq!(page.products.len(), crate::discovery::MAX_PAGE_BYTES / size);
        assert!(page.products.len() * size <= crate::discovery::MAX_PAGE_BYTES);
        assert!((page.products.len() + 1) * size > crate::discovery::MAX_PAGE_BYTES);
        assert_eq!(page.products.len() + page.failures.len(), 8);
        assert!(
            page.failures
                .iter()
                .all(|failure| failure.error.code == ErrorCode::LimitExceeded)
        );
        let frame = serde_json::json!({"kind":"result","protocol":{"major":1,"minor":0},
            "requestID":"fixture-budget","ok":true,"data":page});
        assert!(serde_json::to_vec(&frame).unwrap().len() <= MAX_LINE_BYTES);
    }

    #[tokio::test]
    async fn owned_page_cancellation_drops_all_four_metadata_workers() {
        let params = params();
        let lookup = Arc::new(Lookup::new(true));
        let provider = lookup.clone();
        let task = tokio::spawn(resolve_page(
            provider,
            source(20, &params),
            params.clone(),
            decode_cursor(&params).unwrap(),
            tokio::time::Instant::now() + PAGE_TIMEOUT,
        ));
        tokio::time::timeout(Duration::from_secs(1), async {
            while lookup.active.load(Ordering::SeqCst) != 4 {
                tokio::task::yield_now().await;
            }
        })
        .await
        .unwrap();
        assert_eq!(lookup.peak.load(Ordering::SeqCst), 4);
        task.abort();
        assert!(task.await.unwrap_err().is_cancelled());
        tokio::time::timeout(Duration::from_secs(1), async {
            while lookup.active.load(Ordering::SeqCst) != 0 {
                tokio::task::yield_now().await;
            }
        })
        .await
        .unwrap();
    }
}
