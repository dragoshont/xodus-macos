use std::collections::{BTreeMap, HashSet};
use std::sync::Arc;
use std::time::Duration;

use sha2::{Digest, Sha256};
use tokio::sync::Semaphore;
use tokio::task::JoinSet;

use crate::adapter::CatalogProvider;
use crate::state::now;
use crate::transport::{invalid, product_valid};
use crate::wire::*;

pub const CORPUS: &str = "pcGamePassDiscovery";
pub const SOURCE: &str = "MicrosoftGamePassSigls:v3";
pub const CATEGORY: &str = "609d944c-d395-4c0a-9ea4-e9f39b52c1ad";
pub const FEED_TIMEOUT: Duration = Duration::from_secs(10);
pub const LOOKUP_TIMEOUT: Duration = Duration::from_secs(8);
pub const PAGE_TIMEOUT: Duration = Duration::from_secs(30);
const MAX_FEED_BYTES: usize = 512 * 1024;
const MAX_FEED_PRODUCTS: usize = 2048;
const MAX_PAGE_BYTES: usize = 896 * 1024;

fn network() -> WireError {
    WireError::new(
        ErrorCode::NetworkUnavailable,
        "Public PC discovery timed out or is temporarily unavailable. Retry later.",
        true,
    )
}

fn metadata() -> WireError {
    WireError::new(
        ErrorCode::PackageUnavailable,
        "Official PC discovery returned unsupported or invalid public metadata.",
        false,
    )
}

pub fn validate_cursor(cursor: &str) -> bool {
    let pieces: Vec<_> = cursor.split('-').collect();
    pieces.len() == 3
        && pieces[0] == "d1"
        && pieces[1].len() == 64
        && pieces[1]
            .bytes()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
        && pieces[2].parse::<usize>().is_ok_and(|offset| {
            offset > 0 && offset < MAX_FEED_PRODUCTS && offset.to_string() == pieces[2]
        })
}

fn parse_index(bytes: &[u8], params: &DiscoveryParams) -> Result<Vec<String>, WireError> {
    let values: Vec<serde_json::Value> = serde_json::from_slice(bytes).map_err(|_| metadata())?;
    if values.is_empty()
        || values.len() > MAX_FEED_PRODUCTS + 1
        || values[0]["siglId"].as_str() != Some(CATEGORY)
    {
        return Err(metadata());
    }
    if values.len() == 1 {
        return Err(WireError::new(
            ErrorCode::NotFound,
            "Official PC discovery has no public records for this market.",
            false,
        ));
    }
    let mut seen = HashSet::new();
    values
        .into_iter()
        .skip(1)
        .map(|value| {
            let id = value["id"].as_str().ok_or_else(metadata)?.to_owned();
            if !product_valid(&ProductParams {
                product_id: id.clone(),
                market: params.market.clone(),
                language: params.language.clone(),
                refresh: Refresh::Network,
            }) || !seen.insert(id.clone())
            {
                return Err(metadata());
            }
            Ok(id)
        })
        .collect()
}

fn page_scope(
    ids: &[String],
    params: &DiscoveryParams,
) -> Result<(String, usize, usize), WireError> {
    let mut hash = Sha256::new();
    for value in [CORPUS, &params.market, &params.language]
        .into_iter()
        .chain(ids.iter().map(String::as_str))
    {
        hash.update(value.as_bytes());
        hash.update([0]);
    }
    let revision = crate::staging::digest_hex(&hash.finalize());
    let start = match &params.cursor {
        None => 0,
        Some(cursor) if validate_cursor(cursor) => {
            let pieces: Vec<_> = cursor.split('-').collect();
            if pieces[1] != revision {
                return Err(WireError::new(
                    ErrorCode::RevisionConflict,
                    "PC discovery IDs or market/language changed. Restart browsing without a cursor.",
                    true,
                ));
            }
            pieces[2].parse::<usize>().map_err(|_| invalid())?
        }
        Some(_) => return Err(invalid()),
    };
    if start >= ids.len() || !(1..=16).contains(&params.limit) {
        return Err(invalid());
    }
    Ok((
        revision,
        start,
        (start + params.limit as usize).min(ids.len()),
    ))
}

async fn fetch_index(
    client: &reqwest::Client,
    permits: &Semaphore,
    params: &DiscoveryParams,
) -> Result<Vec<String>, WireError> {
    let _permit = permits.acquire().await.map_err(|_| network())?;
    let mut url = reqwest::Url::parse("https://catalog.gamepass.com/sigls/v3")
        .expect("static official discovery URL is valid");
    url.query_pairs_mut().extend_pairs([
        ("id", CATEGORY),
        ("language", params.language.as_str()),
        ("market", params.market.as_str()),
        ("platformContext", "pc"),
        ("subscriptionContext", "cfq7ttc0kgq8"),
    ]);
    let mut response = client.get(url).send().await.map_err(|_| network())?;
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
        .is_some_and(|length| length > MAX_FEED_BYTES as u64)
    {
        return Err(metadata());
    }
    let mut bytes = Vec::new();
    while let Some(chunk) = response.chunk().await.map_err(|_| network())? {
        if chunk.len() > MAX_FEED_BYTES - bytes.len() {
            return Err(metadata());
        }
        bytes.extend_from_slice(&chunk);
    }
    parse_index(&bytes, params)
}

