use std::collections::HashMap;
use std::sync::Arc;
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::Instant;

use crate::models::secrets::{Device, Token, TokenStore, User};
use crate::models::xbox::XstsResponse;
use crate::tokens::backend::{KeychainBackend, ManagementKeychainBackend, MemoryBackend};
use crate::tokens::store::{ExpiringTokenBackend, TokenBackend, TokenStoreError};

pub(crate) mod keys {
    pub const DEV_LICENSE: &str = "dev_license";
    pub const DEVICE_TOKENS: &str = "device-tokens";
    pub const USER_TOKENS: &str = "user-tokens";
    pub const USER_INFO: &str = "user-DA";
    pub const XAL_USER_SESSION: &str = "management-xal-user";
    pub const STORE_USER_SESSION: &str = "management-store-user";
    pub const PENDING_STORE_EXCHANGE: &str = "management-pending-exchange";
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
    management_mutations: Arc<AtomicU64>,
    pending_exchange_lock: Arc<std::sync::Mutex<()>>,
    management_profile: bool,
}

struct ManagementMutation {
    epoch: Arc<AtomicU64>,
    active: Arc<AtomicU64>,
}

impl Drop for ManagementMutation {
    fn drop(&mut self) {
        self.epoch.fetch_add(1, Ordering::SeqCst);
        self.active.fetch_sub(1, Ordering::SeqCst);
    }
}

#[derive(Clone)]
pub struct ManagementProfileStamp {
    epoch: u64,
    epoch_source: Arc<AtomicU64>,
    expires_at: chrono::DateTime<chrono::Utc>,
    fingerprint: Arc<serde_json::Value>,
}

#[derive(Clone)]
pub struct ManagementProfileWitness {
    epoch: u64,
    epoch_source: Arc<AtomicU64>,
    expires_at: chrono::DateTime<chrono::Utc>,
    fingerprint: Arc<serde_json::Value>,
}

impl ManagementProfileStamp {
    pub fn publication_witness(&self) -> ManagementProfileWitness {
        ManagementProfileWitness {
            epoch: self.epoch,
            epoch_source: self.epoch_source.clone(),
            expires_at: self.expires_at,
            fingerprint: self.fingerprint.clone(),
        }
    }
}

impl TokenManager {
    pub fn is_management_profile(&self) -> bool {
        self.management_profile
    }

