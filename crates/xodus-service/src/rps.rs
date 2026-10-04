use std::time::Duration;

use xodus::models::live::ExchangeUserTokenOutcome;
use xodus::models::secrets::{
    LegacyToken, Token, device_token_structurally_valid, legacy_token_valid,
};
use xodus::models::soap;
use xodus::models::xgameruntime::xuser::{MSATokenRequest, MSATokenResponse};
use xodus::tokens::{PASSPORT_STS, TokenManager};

use crate::simple_context::SimpleContext;

const DEVICE_SCOPE: &str = "scope=service::user.auth.xboxlive.com::MBI_SSL";
const PASSPORT_REQUEST: &str = "http://Passport.NET/tb";
const IO_DEADLINE: Duration = Duration::from_secs(2);
const EXCHANGE_DEADLINE: Duration = Duration::from_secs(30);
static IO_PERMITS: tokio::sync::Semaphore = tokio::sync::Semaphore::const_new(4);

#[derive(Debug, PartialEq, Eq)]
pub enum RpsError {
    InvalidRequest,
    ConsentRequired,
    AuthenticationRequired,
    CredentialStoreUnavailable,
    InvalidResponse,
    ExchangeFailed,
    Deadline,
    ProfileChanged,
}

impl std::fmt::Display for RpsError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter.write_str(match self {
            Self::InvalidRequest => "Runtime MSA request is invalid",
            Self::ConsentRequired => "Interactive consent must be initiated in the native launcher",
            Self::AuthenticationRequired => "A valid signed-in credential profile is required",
            Self::CredentialStoreUnavailable => {
                "The credential store could not complete the request"
            }
            Self::InvalidResponse => {
                "The MSA provider returned an invalid or incomplete ticket result"
            }
            Self::ExchangeFailed => "The MSA ticket exchange failed",
            Self::Deadline => "The bounded MSA ticket request deadline expired",
            Self::ProfileChanged => "The account profile changed during ticket issuance",
        })
    }
}

impl std::error::Error for RpsError {}

struct Credentials {
    user: LegacyToken,
    device: LegacyToken,
    username: String,
    stamp: Option<ProfileStamp>,
}

struct ProfileStamp {
    flow_id: String,
    user_ticket: String,
    device_ticket: String,
}

fn credential_error(error: xodus::tokens::store::TokenStoreError) -> RpsError {
    match error {
        xodus::tokens::store::TokenStoreError::NotFound
        | xodus::tokens::store::TokenStoreError::InvalidCredential => {
            RpsError::AuthenticationRequired
        }
        _ => RpsError::CredentialStoreUnavailable,
    }
}

fn credentials(
    tokens: &TokenManager,
    management: bool,
    legacy_device: Option<LegacyToken>,
) -> Result<Credentials, RpsError> {
    if management {
        if !tokens.is_management_profile() {
            return Err(RpsError::AuthenticationRequired);
        }
        let mut session = tokens
            .get_management_store_session()
            .map_err(|_| RpsError::CredentialStoreUnavailable)?
            .filter(|session| session.valid())
            .ok_or(RpsError::AuthenticationRequired)?;
        let Some(Token::Legacy(user)) = session.tokens.remove(PASSPORT_STS) else {
            return Err(RpsError::AuthenticationRequired);
        };
        let stamp = ProfileStamp {
            flow_id: session.flow_id,
            user_ticket: user.token.clone(),
            device_ticket: session.device_token.token.clone(),
        };
        Ok(Credentials {
            user,
            device: session.device_token,
            username: session.user.username,
            stamp: Some(stamp),
        })
    } else {
        let Token::Legacy(user) = tokens.get_user_sts_token().map_err(credential_error)? else {
            return Err(RpsError::AuthenticationRequired);
        };
        let device = legacy_device.ok_or(RpsError::AuthenticationRequired)?;
        let username = tokens.get_user().map_err(credential_error)?.username;
        if !legacy_token_valid(&user)
            || !legacy_token_valid(&device)
            || !device_token_structurally_valid(&device)
            || username.trim().is_empty()
        {
            return Err(RpsError::AuthenticationRequired);
        }
        Ok(Credentials {
            user,
            device,
            username,
            stamp: None,
        })
    }
}

