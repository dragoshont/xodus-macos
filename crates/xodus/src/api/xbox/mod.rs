use crate::models::secrets::LegacyToken;
use crate::models::soap;
use crate::models::xbox::XstsResponse;

pub mod auth;
pub mod title;
pub use auth::{XboxAuthError, authenticate_xbox_user, get_xsts_auth_header, request_xsts_token};

pub async fn run(
    client: &reqwest::Client,
    dev_token: LegacyToken,
    legacy: LegacyToken,
    username: String,
    relying_party: &str,
) -> Result<XstsResponse, XboxAuthError> {
    if username.trim().is_empty() {
        return Err(XboxAuthError::InvalidCredentials);
    }
    let user_token = crate::api::live::exchange_user_token(
        client,
        legacy,
        username,
        dev_token,
        None,
        Some("Silent".to_string()),
        "{d6d5a677-0872-4ab0-9442-bb792fce85c5}".to_string(),
        &[(
            "user.auth.xboxlive.com".to_owned(),
            Some(soap::PolicyReference::mbi_ssl()),
        )],
    )
    .await
    .map_err(|_| XboxAuthError::ExchangeFailed)?;

    let user_token =
        crate::api::live::compact_ticket_from_outcome(user_token, "user.auth.xboxlive.com")
            .map_err(|_| XboxAuthError::InvalidResponse)?;
    let resp = authenticate_xbox_user(client, user_token).await?;
    get_xsts_auth_header(resp.clone())?;

    let response = request_xsts_token(client, resp.token, relying_party).await?;
    get_xsts_auth_header(response.clone())?;
    Ok(response)
}