    pub fn new(
        persistent: Arc<dyn TokenBackend>,
        ephemeral: Arc<dyn ExpiringTokenBackend>,
    ) -> Self {
        Self {
            persistent,
            ephemeral,
            cache_epoch: Arc::new(AtomicU64::new(0)),
            management_mutations: Arc::new(AtomicU64::new(0)),
            pending_exchange_lock: Arc::new(std::sync::Mutex::new(())),
            management_profile: false,
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

    pub fn with_management_keychain_and_memory() -> Self {
        Self::with_management_backend(Arc::new(ManagementKeychainBackend {
            interactive_reads: true,
        }))
    }

    pub fn with_management_backend(persistent: Arc<dyn TokenBackend>) -> Self {
        let mut manager = Self::new(persistent, Arc::new(MemoryBackend::default()));
        manager.management_profile = true;
        manager
    }

    pub fn readonly_management_profile(&self) -> Result<Self, TokenStoreError> {
        if !self.management_profile {
            return Err(TokenStoreError::InvalidCredential);
        }
        struct ReadonlyBackend(Arc<dyn TokenBackend>);
        impl TokenBackend for ReadonlyBackend {
            fn get(&self, key: &str) -> Result<Option<Vec<u8>>, TokenStoreError> {
                self.0.get(key)
            }
            fn set(&self, _: &str, _: &[u8]) -> Result<(), TokenStoreError> {
                Err(TokenStoreError::InvalidCredential)
            }
            fn remove(&self, _: &str) -> Result<(), TokenStoreError> {
                Err(TokenStoreError::InvalidCredential)
            }
        }
        let mut manager = self.clone();
        manager.persistent = Arc::new(ReadonlyBackend(self.persistent.clone()));
        Ok(manager)
    }

    pub fn with_explicit_management_keychain_interaction(&self) -> Result<Self, TokenStoreError> {
        if !self.management_profile {
            return Err(TokenStoreError::InvalidCredential);
        }
        let mut manager = self.clone();
        manager.persistent = Arc::new(ManagementKeychainBackend {
            interactive_reads: true,
        });
        Ok(manager)
    }

    pub fn with_noninteractive_management_keychain(&self) -> Result<Self, TokenStoreError> {
        if !self.management_profile {
            return Err(TokenStoreError::InvalidCredential);
        }
        let mut manager = self.clone();
        manager.persistent = Arc::new(ManagementKeychainBackend {
            interactive_reads: false,
        });
        Ok(manager)
    }

    pub fn get_management_store_session(
        &self,
    ) -> Result<Option<crate::models::secrets::ManagementStoreSession>, TokenStoreError> {
        if !self.management_profile {
            return Ok(None);
        }
        self.persistent
            .get(keys::STORE_USER_SESSION)?
            .map(|bytes| serde_json::from_slice(&bytes).map_err(Into::into))
            .transpose()
    }

    pub fn save_management_store_session(
        &self,
        session: crate::models::secrets::ManagementStoreSession,
    ) -> Result<(), TokenStoreError> {
        if !self.management_profile || !session.valid() {
            return Err(TokenStoreError::InvalidCredential);
        }
        let _mutation = self.begin_management_mutation();
        self.cache_epoch.fetch_add(1, Ordering::SeqCst);
        self.persistent
            .set(keys::STORE_USER_SESSION, &serde_json::to_vec(&session)?)?;
        self.clear_pending_management_exchange()
    }

    pub fn get_pending_management_exchange(
        &self,
    ) -> Result<Option<crate::models::secrets::PendingManagementExchange>, TokenStoreError> {
        if !self.management_profile {
            return Err(TokenStoreError::InvalidCredential);
        }
        let _exclusive = self
            .pending_exchange_lock
            .lock()
            .map_err(|_| TokenStoreError::InvalidCredential)?;
        let Some(bytes) = self.persistent.get(keys::PENDING_STORE_EXCHANGE)? else {
            return Ok(None);
        };
        if bytes.len() > 192 * 1024 {
            return Err(TokenStoreError::InvalidCredential);
        }
        let pending: crate::models::secrets::PendingManagementExchange =
            serde_json::from_slice(&bytes)?;
        if chrono::Utc::now() >= pending.expires_at {
            self.clear_pending_management_exchange_locked()?;
            return Ok(None);
        }
        if !pending.valid() {
            return Err(TokenStoreError::InvalidCredential);
        }
        Ok(Some(pending))
    }

    pub fn save_pending_management_exchange(
        &self,
        pending: crate::models::secrets::PendingManagementExchange,
    ) -> Result<(), TokenStoreError> {
        let _exclusive = self
            .pending_exchange_lock
            .lock()
            .map_err(|_| TokenStoreError::InvalidCredential)?;
        if !self.management_profile
            || !pending.valid()
            || self.get_management_store_session()?.is_some()
        {
            return Err(TokenStoreError::InvalidCredential);
        }
        self.persistent
            .set(keys::PENDING_STORE_EXCHANGE, &serde_json::to_vec(&pending)?)
    }

    pub fn clear_pending_management_exchange(&self) -> Result<(), TokenStoreError> {
        let _exclusive = self
            .pending_exchange_lock
            .lock()
            .map_err(|_| TokenStoreError::InvalidCredential)?;
        self.clear_pending_management_exchange_locked()
    }

    fn clear_pending_management_exchange_locked(&self) -> Result<(), TokenStoreError> {
        if !self.management_profile {
            return Err(TokenStoreError::InvalidCredential);
        }
        if self.persistent.get(keys::PENDING_STORE_EXCHANGE)?.is_none() {
            return Ok(());
        }
        match self.persistent.remove(keys::PENDING_STORE_EXCHANGE) {
            Ok(())
            | Err(TokenStoreError::NotFound)
            | Err(TokenStoreError::Keychain(keyring_core::Error::NoEntry)) => Ok(()),
            Err(error) => Err(error),
        }
    }

    pub fn management_store_snapshot(
        &self,
    ) -> Result<
        (
            crate::models::secrets::ManagementStoreSession,
            ManagementProfileStamp,
        ),
        TokenStoreError,
    > {
        if !self.management_profile || self.management_mutations.load(Ordering::SeqCst) != 0 {
            return Err(TokenStoreError::InvalidCredential);
        }
        let epoch = self.cache_epoch.load(Ordering::SeqCst);
        let session = self
            .get_management_store_session()?
            .ok_or(TokenStoreError::NotFound)?;
        if !session.valid() {
            return Err(TokenStoreError::InvalidCredential);
        }
        let Some(Token::Legacy(user)) = session.tokens.get(PASSPORT_STS) else {
            return Err(TokenStoreError::InvalidCredential);
        };
        let expires_at = chrono::DateTime::parse_from_rfc3339(&user.lifetime.expires)
            .and_then(|user| {
                chrono::DateTime::parse_from_rfc3339(&session.device_token.lifetime.expires)
                    .map(|device| user.min(device).with_timezone(&chrono::Utc))
            })
            .map_err(|_| TokenStoreError::InvalidCredential)?;
        let fingerprint = Arc::new(serde_json::to_value(&session)?);
        if self.management_mutations.load(Ordering::SeqCst) != 0
            || self.cache_epoch.load(Ordering::SeqCst) != epoch
        {
            return Err(TokenStoreError::InvalidCredential);
        }
        Ok((
            session,
            ManagementProfileStamp {
                epoch,
                epoch_source: self.cache_epoch.clone(),
                expires_at,
                fingerprint,
            },
        ))
    }

    pub fn management_publication_current(&self, witness: &ManagementProfileWitness) -> bool {
        self.management_profile
            && Arc::ptr_eq(&self.cache_epoch, &witness.epoch_source)
            && self.management_mutations.load(Ordering::SeqCst) == 0
            && self.cache_epoch.load(Ordering::SeqCst) == witness.epoch
            && chrono::Utc::now() < witness.expires_at
    }

    pub fn verify_management_publication(
        &self,
        witness: &ManagementProfileWitness,
    ) -> Result<(), TokenStoreError> {
        if !self.management_publication_current(witness) {
            return Err(TokenStoreError::InvalidCredential);
        }
        let (_, current) = self.management_store_snapshot()?;
        if current.fingerprint != witness.fingerprint
            || !self.management_publication_current(witness)
        {
            return Err(TokenStoreError::InvalidCredential);
        }
        Ok(())
    }

    pub fn verify_management_profile(
        &self,
        stamp: &ManagementProfileStamp,
    ) -> Result<(), TokenStoreError> {
        if !self.management_profile
            || self.management_mutations.load(Ordering::SeqCst) != 0
            || self.cache_epoch.load(Ordering::SeqCst) != stamp.epoch
        {
            return Err(TokenStoreError::InvalidCredential);
        }
        let (_, current) = self.management_store_snapshot()?;
        if current.epoch != stamp.epoch
            || current.fingerprint != stamp.fingerprint
            || self.management_mutations.load(Ordering::SeqCst) != 0
            || self.cache_epoch.load(Ordering::SeqCst) != stamp.epoch
        {
            return Err(TokenStoreError::InvalidCredential);
        }
        Ok(())
    }

    fn begin_management_mutation(&self) -> Option<ManagementMutation> {
        if !self.management_profile {
            return None;
        }
        self.management_mutations.fetch_add(1, Ordering::SeqCst);
        self.cache_epoch.fetch_add(1, Ordering::SeqCst);
        Some(ManagementMutation {
            epoch: self.cache_epoch.clone(),
            active: self.management_mutations.clone(),
        })
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
        let _mutation = self.begin_management_mutation();
        self.persistent.remove(keys::DEVICE_TOKENS)?;
        self.remove_user_credentials()
    }

    /// Disconnect the account without removing the separately owned device identity.
    pub fn remove_user_credentials(&self) -> Result<(), TokenStoreError> {
        let _mutation = self.begin_management_mutation();
        if let Some(session) = self.get_management_store_session()? {
            self.save_device_license(&session.device)?;
            self.save_device_token(PASSPORT_STS.to_owned(), Token::Legacy(session.device_token))?;
        }
        self.cache_epoch.fetch_add(1, Ordering::SeqCst);
        let mut keys = vec![keys::USER_TOKENS, keys::USER_INFO, keys::XAL_USER_SESSION];
        if self.management_profile {
            keys.push(keys::STORE_USER_SESSION);
            keys.push(keys::PENDING_STORE_EXCHANGE);
        }
        for key in keys {
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
        if let Some(session) = self.get_management_store_session()? {
            return Ok(session.device);
        }
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
        if address == PASSPORT_STS
            && let Some(session) = self.get_management_store_session()?
        {
            return Ok(Some(Token::Legacy(session.device_token)));
        }
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
        if let Some(mut session) = self.get_management_store_session()? {
            return Ok(session.tokens.remove(address));
        }
        Self::read_token_store(&*self.persistent, keys::USER_TOKENS, address)
    }

    pub fn save_user_token(&self, address: String, token: Token) -> Result<(), TokenStoreError> {
        Self::write_token_store(&*self.persistent, keys::USER_TOKENS, address, token)
    }

    pub fn replace_user_tokens(
        &self,
        tokens: HashMap<String, Token>,
    ) -> Result<(), TokenStoreError> {
        let _mutation = self.begin_management_mutation();
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
        if let Some(session) = self.get_management_store_session()? {
            return Ok(session.user);
        }
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

    fn fixture_store_session() -> crate::models::secrets::ManagementStoreSession {
        use base64::Engine;
        let mut secret = vec![0; 4096];
        secret[..4].copy_from_slice(&4u32.to_le_bytes());
        let token = crate::models::secrets::LegacyToken {
            key_name: Some(PASSPORT_STS.to_owned()),
            token: quick_xml::se::to_string(&crate::models::soap::EncryptedData::devicesoftware(
                base64::prelude::BASE64_STANDARD.encode(b"fixture-not-a-real-credential"),
            ))
            .unwrap(),
            binary_secret: Some(base64::prelude::BASE64_STANDARD.encode(secret)),
            tpm_key: None,
            lifetime: crate::models::soap::Timestamp {
                id: None,
                created: "2026-01-01T00:00:00Z".to_owned(),
                expires: "2099-01-01T00:00:00Z".to_owned(),
            },
        };
        crate::models::secrets::ManagementStoreSession {
            flow_id: "fixture-flow".to_owned(),
            user: User {
                puid: "fixture".to_owned(),
                username: "fixture".to_owned(),
            },
            tokens: HashMap::from([(PASSPORT_STS.to_owned(), Token::Legacy(token.clone()))]),
            device: Device {
                username: "fixture".to_owned(),
                password: "fixture-not-a-password".to_owned(),
                splicense: "fixture-not-a-license".to_owned(),
                puid: "fixture".to_owned(),
                hwid: "fixture".to_owned(),
                device_id: "fixture".to_owned(),
            },
            device_token: token,
        }
    }

    fn fixture_pending_exchange() -> crate::models::secrets::PendingManagementExchange {
        let session = fixture_store_session();
        crate::models::secrets::PendingManagementExchange::new(
            session.flow_id,
            session.device,
            session.device_token,
            crate::models::live::DAProperty {
                da_token: "NEUTRAL_NOT_TOKEN".to_owned(),
                da_session_key: "NEUTRAL_NOT_KEY".to_owned(),
                da_start_time: "2026-01-01T00:00:00Z".to_owned(),
                da_expires: "2099-01-01T00:00:00Z".to_owned(),
                sts_inline_flow_token: "NEUTRAL_INLINE".to_owned(),
                username: "fixture".to_owned(),
                puid: "fixture".to_owned(),
            },
            false,
        )
        .unwrap()
    }

    #[test]
    fn pending_exchange_is_management_only_bounded_and_not_a_signed_in_session() {
        let memory = Arc::new(MemoryBackend::default());
        let manager = TokenManager::with_management_backend(memory.clone());
        assert!(
            TokenManager::with_memory()
                .save_pending_management_exchange(fixture_pending_exchange())
                .is_err()
        );
        manager
            .save_pending_management_exchange(fixture_pending_exchange())
            .unwrap();
        assert!(manager.get_management_store_session().unwrap().is_none());
        let pending = manager.get_pending_management_exchange().unwrap().unwrap();
        assert!(pending.expires_at <= pending.created_at + chrono::Duration::seconds(300));
        assert!(!pending.after_continuation);
        manager
            .save_management_store_session(fixture_store_session())
            .unwrap();
        assert!(memory.get(keys::PENDING_STORE_EXCHANGE).unwrap().is_none());
        assert!(
            manager
                .get_management_store_session()
                .unwrap()
                .unwrap()
                .valid()
        );
        assert!(
            manager
                .save_pending_management_exchange(fixture_pending_exchange())
                .is_err()
        );
    }

    #[test]
    fn expired_pending_exchange_is_deleted_without_deleting_device_or_final_profile() {
        let memory = Arc::new(MemoryBackend::default());
        let manager = TokenManager::with_management_backend(memory.clone());
        let mut pending = fixture_pending_exchange();
        pending.created_at = chrono::Utc::now() - chrono::Duration::seconds(301);
        pending.expires_at = chrono::Utc::now() - chrono::Duration::seconds(1);
        memory
            .set(
                keys::PENDING_STORE_EXCHANGE,
                &serde_json::to_vec(&pending).unwrap(),
            )
            .unwrap();
        manager.save_device_license(&pending.device).unwrap();
        let device_before = memory.get(keys::DEV_LICENSE).unwrap();
        assert!(manager.get_pending_management_exchange().unwrap().is_none());
        assert!(memory.get(keys::PENDING_STORE_EXCHANGE).unwrap().is_none());
        assert_eq!(memory.get(keys::DEV_LICENSE).unwrap(), device_before);
    }

    #[test]
    fn pending_exchange_rejects_corruption_and_overlong_or_extended_expiry() {
        let memory = Arc::new(MemoryBackend::default());
        let manager = TokenManager::with_management_backend(memory.clone());
        let mut pending = fixture_pending_exchange();
        pending.expires_at += chrono::Duration::seconds(301);
        assert!(manager.save_pending_management_exchange(pending).is_err());
        memory
            .set(keys::PENDING_STORE_EXCHANGE, b"PRIVATE_SENTINEL")
            .unwrap();
        assert!(manager.get_pending_management_exchange().is_err());
        memory
            .set(keys::PENDING_STORE_EXCHANGE, &vec![0; 192 * 1024 + 1])
            .unwrap();
        assert!(manager.get_pending_management_exchange().is_err());
    }

    #[test]
    fn pending_cleanup_failure_is_explicit_and_preserves_the_committed_final_session() {
        #[derive(Default)]
        struct RejectPendingRemoval(MemoryBackend);
        impl TokenBackend for RejectPendingRemoval {
            fn get(&self, key: &str) -> Result<Option<Vec<u8>>, TokenStoreError> {
                self.0.get(key)
            }
            fn set(&self, key: &str, value: &[u8]) -> Result<(), TokenStoreError> {
                self.0.set(key, value)
            }
            fn remove(&self, key: &str) -> Result<(), TokenStoreError> {
                if key == keys::PENDING_STORE_EXCHANGE {
                    Err(TokenStoreError::InvalidCredential)
                } else {
                    self.0.remove(key)
                }
            }
        }
        let manager =
            TokenManager::with_management_backend(Arc::new(RejectPendingRemoval::default()));
        manager
            .save_pending_management_exchange(fixture_pending_exchange())
            .unwrap();
        assert!(
            manager
                .save_management_store_session(fixture_store_session())
                .is_err()
        );
        assert!(
            manager
                .get_management_store_session()
                .unwrap()
                .unwrap()
                .valid()
        );
        assert!(manager.get_pending_management_exchange().unwrap().is_some());
    }

    #[test]
    fn management_snapshot_is_readonly_and_order_independent() {
        let memory = Arc::new(MemoryBackend::default());
        let tokens = TokenManager::with_management_backend(memory.clone());
        tokens
            .save_management_store_session(fixture_store_session())
            .unwrap();
        let original = memory.get(keys::STORE_USER_SESSION).unwrap().unwrap();
        let (session, stamp) = tokens.management_store_snapshot().unwrap();
        assert_eq!(session.user.username, "fixture");
        for _ in 0..5 {
            tokens.verify_management_profile(&stamp).unwrap();
        }

        assert_eq!(
            memory.get(keys::STORE_USER_SESSION).unwrap().unwrap(),
            original
        );
        assert!(memory.get(keys::USER_TOKENS).unwrap().is_none());
    }

    #[test]
    fn management_interaction_clones_keep_profile_fences_and_readonly_last_without_keychain_access()
    {
        let manager = TokenManager::with_management_backend(Arc::new(MemoryBackend::default()));
        manager
            .save_management_store_session(fixture_store_session())
            .unwrap();
        let (_, stamp) = manager.management_store_snapshot().unwrap();
        let witness = stamp.publication_witness();
        for clone in [
            manager
                .with_explicit_management_keychain_interaction()
                .unwrap(),
            manager.with_noninteractive_management_keychain().unwrap(),
        ] {
            assert!(clone.management_publication_current(&witness));
            assert!(Arc::ptr_eq(&clone.cache_epoch, &manager.cache_epoch));
            assert!(Arc::ptr_eq(
                &clone.management_mutations,
                &manager.management_mutations
            ));
            let readonly = clone.readonly_management_profile().unwrap();
            assert!(readonly.save_user(&fixture_store_session().user).is_err());
        }
        manager
            .save_management_store_session(fixture_store_session())
            .unwrap();
        let silent = manager.with_noninteractive_management_keychain().unwrap();
        assert!(!silent.management_publication_current(&witness));
        assert!(
            TokenManager::with_memory()
                .with_noninteractive_management_keychain()
                .is_err()
        );
    }

    #[test]
    fn readonly_management_profile_rejects_every_persistent_write_and_keeps_shared_fences() {
        let memory = Arc::new(MemoryBackend::default());
        let manager = TokenManager::with_management_backend(memory.clone());
        manager
            .save_management_store_session(fixture_store_session())
            .unwrap();
        let original = memory.get(keys::STORE_USER_SESSION).unwrap();
        let readonly = manager.readonly_management_profile().unwrap();
        let (_, stamp) = readonly.management_store_snapshot().unwrap();
        assert!(
            readonly
                .save_management_store_session(fixture_store_session())
                .is_err()
        );
        assert!(readonly.save_user(&fixture_store_session().user).is_err());
        assert!(readonly.remove_user_credentials().is_err());
        assert_eq!(memory.get(keys::STORE_USER_SESSION).unwrap(), original);
        manager
            .save_management_store_session(fixture_store_session())
            .unwrap();
        assert!(readonly.verify_management_profile(&stamp).is_err());
        assert!(
            TokenManager::with_memory()
                .readonly_management_profile()
                .is_err()
        );
    }

    #[test]
    fn management_snapshot_fences_identical_recommit_and_logout_across_clones() {
        let tokens = TokenManager::with_management_backend(Arc::new(MemoryBackend::default()));
        tokens
            .save_management_store_session(fixture_store_session())
            .unwrap();
        let clone = tokens.clone();
        let (_, stamp) = tokens.management_store_snapshot().unwrap();
        clone
            .save_management_store_session(fixture_store_session())
            .unwrap();
        assert!(tokens.verify_management_profile(&stamp).is_err());
        let (_, stamp) = tokens.management_store_snapshot().unwrap();
        clone.remove_user_credentials().unwrap();
        assert!(tokens.verify_management_profile(&stamp).is_err());
        assert!(tokens.management_store_snapshot().is_err());
        assert!(tokens.get_device_sts_token().is_ok());
    }

    #[test]
    fn management_publication_witness_is_bound_to_owner_epoch_and_unexpired_proof() {
        let manager = TokenManager::with_management_backend(Arc::new(MemoryBackend::default()));
        manager
            .save_management_store_session(fixture_store_session())
            .unwrap();
        let (_, stamp) = manager.management_store_snapshot().unwrap();
        let witness = stamp.publication_witness();
        assert!(manager.management_publication_current(&witness));
        assert!(
            manager
                .readonly_management_profile()
                .unwrap()
                .management_publication_current(&witness)
        );
        let other = TokenManager::with_management_backend(Arc::new(MemoryBackend::default()));
        other
            .save_management_store_session(fixture_store_session())
            .unwrap();
        assert!(!other.management_publication_current(&witness));
        let mut expired = witness.clone();
        expired.expires_at = chrono::Utc::now();
        assert!(!manager.management_publication_current(&expired));
        manager
            .save_management_store_session(fixture_store_session())
            .unwrap();
        assert!(!manager.management_publication_current(&witness));
        let (_, stamp) = manager.management_store_snapshot().unwrap();
        let witness = stamp.publication_witness();
        manager.remove_user_credentials().unwrap();
        assert!(!manager.management_publication_current(&witness));
    }

    #[test]
    fn management_publication_reads_fence_independent_writers_on_a_shared_backend() {
        let memory = Arc::new(MemoryBackend::default());
        let first = TokenManager::with_management_backend(memory.clone());
        let second = TokenManager::with_management_backend(memory);
        first
            .save_management_store_session(fixture_store_session())
            .unwrap();
        let (_, stamp) = first.management_store_snapshot().unwrap();
        let witness = stamp.publication_witness();
        first.verify_management_publication(&witness).unwrap();
        let mut replacement = fixture_store_session();
        replacement.flow_id = "fixture-independent-flow".to_owned();
        second.save_management_store_session(replacement).unwrap();
        assert!(first.management_publication_current(&witness));
        assert!(first.verify_management_publication(&witness).is_err());
        let (_, stamp) = first.management_store_snapshot().unwrap();
        let witness = stamp.publication_witness();
        second.remove_user_credentials().unwrap();
        assert!(first.management_publication_current(&witness));
        assert!(first.verify_management_publication(&witness).is_err());
    }

    #[test]
    fn management_snapshot_rejects_recommit_during_backend_read() {
        #[derive(Default)]
        struct MutatingBackend {
            memory: MemoryBackend,
            on_read: std::sync::Mutex<Option<TokenManager>>,
        }

        impl TokenBackend for MutatingBackend {
            fn get(&self, key: &str) -> Result<Option<Vec<u8>>, TokenStoreError> {
                let result = self.memory.get(key)?;
                let manager = self.on_read.lock().unwrap().take();
                if let Some(manager) = manager {
                    manager.save_management_store_session(fixture_store_session())?;
                }
                Ok(result)
            }

            fn set(&self, key: &str, value: &[u8]) -> Result<(), TokenStoreError> {
                self.memory.set(key, value)
            }

            fn remove(&self, key: &str) -> Result<(), TokenStoreError> {
                self.memory.remove(key)
            }
        }

        let backend = Arc::new(MutatingBackend::default());
        let manager = TokenManager::with_management_backend(backend.clone());
        manager
            .save_management_store_session(fixture_store_session())
            .unwrap();
        *backend.on_read.lock().unwrap() = Some(manager.clone());
        assert!(matches!(
            manager.management_store_snapshot(),
            Err(TokenStoreError::InvalidCredential)
        ));
        manager.management_store_snapshot().unwrap();
    }

    #[test]
    fn management_snapshot_fences_full_bundle_changes_without_local_epoch_mutation() {
        let memory = Arc::new(MemoryBackend::default());
        let tokens = TokenManager::with_management_backend(memory.clone());
        tokens
            .save_management_store_session(fixture_store_session())
            .unwrap();
        let (_, stamp) = tokens.management_store_snapshot().unwrap();
        for field in ["username", "puid", "license", "expiry", "flow"] {
            let mut session = fixture_store_session();
            match field {
                "username" => session.user.username = "different-fixture".to_owned(),
                "puid" => session.user.puid = "different-fixture".to_owned(),
                "license" => session.device.splicense = "different-fixture".to_owned(),
                "expiry" => {
                    session.device_token.lifetime.expires = "2098-01-01T00:00:00Z".to_owned()
                }
                "flow" => session.flow_id = "different-fixture".to_owned(),
                _ => unreachable!(),
            }
            memory
                .set(
                    keys::STORE_USER_SESSION,
                    &serde_json::to_vec(&session).unwrap(),
                )
                .unwrap();
            assert!(tokens.verify_management_profile(&stamp).is_err());
        }
    }

    #[derive(Clone, Copy, PartialEq, Eq)]
    enum MutationOperation {
        Set,
        Remove,
    }

    struct PausedMutation {
        operation: MutationOperation,
        ready: std::sync::mpsc::Sender<()>,
        release: std::sync::mpsc::Receiver<()>,
    }

    #[derive(Default)]
    struct PausedBackend {
        memory: MemoryBackend,
        pending: std::sync::Mutex<Option<PausedMutation>>,
        fail_set: std::sync::atomic::AtomicBool,
    }

    impl PausedBackend {
        fn pause(
            &self,
            operation: MutationOperation,
        ) -> (std::sync::mpsc::Receiver<()>, std::sync::mpsc::Sender<()>) {
            let (ready, wait) = std::sync::mpsc::channel();
            let (release, receive) = std::sync::mpsc::channel();
            *self.pending.lock().unwrap() = Some(PausedMutation {
                operation,
                ready,
                release: receive,
            });
            (wait, release)
        }

        fn checkpoint(
            &self,
            operation: MutationOperation,
            key: &str,
        ) -> Result<(), TokenStoreError> {
            if key != keys::STORE_USER_SESSION {
                return Ok(());
            }
            let checkpoint = {
                let mut pending = self.pending.lock().unwrap();
                if pending
                    .as_ref()
                    .is_some_and(|pause| pause.operation == operation)
                {
                    pending.take()
                } else {
                    None
                }
            };
            if let Some(checkpoint) = checkpoint {
                checkpoint
                    .ready
                    .send(())
                    .map_err(|_| std::io::Error::other("Synthetic mutation peer closed"))?;
                checkpoint
                    .release
                    .recv_timeout(std::time::Duration::from_secs(2))
                    .map_err(|_| {
                        std::io::Error::other("Synthetic mutation peer did not release")
                    })?;
            }
            Ok(())
        }
    }

    impl TokenBackend for PausedBackend {
        fn get(&self, key: &str) -> Result<Option<Vec<u8>>, TokenStoreError> {
            self.memory.get(key)
        }

        fn set(&self, key: &str, value: &[u8]) -> Result<(), TokenStoreError> {
            self.checkpoint(MutationOperation::Set, key)?;
            if self.fail_set.load(Ordering::SeqCst) {
                return Err(std::io::Error::other("Synthetic credential write failed").into());
            }
            self.memory.set(key, value)
        }

        fn remove(&self, key: &str) -> Result<(), TokenStoreError> {
            self.checkpoint(MutationOperation::Remove, key)?;
            self.memory.remove(key)
        }
    }

    #[test]
    fn management_snapshot_refuses_commit_until_blocked_backend_write_finishes() {
        let backend = Arc::new(PausedBackend::default());
        let manager = TokenManager::with_management_backend(backend.clone());
        manager
            .save_management_store_session(fixture_store_session())
            .unwrap();
        let (_, stamp) = manager.management_store_snapshot().unwrap();
        let (ready, release) = backend.pause(MutationOperation::Set);
        let writer = manager.clone();
        let work = std::thread::spawn(move || {
            writer.save_management_store_session(fixture_store_session())
        });
        ready
            .recv_timeout(std::time::Duration::from_secs(2))
            .unwrap();
        assert!(
            backend
                .memory
                .get(keys::STORE_USER_SESSION)
                .unwrap()
                .is_some()
        );
        assert!(matches!(
            manager.management_store_snapshot(),
            Err(TokenStoreError::InvalidCredential)
        ));
        assert!(manager.verify_management_profile(&stamp).is_err());
        release.send(()).unwrap();
        work.join().unwrap().unwrap();
        let (_, current) = manager.management_store_snapshot().unwrap();
        manager.verify_management_profile(&current).unwrap();
        assert!(manager.verify_management_profile(&stamp).is_err());
    }

    #[test]
    fn management_snapshot_refuses_logout_until_blocked_backend_delete_finishes() {
        let backend = Arc::new(PausedBackend::default());
        let manager = TokenManager::with_management_backend(backend.clone());
        manager
            .save_management_store_session(fixture_store_session())
            .unwrap();
        let (_, stamp) = manager.management_store_snapshot().unwrap();
        let (ready, release) = backend.pause(MutationOperation::Remove);
        let writer = manager.clone();
        let work = std::thread::spawn(move || writer.remove_user_credentials());
        ready
            .recv_timeout(std::time::Duration::from_secs(2))
            .unwrap();
        assert!(
            backend
                .memory
                .get(keys::STORE_USER_SESSION)
                .unwrap()
                .is_some()
        );
        assert!(matches!(
            manager.management_store_snapshot(),
            Err(TokenStoreError::InvalidCredential)
        ));
        assert!(manager.verify_management_profile(&stamp).is_err());
        release.send(()).unwrap();
        work.join().unwrap().unwrap();
        assert!(matches!(
            manager.management_store_snapshot(),
            Err(TokenStoreError::NotFound)
        ));
        assert!(manager.get_device_sts_token().is_ok());
    }

    #[test]
    fn management_snapshot_stays_fenced_until_every_overlapping_write_finishes() {
        let backend = Arc::new(PausedBackend::default());
        let manager = TokenManager::with_management_backend(backend.clone());
        manager
            .save_management_store_session(fixture_store_session())
            .unwrap();
        let (ready, release) = backend.pause(MutationOperation::Set);
        let writer = manager.clone();
        let work = std::thread::spawn(move || {
            writer.save_management_store_session(fixture_store_session())
        });
        ready
            .recv_timeout(std::time::Duration::from_secs(2))
            .unwrap();
        let mut replacement = fixture_store_session();
        replacement.user.username = "different-fixture".to_owned();
        manager
            .clone()
            .save_management_store_session(replacement)
            .unwrap();
        assert!(matches!(
            manager.management_store_snapshot(),
            Err(TokenStoreError::InvalidCredential)
        ));
        release.send(()).unwrap();
        work.join().unwrap().unwrap();
        let (current, stamp) = manager.management_store_snapshot().unwrap();
        assert_eq!(current.user.username, "fixture");
        manager.verify_management_profile(&stamp).unwrap();
    }

    #[test]
    fn management_snapshot_write_failure_releases_fence_but_invalidates_previous_stamp() {
        let backend = Arc::new(PausedBackend::default());
        let manager = TokenManager::with_management_backend(backend.clone());
        manager
            .save_management_store_session(fixture_store_session())
            .unwrap();
        let (_, stamp) = manager.management_store_snapshot().unwrap();
        backend.fail_set.store(true, Ordering::SeqCst);
        assert!(
            manager
                .save_management_store_session(fixture_store_session())
                .is_err()
        );
        assert!(manager.verify_management_profile(&stamp).is_err());
        let (_, current) = manager.management_store_snapshot().unwrap();
        manager.verify_management_profile(&current).unwrap();
    }

    #[test]
    fn management_snapshot_unwind_releases_fence_without_reviving_previous_stamp() {
        let manager = TokenManager::with_management_backend(Arc::new(MemoryBackend::default()));
        manager
            .save_management_store_session(fixture_store_session())
            .unwrap();
        let (_, stamp) = manager.management_store_snapshot().unwrap();
        let writer = manager.clone();
        let work = std::thread::spawn(move || {
            let _mutation = writer.begin_management_mutation();
            panic!("Synthetic mutation unwind");
        });
        assert!(work.join().is_err());
        assert!(manager.verify_management_profile(&stamp).is_err());
        let (_, current) = manager.management_store_snapshot().unwrap();
        manager.verify_management_profile(&current).unwrap();
    }

    #[test]
    fn management_snapshot_refuses_default_profile_and_legacy_key_fallback() {
        let memory = Arc::new(MemoryBackend::default());
        let tokens = TokenManager::with_management_backend(memory.clone());
        let session = fixture_store_session();
        tokens.save_user(&session.user).unwrap();
        tokens
            .save_user_token(
                PASSPORT_STS.to_owned(),
                session.tokens[PASSPORT_STS].clone(),
            )
            .unwrap();
        tokens
            .save_device_token(PASSPORT_STS.to_owned(), Token::Legacy(session.device_token))
            .unwrap();
        assert!(tokens.management_store_snapshot().is_err());
        tokens
            .save_management_store_session(fixture_store_session())
            .unwrap();
        let default = TokenManager::new(memory, Arc::new(MemoryBackend::default()));
        assert!(default.management_store_snapshot().is_err());
    }

    #[test]
    fn store_bundle_serves_existing_providers_and_logout_retains_only_device() {
        let memory = Arc::new(MemoryBackend::default());
        let tokens = TokenManager::with_management_backend(memory.clone());
        tokens
            .save_management_store_session(fixture_store_session())
            .unwrap();
        assert!(memory.get(keys::USER_TOKENS).unwrap().is_none());
        assert!(memory.get(keys::USER_INFO).unwrap().is_none());
        assert!(tokens.get_user_sts_token().is_ok());
        assert!(tokens.get_user().is_ok());
        assert!(tokens.get_device_sts_token().is_ok());
        tokens.remove_user_credentials().unwrap();
        assert!(tokens.get_management_store_session().unwrap().is_none());
        assert!(matches!(
            tokens.get_user_sts_token(),
            Err(TokenStoreError::NotFound)
        ));
        assert!(tokens.get_device_license().is_ok());
        assert!(tokens.get_device_sts_token().is_ok());
    }

    #[test]
    fn default_cli_profile_never_reads_imports_or_deletes_management_bundle() {
        let memory = Arc::new(MemoryBackend::default());
        let management = TokenManager::with_management_backend(memory.clone());
        management
            .save_management_store_session(fixture_store_session())
            .unwrap();
        let original = memory.get(keys::STORE_USER_SESSION).unwrap().unwrap();
        let cli = TokenManager::new(memory.clone(), Arc::new(MemoryBackend::default()));
        assert!(cli.get_management_store_session().unwrap().is_none());
        assert!(matches!(cli.get_user(), Err(TokenStoreError::NotFound)));
        assert!(matches!(
            cli.get_device_license(),
            Err(TokenStoreError::NotFound)
        ));
        assert!(
            cli.save_management_store_session(fixture_store_session())
                .is_err()
        );
        cli.remove_user_credentials().unwrap();
        assert_eq!(
            memory.get(keys::STORE_USER_SESSION).unwrap().unwrap(),
            original
        );
    }

    #[test]
    fn incomplete_expired_or_wrong_audience_proof_never_commits_a_bundle() {
        let memory = Arc::new(MemoryBackend::default());
        let tokens = TokenManager::with_management_backend(memory.clone());
        let mut incomplete = fixture_store_session();
        incomplete.device_token.binary_secret = None;
        assert!(tokens.save_management_store_session(incomplete).is_err());
        let mut expired = fixture_store_session();
        expired.device_token.lifetime.expires = "2000-01-01T00:00:00Z".to_owned();
        assert!(tokens.save_management_store_session(expired).is_err());
        let mut wrong = fixture_store_session();
        wrong.tokens.clear();
        assert!(tokens.save_management_store_session(wrong).is_err());
        assert!(memory.get(keys::STORE_USER_SESSION).unwrap().is_none());
    }

    struct RetentionFailure {
        memory: MemoryBackend,
        writes: AtomicU64,
    }
    impl TokenBackend for RetentionFailure {
        fn get(&self, key: &str) -> Result<Option<Vec<u8>>, TokenStoreError> {
            self.memory.get(key)
        }
        fn set(&self, key: &str, value: &[u8]) -> Result<(), TokenStoreError> {
            self.writes.fetch_add(1, Ordering::SeqCst);
            if key != keys::STORE_USER_SESSION {
                return Err(TokenStoreError::InvalidCredential);
            }
            self.memory.set(key, value)
        }
        fn remove(&self, key: &str) -> Result<(), TokenStoreError> {
            self.memory.remove(key)
        }
    }

    #[test]
    fn user_device_commit_is_one_write_and_failed_logout_retention_keeps_user_bundle() {
        let persistent = Arc::new(RetentionFailure {
            memory: MemoryBackend::default(),
            writes: AtomicU64::new(0),
        });
        let tokens = TokenManager::with_management_backend(persistent.clone());
        tokens
            .save_management_store_session(fixture_store_session())
            .unwrap();
        assert_eq!(persistent.writes.load(Ordering::SeqCst), 1);
        let original = persistent.get(keys::STORE_USER_SESSION).unwrap().unwrap();
        assert!(tokens.remove_user_credentials().is_err());
        assert_eq!(
            persistent.get(keys::STORE_USER_SESSION).unwrap().unwrap(),
            original
        );
        assert!(tokens.get_user_sts_token().is_ok());
    }
}