async fn credential_io<T: Send + 'static>(
    read: impl FnOnce() -> Result<T, RpsError> + Send + 'static,
) -> Result<T, RpsError> {
    let deadline = tokio::time::Instant::now() + IO_DEADLINE;
    let permit = tokio::time::timeout_at(deadline, IO_PERMITS.acquire())
        .await
        .map_err(|_| RpsError::Deadline)?
        .map_err(|_| RpsError::CredentialStoreUnavailable)?;
    let work = tokio::task::spawn_blocking(move || {
        let _permit = permit;
        read()
    });
    tokio::time::timeout_at(deadline, work)
        .await
        .map_err(|_| RpsError::Deadline)?
        .map_err(|_| RpsError::CredentialStoreUnavailable)?
}

fn request_scope(request: &MSATokenRequest) -> Result<String, RpsError> {
    if request.client_id.len() != 16
        || !request
            .client_id
            .bytes()
            .all(|byte| byte.is_ascii_hexdigit())
    {
        return Err(RpsError::InvalidRequest);
    }
    let scope = if request.msa_full_trust {
        "service::user.auth.xboxlive.com::MBI_SSL"
    } else {
        "xboxlive.signin"
    };
    Ok(format!(
        "scope={scope}&api-version=2.0&clientid={}",
        request.client_id
    ))
}

fn compact(
    response: soap::RequestSecurityTokenResponse,
    audience: &str,
) -> Result<(String, i64), RpsError> {
    if response.applies_to.endpoint_reference.address != audience
        || response.requested_security_token.encrypted_data.is_some()
    {
        return Err(RpsError::InvalidResponse);
    }
    let expiry = chrono::DateTime::parse_from_rfc3339(&response.lifetime.expires)
        .map_err(|_| RpsError::InvalidResponse)?
        .timestamp();
    let Token::Compact(ticket) =
        Token::from_response_checked(response).map_err(|_| RpsError::InvalidResponse)?
    else {
        return Err(RpsError::InvalidResponse);
    };
    if ticket.len() > 16 * 1024 || !ticket.bytes().all(|byte| byte.is_ascii_graphic()) {
        return Err(RpsError::InvalidResponse);
    }
    Ok((ticket, expiry))
}

fn user_response(
    outcome: ExchangeUserTokenOutcome,
    audience: &str,
) -> Result<(String, i64, LegacyToken), RpsError> {
    if matches!(outcome, ExchangeUserTokenOutcome::Fault(_)) {
        return Err(RpsError::ConsentRequired);
    }
    let ExchangeUserTokenOutcome::Issued(
        soap::BodyContent::RequestSecurityTokenResponseCollection(collection),
    ) = outcome
    else {
        return Err(RpsError::InvalidResponse);
    };
    if collection.security_tokens.len() != 2 {
        return Err(RpsError::InvalidResponse);
    }
    let mut issued = None;
    let mut refreshed = None;
    for response in collection.security_tokens {
        let address = response.applies_to.endpoint_reference.address.as_str();
        if address == audience && issued.is_none() {
            issued = Some(compact(response, audience)?);
        } else if address == PASSPORT_REQUEST && refreshed.is_none() {
            let Token::Legacy(token) =
                Token::from_response_checked(response).map_err(|_| RpsError::InvalidResponse)?
            else {
                return Err(RpsError::InvalidResponse);
            };
            if !legacy_token_valid(&token) {
                return Err(RpsError::InvalidResponse);
            }
            refreshed = Some(token);
        } else {
            return Err(RpsError::InvalidResponse);
        }
    }
    let (ticket, expiry) = issued.ok_or(RpsError::InvalidResponse)?;
    Ok((ticket, expiry, refreshed.ok_or(RpsError::InvalidResponse)?))
}

fn current_profile(tokens: &TokenManager, stamp: ProfileStamp) -> Result<(), RpsError> {
    let session = tokens
        .get_management_store_session()
        .map_err(|_| RpsError::CredentialStoreUnavailable)?
        .filter(|session| session.valid())
        .ok_or(RpsError::ProfileChanged)?;
    if session.flow_id != stamp.flow_id
        || session.device_token.token != stamp.device_ticket
        || !matches!(session.tokens.get(PASSPORT_STS),
            Some(Token::Legacy(token)) if token.token == stamp.user_ticket)
    {
        return Err(RpsError::ProfileChanged);
    }
    Ok(())
}

