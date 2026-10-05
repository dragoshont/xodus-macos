use std::time::Duration;

use xodus::models::secrets::{Device, LegacyToken, Token, User};
use xodus::tokens::store::TokenStoreError;
use xodus::tokens::{ManagementProfileStamp, PASSPORT_STS, TokenManager};

const IO_DEADLINE: Duration = Duration::from_secs(2);
static IO_PERMITS: tokio::sync::Semaphore = tokio::sync::Semaphore::const_new(4);

#[derive(Debug, PartialEq, Eq)]
pub enum CredentialError {
    DeviceUnavailable,
    AccountUnavailable,
    UserUnavailable,
    UnsupportedDevice,
    UnsupportedUser,
    DeviceLicenseUnavailable,
    AuthenticationRequired,
    ProfileChanged,
    StoreUnavailable,
    Deadline,
}

impl std::fmt::Display for CredentialError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter.write_str(match self {
            Self::DeviceUnavailable => "Device credentials are unavailable",
            Self::AccountUnavailable => "Signed-in account is unavailable",
            Self::UserUnavailable => "User credentials are unavailable",
            Self::UnsupportedDevice => "Invalid STS token",
            Self::UnsupportedUser => "Unsupported user token",
            Self::DeviceLicenseUnavailable => "Device license is unavailable",
            Self::AuthenticationRequired => "A valid isolated management account is required",
            Self::ProfileChanged => "The account profile changed during the provider request",
            Self::StoreUnavailable => {
                "The credential store could not complete the provider request"
            }
            Self::Deadline => "The provider credential request exceeded its deadline",
        })
    }
}

impl std::error::Error for CredentialError {}

pub struct ProviderCredentials {
    pub user: LegacyToken,
    pub device: LegacyToken,
    pub account: User,
    license: Option<Device>,
    stamp: Option<ManagementProfileStamp>,
}

impl ProviderCredentials {
    #[cfg(test)]
    pub(crate) fn read_neutral(tokens: &TokenManager) -> Result<Self, CredentialError> {
        Self::management(tokens)
    }

    pub async fn read(tokens: &TokenManager) -> Result<Self, CredentialError> {
        if !tokens.is_management_profile() {
            return Self::legacy(tokens);
        }
        if !xodus::secrets::management_native_keychain_enabled() {
            return Err(CredentialError::AuthenticationRequired);
        }
        let tokens = tokens.clone();
        credential_io(move || Self::management(&tokens)).await
    }

    fn management(tokens: &TokenManager) -> Result<Self, CredentialError> {
        let (mut session, stamp) =
            tokens
                .management_store_snapshot()
                .map_err(|error| match error {
                    TokenStoreError::NotFound | TokenStoreError::InvalidCredential => {
                        CredentialError::AuthenticationRequired
                    }
                    _ => CredentialError::StoreUnavailable,
                })?;
        let Some(Token::Legacy(user)) = session.tokens.remove(PASSPORT_STS) else {
            return Err(CredentialError::AuthenticationRequired);
        };
        Ok(Self {
            user,
            device: session.device_token,
            account: session.user,
            license: Some(session.device),
            stamp: Some(stamp),
        })
    }

    fn legacy(tokens: &TokenManager) -> Result<Self, CredentialError> {
        let Token::Legacy(device) = tokens
            .get_device_sts_token()
            .map_err(|_| CredentialError::DeviceUnavailable)?
        else {
            return Err(CredentialError::UnsupportedDevice);
        };
        let account = tokens
            .get_user()
            .map_err(|_| CredentialError::AccountUnavailable)?;
        let Token::Legacy(user) = tokens
            .get_user_sts_token()
            .map_err(|_| CredentialError::UserUnavailable)?
        else {
            return Err(CredentialError::UnsupportedUser);
        };
        Ok(Self {
            user,
            device,
            account,
            license: None,
            stamp: None,
        })
    }

