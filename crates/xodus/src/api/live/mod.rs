use zerocopy::transmute;

use crate::licensing::splicense::{ClepHmacState, HmacBinarySecret};
use crate::models::devicecredential::{DeviceAddRequest, DeviceAddResponse};
use crate::models::live::ExchangeUserTokenOutcome;
use crate::models::secrets::{LegacyToken, Token};
use crate::models::soap;

mod rst;
mod utils;

pub const XML_HEADER: &str = r#"<?xml version="1.0" encoding="UTF-8"?>"#;

pub async fn login_device_credential(
    client: &reqwest::Client,
    data: DeviceAddRequest,
) -> Result<DeviceAddResponse, rst::RSTError> {
    let data = quick_xml::se::to_string(&data)?;

    let response = client
        .post("https://login.live.com/ppsecure/deviceaddcredential.srf")
        .header("User-Agent", "MSAWindows/55 (OS 10.0.26100.0.0 ge_release; IDK 10.0.26100.5074 ge_release; Cfg 16.000.29325.00; Test 0)")
        .header("Content-Type", "application/soap+xml")
        .header("Host", "login.live.com")
        .body(data)
        .send()
        .await?.error_for_status()?;
    let text = rst::request::bounded_response_text(response).await?;
    let resp: DeviceAddResponse = quick_xml::de::from_str(&text)?;
    Ok(resp)
}

pub async fn authenticate_device(
    client: &reqwest::Client,
    username: String,
    private_key: rsa::RsaPrivateKey,
) -> Result<soap::Envelope, rst::RSTError> {
    let request = rst::RSTRequestBuilder::new()
        .username(soap::UsernameToken::devicetoken(username))
        .signature(rst::RSTSignature::Rsa(Box::new(private_key)))
        .scope_policy("http://Passport.NET/tb", None)
        .build()?;

    request.request(client).await
}

pub async fn exchange_device_token(
    client: &reqwest::Client,
    token: LegacyToken,
    hosting_app: String,
    scope: String,
    policy: Option<soap::PolicyReference>,
) -> Result<soap::RequestSecurityTokenResponse, rst::RSTError> {
    let hmac_secret = device_hmac_secret(&token)?;

    let request = rst::RSTRequestBuilder::new()
        .sso_flags("SsoRestr")
        .hosting_app(&hosting_app)
        .device_token(token)
        .signature(rst::RSTSignature::Hmac {
            clep_secret: &*hmac_secret,
            tpm_secret: &[],
        })
        .scope_policy(&scope, policy)
        .build()?;

    let envelope = request.request(client).await?;

    single_device_response(envelope.body.body)
}

fn device_hmac_secret(token: &LegacyToken) -> Result<HmacBinarySecret, rst::RSTError> {
    let secret = soap::decode_xml_base64(
        token
            .binary_secret
            .as_ref()
            .ok_or(rst::RSTError::InvalidEncryptedPayload)?,
    )
    .map_err(|_| rst::RSTError::InvalidEncryptedPayload)?;
    let secret: [u8; 4096] = secret
        .try_into()
        .map_err(|_| rst::RSTError::InvalidEncryptedPayload)?;
    let secret: ClepHmacState = transmute!(secret);
    secret
        .try_get_hmac_state()
        .map_err(|_| rst::RSTError::InvalidEncryptedPayload)
}

pub(crate) fn single_device_response(
    body: soap::BodyContent,
) -> Result<soap::RequestSecurityTokenResponse, rst::RSTError> {
    match body {
        soap::BodyContent::RequestSecurityTokenResponse(res) => Ok(*res),
        soap::BodyContent::RequestSecurityTokenResponseCollection(mut collection)
            if collection.security_tokens.len() == 1 =>
        {
            collection
                .security_tokens
                .pop()
                .ok_or(rst::RSTError::InvalidTokenResponse)
        }
        _ => Err(rst::RSTError::InvalidTokenResponse),
    }
}

pub fn compact_ticket_from_response(
    response: soap::RequestSecurityTokenResponse,
    audience: &str,
) -> Result<String, rst::RSTError> {
    if response.applies_to.endpoint_reference.address != audience
        || !matches!(
            response.token_type.as_str(),
            "urn:passport:compact" | "urn:passport:delegationcompact"
        )
    {
        return Err(rst::RSTError::InvalidTokenResponse);
    }
    let Token::Compact(ticket) =
        Token::from_response_checked(response).map_err(|_| rst::RSTError::InvalidTokenResponse)?
    else {
        return Err(rst::RSTError::InvalidTokenResponse);
    };
    if ticket.len() > u16::MAX as usize || !ticket.bytes().all(|byte| byte.is_ascii_graphic()) {
        return Err(rst::RSTError::InvalidTokenResponse);
    }
    Ok(ticket)
}

