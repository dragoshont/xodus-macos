use crate::provider_credentials::{CredentialError, ProviderCredentials};
use inquire::Select;
use xodus::XBOX_LIVE_PACKAGES_PC;
use xodus::api::displaycatalog::find_products_by_id;
use xodus::api::response::{PACKAGE_RESPONSE_LIMIT, ProviderResponseError, request_json};
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
    get_packages_checked(client, tokens, content_id)
        .await
        .map_err(|error| Box::new(error) as Box<dyn std::error::Error>)
}

#[derive(Debug)]
pub(crate) enum PackageReadError {
    InvalidContent,
    Credentials(CredentialError),
    Authentication(xodus::api::xbox::XboxAuthError),
    Provider(ProviderResponseError),
    Unavailable,
    InvalidResponse,
}

impl std::fmt::Display for PackageReadError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::InvalidContent => formatter.write_str("Invalid package content ID"),
            Self::Credentials(error) => error.fmt(formatter),
            Self::Authentication(error) => error.fmt(formatter),
            Self::Provider(error) => error.fmt(formatter),
            Self::Unavailable => formatter.write_str(
                "Package is unavailable; authorization and availability must be checked",
            ),
            Self::InvalidResponse => formatter
                .write_str("Package response is invalid or does not match the requested content"),
        }
    }
}

impl std::error::Error for PackageReadError {}

pub(crate) async fn get_packages_checked(
    client: &reqwest::Client,
    tokens: &TokenManager,
    content_id: String,
) -> Result<PackageDetails, PackageReadError> {
    get_packages_verified(client, tokens, content_id)
        .await
        .map(|read| read.package)
}

#[derive(Debug)]
pub(crate) struct VerifiedPackageRead {
    package: PackageDetails,
    pub profile: Option<xodus::tokens::ManagementProfileWitness>,
}

pub(crate) async fn get_packages_verified(
    client: &reqwest::Client,
    tokens: &TokenManager,
    content_id: String,
) -> Result<VerifiedPackageRead, PackageReadError> {
    let content_id =
        uuid::Uuid::parse_str(&content_id).map_err(|_| PackageReadError::InvalidContent)?;
    let credentials = ProviderCredentials::read(tokens)
        .await
        .map_err(PackageReadError::Credentials)?;
    get_packages_using(
        tokens,
        credentials,
        content_id,
        |device, user, username| {
            xodus::api::xbox::run(client, device, user, username, "http://update.xboxlive.com")
        },
        |token, id| async move {
            let request = package_request(client, token, id)?;
            request_json(request, PACKAGE_RESPONSE_LIMIT)
                .await
                .map_err(PackageReadError::Provider)?
                .require_success()
                .map_err(PackageReadError::Provider)
        },
    )
    .await
}

async fn get_packages_using<A, AF, R, RF>(
    tokens: &TokenManager,
    credentials: ProviderCredentials,
    content_id: uuid::Uuid,
    authenticate: A,
    read: R,
) -> Result<VerifiedPackageRead, PackageReadError>
where
    A: FnOnce(
        xodus::models::secrets::LegacyToken,
        xodus::models::secrets::LegacyToken,
        String,
    ) -> AF,
    AF: std::future::Future<
            Output = Result<xodus::models::xbox::XstsResponse, xodus::api::xbox::XboxAuthError>,
        >,
    R: FnOnce(xodus::models::xbox::XstsResponse, uuid::Uuid) -> RF,
    RF: std::future::Future<Output = Result<PackageResponse, PackageReadError>>,
{
    let token = authenticate(
        credentials.device.clone(),
        credentials.user.clone(),
        credentials.account.username.clone(),
    )
    .await
    .map_err(PackageReadError::Authentication)?;
    credentials
        .verify_current(tokens)
        .await
        .map_err(PackageReadError::Credentials)?;
    let response = read(token, content_id).await?;
    match &response {
        PackageResponse::NotFound {
            package_found: false,
        }
        | PackageResponse::Found(PackageDetails {
            package_found: false,
            ..
        }) => {
            return Err(PackageReadError::Unavailable);
        }
        PackageResponse::NotFound {
            package_found: true,
        } => {
            return Err(PackageReadError::InvalidResponse);
        }
        _ => {}
    }
    let package =
        checked_package(response, content_id).map_err(|_| PackageReadError::InvalidResponse)?;
    credentials
        .verify_current(tokens)
        .await
        .map_err(PackageReadError::Credentials)?;
    Ok(VerifiedPackageRead {
        package,
        profile: credentials.publication_witness(),
    })
}

