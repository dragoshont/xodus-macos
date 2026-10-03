use crate::models::displaycatalog::DisplayCatalogProductsResponse;

pub async fn find_products_by_id(
    client: &reqwest::Client,
    product: String,
    market: String,
    languages: Vec<String>,
) -> reqwest::Result<DisplayCatalogProductsResponse> {
    let langs = languages.join(",");
    let response = client
        .get(product_url(&product, &market, &langs))
        .send()
        .await?;
    let response = response.error_for_status()?;
    response.json().await
}

#[derive(Debug, thiserror::Error)]
pub enum BoundedCatalogError {
    #[error("catalog request failed")]
    Http(#[from] reqwest::Error),
    #[error("catalog response exceeds limit")]
    TooLarge,
    #[error("catalog response invalid")]
    Json(#[from] serde_json::Error),
}

/// Same public provider as the CLI, with a bounded response for the long-lived adapter.
pub async fn find_products_by_id_bounded(
    client: &reqwest::Client,
    product: &str,
    market: &str,
    language: &str,
) -> Result<DisplayCatalogProductsResponse, BoundedCatalogError> {
    const LIMIT: usize = 4 * 1024 * 1024;
    let mut response = client
        .get(product_url(product, market, language))
        .send()
        .await?
        .error_for_status()?;
    if response
        .content_length()
        .is_some_and(|length| length > LIMIT as u64)
    {
        return Err(BoundedCatalogError::TooLarge);
    }
    let mut body = Vec::new();
    while let Some(chunk) = response.chunk().await? {
        if body.len().saturating_add(chunk.len()) > LIMIT {
            return Err(BoundedCatalogError::TooLarge);
        }
        body.extend_from_slice(&chunk);
    }
    Ok(serde_json::from_slice(&body)?)
}

fn product_url(product: &str, market: &str, language: &str) -> url::Url {
    let mut url = url::Url::parse("https://displaycatalog.mp.microsoft.com/v7.0/products/")
        .expect("static catalog origin is valid");
    url.path_segments_mut()
        .expect("static URL is hierarchical")
        .pop_if_empty()
        .push(product);
    url.query_pairs_mut()
        .append_pair("market", market)
        .append_pair("languages", language);
    url
}
