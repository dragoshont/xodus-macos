use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "PascalCase")]
pub struct DisplayCatalogProductsResponse {
    pub product: Product,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "PascalCase")]
pub struct Product {
    #[serde(default)]
    pub product_id: Option<String>,
    #[serde(default)]
    pub localized_properties: Vec<LocalizedProperties>,
    pub display_sku_availabilities: Vec<DisplaySkuAvailability>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "PascalCase")]
pub struct LocalizedProperties {
    pub product_title: String,
    #[serde(default)]
    pub language: Option<String>,
    #[serde(default, skip_serializing_if = "serde_json::Value::is_null")]
    pub images: serde_json::Value,
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
    #[serde(default)]
    pub sku_id: Option<String>,
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
    pub package_id: Option<String>,
    #[serde(default)]
    pub version: Option<String>,
    #[serde(default)]
    pub content_id: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub architectures: Option<serde_json::Value>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub languages: Option<serde_json::Value>,
    #[serde(
        default,
        skip_serializing_if = "Option::is_none",
        deserialize_with = "dependency_declaration"
    )]
    pub framework_dependencies: Option<serde_json::Value>,
    #[serde(
        default,
        skip_serializing_if = "Option::is_none",
        deserialize_with = "dependency_declaration"
    )]
    pub hardware_dependencies: Option<serde_json::Value>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub package_format: Option<serde_json::Value>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub hash: Option<serde_json::Value>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub hash_algorithm: Option<serde_json::Value>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub max_download_size_in_bytes: Option<serde_json::Value>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub max_install_size_in_bytes: Option<serde_json::Value>,
    pub platform_dependencies: Vec<PlatformDependency>,
}

fn dependency_declaration<'de, D: serde::Deserializer<'de>>(
    deserializer: D,
) -> Result<Option<serde_json::Value>, D::Error> {
    // Preserve explicit null as malformed input, distinct from an absent declaration.
    serde_json::Value::deserialize(deserializer).map(Some)
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