fn package_request(
    client: &reqwest::Client,
    token: xodus::models::xbox::XstsResponse,
    content_id: uuid::Uuid,
) -> Result<reqwest::RequestBuilder, PackageReadError> {
    Ok(client
        .get(format!(
            "{XBOX_LIVE_PACKAGES_PC}/GetBasePackage/{content_id}"
        ))
        .header("x-xbl-contract-version", "3")
        .header(
            "Authorization",
            xodus::api::xbox::get_xsts_auth_header(token)
                .map_err(PackageReadError::Authentication)?,
        ))
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
        if uuid::Uuid::parse_str(&file.content_id).ok() != Some(content_id) {
            return Err(Box::new(std::io::Error::other(
                "Package file content does not match the requested content",
            )));
        }
        let extension = std::path::Path::new(&file.file_name)
            .extension()
            .and_then(|extension| extension.to_str());
        let base_payload = extension.is_none()
            || extension.is_some_and(|extension| extension.eq_ignore_ascii_case("msixvc"));
        if base_payload && !same_package_version(&file.version_id, &package.version_id) {
            return Err(Box::new(std::io::Error::other(
                "Base package file version does not match the enclosing version",
            )));
        }
        checked_package_file_source(file)?;
    }
    Ok(package)
}

fn same_package_version(file: &str, package: &str) -> bool {
    match (uuid::Uuid::parse_str(file), uuid::Uuid::parse_str(package)) {
        (Ok(file), Ok(package)) => file == package,
        _ => !file.is_empty() && file == package,
    }
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

    fn auth_response() -> xodus::models::xbox::XstsResponse {
        serde_json::from_value(serde_json::json!({
            "NotAfter": "2099-01-01T00:00:00Z", "Token": "NEUTRAL_NOT_TOKEN",
            "DisplayClaims": {"xui": [{"uhs": "12345"}]},
        }))
        .unwrap()
    }

    fn management_profile() -> (
        TokenManager,
        std::sync::Arc<xodus::tokens::backend::MemoryBackend>,
    ) {
        use xodus::models::secrets::{Device, LegacyToken, ManagementStoreSession, Token, User};
        use xodus::tokens::backend::MemoryBackend;
        let token = LegacyToken {
            key_name: Some(xodus::tokens::PASSPORT_STS.to_owned()),
            token: "<EncryptedData Id=\"fixture\" xmlns=\"http://www.w3.org/2001/04/xmlenc#\" Type=\"http://www.w3.org/2001/04/xmlenc#Element\"><EncryptionMethod Algorithm=\"fixture\"/><KeyInfo><KeyName>http://Passport.NET/STS</KeyName></KeyInfo><CipherData><CipherValue>PRIVATE_SENTINEL</CipherValue></CipherData></EncryptedData>".to_owned(),
            binary_secret: Some(format!("BAAA{}AA==", "AAAA".repeat(1364))), tpm_key: None,
            lifetime: xodus::models::soap::Timestamp { id: None,
                created: "2026-01-01T00:00:00Z".to_owned(), expires: "2099-01-01T00:00:00Z".to_owned() },
        };
        let memory = std::sync::Arc::new(MemoryBackend::default());
        let manager = TokenManager::with_management_backend(memory.clone());
        manager
            .save_management_store_session(ManagementStoreSession {
                flow_id: "fixture-flow".to_owned(),
                user: User {
                    puid: "fixture".to_owned(),
                    username: "fixture@example.invalid".to_owned(),
                },
                tokens: std::collections::HashMap::from([(
                    xodus::tokens::PASSPORT_STS.to_owned(),
                    Token::Legacy(token.clone()),
                )]),
                device: Device {
                    puid: "fixture".to_owned(),
                    hwid: "fixture".to_owned(),
                    device_id: "fixture".to_owned(),
                    splicense: "fixture".to_owned(),
                    username: "fixture".to_owned(),
                    password: "fixture".to_owned(),
                },
                device_token: token,
            })
            .unwrap();
        (manager, memory)
    }

    #[test]
    fn authenticated_package_request_is_exact_read_only_endpoint_and_checked_header() {
        let request = package_request(
            &reqwest::Client::new(),
            auth_response(),
            uuid::Uuid::parse_str(CONTENT_ID).unwrap(),
        )
        .unwrap()
        .build()
        .unwrap();
        assert_eq!(request.method(), reqwest::Method::GET);
        assert_eq!(
            request.url().as_str(),
            format!("{XBOX_LIVE_PACKAGES_PC}/GetBasePackage/{CONTENT_ID}")
        );
        assert!(request.url().query().is_none());
        assert_eq!(request.headers()["x-xbl-contract-version"], "3");
        assert_eq!(
            request.headers()["Authorization"],
            "XBL3.0 x=12345;NEUTRAL_NOT_TOKEN"
        );
        assert!(request.body().is_none());
    }

    #[tokio::test]
    async fn authenticated_package_read_preserves_profile_and_checks_matching_response() {
        use xodus::tokens::store::TokenBackend;
        for outcome in [
            "success",
            "unavailable",
            "mismatch",
            "wrong_file_content",
            "wrong_base_version",
            "malformed",
            "rejected",
        ] {
            let (manager, memory) = management_profile();
            let original = memory.get("management-store-user").unwrap();
            let tokens = manager.readonly_management_profile().unwrap();
            let credentials = ProviderCredentials::read_neutral(&tokens).unwrap();
            let result = get_packages_using(
                &tokens,
                credentials,
                uuid::Uuid::parse_str(CONTENT_ID).unwrap(),
                |_, _, username| async move {
                    assert_eq!(username, "fixture@example.invalid");
                    Ok(auth_response())
                },
                |_, id| async move {
                    assert_eq!(id.to_string(), CONTENT_ID);
                    match outcome {
                        "success" => Ok(response(true, CONTENT_ID)),
                        "unavailable" => Ok(PackageResponse::NotFound {
                            package_found: false,
                        }),
                        "mismatch" => Ok(response(true, "PRIVATE_SENTINEL")),
                        "wrong_file_content" | "wrong_base_version" => {
                            let PackageResponse::Found(mut package) = response(true, CONTENT_ID)
                            else {
                                panic!("neutral package required");
                            };
                            let mut file = package_file();
                            if outcome == "wrong_file_content" {
                                file.content_id = "PRIVATE_SENTINEL".to_owned();
                            } else {
                                file.version_id = "PRIVATE_SENTINEL".to_owned();
                            }
                            package.package_files.push(file);
                            Ok(PackageResponse::Found(package))
                        }
                        "malformed" => Err(PackageReadError::Provider(
                            ProviderResponseError::InvalidJson,
                        )),
                        _ => Err(PackageReadError::Provider(
                            ProviderResponseError::HttpRejected { status: 403 },
                        )),
                    }
                },
            )
            .await;
            assert_eq!(result.is_ok(), outcome == "success");
            if let Err(error) = result {
                assert!(!format!("{error:?}").contains("PRIVATE_SENTINEL"));
            }
            assert_eq!(memory.get("management-store-user").unwrap(), original);
            for key in [
                "user-tokens",
                "device-tokens",
                "management-pending-exchange",
            ] {
                assert!(memory.get(key).unwrap().is_none());
            }
        }
    }

    #[tokio::test]
    async fn authenticated_package_read_fences_profile_changes_before_and_after_provider_read() {
        for after_read in [false, true] {
            let (manager, _) = management_profile();
            let tokens = manager.readonly_management_profile().unwrap();
            let credentials = ProviderCredentials::read_neutral(&tokens).unwrap();
            let before = manager.clone();
            let after = manager.clone();
            let result = get_packages_using(
                &tokens,
                credentials,
                uuid::Uuid::parse_str(CONTENT_ID).unwrap(),
                |_, _, _| async move {
                    if !after_read {
                        before.remove_user_credentials().unwrap();
                    }
                    Ok(auth_response())
                },
                |_, _| async move {
                    assert!(after_read, "Read must not start after profile changed");
                    after.remove_user_credentials().unwrap();
                    Ok(response(true, CONTENT_ID))
                },
            )
            .await;
            assert!(matches!(
                result,
                Err(PackageReadError::Credentials(
                    CredentialError::ProfileChanged
                ))
            ));
        }
    }

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

    #[tokio::test]
    async fn real_package_not_found_json_is_unavailable_without_invalidating_credentials() {
        use xodus::tokens::store::TokenBackend;
        let (manager, memory) = management_profile();
        let original = memory.get("management-store-user").unwrap();
        let readonly = manager.readonly_management_profile().unwrap();
        let credentials = ProviderCredentials::read_neutral(&readonly).unwrap();
        let response: PackageResponse = serde_json::from_str(
            r#"{"PackageFound":false,"FutureProviderField":"PRIVATE_SENTINEL"}"#,
        )
        .unwrap();
        assert!(matches!(
            response,
            PackageResponse::NotFound {
                package_found: false
            }
        ));
        let result = get_packages_using(
            &readonly,
            credentials,
            uuid::Uuid::parse_str(CONTENT_ID).unwrap(),
            |_, _, _| async { Ok(auth_response()) },
            |_, _| async { Ok(response) },
        )
        .await;
        assert!(matches!(result, Err(PackageReadError::Unavailable)));
        assert_eq!(memory.get("management-store-user").unwrap(), original);
        assert!(manager.management_store_snapshot().is_ok());
    }

    #[test]
    fn package_files_match_content_and_base_version_without_conflating_auxiliary_versions() {
        let id = uuid::Uuid::parse_str(CONTENT_ID).unwrap();
        let PackageResponse::Found(mut package) = response(true, CONTENT_ID) else {
            panic!("neutral package required");
        };
        let mut base = package_file();
        base.version_id = package.version_id.to_ascii_uppercase();
        package.package_files.push(base.clone());
        for name in ["fixture.phf", "fixture.xsp", "fixture.PHF", "fixture.XSP"] {
            let mut auxiliary = base.clone();
            auxiliary.file_name = name.to_owned();
            auxiliary.version_id = "00000000-0000-0000-0000-000000000003".to_owned();
            auxiliary.delta_version_id = Some("00000000-0000-0000-0000-000000000004".to_owned());
            auxiliary.update_type = 123;
            package.package_files.push(auxiliary);
        }
        assert!(checked_package(PackageResponse::Found(package.clone()), id).is_ok());
        for name in ["fixture.msixvc", "fixture.MSIXVC", "fixture"] {
            let mut wrong = package.clone();
            wrong.package_files[0].file_name = name.to_owned();
            wrong.package_files[0].version_id = "PRIVATE_SENTINEL".to_owned();
            let error = checked_package(PackageResponse::Found(wrong), id).unwrap_err();
            assert_eq!(
                error.to_string(),
                "Base package file version does not match the enclosing version"
            );
            assert!(!error.to_string().contains("PRIVATE_SENTINEL"));
        }
        for index in 0..package.package_files.len() {
            let mut wrong = package.clone();
            wrong.package_files[index].content_id =
                "00000000-0000-0000-0000-000000000005".to_owned();
            assert_eq!(
                checked_package(PackageResponse::Found(wrong), id)
                    .unwrap_err()
                    .to_string(),
                "Package file content does not match the requested content"
            );
        }
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
