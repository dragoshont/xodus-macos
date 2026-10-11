use std::process::ExitCode;

use xodus::api::displaycatalog::find_products_by_id;
use xodus::models::displaycatalog::Product;

fn desktop_packages(product: &Product) -> Result<Vec<serde_json::Value>, serde_json::Error> {
    let mut packages = Vec::new();
    for availability in &product.display_sku_availabilities {
        for package in &availability.sku.properties.packages {
            if package
                .platform_dependencies
                .iter()
                .any(|dependency| dependency.platform_name == "Windows.Desktop")
            {
                let metadata = serde_json::to_value(package)?;
                if !packages.contains(&metadata) {
                    packages.push(metadata);
                }
            }
        }
    }
    Ok(packages)
}

pub async fn run(client: &reqwest::Client, product_id: String, market: Option<String>) -> ExitCode {
    let response = match find_products_by_id(
        client,
        product_id.clone(),
        market.unwrap_or_else(|| "US".to_owned()),
        vec!["en-US".to_owned()],
    )
    .await
    {
        Ok(response) => response,
        Err(error) => {
            eprintln!("Failed to inspect public product metadata: {error}");
            return ExitCode::FAILURE;
        }
    };
    let packages = match desktop_packages(&response.product) {
        Ok(packages) => packages,
        Err(error) => {
            eprintln!("Failed to serialize package metadata: {error}");
            return ExitCode::FAILURE;
        }
    };
    if packages.is_empty() {
        eprintln!("No Windows.Desktop packages found for this product");
        return ExitCode::FAILURE;
    }
    match serde_json::to_string_pretty(&serde_json::json!({
        "ProductId": product_id,
        "Packages": packages
    })) {
        Ok(output) => {
            println!("{output}");
            ExitCode::SUCCESS
        }
        Err(error) => {
            eprintln!("Failed to serialize inspection output: {error}");
            ExitCode::FAILURE
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn desktop_inspection_deduplicates_skus_and_excludes_console_packages() {
        let desktop = serde_json::json!({
            "ContentId": "desktop",
            "PackageFormat": "EAppxBundle",
            "PlatformDependencies": [{"PlatformName": "Windows.Desktop"}]
        });
        let console = serde_json::json!({
            "ContentId": "console",
            "PackageFormat": "XVC",
            "PlatformDependencies": [{"PlatformName": "Windows.Xbox"}]
        });
        let sku = serde_json::json!({
            "Sku": {"Properties": {"Packages": [desktop, console]}},
            "Availabilities": []
        });
        let product: Product = serde_json::from_value(serde_json::json!({
            "DisplaySkuAvailabilities": [sku.clone(), sku]
        }))
        .unwrap();
        let packages = desktop_packages(&product).unwrap();
        assert_eq!(packages.len(), 1);
        assert_eq!(packages[0]["ContentId"], "desktop");
        assert_eq!(packages[0]["PackageFormat"], "EAppxBundle");
    }

    #[test]
    fn empty_catalog_has_no_desktop_packages() {
        let product = Product {
            display_sku_availabilities: vec![],
        };
        assert!(desktop_packages(&product).unwrap().is_empty());
    }
}
