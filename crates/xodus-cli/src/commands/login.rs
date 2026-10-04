use std::process::ExitCode;

use xodus::models::live::{DAProperty, ExchangeUserTokenOutcome};
use xodus::models::{secrets, soap};
use xodus::tokens::TokenManager;

use crate::webview;

pub(crate) const CLIENT_ID: &str = "000000004424da1f";
pub(crate) const LOGIN_MARKET: &str = "en-US";
const USER_AUTH_SCOPE: &str = "scope=service::user.auth.xboxlive.com::MBI_SSL&api-version=2.0";

pub async fn run(client: &reqwest::Client, tokens: &TokenManager) -> ExitCode {
    let Ok(token) = tokens.get_device_sts_token() else {
        eprintln!("Device credentials are unavailable");
        return ExitCode::FAILURE;
    };
    let secrets::Token::Legacy(token) = token else {
        eprintln!("Invalid STS token");
        return ExitCode::FAILURE;
    };
    match issue_credentials_inner(client.clone(), token, false) {
        Ok((issued, user)) => {
            if tokens.replace_user_tokens(issued).is_err() || tokens.save_user(&user).is_err() {
                eprintln!("Sign-in credentials could not be persisted");
                return ExitCode::FAILURE;
            }
            ExitCode::SUCCESS
        }
        Err(reason) => {
            eprintln!("{reason}");
            ExitCode::FAILURE
        }
    }
}

fn issue_credentials_inner(
    client: reqwest::Client,
    device: secrets::LegacyToken,
    isolated: bool,
) -> Result<
    (
        std::collections::HashMap<String, secrets::Token>,
        secrets::User,
    ),
    &'static str,
> {
    validate_device(&device)?;
    let output = webview::run_sessions(LoginHandler::new(client, device, isolated))
        .map_err(|_| "Native Microsoft sign-in failed")?
        .flatten();
    finish_issued(output)
}

pub(crate) fn validate_device(device: &secrets::LegacyToken) -> Result<(), &'static str> {
    if !secrets::device_token_structurally_valid(device) || !secrets::legacy_token_valid(device) {
        return Err("Device credential proof is invalid or expired");
    }
    Ok(())
}

pub(crate) fn finish_issued(
    output: Option<LoginOutput>,
) -> Result<
    (
        std::collections::HashMap<String, secrets::Token>,
        secrets::User,
    ),
    &'static str,
> {
    let (tokens, user) = validate_issued(output)?;
    if !matches!(tokens.get(xodus::tokens::PASSPORT_STS), Some(secrets::Token::Legacy(token))
        if secrets::legacy_token_valid(token))
    {
        return Err("Sign-in did not issue the required Passport store credential");
    }
    Ok((tokens, user))
}

#[derive(Debug)]
pub(crate) struct LoginOutput {
    pub body: soap::BodyContent,
    pub user: secrets::User,
}

fn validate_issued(
    output: Option<LoginOutput>,
) -> Result<
    (
        std::collections::HashMap<String, secrets::Token>,
        secrets::User,
    ),
    &'static str,
> {
    let output = output.ok_or("Sign-in was cancelled or no credentials were issued")?;
    let issued = match output.body {
        soap::BodyContent::RequestSecurityTokenResponseCollection(collection) => {
            collection.security_tokens
        }
        soap::BodyContent::RequestSecurityTokenResponse(token) => vec![*token],
        _ => return Err("Sign-in did not return credential proof"),
    };
    if issued.is_empty()
        || output.user.puid.trim().is_empty()
        || output.user.username.trim().is_empty()
    {
        return Err("Sign-in did not return nonempty credential proof");
    }
    let mut tokens = std::collections::HashMap::new();
    for response in issued {
        let address = response.applies_to.endpoint_reference.address.clone();
        let token = secrets::Token::from_response_checked(response)?;
        let address = match &token {
            secrets::Token::Legacy(legacy) => legacy.key_name.clone().unwrap_or(address),
            _ => address,
        };
        if tokens.insert(address, token).is_some() {
            return Err("Sign-in returned duplicate credential audiences");
        }
    }
    Ok((tokens, output.user))
}

struct LoginHandler {
    client: reqwest::Client,
    device: xodus::models::secrets::LegacyToken,
    client_id: String,
    finish_count: u8,
    isolated: bool,
}

