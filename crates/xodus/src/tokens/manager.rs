use std::collections::HashMap;
use std::sync::Arc;
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::Instant;

use crate::models::secrets::{Device, Token, TokenStore, User};
use crate::models::xbox::XstsResponse;
use crate::tokens::backend::{KeychainBackend, MemoryBackend};
use crate::tokens::store::{ExpiringTokenBackend, TokenBackend, TokenStoreError};

mod keys {
    pub const DEV_LICENSE: &str = "dev_license";
    pub const DEVICE_TOKENS: &str = "device-tokens";
    pub const USER_TOKENS: &str = "user-tokens";
    pub const USER_INFO: &str = "user-DA";
    pub const XAL_USER_SESSION: &str = "management-xal-user";
}

pub const PASSPORT_STS: &str = "http://Passport.NET/STS";

/// Semantic facade over the two storage tiers: a persistent, keychain-backed tier
/// for STS/device/user credentials, and an ephemeral tier for short-lived
/// per-relying-party XSTS tokens. Centralizes the read-merge-write pattern that was
/// previously duplicated across `xodus-cli` and `xodus-service`.
#[derive(Clone)]
pub struct TokenManager {
    persistent: Arc<dyn TokenBackend>,
    ephemeral: Arc<dyn ExpiringTokenBackend>,
    cache_epoch: Arc<AtomicU64>,
}

impl TokenManager {
    pub fn new(
        persistent: Arc<dyn TokenBackend>,
        ephemeral: Arc<dyn ExpiringTokenBackend>,
    ) -> Self {
        Self {
            persistent,
            ephemeral,
            cache_epoch: Arc::new(AtomicU64::new(0)),
        }
    }

    /// Keychain for persistent storage, in-memory for ephemeral - the default
    /// wiring for both `xodus-cli` and `xodus-service` today.
    pub fn with_keychain_and_memory() -> Self {
        Self::new(
            Arc::new(KeychainBackend),
            Arc::new(MemoryBackend::default()),
        )
    }

    /// Keychain for persistent storage, in-memory for ephemeral - the default
    /// wiring for both `xodus-cli` and `xodus-service` today.
    pub fn with_memory() -> Self {
        Self::new(
            Arc::new(MemoryBackend::default()),
            Arc::new(MemoryBackend::default()),
        )
    }

    pub fn remove_persistent(&self) -> Result<(), TokenStoreError> {
        self.persistent.remove(keys::DEVICE_TOKENS)?;
        self.remove_user_credentials()
    }

    /// Disconnect the account without removing the separately owned device identity.
    pub fn remove_user_credentials(&self) -> Result<(), TokenStoreError> {
        self.cache_epoch.fetch_add(1, Ordering::SeqCst);
        for key in [keys::USER_TOKENS, keys::USER_INFO, keys::XAL_USER_SESSION] {
            match self.persistent.remove(key) {
                Ok(())
                | Err(TokenStoreError::NotFound)
                | Err(TokenStoreError::Keychain(keyring_core::Error::NoEntry)) => {}
                Err(error) => return Err(error),
            }
        }
        Ok(())
    }

    pub fn get_xal_user_session(
        &self,
    ) -> Result<Option<crate::models::secrets::ManagementXalSession>, TokenStoreError> {
        self.persistent
            .get(keys::XAL_USER_SESSION)?
            .map(|bytes| serde_json::from_slice(&bytes).map_err(Into::into))
            .transpose()
    }

    /// One Keychain write commits the complete consent result, never a partial token set.
    pub fn save_xal_user_session(
        &self,
        flow_id: String,
        session: crate::xal::TokenStore,
    ) -> Result<(), TokenStoreError> {
        self.persistent.set(
            keys::XAL_USER_SESSION,
            &serde_json::to_vec(&crate::models::secrets::ManagementXalSession {
                flow_id,
                session,
            })?,
        )
    }

    // ---- Device identity / license -----------------------------------------

    pub fn get_device_license(&self) -> Result<Device, TokenStoreError> {
        let bytes = self
            .persistent
            .get(keys::DEV_LICENSE)?
            .ok_or(TokenStoreError::NotFound)?;
        Ok(serde_json::from_slice(&bytes)?)
    }

    pub fn save_device_license(&self, device: &Device) -> Result<(), TokenStoreError> {
        self.persistent
            .set(keys::DEV_LICENSE, &serde_json::to_vec(device)?)
    }

    pub fn remove_device_license(&self) -> Result<(), TokenStoreError> {
        self.persistent.remove(keys::DEV_LICENSE)
    }

    // ---- Device STS tokens (keyed by SOAP "applies_to" address) -----------

    pub fn get_device_token_for(&self, address: &str) -> Result<Option<Token>, TokenStoreError> {
        Self::read_token_store(&*self.persistent, keys::DEVICE_TOKENS, address)
    }

    pub fn save_device_token(&self, address: String, token: Token) -> Result<(), TokenStoreError> {
        Self::write_token_store(&*self.persistent, keys::DEVICE_TOKENS, address, token)
    }

    pub fn get_device_sts_token(&self) -> Result<Token, TokenStoreError> {
        self.get_device_token_for(PASSPORT_STS)?
            .ok_or(TokenStoreError::NotFound)
    }

    // ---- User STS tokens (keyed by SOAP "applies_to" address) --------------

    pub fn get_user_token_for(&self, address: &str) -> Result<Option<Token>, TokenStoreError> {
        Self::read_token_store(&*self.persistent, keys::USER_TOKENS, address)
    }

    pub fn save_user_token(&self, address: String, token: Token) -> Result<(), TokenStoreError> {
        Self::write_token_store(&*self.persistent, keys::USER_TOKENS, address, token)
    }

