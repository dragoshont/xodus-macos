use std::future::Future;
use std::pin::Pin;

use xodus::api::response::ProviderResponseError;
use xodus::api::xbox::XboxAuthError;
use xodus::tokens::{ManagementProfileWitness, TokenManager};
use xodus_management::auth_verify::{AuthVerifier, VerificationFailure};
use xodus_management::install_plan::{PlanFailure, PlanRead, verification_failure};
use xodus_management::wire::PlanParams;

use crate::package::PackageReadError;
use crate::provider_credentials::CredentialError;

pub struct PackageVerifier {
    client: reqwest::Client,
}

impl PackageVerifier {
    pub fn new() -> Result<Self, VerificationFailure> {
        let client = reqwest::Client::builder()
            .timeout(xodus_management::auth_verify::DEADLINE)
            .connect_timeout(std::time::Duration::from_secs(10))
            .redirect(reqwest::redirect::Policy::none())
            .build()
            .map_err(|_| VerificationFailure::TransportFailed)?;
        Ok(Self { client })
    }
}

pub(crate) fn provider_failure(error: ProviderResponseError) -> VerificationFailure {
    match error {
        ProviderResponseError::HttpRejected { status: 401 | 403 } => {
            VerificationFailure::AuthRejected
        }
        ProviderResponseError::HttpRejected { status: 404 } => {
            VerificationFailure::PackageUnavailable
        }
        ProviderResponseError::RequestFailed
        | ProviderResponseError::Deadline
        | ProviderResponseError::HttpRejected { .. } => VerificationFailure::TransportFailed,
        _ => VerificationFailure::ResponseInvalid,
    }
}

pub(crate) fn classify(error: PackageReadError) -> VerificationFailure {
    match error {
        PackageReadError::Credentials(CredentialError::ProfileChanged) => {
            VerificationFailure::ProfileChanged
        }
        PackageReadError::Credentials(CredentialError::Deadline) => {
            VerificationFailure::TransportFailed
        }
        PackageReadError::Credentials(_) => VerificationFailure::CredentialUnavailable,
        PackageReadError::Authentication(XboxAuthError::Provider(
            ProviderResponseError::HttpRejected { status: 404 },
        )) => VerificationFailure::AuthExchangeFailed,
        PackageReadError::Authentication(XboxAuthError::Provider(error)) => provider_failure(error),
        PackageReadError::Authentication(XboxAuthError::ExchangeFailed) => {
            VerificationFailure::AuthExchangeFailed
        }
        PackageReadError::Authentication(_)
        | PackageReadError::InvalidResponse
        | PackageReadError::InvalidContent => VerificationFailure::ResponseInvalid,
        PackageReadError::Provider(error) => provider_failure(error),
        PackageReadError::Unavailable => VerificationFailure::PackageUnavailable,
    }
}

impl AuthVerifier for PackageVerifier {
    fn plan_supported(&self) -> bool {
        true
    }

    fn plan_preflight(&self) -> Option<PlanFailure> {
        None
    }

    fn plan(
        &self,
        tokens: Option<TokenManager>,
        params: PlanParams,
    ) -> Pin<Box<dyn Future<Output = Result<PlanRead, PlanFailure>> + Send + '_>> {
        Box::pin(async move {
            if let Some(failure) = self.plan_preflight() {
                return Err(failure);
            }
            read_plan_using(
                tokens,
                params,
                |params| async move {
                    xodus::api::displaycatalog::find_products_by_id_bounded(
                        &self.client,
                        &params.product_id,
                        &params.market,
                        &params.language,
                    )
                    .await
                    .map_err(|error| match error {
                        xodus::api::displaycatalog::BoundedCatalogError::Http(error)
                            if error.status() == Some(reqwest::StatusCode::NOT_FOUND) =>
                        {
                            PlanFailure::PackageUnavailable
                        }
                        xodus::api::displaycatalog::BoundedCatalogError::Http(_) => {
                            PlanFailure::ProviderUnavailable
                        }
                        _ => PlanFailure::ResponseInvalid,
                    })
                },
                |tokens, content_id| async move {
                    crate::package::get_packages_verified(&self.client, &tokens, content_id)
                        .await
                        .map_err(|error| verification_failure(classify(error)))
                },
            )
            .await
        })
    }

    fn recent_supported(&self) -> bool {
        true
    }

    fn recent(
        &self,
        tokens: TokenManager,
        params: xodus_management::wire::RecentLibraryParams,
    ) -> Pin<
        Box<
            dyn Future<
                    Output = Result<
                        xodus_management::auth_verify::RecentLibraryRead,
                        VerificationFailure,
                    >,
                > + Send
                + '_,
        >,
    > {
        Box::pin(crate::recent_library::read(&self.client, tokens, params))
    }

    fn verify(
        &self,
        tokens: TokenManager,
        content_id: String,
    ) -> Pin<
        Box<dyn Future<Output = Result<ManagementProfileWitness, VerificationFailure>> + Send + '_>,
    > {
        Box::pin(async move {
            let readonly = tokens
                .readonly_management_profile()
                .map_err(|_| VerificationFailure::CredentialUnavailable)?;
            crate::package::get_packages_verified(&self.client, &readonly, content_id)
                .await
                .map_err(classify)
                .and_then(|read| {
                    read.profile
                        .ok_or(VerificationFailure::CredentialUnavailable)
                })
        })
    }
}

