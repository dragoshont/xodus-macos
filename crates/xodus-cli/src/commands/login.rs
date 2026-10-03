use std::process::ExitCode;

use xodus::models::live::{DAProperty, ExchangeUserTokenOutcome};
use xodus::models::{secrets, soap};
use xodus::tokens::TokenManager;

use crate::webview;

const CLIENT_ID: &str = "000000004424da1f";
const LOGIN_MARKET: &str = "en-US";
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
    let handler = LoginHandler::new(client.clone(), token, tokens.clone());
    let output = match webview::run_sessions(handler) {
        Ok(output) => output.flatten(),
        Err(_) => {
            eprintln!("Sign-in failed before credentials were issued");
            return ExitCode::FAILURE;
        }
    };
    match validate_issued(output) {
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

#[derive(Debug)]
struct LoginOutput {
    body: soap::BodyContent,
    user: secrets::User,
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
    finish: bool,
}

impl LoginHandler {
    fn new(
        client: reqwest::Client,
        device: xodus::models::secrets::LegacyToken,
        _tokens: TokenManager,
    ) -> Self {
        Self {
            client,
            device,
            client_id: CLIENT_ID.to_string(),
            finish: false,
        }
    }

    fn exchange_user_token(
        &self,
        prop: DAProperty,
    ) -> Result<ExchangeUserTokenOutcome, Box<dyn std::error::Error>> {
        let client = self.client.clone();
        let device_token = self.device.clone();
        let client_id = self.client_id.clone();
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

        tokio::task::block_in_place(|| {
            tokio::runtime::Handle::current().block_on(async move {
                let mut scopes = vec![(
                    USER_AUTH_SCOPE.to_string(),
                    Some(soap::PolicyReference::token_broker()),
                )];

                if self.finish {
                    scopes.push(("http://Passport.NET/tb".to_string(), None));
                }

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
            })
        })
    }
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
                    runtime.close_session(session_id);
                    self.finish = true;
                    runtime.open_session(webview::finalize_request(auth_url));
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