    pub async fn verify_current(&self, tokens: &TokenManager) -> Result<(), CredentialError> {
        let Some(stamp) = self.stamp.clone() else {
            return Ok(());
        };
        let tokens = tokens.clone();
        credential_io(move || {
            tokens
                .verify_management_profile(&stamp)
                .map_err(|error| match error {
                    TokenStoreError::NotFound | TokenStoreError::InvalidCredential => {
                        CredentialError::ProfileChanged
                    }
                    _ => CredentialError::StoreUnavailable,
                })
        })
        .await
    }

    pub fn publication_witness(&self) -> Option<xodus::tokens::ManagementProfileWitness> {
        self.stamp
            .as_ref()
            .map(ManagementProfileStamp::publication_witness)
    }

    pub fn device_license_block(&self, tokens: &TokenManager) -> Result<String, CredentialError> {
        if let Some(device) = &self.license {
            return Ok(device.splicense.clone());
        }
        tokens
            .get_device_license()
            .map(|device| device.splicense)
            .map_err(|_| CredentialError::DeviceLicenseUnavailable)
    }
}

async fn credential_io<T: Send + 'static>(
    read: impl FnOnce() -> Result<T, CredentialError> + Send + 'static,
) -> Result<T, CredentialError> {
    bounded_credential_io(&IO_PERMITS, IO_DEADLINE, read).await
}

async fn bounded_credential_io<T: Send + 'static>(
    permits: &'static tokio::sync::Semaphore,
    timeout: Duration,
    read: impl FnOnce() -> Result<T, CredentialError> + Send + 'static,
) -> Result<T, CredentialError> {
    let deadline = tokio::time::Instant::now() + timeout;
    let permit = tokio::time::timeout_at(deadline, permits.acquire())
        .await
        .map_err(|_| CredentialError::Deadline)?
        .map_err(|_| CredentialError::StoreUnavailable)?;
    let work = tokio::task::spawn_blocking(move || {
        let _permit = permit;
        read()
    });
    tokio::time::timeout_at(deadline, work)
        .await
        .map_err(|_| CredentialError::Deadline)?
        .map_err(|_| CredentialError::StoreUnavailable)?
}

#[cfg(test)]
mod tests {
    use super::*;
    use base64::Engine;
    use std::sync::Arc;
    use xodus::models::secrets::ManagementStoreSession;
    use xodus::tokens::backend::MemoryBackend;
    use xodus::tokens::store::TokenBackend;

    fn session() -> ManagementStoreSession {
        let mut secret = vec![0; 4096];
        secret[..4].copy_from_slice(&4u32.to_le_bytes());
        let token = LegacyToken {
            key_name: Some(PASSPORT_STS.to_owned()),
            token: quick_fixture_token(),
            binary_secret: Some(base64::prelude::BASE64_STANDARD.encode(secret)),
            tpm_key: None,
            lifetime: xodus::models::soap::Timestamp {
                id: None,
                created: "2026-01-01T00:00:00Z".to_owned(),
                expires: "2099-01-01T00:00:00Z".to_owned(),
            },
        };
        ManagementStoreSession {
            flow_id: "fixture-flow".to_owned(),
            user: User {
                username: "fixture@example.invalid".to_owned(),
                puid: "fixture".to_owned(),
            },
            tokens: std::collections::HashMap::from([(
                PASSPORT_STS.to_owned(),
                Token::Legacy(token.clone()),
            )]),
            device_token: token,
            device: Device {
                username: "fixture".to_owned(),
                password: "fixture-not-password".to_owned(),
                splicense: "fixture-not-license".to_owned(),
                puid: "fixture".to_owned(),
                hwid: "fixture".to_owned(),
                device_id: "fixture".to_owned(),
            },
        }
    }

    fn quick_fixture_token() -> String {
        format!(
            r#"<EncryptedData Id="fixture" xmlns="http://www.w3.org/2001/04/xmlenc#" Type="http://www.w3.org/2001/04/xmlenc#Element"><EncryptionMethod Algorithm="fixture"/><KeyInfo><KeyName>{PASSPORT_STS}</KeyName></KeyInfo><CipherData><CipherValue>Zml4dHVyZQ==</CipherValue></CipherData></EncryptedData>"#
        )
    }

