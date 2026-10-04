use crate::provider_credentials::ProviderCredentials;
use xodus::licensing::splicense::{DeviceKey, SPLicense};
use xodus::models::soap;
use xodus::tokens::TokenManager;

pub async fn get_license(
    client: &reqwest::Client,
    tokens: &TokenManager,
    content_id: String,
    market: String,
) -> std::result::Result<(DeviceKey, SPLicense), String> {
    uuid::Uuid::parse_str(&content_id).map_err(|_| "Invalid license content ID".to_owned())?;
    let credentials = ProviderCredentials::read(tokens)
        .await
        .map_err(|error| error.to_string())?;

    let ms_device_token = xodus::api::live::exchange_device_token(
        client,
        credentials.device.clone(),
        "{d6d5a677-0872-4ab0-9442-bb792fce85c5}".to_string(),
        "www.microsoft.com".to_owned(),
        Some(soap::PolicyReference::mbi_ssl()),
    )
    .await
    .map_err(|_| "Device license authentication failed".to_owned())?;
    credentials
        .verify_current(tokens)
        .await
        .map_err(|error| error.to_string())?;

    let user_token = xodus::api::live::exchange_user_token(
        client,
        credentials.user.clone(),
        credentials.account.username.clone(),
        credentials.device.clone(),
        None,
        Some("Silent".to_string()),
        "{d6d5a677-0872-4ab0-9442-bb792fce85c5}".to_string(),
        &[(
            "www.microsoft.com".to_owned(),
            Some(soap::PolicyReference::mbi_ssl()),
        )],
    )
    .await
    .map_err(|_| "User license authentication failed".to_owned())?;
    credentials
        .verify_current(tokens)
        .await
        .map_err(|error| error.to_string())?;

    let ms_device_token =
        xodus::api::live::compact_ticket_from_response(ms_device_token, "www.microsoft.com")
            .map_err(|_| "Device license ticket is invalid".to_owned())?;
    let user_token = xodus::api::live::compact_ticket_from_outcome(user_token, "www.microsoft.com")
        .map_err(|_| "User license ticket is invalid".to_owned())?;

    let (_content, game_license) = xodus::licensing::content::get_license_content(
        client,
        ms_device_token,
        user_token,
        credentials.account.puid.clone(),
        content_id,
        market,
    )
    .await
    .map_err(|err| match err {
        xodus::licensing::content::LicenseContentError::NotEntitled { .. } => {
            "The account is not entitled to this content".to_owned()
        }
        xodus::licensing::content::LicenseContentError::Provider(error) => error.to_string(),
        _ => "License service request failed or returned an invalid response".to_owned(),
    })?;
    credentials
        .verify_current(tokens)
        .await
        .map_err(|error| error.to_string())?;

    let game_splicense = SPLicense::parse_base64(&game_license.splicense_block)
        .map_err(|_| "Game license data is invalid".to_owned())?;

    let dev_license = credentials
        .device_license_block(tokens)
        .map_err(|error| error.to_string())?;
    let device_license = SPLicense::parse_base64(&dev_license)
        .map_err(|_| "Device license data is invalid".to_owned())?;
    let key = device_license
        .encrypted_device_key
        .ok_or_else(|| "Device license has no device key".to_owned())?
        .derive_device_key()
        .map_err(|error| error.to_string())?;
    credentials
        .verify_current(tokens)
        .await
        .map_err(|error| error.to_string())?;
    Ok((key, game_splicense))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[tokio::test]
    async fn license_missing_credentials_returns_error_without_network_or_panic() {
        let result = get_license(
            &reqwest::Client::new(),
            &TokenManager::with_memory(),
            "00000000-0000-0000-0000-000000000001".to_owned(),
            "US".to_owned(),
        )
        .await;
        assert!(matches!(result, Err(error) if error == "Device credentials are unavailable"));
    }

    #[tokio::test]
    async fn invalid_license_identity_is_rejected_before_credentials() {
        let result = get_license(
            &reqwest::Client::new(),
            &TokenManager::with_memory(),
            "private-value".to_owned(),
            "US".to_owned(),
        )
        .await;
        assert!(matches!(result, Err(error) if error == "Invalid license content ID"));
    }
}
