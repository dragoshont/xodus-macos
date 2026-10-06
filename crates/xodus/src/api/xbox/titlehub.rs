use crate::api::response::{PACKAGE_RESPONSE_LIMIT, ProviderResponseError, request_json};
use crate::api::xbox::{XboxAuthError, get_xsts_auth_header};
use crate::models::xbox::XstsResponse;

pub const RELYING_PARTY: &str = "http://xboxlive.com";

pub async fn recent(
    client: &reqwest::Client,
    token: XstsResponse,
    limit: u32,
) -> Result<serde_json::Value, XboxAuthError> {
    let request = recent_request(client, token, limit)?;
    request_json(request, PACKAGE_RESPONSE_LIMIT)
        .await?
        .require_success()
        .map_err(XboxAuthError::Provider)
}

fn recent_request(
    client: &reqwest::Client,
    token: XstsResponse,
    limit: u32,
) -> Result<reqwest::RequestBuilder, XboxAuthError> {
    if !(1..=100).contains(&limit) {
        return Err(XboxAuthError::Provider(
            ProviderResponseError::InvalidLimits,
        ));
    }
    let id = token
        .user_id()
        .ok_or(XboxAuthError::InvalidResponse)?
        .to_owned();
    let authorization = get_xsts_auth_header(token)?;
    let mut authorization = reqwest::header::HeaderValue::from_str(&authorization)
        .map_err(|_| XboxAuthError::InvalidResponse)?;
    authorization.set_sensitive(true);
    let mut url = url::Url::parse(&format!(
        "https://titlehub.xboxlive.com/users/xuid({id})/titles/titlehistory/decoration/image"
    ))
    .map_err(|_| XboxAuthError::InvalidResponse)?;
    url.query_pairs_mut()
        .append_pair("maxItems", &limit.to_string());
    Ok(client
        .get(url)
        .header("Authorization", authorization)
        .header("x-xbl-contract-version", "2")
        .header("x-xbl-client-name", "XboxApp")
        .header("x-xbl-client-type", "UWA")
        .header("x-xbl-client-version", "39.39.22001.0")
        .header("Accept-Language", "en-US"))
}

#[cfg(test)]
mod management_tests {
    use super::*;

    fn token(id: &str, extra: bool) -> XstsResponse {
        let claim = serde_json::json!({"uhs":"1","xid":id});
        serde_json::from_value(serde_json::json!({
            "Token":"fixture-not-token","NotAfter":"2099-01-01T00:00:00Z",
            "DisplayClaims":{"xui":if extra {vec![claim.clone(),claim]} else {vec![claim]}}
        }))
        .unwrap()
    }

    #[test]
    fn management_recent_request_has_exact_route_audience_and_headers_without_execution() {
        let client = reqwest::Client::builder()
            .redirect(reqwest::redirect::Policy::none())
            .build()
            .unwrap();
        let request = recent_request(&client, token("1", false), 100)
            .unwrap()
            .build()
            .unwrap();
        assert_eq!(RELYING_PARTY, "http://xboxlive.com");
        assert_eq!(request.method(), reqwest::Method::GET);
        assert_eq!(
            request.url().as_str(),
            "https://titlehub.xboxlive.com/users/xuid(1)/titles/titlehistory/decoration/image?maxItems=100"
        );
        assert_eq!(request.headers()["x-xbl-contract-version"], "2");
        assert_eq!(
            request.headers()["Authorization"],
            "XBL3.0 x=1;fixture-not-token"
        );
        assert!(request.headers()["Authorization"].is_sensitive());
        assert_eq!(request.headers()["x-xbl-client-name"], "XboxApp");
        assert_eq!(request.headers()["x-xbl-client-type"], "UWA");
        for invalid in ["", "0", "01", "+1", "1/other", "18446744073709551616"] {
            assert!(recent_request(&client, token(invalid, false), 1).is_err());
        }
        assert!(recent_request(&client, token("1", true), 1).is_err());
        assert!(recent_request(&client, token("1", false), 101).is_err());
    }
}