pub async fn fetch_page(
    client: &reqwest::Client,
    permits: &Semaphore,
    provider: Arc<dyn CatalogProvider>,
    params: DiscoveryParams,
) -> Result<DiscoveryData, WireError> {
    let deadline = tokio::time::Instant::now() + PAGE_TIMEOUT;
    let ids = tokio::time::timeout(FEED_TIMEOUT, fetch_index(client, permits, &params))
        .await
        .map_err(|_| network())??;
    resolve_page(provider, ids, params, deadline).await
}

async fn resolve_page(
    provider: Arc<dyn CatalogProvider>,
    ids: Vec<String>,
    params: DiscoveryParams,
    deadline: tokio::time::Instant,
) -> Result<DiscoveryData, WireError> {
    let (revision, start, end) = page_scope(&ids, &params)?;
    let checked_at = now();
    let mut workers = JoinSet::new();
    let mut outcomes = BTreeMap::new();
    let mut next = start;
    while outcomes.len() < end - start {
        while workers.len() < 4 && next < end {
            let index = next;
            next += 1;
            let provider = provider.clone();
            let product = ProductParams {
                product_id: ids[index].clone(),
                market: params.market.clone(),
                language: params.language.clone(),
                refresh: Refresh::Network,
            };
            workers.spawn(async move {
                let result = tokio::time::timeout(LOOKUP_TIMEOUT, provider.fetch(product))
                    .await
                    .unwrap_or_else(|_| Err(network()));
                (index, result)
            });
        }
        tokio::select! {
            outcome = workers.join_next() => match outcome {
                Some(Ok((index, result))) => { outcomes.insert(index, result); },
                Some(Err(_)) => return Err(WireError::new(ErrorCode::InternalError,
                    "Public discovery metadata worker failed unexpectedly.", false)),
                None => return Err(metadata()),
            },
            _ = tokio::time::sleep_until(deadline) => {
                workers.abort_all();
                while workers.join_next().await.is_some() {}
                for index in start..end {
                    outcomes.entry(index).or_insert_with(|| Err(network()));
                }
            },
        }
    }
    let mut products = Vec::new();
    let mut failures = Vec::new();
    let mut size = 0;
    for (index, result) in outcomes {
        let result = result.and_then(|product| {
            if product.product_id != ids[index] || product.market != params.market
                || product.language != params.language || product.freshness != Freshness::Live {
                return Err(metadata());
            }
            let length = serde_json::to_vec(&product).map_err(|_| metadata())?.len();
            if length > MAX_PAGE_BYTES - size {
                return Err(WireError::new(ErrorCode::LimitExceeded,
                    "Product exceeds this page response budget. Request a smaller page or product.detail.", false));
            }
            size += length;
            Ok(product)
        });
        match result {
            Ok(product) => products.push(product),
            Err(error) => failures.push(DiscoveryFailure {
                product_id: ids[index].clone(),
                error,
            }),
        }
    }
    let page = DiscoveryData {
        corpus: CORPUS.to_owned(),
        completeness: "partial".to_owned(),
        source: SOURCE.to_owned(),
        checked_at,
        freshness: Freshness::Live,
        corpus_revision: revision.clone(),
        products,
        failures,
        next_cursor: (end < ids.len()).then(|| format!("d1-{revision}-{end}")),
    };
    if page.products.is_empty() {
        let mut error = WireError::new(
            ErrorCode::PackageUnavailable,
            "No product metadata resolved for this discovery page. Review per-product failures and retry.",
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

#[cfg(test)]
mod tests {
    use super::*;

    fn params() -> DiscoveryParams {
        DiscoveryParams {
            market: "US".to_owned(),
            language: "en-US".to_owned(),
            limit: 2,
            cursor: None,
        }
    }

    struct Lookup {
        pending: bool,
    }
    impl CatalogProvider for Lookup {
        fn fetch(
            &self,
            params: ProductParams,
        ) -> std::pin::Pin<
            Box<dyn std::future::Future<Output = Result<ProductRecord, WireError>> + Send + '_>,
        > {
            Box::pin(async move {
                if self.pending {
                    std::future::pending::<()>().await;
                }
                if params.product_id == "FIXTURE00002" {
                    return Err(network());
                }
                Ok(ProductRecord {
                    product_id: params.product_id,
                    title: "Fixture".to_owned(),
                    market: params.market,
                    language: params.language,
                    resolved_language: None,
                    source: "fixture".to_owned(),
                    checked_at: now(),
                    freshness: Freshness::Live,
                    editions: vec![],
                    pc_catalog_candidate: true,
                })
            })
        }
    }

    #[tokio::test]
    async fn mixed_page_keeps_failed_ids_visible_and_cursor_advances_over_every_attempt() {
        let ids = vec![
            "FIXTURE00001".to_owned(),
            "FIXTURE00002".to_owned(),
            "FIXTURE00003".to_owned(),
        ];
        let page = resolve_page(
            Arc::new(Lookup { pending: false }),
            ids,
            params(),
            tokio::time::Instant::now() + PAGE_TIMEOUT,
        )
        .await
        .unwrap();
        assert_eq!(page.products.len(), 1);
        assert_eq!(page.failures[0].product_id, "FIXTURE00002");
        assert_eq!(page.failures[0].error.code, ErrorCode::NetworkUnavailable);
        assert!(page.next_cursor.unwrap().ends_with("-2"));
        assert_eq!(page.completeness, "partial");
    }

    #[tokio::test]
    async fn exhausted_page_deadline_is_failure_not_success_shaped_empty_discovery() {
        let error = resolve_page(
            Arc::new(Lookup { pending: true }),
            vec!["FIXTURE00001".to_owned(), "FIXTURE00002".to_owned()],
            params(),
            tokio::time::Instant::now(),
        )
        .await
        .unwrap_err();
        assert_eq!(error.code, ErrorCode::PackageUnavailable);
        assert_eq!(
            error.details.as_ref().unwrap()["failures"]
                .as_array()
                .unwrap()
                .len(),
            2
        );
        assert!(
            error.details.as_ref().unwrap()["products"]
                .as_array()
                .unwrap()
                .is_empty()
        );
        assert!(error.retryable);
    }

    #[tokio::test]
    async fn discovery_cache_eviction_is_bounded_atomic_and_preserves_the_current_page() {
        let temporary = tempfile::tempdir().unwrap();
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            std::fs::set_permissions(temporary.path(), std::fs::Permissions::from_mode(0o700))
                .unwrap();
        }
        let path = temporary.path().canonicalize().unwrap();
        let mut store = crate::state::Store::open(&path).unwrap();
        let provider = Lookup { pending: false };
        for index in 100..612 {
            let mut record = provider
                .fetch(ProductParams {
                    product_id: format!("FIXTURE{index:05}"),
                    market: "US".to_owned(),
                    language: "en-US".to_owned(),
                    refresh: Refresh::Network,
                })
                .await
                .unwrap();
            record.checked_at = "2026-01-01T00:00:00Z".to_owned();
            store
                .state
                .catalog
                .insert(format!("{}:US:en-US", record.product_id), record);
        }
        let mut page = Vec::new();
        for index in 900..916 {
            page.push(
                provider
                    .fetch(ProductParams {
                        product_id: format!("FIXTURE{index:05}"),
                        market: "US".to_owned(),
                        language: "en-US".to_owned(),
                        refresh: Refresh::Network,
                    })
                    .await
                    .unwrap(),
            );
        }
        store.cache_discovery_products(&page).unwrap();
        assert_eq!(store.state.catalog.len(), 512);
        assert_eq!(store.state.cache_revision, 1);
        for record in &page {
            assert!(
                store
                    .state
                    .catalog
                    .contains_key(&format!("{}:US:en-US", record.product_id))
            );
        }
        assert!(store.state.jobs.is_empty());
        drop(store);
        assert_eq!(
            crate::state::Store::open(&path)
                .unwrap()
                .state
                .catalog
                .len(),
            512
        );
    }

    #[test]
    fn official_index_rejects_missing_mismatched_duplicate_and_unsafe_ids() {
        let header = serde_json::json!({"siglId": CATEGORY});
        for values in [
            serde_json::json!([]),
            serde_json::json!([{"siglId":"wrong"},{"id":"FIXTURE00001"}]),
            serde_json::json!([header,{"id":"../unsafe"}]),
            serde_json::json!([header,{"id":"FIXTURE00001"},{"id":"FIXTURE00001"}]),
        ] {
            assert!(parse_index(&serde_json::to_vec(&values).unwrap(), &params()).is_err());
        }
        assert_eq!(
            parse_index(
                &serde_json::to_vec(&serde_json::json!([
            header,{"id":"FIXTURE00001"},{"id":"FIXTURE00002"}]))
                .unwrap(),
                &params()
            )
            .unwrap()
            .len(),
            2
        );
    }

    #[test]
    fn cursors_pin_ordered_ids_market_language_and_valid_bounded_offsets() {
        let ids = vec![
            "FIXTURE00001".to_owned(),
            "FIXTURE00002".to_owned(),
            "FIXTURE00003".to_owned(),
        ];
        let mut params = params();
        let (revision, _, end) = page_scope(&ids, &params).unwrap();
        params.cursor = Some(format!("d1-{revision}-{end}"));
        assert_eq!(page_scope(&ids, &params).unwrap().1, 2);
        let mut reordered = ids.clone();
        reordered.reverse();
        assert_eq!(
            page_scope(&reordered, &params).unwrap_err().code,
            ErrorCode::RevisionConflict
        );
        params.market = "GB".to_owned();
        assert_eq!(
            page_scope(&ids, &params).unwrap_err().code,
            ErrorCode::RevisionConflict
        );
        for cursor in [
            "bad",
            &format!("d1-{revision}-0"),
            &format!("d1-{revision}-9999"),
            &format!("d1-{revision}-02"),
        ] {
            assert!(!validate_cursor(cursor));
        }
    }
}