pub fn compact_ticket_from_outcome(
    outcome: ExchangeUserTokenOutcome,
    audience: &str,
) -> Result<String, rst::RSTError> {
    let ExchangeUserTokenOutcome::Issued(body) = outcome else {
        return Err(rst::RSTError::InvalidTokenResponse);
    };
    compact_ticket_from_response(single_device_response(body)?, audience)
}

// Each parameter maps directly to a distinct SOAP request field; grouping them
// into a params struct would just move the sprawl rather than reduce it.
#[allow(clippy::too_many_arguments)]
pub async fn exchange_user_token(
    client: &reqwest::Client,
    user_token: LegacyToken,
    username: String,
    device_token: LegacyToken,
    inline_token: Option<String>,
    inline_ux: Option<String>,
    hosting_app: String,
    scope_policies: &[(String, Option<soap::PolicyReference>)],
) -> Result<ExchangeUserTokenOutcome, rst::RSTError> {
    let hmac_secret = device_hmac_secret(&device_token)?;

    let mut builder = rst::RSTRequestBuilder::new()
        .username(soap::UsernameToken::user_hint(username))
        .device_token(device_token)
        .user_token(Token::Legacy(user_token))
        .hosting_app(&hosting_app)
        .sso_flags("SsoRestr")
        .license_signature_key_version(None)
        .signature(rst::RSTSignature::Hmac {
            clep_secret: &*hmac_secret,
            tpm_secret: &[],
        });

    if let Some(ux) = inline_ux.as_deref() {
        builder = builder.inline_ux(ux);
    }
    if let Some(ft) = inline_token.as_deref() {
        builder = builder.inline_ft(ft);
    }
    for (scope, policy) in scope_policies {
        builder = builder.scope_policy(scope, policy.clone());
    }

    let request = builder.build()?;
    let envelope = request.request(client).await?;

    Ok(match envelope.body.body {
        soap::BodyContent::Fault(_) => ExchangeUserTokenOutcome::Fault(envelope.header.pp),
        body => ExchangeUserTokenOutcome::Issued(body),
    })
}

#[cfg(test)]
mod test {
    use crate::api::live::exchange_device_token;
    use crate::models::secrets::Token;
    use crate::models::soap;
    use crate::tokens::TokenManager;
    use crate::tokens::device::ensure_device_credentials;

    fn synthetic_hmac_token(secret: Option<String>) -> crate::models::secrets::LegacyToken {
        crate::models::secrets::LegacyToken {
            key_name: Some(crate::tokens::PASSPORT_STS.to_owned()),
            token: "synthetic-not-a-ticket".to_owned(),
            binary_secret: secret,
            tpm_key: None,
            lifetime: soap::Timestamp {
                id: None,
                created: "2000-01-01T00:00:00Z".to_owned(),
                expires: "2099-01-01T00:00:00Z".to_owned(),
            },
        }
    }

    #[test]
    fn management_xml_base64_both_exchange_consumers_share_identical_checked_hmac_secret() {
        use base64::prelude::*;

        let mut bytes = [0; 4096];
        bytes[..4].copy_from_slice(&4u32.to_le_bytes());
        let encoded = BASE64_STANDARD.encode(bytes);
        let wrapped = encoded
            .as_bytes()
            .chunks(4)
            .map(|chunk| std::str::from_utf8(chunk).unwrap())
            .collect::<Vec<_>>()
            .join(" \t\r\n");
        let original = super::device_hmac_secret(&synthetic_hmac_token(Some(encoded))).unwrap();
        let normalized = super::device_hmac_secret(&synthetic_hmac_token(Some(wrapped))).unwrap();
        assert!(*original == *normalized);
    }

    #[test]
    fn management_xml_base64_exchange_secret_rejects_missing_invalid_and_unsupported_proof() {
        use base64::prelude::*;

        for encoded in [
            None,
            Some("AA\u{00a0}==".to_owned()),
            Some("AA\u{000b}==".to_owned()),
            Some("AA\u{000c}==".to_owned()),
            Some("AB==".to_owned()),
            Some(BASE64_STANDARD.encode([4; 4095])),
            Some(BASE64_STANDARD.encode([0; 4096])),
            Some(" ".repeat(soap::MAX_XML_RESPONSE_BYTES + 1)),
        ] {
            assert!(matches!(
                super::device_hmac_secret(&synthetic_hmac_token(encoded)),
                Err(super::rst::RSTError::InvalidEncryptedPayload)
            ));
        }
    }

    fn compact_response(
        audience: &str,
        ticket: Option<&str>,
        expiry: &str,
    ) -> soap::RequestSecurityTokenResponse {
        soap::RequestSecurityTokenResponse {
            token_type: "urn:passport:compact".to_owned(),
            applies_to: soap::AppliesTo {
                endpoint_reference: soap::EndpointReference {
                    address: audience.to_owned(),
                },
            },
            lifetime: soap::Timestamp {
                id: None,
                created: "2000-01-01T00:00:00Z".to_owned(),
                expires: expiry.to_owned(),
            },
            requested_security_token: soap::RequestedSecurityToken {
                encrypted_data: None,
                binary_security_token: ticket.map(|ticket| soap::BinarySecurityTokenRes {
                    id: "fixture-only".to_owned(),
                    value: ticket.to_owned(),
                    value_type: None,
                }),
            },
            requested_proof_token: None,
        }
    }

