use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "PascalCase")]
pub struct DisplayCatalogProductsResponse {
    pub product: Product,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "PascalCase")]
pub struct Product {
    pub display_sku_availabilities: Vec<DisplaySkuAvailability>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "PascalCase")]
pub struct DisplaySkuAvailability {
    pub sku: Sku,
    pub availabilities: Vec<Availability>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "PascalCase")]
pub struct Sku {
    pub properties: SkuProperties,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "PascalCase")]
pub struct SkuProperties {
    pub packages: Vec<Package>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "PascalCase")]
pub struct Package {
    #[serde(default)]
    pub content_id: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub package_format: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub package_full_name: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub package_family_name: Option<String>,
    #[serde(default)]
    pub architectures: Vec<String>,
    #[serde(default)]
    pub framework_dependencies: Vec<FrameworkDependency>,
    #[serde(default)]
    pub applications: Vec<Application>,
    pub platform_dependencies: Vec<PlatformDependency>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "PascalCase")]
pub struct FrameworkDependency {
    pub package_identity: String,
    pub min_version: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "PascalCase")]
pub struct Application {
    pub application_id: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "PascalCase")]
pub struct PlatformDependency {
    pub platform_name: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "PascalCase")]
pub struct Availability {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub licensing_data: Option<LicensingData>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "PascalCase")]
pub struct LicensingData {
    pub satisfying_entitlement_keys: Vec<SatisfyingEntitlementKey>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "PascalCase")]
pub struct SatisfyingEntitlementKey {
    pub entitlement_keys: Vec<String>,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn legacy_package_metadata_remains_valid() {
        let package: Package = serde_json::from_value(serde_json::json!({
            "ContentId": "synthetic-content",
            "PlatformDependencies": [{"PlatformName": "Windows.Desktop"}]
        }))
        .unwrap();
        assert!(package.package_format.is_none());
        assert!(package.architectures.is_empty());
        assert!(package.framework_dependencies.is_empty());
    }

    #[test]
    fn inspection_retains_metadata_but_not_keys_or_download_locations() {
        let package: Package = serde_json::from_value(serde_json::json!({
            "ContentId": "synthetic-content",
            "PackageFormat": "EAppxBundle",
            "PackageFullName": "Example.Game_1.0.0.0_neutral_~_example",
            "PackageFamilyName": "Example.Game_example",
            "Architectures": ["x64"],
            "FrameworkDependencies": [{
                "PackageIdentity": "Microsoft.NET.Native.Runtime.2.2",
                "MinVersion": 562960417947648u64
            }],
            "Applications": [{"ApplicationId": "App", "Extensions": []}],
            "PlatformDependencies": [{"PlatformName": "Windows.Desktop"}],
            "KeyId": "synthetic-key-id",
            "KeyBlob": "synthetic-key-blob",
            "PackageUri": "https://example.invalid/private-package",
            "PackageDownloadUris": ["https://example.invalid/private-download"]
        }))
        .unwrap();
        let output = serde_json::to_value(&package).unwrap();
        assert_eq!(output["PackageFormat"], "EAppxBundle");
        assert_eq!(output["Architectures"][0], "x64");
        assert_eq!(output["Applications"][0]["ApplicationId"], "App");
        assert_eq!(
            output["FrameworkDependencies"][0]["MinVersion"],
            562960417947648u64
        );
        for field in ["KeyId", "KeyBlob", "PackageUri", "PackageDownloadUris"] {
            assert!(output.get(field).is_none(), "{field} must not be exported");
        }
    }
}
