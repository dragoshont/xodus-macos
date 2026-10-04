use crate::api::response::{AUTH_RESPONSE_LIMIT, ProviderResponseError, request_json};
use crate::models::xbox::{
    UserAuthProperties, UserAuthRequest, XstsPropertyBag, XstsRequest, XstsResponse,
};

#[derive(Debug, thiserror::Error)]
pub enum XboxAuthError {
    #[error("A valid signed-in account is required")]
    InvalidCredentials,
    #[error("Xbox authentication exchange failed")]
    ExchangeFailed,
    #[error("Xbox authentication returned an invalid or incomplete response")]
    InvalidResponse,
    #[error("{0}")]
    Provider(#[from] ProviderResponseError),
}

pub async fn authenticate_xbox_user(
    client: &reqwest::Client,
    rps_ticket: String,
) -> Result<XstsResponse, ProviderResponseError> {
    let body = UserAuthRequest {
        relying_party: "http://auth.xboxlive.com".to_string(),
        token_type: "JWT".to_string(),
        properties: UserAuthProperties {
            auth_method: "RPS".to_string(),
            site_name: "user.auth.xboxlive.com".to_string(),
            rps_ticket,
        },
    };

    request_json(
        client
            .post("https://user.auth.xboxlive.com/user/authenticate")
            .header("Content-Type", "application/json")
            .header("x-xbl-contract-version", "1")
            .json(&body),
        AUTH_RESPONSE_LIMIT,
    )
    .await?
    .require_success()
}

pub async fn request_xsts_token(
    client: &reqwest::Client,
    token: String,
    relying_party: &str,
) -> Result<XstsResponse, ProviderResponseError> {
    let body = XstsRequest {
        relying_party: Some(relying_party.to_string()),
        token_type: Some("JWT".to_string()),
        properties: XstsPropertyBag {
            user_tokens: Some(vec![token]),
            sandbox_id: Some("RETAIL".to_string()),
            delegation_token: None,
            service_token: None,
        },
    };

    request_json(
        client
            .post("https://xsts.auth.xboxlive.com/xsts/authorize")
            .header("Content-Type", "application/json")
            .header("x-xbl-contract-version", "1")
            .json(&body),
        AUTH_RESPONSE_LIMIT,
    )
    .await?
    .require_success()
}

pub fn get_xsts_auth_header(xsts: XstsResponse) -> Result<String, XboxAuthError> {
    let uhs = xsts.user_hash().ok_or(XboxAuthError::InvalidResponse)?;
    if uhs.is_empty()
        || !uhs.bytes().all(|byte| byte.is_ascii_digit())
        || xsts.not_after <= chrono::Utc::now()
        || xsts.token.is_empty()
        || xsts.token.len() > u16::MAX as usize
        || !xsts.token.bytes().all(|byte| byte.is_ascii_graphic())
    {
        return Err(XboxAuthError::InvalidResponse);
    }
    Ok(format!("XBL3.0 x={uhs};{}", xsts.token))
}

#[cfg(test)]
mod management_tests {
    use super::*;

    fn response(claims: serde_json::Value, token: &str, expiry: &str) -> XstsResponse {
        serde_json::from_value(serde_json::json!({
            "NotAfter": expiry,
            "Token": token,
            "DisplayClaims": { "xui": claims }
        }))
        .unwrap()
    }

    #[test]
    fn management_xsts_header_preserves_valid_wire_format() {
        let response = response(
            serde_json::json!([{"uhs": "12345"}]),
            "fixture-only",
            "2099-01-01T00:00:00Z",
        );
        assert_eq!(
            get_xsts_auth_header(response).unwrap(),
            "XBL3.0 x=12345;fixture-only"
        );
    }

    #[test]
    fn management_xsts_header_rejects_missing_ambiguous_and_invalid_claims() {
        for claims in [
            serde_json::json!([]),
            serde_json::json!([{"uhs": "1"}, {"uhs": "2"}]),
            serde_json::json!([{"uhs": ""}]),
            serde_json::json!([{"uhs": "1;hidden"}]),
        ] {
            assert!(
                get_xsts_auth_header(response(claims, "fixture-only", "2099-01-01T00:00:00Z"))
                    .is_err()
            );
        }
    }

    #[test]
    fn management_xsts_header_rejects_expired_empty_and_injected_tokens() {
        for (ticket, expiry) in [
            ("fixture-only", "2000-01-01T00:00:00Z"),
            ("", "2099-01-01T00:00:00Z"),
            ("hidden\r\nheader", "2099-01-01T00:00:00Z"),
        ] {
            let error = get_xsts_auth_header(response(
                serde_json::json!([{"uhs": "12345"}]),
                ticket,
                expiry,
            ))
            .unwrap_err();
            assert_eq!(
                error.to_string(),
                "Xbox authentication returned an invalid or incomplete response"
            );
            assert!(!format!("{error:?}").contains("hidden"));
        }
    }
}
