use inquire::Select;
use xodus::XBOX_LIVE_PACKAGES_PC;
use xodus::api::displaycatalog::find_products_by_id;
use xodus::models::packagespc::{PackageDetails, PackageResponse};
use xodus::models::secrets::Token;
use xodus::tokens::TokenManager;

pub async fn get_content_id(
    client: &reqwest::Client,
    product: String,
    market: Option<String>,
) -> Result<String, Box<dyn std::error::Error>> {
    let displaycatalog = find_products_by_id(
        client,
        product,
        market.clone().unwrap_or("neutral".to_owned()),
        vec!["en".to_string(), "neutral".to_string()],
    )
    .await?;

    let product_details = displaycatalog.product;

    let mut found_package = None;
    let mut subprods: Vec<String> = vec![];
    'o: for availability in &product_details.display_sku_availabilities {
        for package in &availability.sku.properties.packages {
            if package
                .platform_dependencies
                .iter()
                .any(|dep| dep.platform_name == "Windows.Desktop")
            {
                found_package = Some(package);
                break 'o;
            }
        }
        for availability in &availability.availabilities {
            if let Some(licensing_data) = &availability.licensing_data {
                for satisfies in &licensing_data.satisfying_entitlement_keys {
                    for entitlement_key in &satisfies.entitlement_keys {
                        let key: Vec<&str> = entitlement_key.split(":").collect();
                        if key.len() == 3 && key[0] == "big" {
                            subprods.push(key[1].to_string());
                        }
                    }
                }
            }
        }
    }
    subprods.sort();
    subprods.dedup();

    let Some(package) = found_package else {
        if !subprods.is_empty() {
            let Ok(item) = Select::new("Select files to download", subprods)
                .with_page_size(30)
                .prompt()
            else {
                return Err(Box::new(std::io::Error::other("Selection failed")));
            };
            return Box::pin(get_content_id(client, item, market)).await;
        }

        return Err(Box::new(std::io::Error::other(
            "Windows.Desktop package not found, if you believe this is an error, please report it",
        )));
    };

    let Some(content_id) = &package.content_id else {
        tracing::error!("ContentId not found, if you believe this is an error, please report it");
        return Err(Box::new(std::io::Error::other(
            "ContentId not found, if you believe this is an error, please report it",
        )));
    };
    Ok(content_id.to_owned())
}

pub async fn get_packages(
    client: &reqwest::Client,
    tokens: &TokenManager,
    content_id: String,
) -> Result<PackageDetails, Box<dyn std::error::Error>> {
    let content_id = uuid::Uuid::parse_str(&content_id)
        .map_err(|_| std::io::Error::other("Invalid package content ID"))?;
    let dev_token = tokens
        .get_device_sts_token()
        .map_err(|_| std::io::Error::other("Device credentials are unavailable"))?;
    let Token::Legacy(dev_token) = dev_token else {
        return Err(Box::new(std::io::Error::other("Invalid STS token")));
    };
    let user = tokens
        .get_user()
        .map_err(|_| std::io::Error::other("Signed-in account is unavailable"))?;
    let user_token = tokens
        .get_user_sts_token()
        .map_err(|_| std::io::Error::other("User credentials are unavailable"))?;
    let Token::Legacy(legacy) = user_token else {
        return Err(Box::new(std::io::Error::other("Unsupported user token")));
    };

    let xsts_token = xodus::api::xbox::run(
        client,
        dev_token,
        legacy,
        user.username,
        "http://update.xboxlive.com",
    )
    .await?;

    let response = client
        .get(format!(
            "{XBOX_LIVE_PACKAGES_PC}/GetBasePackage/{content_id}"
        ))
        .header("x-xbl-contract-version", "3")
        .header(
            "Authorization",
            xodus::api::xbox::get_xsts_auth_header(xsts_token)?,
        )
        .send()
        .await
        .map_err(|_| std::io::Error::other("Package request failed"))?
        .error_for_status()
        .map_err(|_| std::io::Error::other("Package service rejected the request"))?;

    let res: PackageResponse = response
        .json()
        .await
        .map_err(|_| std::io::Error::other("Package response is invalid"))?;

    checked_package(res, content_id)
}

fn checked_package(
    response: PackageResponse,
    content_id: uuid::Uuid,
) -> Result<PackageDetails, Box<dyn std::error::Error>> {
    let PackageResponse::Found(package) = response else {
        return Err(Box::new(std::io::Error::other(
            "Package is unavailable; authorization and availability must be checked",
        )));
    };
    if !package.package_found || uuid::Uuid::parse_str(&package.content_id).ok() != Some(content_id)
    {
        return Err(Box::new(std::io::Error::other(
            "Package response does not match the requested content",
        )));
    }
    Ok(package)
}

#[cfg(test)]
mod tests {
    use super::*;

    const CONTENT_ID: &str = "00000000-0000-0000-0000-000000000001";

    fn response(found: bool, content_id: &str) -> PackageResponse {
        serde_json::from_value(serde_json::json!({
            "PackageFound": found,
            "ContentId": content_id,
            "VersionId": "00000000-0000-0000-0000-000000000002",
            "PackageFiles": [],
            "Version": "1.0.0.0",
            "UpdatePredownload": false,
            "AvailabilityDate": "2000-01-01T00:00:00Z"
        }))
        .unwrap()
    }

    #[test]
    fn package_response_requires_true_found_and_matching_content_identity() {
        let id = uuid::Uuid::parse_str(CONTENT_ID).unwrap();
        assert!(checked_package(response(true, CONTENT_ID), id).is_ok());
        assert!(checked_package(response(false, CONTENT_ID), id).is_err());
        assert!(checked_package(response(true, "different"), id).is_err());
        assert!(
            checked_package(
                PackageResponse::NotFound {
                    package_found: false
                },
                id
            )
            .is_err()
        );
    }

    #[tokio::test]
    async fn package_missing_credentials_returns_error_without_network_or_panic() {
        let error = get_packages(
            &reqwest::Client::new(),
            &TokenManager::with_memory(),
            CONTENT_ID.to_owned(),
        )
        .await
        .unwrap_err();
        assert_eq!(error.to_string(), "Device credentials are unavailable");
    }

    #[tokio::test]
    async fn invalid_package_identity_is_rejected_before_credentials() {
        let error = get_packages(
            &reqwest::Client::new(),
            &TokenManager::with_memory(),
            "../secret?query=hidden".to_owned(),
        )
        .await
        .unwrap_err();
        assert_eq!(error.to_string(), "Invalid package content ID");
    }
}