fn select_plan_package(
    params: &PlanParams,
    catalog: xodus::models::displaycatalog::DisplayCatalogProductsResponse,
) -> Result<xodus::models::displaycatalog::Package, PlanFailure> {
    if catalog.product.product_id.as_deref() != Some(&params.product_id)
        || catalog.product.display_sku_availabilities.len() > 256
    {
        return Err(PlanFailure::ResponseInvalid);
    }
    let editions: Vec<_> = catalog
        .product
        .display_sku_availabilities
        .iter()
        .filter(|edition| edition.sku.sku_id.as_deref() == Some(&params.edition_id))
        .collect();
    if editions.len() != 1 {
        return Err(if editions.is_empty() {
            PlanFailure::PackageUnavailable
        } else {
            PlanFailure::Ambiguous
        });
    }
    let packages: Vec<_> = editions[0]
        .sku
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
    if packages.is_empty() {
        return Err(PlanFailure::PackageUnavailable);
    }
    let mut applicable = Vec::new();
    let mut unknown = false;
    for package in packages {
        match xodus_management::install_plan::declared_applicability(params, package) {
            Ok(true) => applicable.push(package),
            Ok(false) => {}
            Err(PlanFailure::ApplicabilityUnproven) => unknown = true,
            Err(failure) => return Err(failure),
        }
    }
    if unknown {
        return Err(PlanFailure::ApplicabilityUnproven);
    }
    if applicable.len() != 1 {
        return Err(if applicable.len() > 1 {
            PlanFailure::Ambiguous
        } else {
            PlanFailure::SelectionUnsupported
        });
    }
    let package = applicable[0];
    package
        .package_id
        .as_ref()
        .filter(|id| xodus_management::state::identifier_valid(id))
        .ok_or(PlanFailure::PackageUnavailable)?;
    let content_id = package
        .content_id
        .as_deref()
        .ok_or(PlanFailure::PackageUnavailable)?;
    let content = uuid::Uuid::parse_str(content_id).map_err(|_| PlanFailure::ResponseInvalid)?;
    if content.is_nil() || content.to_string() != content_id {
        return Err(PlanFailure::ResponseInvalid);
    }
    Ok(package.clone())
}

fn catalog_plan_gate(package: &xodus::models::displaycatalog::Package) -> Result<(), PlanFailure> {
    let format = package
        .package_format
        .as_ref()
        .ok_or(PlanFailure::FormatUnsupported)?
        .as_str()
        .ok_or(PlanFailure::ResponseInvalid)?;
    if !format.eq_ignore_ascii_case("MSIXVC") {
        return Err(PlanFailure::FormatUnsupported);
    }
    // Appx SHA256 declarations do not establish MSIXVC or Update API digest coverage.
    Err(PlanFailure::IntegrityUnproven)
}