    #[tokio::test]
    async fn management_provider_snapshot_is_readonly_and_preserves_actual_username() {
        let memory = Arc::new(MemoryBackend::default());
        let tokens = TokenManager::with_management_backend(memory.clone());
        tokens.save_management_store_session(session()).unwrap();
        let original = memory.get("management-store-user").unwrap().unwrap();
        let credentials = ProviderCredentials::management(&tokens).unwrap();
        assert_eq!(credentials.account.username, "fixture@example.invalid");
        assert_eq!(
            credentials.device_license_block(&tokens).unwrap(),
            "fixture-not-license"
        );
        credentials.verify_current(&tokens).await.unwrap();
        assert_eq!(
            memory.get("management-store-user").unwrap().unwrap(),
            original
        );
    }

    #[tokio::test]
    async fn management_provider_snapshot_rejects_recommit_and_logout_without_fallback() {
        let tokens = TokenManager::with_management_backend(Arc::new(MemoryBackend::default()));
        tokens.save_management_store_session(session()).unwrap();
        let credentials = ProviderCredentials::management(&tokens).unwrap();
        let mut next = session();
        next.user.username = "different@example.invalid".to_owned();
        tokens.save_management_store_session(next).unwrap();
        assert!(matches!(
            credentials.verify_current(&tokens).await,
            Err(CredentialError::ProfileChanged)
        ));
        tokens.remove_user_credentials().unwrap();
        let fixture = session();
        tokens.save_user(&fixture.user).unwrap();
        tokens
            .save_user_token(
                PASSPORT_STS.to_owned(),
                fixture.tokens[PASSPORT_STS].clone(),
            )
            .unwrap();
        assert!(matches!(
            ProviderCredentials::management(&tokens),
            Err(CredentialError::AuthenticationRequired)
        ));
    }

    #[tokio::test]
    async fn management_provider_io_deadline_retains_started_os_permit() {
        let permits: &'static tokio::sync::Semaphore =
            Box::leak(Box::new(tokio::sync::Semaphore::new(1)));
        let (release, wait) = std::sync::mpsc::channel();
        let result = bounded_credential_io(permits, Duration::from_millis(20), move || {
            wait.recv_timeout(Duration::from_secs(2))
                .map_err(|_| CredentialError::Deadline)?;
            Ok(())
        })
        .await;
        assert_eq!(result, Err(CredentialError::Deadline));
        assert_eq!(permits.available_permits(), 0);
        release.send(()).unwrap();
        tokio::time::timeout(Duration::from_secs(2), async {
            while permits.available_permits() == 0 {
                tokio::time::sleep(Duration::from_millis(1)).await;
            }
        })
        .await
        .unwrap();
    }

    #[tokio::test]
    async fn management_provider_io_caller_abort_retains_started_os_permit() {
        let permits: &'static tokio::sync::Semaphore =
            Box::leak(Box::new(tokio::sync::Semaphore::new(1)));
        let (started, ready) = tokio::sync::oneshot::channel();
        let (release, wait) = std::sync::mpsc::channel();
        let caller = tokio::spawn(bounded_credential_io(
            permits,
            Duration::from_secs(2),
            move || {
                started
                    .send(())
                    .map_err(|_| CredentialError::StoreUnavailable)?;
                wait.recv_timeout(Duration::from_secs(2))
                    .map_err(|_| CredentialError::Deadline)?;
                Ok(())
            },
        ));
        tokio::time::timeout(Duration::from_secs(2), ready)
            .await
            .unwrap()
            .unwrap();
        caller.abort();
        assert!(caller.await.unwrap_err().is_cancelled());
        assert_eq!(permits.available_permits(), 0);
        release.send(()).unwrap();
        tokio::time::timeout(Duration::from_secs(2), async {
            while permits.available_permits() == 0 {
                tokio::time::sleep(Duration::from_millis(1)).await;
            }
        })
        .await
        .unwrap();
    }
}
