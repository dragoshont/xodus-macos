use crate::provider_credentials::ProviderCredentials;
use inquire::Select;
use xodus::XBOX_LIVE_PACKAGES_PC;
use xodus::api::displaycatalog::find_products_by_id;
use xodus::api::response::{PACKAGE_RESPONSE_LIMIT, request_json};
use xodus::models::packagespc::{PackageDetails, PackageFile, PackageResponse};
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
    let credentials = ProviderCredentials::read(tokens).await?;

    let xsts_token = xodus::api::xbox::run(
        client,
        credentials.device.clone(),
        credentials.user.clone(),
        credentials.account.username.clone(),
        "http://update.xboxlive.com",
    )
    .await?;
    credentials.verify_current(tokens).await?;

    let res: PackageResponse = request_json(
        client
            .get(format!(
                "{XBOX_LIVE_PACKAGES_PC}/GetBasePackage/{content_id}"
            ))
            .header("x-xbl-contract-version", "3")
            .header(
                "Authorization",
                xodus::api::xbox::get_xsts_auth_header(xsts_token)?,
            ),
        PACKAGE_RESPONSE_LIMIT,
    )
    .await?
    .require_success()?;

    let package = checked_package(res, content_id)?;
    credentials.verify_current(tokens).await?;
    Ok(package)
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
    for file in &package.package_files {
        checked_package_file_source(file)?;
    }
    Ok(package)
}

pub fn checked_package_file_source(
    file: &PackageFile,
) -> Result<(reqwest::Url, u64), std::io::Error> {
    if file.file_name.is_empty()
        || file
            .file_name
            .chars()
            .any(|character| character.is_control() || matches!(character, '/' | '\\' | ':'))
        || !matches!(
            std::path::Path::new(&file.file_name)
                .components()
                .collect::<Vec<_>>()
                .as_slice(),
            [std::path::Component::Normal(_)]
        )
    {
        return Err(std::io::Error::other(
            "Package file has an invalid file name",
        ));
    }
    let size = u64::try_from(file.file_size)
        .map_err(|_| std::io::Error::other("Package file has an invalid size"))?;
    let root = file
        .cdn_root_paths
        .first()
        .ok_or_else(|| std::io::Error::other("Package file has no CDN root"))?;
    let root_url = reqwest::Url::parse(root)
        .map_err(|_| std::io::Error::other("Package file has an invalid CDN URL"))?;
    let url = reqwest::Url::parse(&format!("{root}{}", file.relative_url))
        .map_err(|_| std::io::Error::other("Package file has an invalid CDN URL"))?;
    if !matches!(url.scheme(), "http" | "https")
        || url.host_str().is_none()
        || !url.username().is_empty()
        || url.password().is_some()
        || url.fragment().is_some()
        || root_url.origin() != url.origin()
    {
        return Err(std::io::Error::other("Package file has an invalid CDN URL"));
    }
    Ok((url, size))
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

    fn package_file() -> PackageFile {
        serde_json::from_value(serde_json::json!({
            "ContentId": CONTENT_ID,
            "VersionId": "00000000-0000-0000-0000-000000000002",
            "FileName": "fixture.msixvc",
            "FileSize": 12,
            "FileHash": "unverified-fixture",
            "KeyBlob": "fixture-not-key",
            "CdnRootPaths": ["https://fixture.invalid/files/"],
            "BackgroundCdnRootPaths": [],
            "RelativeUrl": "fixture?signature=fixture-only",
            "UpdateType": 0,
            "LicenseUsageType": 0,
            "ModifiedDate": "2000-01-01T00:00:00Z"
        }))
        .unwrap()
    }

    #[test]
    fn package_file_source_preserves_http_https_and_query_without_asserting_hash_semantics() {
        for scheme in ["http", "https"] {
            let mut file = package_file();
            file.cdn_root_paths = vec![format!("{scheme}://fixture.invalid/files/")];
            let (url, size) = checked_package_file_source(&file).unwrap();
            assert_eq!(
                url.as_str(),
                format!("{scheme}://fixture.invalid/files/fixture?signature=fixture-only")
            );
            assert_eq!(size, 12);
        }
    }

    #[test]
    fn package_file_source_rejects_unsafe_file_names_and_negative_sizes() {
        for name in [
            "",
            ".",
            "..",
            "../fixture",
            "folder/file",
            "folder\\file",
            "C:fixture",
            "/fixture",
            "fixture\0",
            "fixture\n",
        ] {
            let mut file = package_file();
            file.file_name = name.to_owned();
            assert_eq!(
                checked_package_file_source(&file).unwrap_err().to_string(),
                "Package file has an invalid file name"
            );
        }
        let mut file = package_file();
        file.file_size = -1;
        assert_eq!(
            checked_package_file_source(&file).unwrap_err().to_string(),
            "Package file has an invalid size"
        );
        let PackageResponse::Found(mut package) = response(true, CONTENT_ID) else {
            panic!("Synthetic package did not decode");
        };
        file.file_name = "fixture\u{1b}[31m".to_owned();
        file.file_size = 12;
        package.package_files.push(file);
        assert_eq!(
            checked_package(
                PackageResponse::Found(package),
                uuid::Uuid::parse_str(CONTENT_ID).unwrap(),
            )
            .unwrap_err()
            .to_string(),
            "Package file has an invalid file name",
        );
    }

    #[test]
    fn package_file_source_rejects_missing_malformed_credentials_and_authority_changes() {
        let mut file = package_file();
        file.cdn_root_paths.clear();
        assert_eq!(
            checked_package_file_source(&file).unwrap_err().to_string(),
            "Package file has no CDN root"
        );
        for (root, relative) in [
            ("file:///fixture/", "fixture"),
            ("not-url", "fixture"),
            ("https://fixture-only@fixture.invalid/", "fixture"),
            ("https://fixture.invalid/", "fixture#fixture-only"),
            ("https://fixture.invalid", ".other.invalid/fixture"),
        ] {
            let mut file = package_file();
            file.cdn_root_paths = vec![root.to_owned()];
            file.relative_url = relative.to_owned();
            let error = checked_package_file_source(&file).unwrap_err();
            assert_eq!(error.to_string(), "Package file has an invalid CDN URL");
            assert!(!format!("{error:?}").contains("fixture-only"));
        }
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