pub async fn exchange(
    context: &SimpleContext,
    request: MSATokenRequest,
) -> Result<MSATokenResponse, RpsError> {
    let scope = request_scope(&request)?;
    let tokens = context.tokens().clone();
    let device = context.device_token.clone();
    let management = context.management_profile;
    let credentials = credential_io(move || credentials(&tokens, management, device)).await?;
    tokio::time::timeout(EXCHANGE_DEADLINE, async {
        let device_response = xodus::api::live::exchange_device_token(
            &context.client,
            credentials.device.clone(),
            "{28C08266-F973-4AE6-FFE4-409B249F138F}".to_owned(),
            DEVICE_SCOPE.to_owned(),
            Some(soap::PolicyReference::token_broker()),
        )
        .await
        .map_err(|_| RpsError::ExchangeFailed)?;
        let (device_rps, device_expiry) = compact(device_response, DEVICE_SCOPE)?;
        let outcome = xodus::api::live::exchange_user_token(
            &context.client,
            credentials.user,
            credentials.username,
            credentials.device,
            None,
            Some("Silent".to_owned()),
            request.client_id,
            &[
                (scope.clone(), Some(soap::PolicyReference::token_broker())),
                (PASSPORT_REQUEST.to_owned(), None),
            ],
        )
        .await
        .map_err(|_| RpsError::ExchangeFailed)?;
        let (token, expiry, refreshed) = user_response(outcome, &scope)?;
        let tokens = context.tokens().clone();
        if let Some(stamp) = credentials.stamp {
            credential_io(move || current_profile(&tokens, stamp)).await?;
            // Management consent owns atomic credential writes; this read-only bridge cannot rotate them.
        } else {
            credential_io(move || {
                tokens
                    .save_user_token(PASSPORT_STS.to_owned(), Token::Legacy(refreshed))
                    .map_err(|_| RpsError::CredentialStoreUnavailable)
            })
            .await?;
        }
        if expiry <= chrono::Utc::now().timestamp()
            || device_expiry <= chrono::Utc::now().timestamp()
        {
            return Err(RpsError::InvalidResponse);
        }
        Ok(MSATokenResponse {
            token,
            expiry,
            device_rps,
            device_expiry,
        })
    })
    .await
    .map_err(|_| RpsError::Deadline)?
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::Arc;
    use xodus::models::soap::{AppliesTo, EndpointReference, RequestedSecurityToken, Timestamp};

    fn request() -> MSATokenRequest {
        MSATokenRequest {
            client_id: "000000004424da1f".to_owned(),
            allow_ui: false,
            msa_full_trust: true,
        }
    }

    fn response(
        audience: &str,
        ticket: Option<&str>,
        expiry: &str,
    ) -> soap::RequestSecurityTokenResponse {
        soap::RequestSecurityTokenResponse {
            token_type: "urn:passport:compact".to_owned(),
            applies_to: AppliesTo {
                endpoint_reference: EndpointReference {
                    address: audience.to_owned(),
                },
            },
            lifetime: Timestamp {
                id: None,
                created: "2000-01-01T00:00:00Z".to_owned(),
                expires: expiry.to_owned(),
            },
            requested_security_token: RequestedSecurityToken {
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
    fn runtime_request_rejects_injection_and_never_requests_interactive_consent() {
        for client_id in [
            "",
            "000000004424da1f&scope=other",
            "not-an-app-id-000",
            "000000004424da1\n",
        ] {
            let mut input = request();
            input.client_id = client_id.to_owned();
            assert_eq!(request_scope(&input).unwrap_err(), RpsError::InvalidRequest);
        }
        let mut input = request();
        input.allow_ui = true;
        assert_eq!(
            request_scope(&input).unwrap(),
            request_scope(&request()).unwrap()
        );
        assert_eq!(
            request_scope(&request()).unwrap(),
            "scope=service::user.auth.xboxlive.com::MBI_SSL&api-version=2.0&clientid=000000004424da1f"
        );
    }

    #[test]
    fn ticket_metadata_requires_exact_audience_type_expiry_and_bounded_nonempty_proof() {
        assert!(
            compact(
                response(
                    DEVICE_SCOPE,
                    Some("fixture-not-a-token"),
                    "2099-01-01T00:00:00Z"
                ),
                DEVICE_SCOPE
            )
            .is_ok()
        );
        for (audience, ticket, expiry) in [
            ("wrong-scope", Some("fixture-only"), "2099-01-01T00:00:00Z"),
            (DEVICE_SCOPE, None, "2099-01-01T00:00:00Z"),
            (DEVICE_SCOPE, Some(""), "2099-01-01T00:00:00Z"),
            (DEVICE_SCOPE, Some("fixture-only"), "2000-01-02T00:00:00Z"),
            (DEVICE_SCOPE, Some("fixture-only"), "invalid"),
            (DEVICE_SCOPE, Some("fixture\nonly"), "2099-01-01T00:00:00Z"),
        ] {
            assert_eq!(
                compact(response(audience, ticket, expiry), DEVICE_SCOPE).unwrap_err(),
                RpsError::InvalidResponse
            );
        }
        let oversized = "x".repeat(16 * 1024 + 1);
        assert!(
            compact(
                response(DEVICE_SCOPE, Some(&oversized), "2099-01-01T00:00:00Z"),
                DEVICE_SCOPE
            )
            .is_err()
        );
    }

    #[test]
    fn failed_missing_duplicate_or_wrong_user_results_never_panic_or_succeed_empty() {
        assert!(user_response(ExchangeUserTokenOutcome::Fault(None), DEVICE_SCOPE).is_err());
        for replies in [
            vec![],
            vec![response(
                DEVICE_SCOPE,
                Some("fixture-only"),
                "2099-01-01T00:00:00Z",
            )],
            vec![
                response(DEVICE_SCOPE, Some("fixture-only"), "2099-01-01T00:00:00Z"),
                response(DEVICE_SCOPE, Some("fixture-only"), "2099-01-01T00:00:00Z"),
            ],
        ] {
            assert!(
                user_response(
                    ExchangeUserTokenOutcome::Issued(
                        soap::BodyContent::RequestSecurityTokenResponseCollection(
                            soap::RequestSecurityTokenResponseCollection {
                                security_tokens: replies
                            }
                        )
                    ),
                    DEVICE_SCOPE
                )
                .is_err()
            );
        }
    }

    #[tokio::test]
    async fn signed_out_management_context_never_falls_back_to_default_credentials() {
        let tokens = Arc::new(TokenManager::with_management_backend(Arc::new(
            xodus::tokens::backend::MemoryBackend::default(),
        )));
        if !xodus::secrets::management_native_keychain_enabled() {
            assert!(SimpleContext::management(tokens).is_err());
            return;
        }
        let device = LegacyToken {
            key_name: None,
            token: "fixture-only".to_owned(),
            binary_secret: None,
            tpm_key: None,
            lifetime: Timestamp {
                id: None,
                created: "2000-01-01T00:00:00Z".to_owned(),
                expires: "2099-01-01T00:00:00Z".to_owned(),
            },
        };
        assert!(SimpleContext::new(device, tokens.clone()).is_err());
        let context = SimpleContext::management(tokens).unwrap();
        assert_eq!(
            exchange(&context, request()).await.err(),
            Some(RpsError::AuthenticationRequired)
        );
        assert!(SimpleContext::management(Arc::new(TokenManager::with_memory())).is_err());
    }

    #[test]
    fn ordered_or_reordered_complete_user_results_preserve_exact_ticket_and_refresh() {
        for reverse in [false, true] {
            let user = response(
                DEVICE_SCOPE,
                Some("fixture-not-a-real-token"),
                "2099-01-01T00:00:00Z",
            );
            let mut refreshed = response(PASSPORT_REQUEST, None, "2099-01-01T00:00:00Z");
            refreshed.token_type = "urn:passport:legacy".to_owned();
            refreshed.requested_security_token.encrypted_data = Some(
                soap::EncryptedData::devicesoftware("Zml4dHVyZS1vbmx5".to_owned()),
            );
            let mut replies = vec![user, refreshed];
            if reverse {
                replies.reverse();
            }
            let (ticket, expiry, refreshed) = user_response(
                ExchangeUserTokenOutcome::Issued(
                    soap::BodyContent::RequestSecurityTokenResponseCollection(
                        soap::RequestSecurityTokenResponseCollection {
                            security_tokens: replies,
                        },
                    ),
                ),
                DEVICE_SCOPE,
            )
            .unwrap();
            assert_eq!(ticket, "fixture-not-a-real-token");
            assert!(expiry > chrono::Utc::now().timestamp());
            assert_eq!(refreshed.key_name.as_deref(), Some(PASSPORT_STS));
        }
    }

    #[test]
    fn unknown_runtime_xml_parameters_are_not_ignored() {
        let xml = "<MSATokenRequest><ClientId>000000004424da1f</ClientId><Scope>arbitrary</Scope></MSATokenRequest>";
        assert!(quick_xml::de::from_str::<MSATokenRequest>(xml).is_err());
    }
}
