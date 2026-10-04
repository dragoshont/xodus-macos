use std::collections::HashMap;

use base64::prelude::*;
use xal::cvlib::CorrelationVector;
//use xal::extensions::CorrelationVectorReqwestBuilder;

use crate::api::response::{LICENSE_RESPONSE_LIMIT, ProviderResponseError, request_json};
use crate::licensing::utils;
use crate::models::devicecredential::License;
use crate::models::licensing::{
    DeviceContext, LicenseContent, LicenseContentRequest, LicenseContentResponse,
    LicenseTokenRequest, LicenseTokenResponse, LicenseUserIdentity,
};

#[derive(Debug, thiserror::Error)]
pub enum LicenseContentError {
    #[error("request error: {0}")]
    Request(#[from] reqwest::Error),

    /// The account has no entitlement for the requested content (not owned, or
    /// not covered by the account's current subscription tier).
    #[error("not entitled to this content: {description}")]
    NotEntitled { description: String },

    #[error("license service returned an invalid or incomplete response")]
    InvalidResponse,

    #[error("{0}")]
    Provider(#[from] ProviderResponseError),
}

// we might need a bump in xal-rs concerning reqwest,
// that might block us from using the correlationvector extension
pub async fn get_license_content(
    client: &reqwest::Client,
    device_ms_token: String,
    user_ms_token: String,
    ticket_reference: String,
    content_id: String,
    market: String,
) -> Result<(LicenseContent, License), LicenseContentError> {
    let cv = CorrelationVector::new();
    let response = request_json::<LicenseContentResponse>(
        client
        .post("https://licensing.mp.microsoft.com/v7.0/licenses/content")
        .header("from", "XboxLicenseManager")
        .header("Authorization", device_ms_token)
        .header("user-agent", "XboxLm-PC/Microsoft.GamingServices_32.107.4002.0_x64__8wekyb3d8bbwe")
        .header("MS-CV", cv.to_string())
        .json(&LicenseContentRequest {
            content_id,
            market,
            client_challenge: "PD94bWwgdmVyc2lvbj0iMS4wIiBlbmNvZGluZz0idXRmLTgiID8+PENsaWVudENoYWxsZW5nZSB4bWxuczp4c2k9Imh0dHA6Ly93d3cudzMub3JnLzIwMDEvWE1MU2NoZW1hLWluc3RhbmNlIiB4bWxuczp4c2Q9Imh0dHA6Ly93d3cudzMub3JnLzIwMDEvWE1MU2NoZW1hIiB4bWxucz0iaHR0cDovL3NjaGVtYXMubWljcm9zb2Z0LmNvbS9vbmVzdG9yZS9zZWN1cml0eS9ta21zL0xpY1JlcS92MSIgVmVyc2lvbj0iMiI+PExpY2Vuc2VQcm90b2NvbFZlcnNpb24+NTwvTGljZW5zZVByb3RvY29sVmVyc2lvbj48U2lnbmluZ0tleVZlcnNpb24+MTwvU2lnbmluZ0tleVZlcnNpb24+PENsaWVudFZlcnNpb24+MjwvQ2xpZW50VmVyc2lvbj48L0NsaWVudENoYWxsZW5nZT4=".into(),
            concurrency_mode: "Rude".into(),
            license_version: 4,
            need_key: true,
            key_only: true,
            device_context: DeviceContext::default(),
            users: HashMap::from_iter(
                [(utils::generate_suid(),
                vec![LicenseUserIdentity {
                    identity_type: "Msa".to_string(),
                    identity_value: user_ms_token,
                    local_ticket_reference: ticket_reference,
                }])],
            ),
        }),
        LICENSE_RESPONSE_LIMIT,
    )
    .await?;

    let (status, body) = response.into_parts();
    let content = match body {
        LicenseContentResponse::Success { license } => {
            if !status.is_success() {
                return Err(ProviderResponseError::HttpRejected {
                    status: status.as_u16(),
                }
                .into());
            }
            license
        }
        LicenseContentResponse::SatisfactionFailure {
            satisfaction_failure,
        } => {
            return Err(LicenseContentError::NotEntitled {
                description: satisfaction_failure.description,
            });
        }
    };
    let license = decode_content_license(&content)?;
    Ok((content, license))
}

fn decode_content_license(content: &LicenseContent) -> Result<License, LicenseContentError> {
    let [key] = content.keys.as_slice() else {
        return Err(LicenseContentError::InvalidResponse);
    };
    let license = BASE64_STANDARD
        .decode(&key.value)
        .map_err(|_| LicenseContentError::InvalidResponse)?;
    let text = std::str::from_utf8(&license).map_err(|_| LicenseContentError::InvalidResponse)?;
    let license = quick_xml::de::from_str::<License>(text)
        .map_err(|_| LicenseContentError::InvalidResponse)?;
    if license.splicense_block.is_empty() {
        return Err(LicenseContentError::InvalidResponse);
    }
    Ok(license)
}

#[cfg(test)]
mod management_tests {
    use super::*;
    use crate::models::licensing::LicenseKeys;

    fn content(values: &[String]) -> LicenseContent {
        LicenseContent {
            keys: values
                .iter()
                .map(|value| LicenseKeys {
                    value: value.clone(),
                })
                .collect(),
            leases: vec![],
        }
    }

    #[test]
    fn management_content_license_rejects_absent_or_ambiguous_keys() {
        for values in [vec![], vec!["fixture-only".to_owned(); 2]] {
            assert!(matches!(
                decode_content_license(&content(&values)),
                Err(LicenseContentError::InvalidResponse)
            ));
        }
    }

    #[test]
    fn management_content_license_preserves_one_well_formed_response() {
        let xml = r#"<License><SPLicenseBlock>fixture-only</SPLicenseBlock><LicenseInfo Type="Full"/><Binding Binding_Type="fixture-only"/></License>"#;
        let license = decode_content_license(&content(&[BASE64_STANDARD.encode(xml)])).unwrap();
        assert_eq!(license.splicense_block, "fixture-only");
        let empty = xml.replace(">fixture-only</SPLicenseBlock>", "></SPLicenseBlock>");
        assert!(decode_content_license(&content(&[BASE64_STANDARD.encode(empty)])).is_err());
    }

    #[test]
    fn management_content_license_rejects_bad_base64_utf8_and_xml_without_panicking() {
        for value in [
            "hidden!not-base64".to_owned(),
            BASE64_STANDARD.encode([0xff]),
            BASE64_STANDARD.encode("<broken>"),
            BASE64_STANDARD.encode("<License/>"),
        ] {
            let error = decode_content_license(&content(&[value])).unwrap_err();
            assert_eq!(
                error.to_string(),
                "license service returned an invalid or incomplete response"
            );
            assert!(!format!("{error:?}").contains("hidden"));
        }
    }
}

pub async fn get_license_token(
    client: &reqwest::Client,
    device_ms_token: String,
    user_ms_token: String,
    ticket_reference: String,
    parent_product_id: String,
    products: Vec<String>,
    custom_developer_string: String,
) -> Result<String, Box<dyn std::error::Error>> {
    let token_resp: LicenseTokenResponse = request_json(
        client
            .post("https://licensing.mp.microsoft.com/v8.0/licenseToken")
            .header("from", "XboxLicenseManager")
            .header("Authorization", device_ms_token)
            .header(
                "user-agent",
                "XboxLm-PC/Microsoft.GamingServices_32.107.4002.0_x64__8wekyb3d8bbwe",
            )
            .json(&LicenseTokenRequest {
                parent_product_id,
                enforce_sellable_by: true,
                related_product_ids: products,
                custom_developer_string,
                beneficiaries: vec![LicenseUserIdentity {
                    identity_type: "Msa".to_string(),
                    identity_value: user_ms_token,
                    local_ticket_reference: ticket_reference,
                }],
            }),
        LICENSE_RESPONSE_LIMIT,
    )
    .await?
    .require_success()?;
    if token_resp.license_token.trim().is_empty() {
        return Err(Box::new(LicenseContentError::InvalidResponse));
    }

    Ok(token_resp.license_token)
}