impl LoginHandler {
    fn new(
        client: reqwest::Client,
        device: xodus::models::secrets::LegacyToken,
        isolated: bool,
    ) -> Self {
        Self {
            client,
            device,
            client_id: CLIENT_ID.to_string(),
            finish_count: 0,
            isolated,
        }
    }

    fn exchange_user_token(
        &self,
        prop: DAProperty,
    ) -> Result<ExchangeUserTokenOutcome, Box<dyn std::error::Error>> {
        tokio::task::block_in_place(|| {
            tokio::runtime::Handle::current().block_on(exchange_user_property(
                self.client.clone(),
                self.device.clone(),
                self.client_id.clone(),
                prop,
            ))
        })
    }
}

pub(crate) async fn exchange_user_property(
    client: reqwest::Client,
    device_token: secrets::LegacyToken,
    client_id: String,
    prop: DAProperty,
) -> Result<ExchangeUserTokenOutcome, Box<dyn std::error::Error>> {
    let username = prop.username;
    let inline_ft = prop.sts_inline_flow_token;
    let user_token = xodus::models::secrets::LegacyToken {
        key_name: None,
        token: prop.da_token,
        binary_secret: None,
        tpm_key: None,
        lifetime: soap::Timestamp {
            id: None,
            created: prop.da_start_time,
            expires: prop.da_expires,
        },
    };

    let scopes = vec![
        (
            USER_AUTH_SCOPE.to_string(),
            Some(soap::PolicyReference::token_broker()),
        ),
        ("http://Passport.NET/tb".to_string(), None),
    ];

    xodus::api::live::exchange_user_token(
        &client,
        user_token,
        username,
        device_token,
        Some(inline_ft),
        None,
        client_id,
        &scopes,
    )
    .await
    .map_err(|e| Box::new(e) as Box<dyn std::error::Error>)
}

impl webview::SessionHandler for LoginHandler {
    type Output = Option<LoginOutput>;

    fn bootstrap(
        &mut self,
        runtime: &mut webview::RuntimeCommands,
    ) -> Result<(), Box<dyn std::error::Error>> {
        runtime.open_session(webview::login_request(
            self.client_id.clone(),
            LOGIN_MARKET.to_string(),
            self.isolated,
        ));
        Ok(())
    }

    fn on_token(
        &mut self,
        session_id: webview::SessionId,
        data: DAProperty,
        runtime: &mut webview::RuntimeCommands,
    ) -> Result<webview::HandlerControl<Self::Output>, Box<dyn std::error::Error>> {
        let exchanged = self.exchange_user_token(data.clone())?;

        match exchanged {
            ExchangeUserTokenOutcome::Fault(pp) => {
                if let Some(pp) = pp
                    && let Some(auth_url) = pp.inline_auth_url
                {
                    if self.finish_count >= 4 || !webview::trusted_login_url(&auth_url) {
                        return Err(std::io::Error::other(
                            "Unsupported or repeated inline authentication",
                        )
                        .into());
                    }
                    self.finish_count += 1;
                    runtime.navigate_session(session_id, auth_url);
                    return Ok(webview::HandlerControl::Continue);
                }

                eprintln!("User token exchange returned a fault without inline auth");
                runtime.close_session(session_id);
                Ok(webview::HandlerControl::Complete(None))
            }
            ExchangeUserTokenOutcome::Issued(da) => {
                runtime.close_session(session_id);
                let user = xodus::models::secrets::User {
                    puid: data.puid,
                    username: data.username,
                };
                Ok(webview::HandlerControl::Complete(Some(LoginOutput {
                    body: da,
                    user,
                })))
            }
        }
    }

    fn on_closed(
        &mut self,
        _session_id: webview::SessionId,
        _runtime: &mut webview::RuntimeCommands,
    ) -> Result<webview::HandlerControl<Self::Output>, Box<dyn std::error::Error>> {
        Ok(webview::HandlerControl::Complete(None))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn no_webview_output_is_cancellation_not_success() {
        assert!(validate_issued(None).is_err());
    }

    #[test]
    fn empty_issued_collection_is_not_success() {
        assert!(
            validate_issued(Some(LoginOutput {
                body: soap::BodyContent::RequestSecurityTokenResponseCollection(
                    soap::RequestSecurityTokenResponseCollection {
                        security_tokens: vec![]
                    }
                ),
                user: secrets::User {
                    puid: "fixture".to_owned(),
                    username: "fixture".to_owned()
                },
            }))
            .is_err()
        );
    }
}