async fn read_plan_using<C, CF, P, PF>(
    tokens: Option<TokenManager>,
    params: PlanParams,
    catalog: C,
    package: P,
) -> Result<PlanRead, PlanFailure>
where
    C: FnOnce(PlanParams) -> CF,
    CF: Future<
        Output = Result<xodus::models::displaycatalog::DisplayCatalogProductsResponse, PlanFailure>,
    >,
    P: FnOnce(TokenManager, String) -> PF,
    PF: Future<Output = Result<crate::package::VerifiedPackageRead, PlanFailure>>,
{
    xodus_management::transport::validate_operation(
        &xodus_management::wire::Operation::InstallPlan(params.clone()),
    )
    .map_err(|_| PlanFailure::ResponseInvalid)?;
    let selected = select_plan_package(&params, catalog(params.clone()).await?)?;
    catalog_plan_gate(&selected)?;
    let tokens = tokens.ok_or(PlanFailure::CredentialUnavailable)?;
    read_selected_plan_using(tokens, params, selected, package).await
}

async fn read_selected_plan_using<P, PF>(
    tokens: TokenManager,
    params: PlanParams,
    selected: xodus::models::displaycatalog::Package,
    package: P,
) -> Result<PlanRead, PlanFailure>
where
    P: FnOnce(TokenManager, String) -> PF,
    PF: Future<Output = Result<crate::package::VerifiedPackageRead, PlanFailure>>,
{
    if !tokens.is_management_profile() {
        return Err(PlanFailure::CredentialUnavailable);
    }
    let readonly = tokens
        .with_noninteractive_management_keychain()
        .map_err(|_| PlanFailure::CredentialUnavailable)?
        .readonly_management_profile()
        .map_err(|_| PlanFailure::CredentialUnavailable)?;
    let package_id = selected
        .package_id
        .clone()
        .ok_or(PlanFailure::PackageUnavailable)?;
    let content_id = selected
        .content_id
        .clone()
        .ok_or(PlanFailure::PackageUnavailable)?;
    let read = package(readonly, content_id.clone()).await?;
    let profile = read.profile.ok_or(PlanFailure::CredentialUnavailable)?;
    let base: Vec<_> = read
        .package
        .package_files
        .iter()
        .filter(|file| crate::package::is_base_payload(file))
        .collect();
    if base.len() != 1 {
        return Err(if base.is_empty() {
            PlanFailure::PackageUnavailable
        } else {
            PlanFailure::Ambiguous
        });
    }
    if base[0].file_size <= 0
        || base[0].delta_version_id.is_some()
        || !crate::package::same_package_version(&base[0].version_id, &read.package.version_id)
        || base[0].content_id != content_id
    {
        return Err(PlanFailure::ResponseInvalid);
    }
    let result = PlanRead {
        params,
        profile,
        package: read.package,
        selected_package: selected,
        package_id,
        content_id,
        expires_at: tokio::time::Instant::now() + std::time::Duration::from_secs(600),
    };
    result.validate(&result.params, tokio::time::Instant::now())?;
    Ok(result)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn plan_params() -> PlanParams {
        PlanParams {
            product_id: "FIXTURE00001".to_owned(),
            edition_id: "fixture-edition".to_owned(),
            architecture: xodus_management::wire::Architecture::X86_64,
            language: "en-US".to_owned(),
            market: "US".to_owned(),
            destination: "/fixture/managed".to_owned(),
            experimental_consent: true,
        }
    }

    fn plan_catalog() -> xodus::models::displaycatalog::DisplayCatalogProductsResponse {
        serde_json::from_value(serde_json::json!({"Product": {
            "ProductId": "FIXTURE00001",
            "LocalizedProperties": [{"ProductTitle": "Fixture edition", "Language": "en-US"}],
            "DisplaySkuAvailabilities": [{"Sku": {"SkuId": "fixture-edition", "Properties": {
                "Packages": [{"PackageId": "fixture-package",
                    "ContentId": "00000000-0000-0000-0000-000000000001",
                    "Architectures": ["x64"], "Languages": ["en-US"],
                    "FrameworkDependencies": [], "HardwareDependencies": [],
                    "PackageFormat": "MSIXVC", "Hash": "PRIVATE_SENTINEL", "HashAlgorithm": "",
                    "MaxDownloadSizeInBytes": 4096, "MaxInstallSizeInBytes": 0,
                    "PlatformDependencies": [{"PlatformName": "Windows.Desktop"}]}]}},
                "Availabilities": []}]
        }}))
        .unwrap()
    }

    #[test]
    fn install_plan_evidenced_plural_dcat_fields_select_real_architecture_and_language_without_hash_promotion()
     {
        let params = plan_params();
        let mut catalog = plan_catalog();
        let packages = &mut catalog.product.display_sku_availabilities[0]
            .sku
            .properties
            .packages;
        let mut arm = packages[0].clone();
        arm.package_id = Some("fixture-arm-package".to_owned());
        arm.architectures = Some(serde_json::json!(["arm64"]));
        packages.push(arm);
        let selected = select_plan_package(&params, catalog.clone()).unwrap();
        assert_eq!(selected.package_id.as_deref(), Some("fixture-package"));
        assert_eq!(
            selected.max_download_size_in_bytes,
            Some(serde_json::json!(4096))
        );
        assert_eq!(
            selected.max_install_size_in_bytes,
            Some(serde_json::json!(0))
        );
        assert_eq!(selected.framework_dependencies, Some(serde_json::json!([])));
        assert_eq!(
            catalog_plan_gate(&selected),
            Err(PlanFailure::IntegrityUnproven)
        );
        let mut arm_params = params.clone();
        arm_params.architecture = xodus_management::wire::Architecture::Arm64;
        assert_eq!(
            select_plan_package(&arm_params, catalog.clone())
                .unwrap()
                .package_id
                .as_deref(),
            Some("fixture-arm-package")
        );
        for action in [
            "missingArchitecture",
            "missingLanguage",
            "unknownLanguage",
            "unknownArchitecture",
        ] {
            let mut candidate = plan_catalog();
            let package = &mut candidate.product.display_sku_availabilities[0]
                .sku
                .properties
                .packages[0];
            match action {
                "missingArchitecture" => package.architectures = None,
                "missingLanguage" => package.languages = None,
                "unknownLanguage" => package.languages = Some(serde_json::json!(["fr-FR"])),
                _ => package.architectures = Some(serde_json::json!(["arm64"])),
            }
            let expected = if action.starts_with("missing") {
                PlanFailure::ApplicabilityUnproven
            } else {
                PlanFailure::SelectionUnsupported
            };
            assert!(
                matches!(select_plan_package(&params, candidate), Err(failure) if failure == expected)
            );
        }
        let mut declared_hash = selected.clone();
        declared_hash.hash_algorithm = Some(serde_json::json!("SHA256"));
        declared_hash.hash = Some(serde_json::json!("a".repeat(64)));
        assert_eq!(
            catalog_plan_gate(&declared_hash),
            Err(PlanFailure::IntegrityUnproven)
        );
        declared_hash.package_format = Some(serde_json::json!("AppxBundle"));
        assert_eq!(
            catalog_plan_gate(&declared_hash),
            Err(PlanFailure::FormatUnsupported)
        );
        let old: xodus::models::displaycatalog::Package = serde_json::from_value(serde_json::json!({
            "PlatformDependencies":[{"PlatformName":"Windows.Desktop"}], "FutureUnrelated": "ignored"
        })).unwrap();
        assert!(old.architectures.is_none() && old.languages.is_none());
        assert!(old.max_install_size_in_bytes.is_none());
        let malformed: xodus::models::displaycatalog::Package =
            serde_json::from_value(serde_json::json!({
                "Architectures":"unexpected", "Languages":["en-US"],
                "PlatformDependencies":[{"PlatformName":"Windows.Desktop"}],
            }))
            .unwrap();
        assert_eq!(
            xodus_management::install_plan::declared_applicability(&params, &malformed),
            Err(PlanFailure::ResponseInvalid)
        );
    }

    fn verified_read(
        tokens: &TokenManager,
        content_id: &str,
    ) -> crate::package::VerifiedPackageRead {
        let mut file = crate::package::tests::package_file();
        file.content_id = content_id.to_owned();
        crate::package::VerifiedPackageRead {
            package: xodus::models::packagespc::PackageDetails {
                package_found: true,
                content_id: content_id.to_owned(),
                version_id: file.version_id.clone(),
                package_files: vec![file],
                version: "1.0.0.0".to_owned(),
                hash_of_hashes: Some("PRIVATE_SENTINEL".to_owned()),
                update_predownload: false,
                availability_date: "2000-01-01T00:00:00Z".to_owned(),
            },
            profile: Some(
                tokens
                    .management_store_snapshot()
                    .unwrap()
                    .1
                    .publication_witness(),
            ),
        }
    }

    #[test]
    fn install_plan_selection_requires_exact_product_edition_and_unique_real_pc_identity() {
        let params = plan_params();
        let selected = select_plan_package(&params, plan_catalog()).unwrap();
        assert_eq!(selected.package_id.as_deref(), Some("fixture-package"));
        assert_eq!(
            selected.content_id.as_deref(),
            Some("00000000-0000-0000-0000-000000000001")
        );
        let mut without_presentation = plan_catalog();
        without_presentation.product.localized_properties.clear();
        assert_eq!(
            select_plan_package(&params, without_presentation)
                .unwrap()
                .package_id,
            selected.package_id
        );
        for action in [
            "product",
            "missingProduct",
            "edition",
            "console",
            "packageID",
            "nil",
            "noncanonical",
            "packages",
            "editions",
            "conflictingEditions",
        ] {
            let mut catalog = plan_catalog();
            let edition = &mut catalog.product.display_sku_availabilities[0];
            match action {
                "product" => catalog.product.product_id = Some("OTHER0000001".to_owned()),
                "missingProduct" => catalog.product.product_id = None,
                "edition" => edition.sku.sku_id = Some("different-edition".to_owned()),
                "console" => {
                    edition.sku.properties.packages[0].platform_dependencies[0].platform_name =
                        "Xbox.One".to_owned()
                }
                "packageID" => edition.sku.properties.packages[0].package_id = None,
                "nil" => {
                    edition.sku.properties.packages[0].content_id =
                        Some(uuid::Uuid::nil().to_string())
                }
                "noncanonical" => {
                    edition.sku.properties.packages[0].content_id =
                        Some("AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA".to_owned())
                }
                "packages" => edition
                    .sku
                    .properties
                    .packages
                    .push(edition.sku.properties.packages[0].clone()),
                "editions" | "conflictingEditions" => {
                    let mut duplicate = edition.clone();
                    if action == "conflictingEditions" {
                        duplicate.sku.properties.packages[0].package_id =
                            Some("conflicting-fixture-package".to_owned());
                        duplicate.sku.properties.packages[0].content_id =
                            Some("00000000-0000-0000-0000-000000000002".to_owned());
                    }
                    catalog.product.display_sku_availabilities.push(duplicate);
                }
                _ => unreachable!(),
            }
            let failure = select_plan_package(&params, catalog).unwrap_err();
            assert_eq!(
                failure,
                if matches!(action, "packages" | "editions" | "conflictingEditions") {
                    PlanFailure::Ambiguous
                } else if matches!(
                    action,
                    "product" | "missingProduct" | "nil" | "noncanonical"
                ) {
                    PlanFailure::ResponseInvalid
                } else {
                    PlanFailure::PackageUnavailable
                }
            );
        }
    }

    #[tokio::test]
    async fn install_plan_dependency_declarations_block_unknown_and_reject_malformed_before_integrity()
     {
        for (action, expected) in [
            ("malformedFramework", PlanFailure::ResponseInvalid),
            ("malformedHardware", PlanFailure::ResponseInvalid),
            ("nullFramework", PlanFailure::ResponseInvalid),
            ("nullHardware", PlanFailure::ResponseInvalid),
            ("missingFramework", PlanFailure::ApplicabilityUnproven),
            ("missingHardware", PlanFailure::ApplicabilityUnproven),
            ("unknownFramework", PlanFailure::ApplicabilityUnproven),
            ("unknownHardware", PlanFailure::ApplicabilityUnproven),
            ("oversizedFramework", PlanFailure::ResponseInvalid),
            ("knownEmpty", PlanFailure::IntegrityUnproven),
        ] {
            let mut catalog = plan_catalog();
            let package = &mut catalog.product.display_sku_availabilities[0]
                .sku
                .properties
                .packages[0];
            match action {
                "malformedFramework" => {
                    package.framework_dependencies = Some(serde_json::json!({"notAnArray": true}))
                }
                "malformedHardware" => {
                    package.hardware_dependencies = Some(serde_json::json!("notAnArray"))
                }
                "nullFramework" => package.framework_dependencies = Some(serde_json::Value::Null),
                "nullHardware" => package.hardware_dependencies = Some(serde_json::Value::Null),
                "missingFramework" => package.framework_dependencies = None,
                "missingHardware" => package.hardware_dependencies = None,
                "unknownFramework" => {
                    package.framework_dependencies =
                        Some(serde_json::json!([{"unmodeledRequirement": true}]))
                }
                "unknownHardware" => {
                    package.hardware_dependencies =
                        Some(serde_json::json!(["unmodeledRequirement"]))
                }
                "oversizedFramework" => {
                    package.framework_dependencies =
                        Some(serde_json::json!(vec![serde_json::json!({}); 257]))
                }
                "knownEmpty" => {}
                _ => unreachable!(),
            }
            let catalog = serde_json::from_value(serde_json::to_value(catalog).unwrap()).unwrap();
            let result = read_plan_using(
                None,
                plan_params(),
                |_| async { Ok(catalog) },
                |_, _| async {
                    panic!(
                        "dependency/integrity blockers must precede credentials and package work"
                    )
                },
            )
            .await;
            assert!(
                matches!(result, Err(failure) if failure == expected),
                "{action}"
            );
        }
    }

    #[tokio::test]
    async fn install_plan_authenticated_seam_detaches_injected_backend_and_keeps_shared_profile_fences_without_io()
     {
        use std::sync::Arc;
        use xodus::tokens::store::TokenBackend;
        let (tokens, memory) = crate::package::tests::management_profile();
        let original = memory.get("management-store-user").unwrap();
        let owners = Arc::strong_count(&memory);
        let read = verified_read(&tokens, "00000000-0000-0000-0000-000000000001");
        let profile = read.profile.clone().unwrap();
        let memory_ref = &memory;
        let result = read_selected_plan_using(
            tokens.clone(),
            plan_params(),
            select_plan_package(&plan_params(), plan_catalog()).unwrap(),
            |readonly, _| async move {
                // A plain read-only clone would retain an extra reference to the injected backend.
                assert_eq!(Arc::strong_count(memory_ref), owners + 1);
                assert!(readonly.management_publication_current(&profile));
                Ok(read)
            },
        )
        .await
        .unwrap();
        assert!(tokens.management_publication_current(&result.profile));
        assert_eq!(memory.get("management-store-user").unwrap(), original);
        let session = tokens.get_management_store_session().unwrap().unwrap();
        tokens.save_management_store_session(session).unwrap();
        assert!(!tokens.management_publication_current(&result.profile));
        assert_eq!(memory.get("management-store-user").unwrap(), original);
    }

    #[tokio::test]
    async fn install_plan_real_provider_preflight_does_no_credential_or_provider_work() {
        let verifier = PackageVerifier::new().unwrap();
        assert!(verifier.plan_supported());
        assert_eq!(verifier.plan_preflight(), None);
        let result = read_plan_using(
            None,
            plan_params(),
            |_| async { Ok(plan_catalog()) },
            |_, _| async {
                panic!("unproven MSIXVC digest must short-circuit all credential/XSTS/package work")
            },
        )
        .await;
        assert!(matches!(result, Err(PlanFailure::IntegrityUnproven)));
    }

    #[tokio::test]
    async fn install_plan_actual_adapter_selection_binds_request_profile_and_never_guesses_hash_readiness()
     {
        use xodus::tokens::store::TokenBackend;
        let (tokens, memory) = crate::package::tests::management_profile();
        let original = memory.get("management-store-user").unwrap();
        for hash in ["", "unknown-hash", &"a".repeat(64)] {
            let mut read = verified_read(&tokens, "00000000-0000-0000-0000-000000000001");
            read.package.package_files[0].file_hash = hash.to_owned();
            let params = plan_params();
            let selected = select_plan_package(&params, plan_catalog()).unwrap();
            let resolved = read_selected_plan_using(
                tokens.readonly_management_profile().unwrap(),
                params.clone(),
                selected,
                |_, id| async move {
                    assert_eq!(id, "00000000-0000-0000-0000-000000000001");
                    Ok(read)
                },
            )
            .await
            .unwrap();
            assert_eq!(resolved.params, params);
            assert_eq!(resolved.package_id, "fixture-package");
            assert_eq!(resolved.readiness_failure(), PlanFailure::IntegrityUnproven);
            assert!(tokens.management_publication_current(&resolved.profile));
            let mut changed = params.clone();
            changed.architecture = xodus_management::wire::Architecture::Arm64;
            assert_eq!(
                resolved.validate(&changed, tokio::time::Instant::now()),
                Err(PlanFailure::ResponseInvalid)
            );
            assert_eq!(
                resolved.validate(&params, resolved.expires_at),
                Err(PlanFailure::Expired)
            );
            assert!(
                !serde_json::to_string(&resolved.readiness_failure().wire_error())
                    .unwrap()
                    .contains("PRIVATE_SENTINEL")
            );
        }
        assert_eq!(memory.get("management-store-user").unwrap(), original);
    }

    #[tokio::test]
    async fn install_plan_ambiguous_selection_short_circuits_package_read_and_preserves_closed_failures()
     {
        let (tokens, _) = crate::package::tests::management_profile();
        let mut catalog = plan_catalog();
        catalog
            .product
            .display_sku_availabilities
            .push(catalog.product.display_sku_availabilities[0].clone());
        let result = read_plan_using(
            Some(tokens.clone()),
            plan_params(),
            |_| async { Ok(catalog) },
            |_, _| async { panic!("package read must not follow ambiguous edition selection") },
        )
        .await;
        assert!(matches!(result, Err(PlanFailure::Ambiguous)));
        for failure in [
            PlanFailure::ProviderUnavailable,
            PlanFailure::AuthRejected,
            PlanFailure::ProfileChanged,
        ] {
            let result = read_selected_plan_using(
                tokens.clone(),
                plan_params(),
                select_plan_package(&plan_params(), plan_catalog()).unwrap(),
                |_, _| async { Err(failure) },
            )
            .await;
            assert!(matches!(result, Err(observed) if observed == failure));
        }
    }

    #[test]
    fn credential_read_deadline_is_exact_retryable_transport_failure() {
        let error = classify(PackageReadError::Credentials(CredentialError::Deadline));
        assert_eq!(
            serde_json::to_value(error.wire_error()).unwrap(),
            serde_json::json!({
                "code":"NETWORK_UNAVAILABLE",
                "message":"Authenticated read failed: transportFailed.",
                "retryable":true,
                "details":{"category":"authenticatedReadFailure","stage":"transportFailed"},
            })
        );
    }

    #[test]
    fn authenticated_read_errors_are_closed_and_distinguish_unavailable_from_rejected() {
        for (error, expected) in [
            (
                PackageReadError::Credentials(CredentialError::ProfileChanged),
                VerificationFailure::ProfileChanged,
            ),
            (
                PackageReadError::Credentials(CredentialError::StoreUnavailable),
                VerificationFailure::CredentialUnavailable,
            ),
            (
                PackageReadError::Authentication(XboxAuthError::ExchangeFailed),
                VerificationFailure::AuthExchangeFailed,
            ),
            (
                PackageReadError::Provider(ProviderResponseError::HttpRejected { status: 401 }),
                VerificationFailure::AuthRejected,
            ),
            (
                PackageReadError::Provider(ProviderResponseError::HttpRejected { status: 403 }),
                VerificationFailure::AuthRejected,
            ),
            (
                PackageReadError::Provider(ProviderResponseError::HttpRejected { status: 404 }),
                VerificationFailure::PackageUnavailable,
            ),
            (
                PackageReadError::Unavailable,
                VerificationFailure::PackageUnavailable,
            ),
            (
                PackageReadError::Provider(ProviderResponseError::Deadline),
                VerificationFailure::TransportFailed,
            ),
            (
                PackageReadError::Provider(ProviderResponseError::InvalidJson),
                VerificationFailure::ResponseInvalid,
            ),
            (
                PackageReadError::Provider(ProviderResponseError::TooLarge { limit: 123 }),
                VerificationFailure::ResponseInvalid,
            ),
        ] {
            let result = classify(error);
            assert_eq!(result, expected);
            let wire = serde_json::to_string(&result.wire_error()).unwrap();
            for value in [
                "PRIVATE_SENTINEL",
                "Authorization",
                "http",
                "123",
                "401",
                "403",
                "404",
            ] {
                assert!(!wire.contains(value));
            }
        }
    }
}