    #[test]
    fn management_single_compact_ticket_accepts_matching_complete_future_proof() {
        let response = compact_response(
            "fixture.invalid",
            Some("fixture-only"),
            "2099-01-01T00:00:00Z",
        );
        assert_eq!(
            super::compact_ticket_from_response(response.clone(), "fixture.invalid").unwrap(),
            "fixture-only"
        );
        for body in [
            soap::BodyContent::RequestSecurityTokenResponse(Box::new(response.clone())),
            soap::BodyContent::RequestSecurityTokenResponseCollection(
                soap::RequestSecurityTokenResponseCollection {
                    security_tokens: vec![response.clone()],
                },
            ),
        ] {
            assert_eq!(
                super::compact_ticket_from_outcome(
                    crate::models::live::ExchangeUserTokenOutcome::Issued(body),
                    "fixture.invalid",
                )
                .unwrap(),
                "fixture-only"
            );
        }
        let mut delegated = response;
        delegated.token_type = "urn:passport:delegationcompact".to_owned();
        assert_eq!(
            super::compact_ticket_from_response(delegated, "fixture.invalid").unwrap(),
            "d=fixture-only"
        );
    }

    #[test]
    fn management_single_compact_ticket_rejects_fault_and_empty_or_multiple_results() {
        use crate::models::live::ExchangeUserTokenOutcome;
        assert!(
            super::compact_ticket_from_outcome(
                ExchangeUserTokenOutcome::Fault(None),
                "fixture.invalid"
            )
            .is_err()
        );
        for count in [0, 2] {
            let body = soap::BodyContent::RequestSecurityTokenResponseCollection(
                soap::RequestSecurityTokenResponseCollection {
                    security_tokens: vec![
                        compact_response(
                            "fixture.invalid",
                            Some("fixture-only"),
                            "2099-01-01T00:00:00Z"
                        );
                        count
                    ],
                },
            );
            assert!(
                super::compact_ticket_from_outcome(
                    ExchangeUserTokenOutcome::Issued(body),
                    "fixture.invalid"
                )
                .is_err()
            );
        }
    }

    #[test]
    fn management_single_compact_ticket_rejects_mismatch_missing_expired_and_injected_proof() {
        for (audience, ticket, expiry) in [
            (
                "different.invalid",
                Some("fixture-only"),
                "2099-01-01T00:00:00Z",
            ),
            ("fixture.invalid", None, "2099-01-01T00:00:00Z"),
            ("fixture.invalid", Some(""), "2099-01-01T00:00:00Z"),
            (
                "fixture.invalid",
                Some("fixture-only"),
                "2000-01-01T00:00:00Z",
            ),
            ("fixture.invalid", Some("fixture-only"), "invalid-time"),
            (
                "fixture.invalid",
                Some("hidden\r\nheader"),
                "2099-01-01T00:00:00Z",
            ),
        ] {
            let error = super::compact_ticket_from_response(
                compact_response(audience, ticket, expiry),
                "fixture.invalid",
            )
            .unwrap_err();
            assert_eq!(
                error.to_string(),
                "Response contains an unexpected token result"
            );
        }
        let mut response = compact_response(
            "fixture.invalid",
            Some("fixture-only"),
            "2099-01-01T00:00:00Z",
        );
        response.token_type = "urn:passport:unknown".to_owned();
        assert!(super::compact_ticket_from_response(response, "fixture.invalid").is_err());
    }

    #[test]
    fn management_device_response_rejects_empty_and_unexpected_body_without_panicking() {
        let empty = soap::BodyContent::RequestSecurityTokenResponseCollection(
            soap::RequestSecurityTokenResponseCollection {
                security_tokens: vec![],
            },
        );
        assert!(super::single_device_response(empty).is_err());
    }

    #[tokio::test]
    async fn test_get_xbox_live_dev_token() {
        let client = reqwest::Client::new();

        let mgr = TokenManager::with_memory();
        ensure_device_credentials(&client, &mgr).await.unwrap();

        let token: Token = mgr.get_device_sts_token().unwrap();
        let Token::Legacy(token) = token else {
            todo!("no a LegacyToken");
        };
        let resp = exchange_device_token(
            &client,
            token,
            "{28C08266-F973-4AE6-FFE4-409B249F138F}".to_string(),
            "scope=service::user.auth.xboxlive.com::MBI_SSL&api-version=2.0".to_owned(),
            Some(soap::PolicyReference::token_broker()),
        )
        .await
        .unwrap();

        let ms_device_token: Token = resp.into();
        let Token::Compact(ms_device_token) = ms_device_token else {
            todo!("Unsupported token");
        };

        println!("{}", ms_device_token);
    }
}
