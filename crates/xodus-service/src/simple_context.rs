// Temporary context, full service design will be much more extensive

use std::sync::Arc;

use xodus::models::secrets::LegacyToken;
use xodus::tokens::TokenManager;

pub struct SimpleContext {
    pub client: reqwest::Client,
    pub device_token: Option<LegacyToken>,
    tokens: Arc<TokenManager>,
    pub(crate) management_profile: bool,
}

impl SimpleContext {
    pub fn new(
        device_token: LegacyToken,
        tokens: Arc<TokenManager>,
    ) -> Result<Self, std::io::Error> {
        if tokens.is_management_profile() {
            return Err(std::io::Error::new(
                std::io::ErrorKind::PermissionDenied,
                "The legacy runtime route cannot use a management credential profile",
            ));
        }
        let client = reqwest::ClientBuilder::new()
            .user_agent(format!("xodus-service/{}", env!("CARGO_PKG_VERSION")))
            .redirect(reqwest::redirect::Policy::none())
            .timeout(std::time::Duration::from_secs(20))
            .build()
            .map_err(|_| std::io::Error::other("Runtime HTTP client initialization failed"))?;

        Ok(Self {
            client,
            device_token: Some(device_token),
            tokens,
            management_profile: false,
        })
    }

    pub fn management(tokens: Arc<TokenManager>) -> Result<Self, std::io::Error> {
        if !xodus::secrets::management_native_keychain_enabled() {
            return Err(std::io::Error::new(
                std::io::ErrorKind::Unsupported,
                "Management runtime credentials require native macOS Keychain without plaintext fallback",
            ));
        }
        if !tokens.is_management_profile() {
            return Err(std::io::Error::new(
                std::io::ErrorKind::PermissionDenied,
                "An isolated management credential profile is required",
            ));
        }
        let client = reqwest::ClientBuilder::new()
            .user_agent(format!("xodus-service/{}", env!("CARGO_PKG_VERSION")))
            .redirect(reqwest::redirect::Policy::none())
            .timeout(std::time::Duration::from_secs(20))
            .build()
            .map_err(|_| std::io::Error::other("Runtime HTTP client initialization failed"))?;
        Ok(Self {
            client,
            device_token: None,
            tokens,
            management_profile: true,
        })
    }

    pub fn tokens(&self) -> &Arc<TokenManager> {
        &self.tokens
    }
}