    pub fn replace_user_tokens(
        &self,
        tokens: HashMap<String, Token>,
    ) -> Result<(), TokenStoreError> {
        self.cache_epoch.fetch_add(1, Ordering::SeqCst);
        self.persistent.set(
            keys::USER_TOKENS,
            &serde_json::to_vec(&TokenStore { tokens })?,
        )
    }

    pub fn get_user_sts_token(&self) -> Result<Token, TokenStoreError> {
        self.get_user_token_for(PASSPORT_STS)?
            .ok_or(TokenStoreError::NotFound)
    }

    // ---- User info -----------------------------------------------------------

    pub fn get_user(&self) -> Result<User, TokenStoreError> {
        let bytes = self
            .persistent
            .get(keys::USER_INFO)?
            .ok_or(TokenStoreError::NotFound)?;
        Ok(serde_json::from_slice(&bytes)?)
    }

    pub fn save_user(&self, user: &User) -> Result<(), TokenStoreError> {
        self.persistent
            .set(keys::USER_INFO, &serde_json::to_vec(user)?)
    }

    // ---- Ephemeral XSTS-by-relying-party cache --------------------------------

    pub fn get_cached_xsts(&self, relying_party: &str) -> Option<XstsResponse> {
        let key = format!(
            "{}:{relying_party}",
            self.cache_epoch.load(Ordering::SeqCst)
        );
        let bytes = self.ephemeral.get(&key).ok()??;
        serde_json::from_slice(&bytes).ok()
    }

    pub fn cache_xsts(&self, relying_party: &str, token: &XstsResponse) {
        self.cache_xsts_response(relying_party, token);
    }

    fn cache_xsts_response(&self, key: &str, token: &XstsResponse) {
        let key = format!("{}:{key}", self.cache_epoch.load(Ordering::SeqCst));
        let Ok(bytes) = serde_json::to_vec(token) else {
            return;
        };
        let remaining = (token.not_after - chrono::Utc::now())
            .to_std()
            .unwrap_or(std::time::Duration::ZERO);
        let _ = self
            .ephemeral
            .set_with_expiry(&key, &bytes, Instant::now() + remaining);
    }

    // ---- shared TokenStore read/modify/write helper ---------------------------

    fn read_token_store(
        backend: &dyn TokenBackend,
        key: &str,
        address: &str,
    ) -> Result<Option<Token>, TokenStoreError> {
        let Some(bytes) = backend.get(key)? else {
            return Ok(None);
        };
        let store: TokenStore = serde_json::from_slice(&bytes)?;
        Ok(store.tokens.get(address).cloned())
    }

    fn write_token_store(
        backend: &dyn TokenBackend,
        key: &str,
        address: String,
        token: Token,
    ) -> Result<(), TokenStoreError> {
        let mut tokens: HashMap<String, Token> = match backend.get(key)? {
            Some(bytes) if !bytes.is_empty() => {
                serde_json::from_slice::<TokenStore>(&bytes)?.tokens
            }
            _ => HashMap::new(),
        };
        tokens.insert(address, token);
        backend.set(key, &serde_json::to_vec(&TokenStore { tokens })?)
    }
}

#[cfg(test)]
mod management_tests {
    use super::*;

    #[test]
    fn disconnect_removes_user_but_preserves_device_credentials() {
        let tokens = TokenManager::with_memory();
        tokens
            .save_device_token(
                PASSPORT_STS.to_owned(),
                Token::Compact("fixture-device".to_owned()),
            )
            .unwrap();
        tokens
            .save_user_token(
                PASSPORT_STS.to_owned(),
                Token::Compact("fixture-user".to_owned()),
            )
            .unwrap();
        tokens
            .save_user(&User {
                puid: "fixture".to_owned(),
                username: "fixture".to_owned(),
            })
            .unwrap();
        tokens.remove_user_credentials().unwrap();
        tokens.remove_user_credentials().unwrap();
        assert!(matches!(
            tokens.get_user_sts_token(),
            Err(TokenStoreError::NotFound)
        ));
        assert!(matches!(tokens.get_user(), Err(TokenStoreError::NotFound)));
        assert!(tokens.get_device_sts_token().is_ok());
    }

    #[test]
    fn disconnect_invalidates_cached_audiences_on_all_manager_clones() {
        let tokens = TokenManager::with_memory();
        let clone = tokens.clone();
        let xsts: XstsResponse = serde_json::from_value(serde_json::json!({
            "NotAfter":"2099-01-01T00:00:00Z","Token":"fixture-not-a-token",
            "DisplayClaims":{"xui":[{"uhs":"fixture"}]}
        }))
        .unwrap();
        tokens.cache_xsts("fixture-audience", &xsts);
        assert!(clone.get_cached_xsts("fixture-audience").is_some());
        tokens.remove_user_credentials().unwrap();
        assert!(clone.get_cached_xsts("fixture-audience").is_none());
    }

    struct RemovalFailure;
    impl TokenBackend for RemovalFailure {
        fn get(&self, _: &str) -> Result<Option<Vec<u8>>, TokenStoreError> {
            Ok(None)
        }
        fn set(&self, _: &str, _: &[u8]) -> Result<(), TokenStoreError> {
            Ok(())
        }
        fn remove(&self, _: &str) -> Result<(), TokenStoreError> {
            Err(TokenStoreError::Io(std::io::Error::new(
                std::io::ErrorKind::PermissionDenied,
                "fixture",
            )))
        }
    }

    #[test]
    fn keychain_removal_failure_is_not_signed_out_success() {
        let tokens =
            TokenManager::new(Arc::new(RemovalFailure), Arc::new(MemoryBackend::default()));
        assert!(tokens.remove_user_credentials().is_err());
    }
}
