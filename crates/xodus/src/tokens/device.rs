use crate::hardware;
use crate::licensing::splicense::SPLicense;
use crate::licensing::utils::{generate_string, parse_bcrypt_rsa_private};
use crate::models::devicecredential::{Authentication, ClientInfo, DeviceAddRequest, DeviceInfo};
use crate::models::secrets::{Device, Token, device_token_structurally_valid, legacy_token_valid};
use crate::models::soap::BodyContent;
use crate::tokens::manager::{PASSPORT_STS, TokenManager};
use crate::tokens::store::TokenStoreError;

#[derive(Debug, thiserror::Error)]
pub enum DeviceCredentialError {
    #[error("Device credential storage is unavailable; review its permission")]
    StorageUnavailable,
    #[error("Stored device credentials are malformed; they were not replaced")]
    InvalidStoredCredential,
    #[error("Microsoft device credential request failed")]
    BrokerFailure,
    #[error("Microsoft device response did not contain complete valid credential proof")]
    InvalidBrokerProof,
}

fn storage_error(error: TokenStoreError) -> DeviceCredentialError {
    match error {
        TokenStoreError::Serde(_) | TokenStoreError::InvalidCredential => {
            DeviceCredentialError::InvalidStoredCredential
        }
        _ => DeviceCredentialError::StorageUnavailable,
    }
}

/// Called only by explicit legacy commands or the memory-only management login worker.
pub async fn ensure_device_credentials(
    client: &reqwest::Client,
    tokens: &TokenManager,
) -> Result<(), DeviceCredentialError> {
    let license = match tokens.get_device_license() {
        Ok(license) => license,
        Err(TokenStoreError::NotFound) => {
            match tokens.get_device_sts_token() {
                Err(TokenStoreError::NotFound) => {}
                Err(error) => return Err(storage_error(error)),
                Ok(_) => return Err(DeviceCredentialError::InvalidStoredCredential),
            }
            let device = provision_device(client, tokens).await?;
            return reauthenticate_device(client, tokens, device).await;
        }
        Err(error) => return Err(storage_error(error)),
    };
    match tokens.get_device_sts_token() {
        Ok(Token::Legacy(token))
            if device_token_structurally_valid(&token) && legacy_token_valid(&token) =>
        {
            return Ok(());
        }
        Ok(Token::Legacy(token)) if device_token_structurally_valid(&token) => {}
        Err(TokenStoreError::NotFound) => {}
        Err(error) => return Err(storage_error(error)),
        _ => return Err(DeviceCredentialError::InvalidStoredCredential),
    }
    reauthenticate_device(client, tokens, license).await
}

async fn provision_device(
    client: &reqwest::Client,
    tokens: &TokenManager,
) -> Result<Device, DeviceCredentialError> {
    let username = format!("02{}", generate_string(14));
    let password = generate_string(20);
    let provision = DeviceAddRequest {
        client_info: ClientInfo::default(),
        authentication: Authentication::new(username.clone(), password.clone()),
        device_info: Some(DeviceInfo {
            id: "DeviceInfo".to_string(),
            components: hardware::probe_provision_components(),
            tpm_info: None,
        }),
    };
    let dev = crate::api::live::login_device_credential(client, provision)
        .await
        .map_err(|_| DeviceCredentialError::BrokerFailure)?;
    if !dev.success
        || dev.puid.is_empty()
        || dev.hw_device_id.is_empty()
        || dev.license.splicense_block.is_empty()
    {
        return Err(DeviceCredentialError::InvalidBrokerProof);
    }
    let device = Device {
        username,
        password,
        puid: dev.puid,
        hwid: dev.hw_device_id,
        device_id: dev.license.binding.device_id.unwrap_or_default(),
        splicense: dev.license.splicense_block,
    };
    tokens.save_device_license(&device).map_err(storage_error)?;
    Ok(device)
}

async fn reauthenticate_device(
    client: &reqwest::Client,
    tokens: &TokenManager,
    license: Device,
) -> Result<(), DeviceCredentialError> {
    let sp_license = SPLicense::parse_base64(&license.splicense)
        .map_err(|_| DeviceCredentialError::InvalidStoredCredential)?;
    let key = sp_license
        .clep_sign_state
        .ok_or(DeviceCredentialError::InvalidStoredCredential)?
        .try_get_rsa_key()
        .map_err(|_| DeviceCredentialError::InvalidStoredCredential)?;
    let private_key = parse_bcrypt_rsa_private(&key)
        .map_err(|_| DeviceCredentialError::InvalidStoredCredential)?;
    let resp = crate::api::live::authenticate_device(client, license.username, private_key)
        .await
        .map_err(|_| DeviceCredentialError::BrokerFailure)?;
    let BodyContent::RequestSecurityTokenResponse(resp) = resp.body.body else {
        return Err(DeviceCredentialError::InvalidBrokerProof);
    };
    let token = Token::from_response_checked(*resp)
        .map_err(|_| DeviceCredentialError::InvalidBrokerProof)?;
    if !matches!(&token, Token::Legacy(token) if device_token_structurally_valid(token)) {
        return Err(DeviceCredentialError::InvalidBrokerProof);
    }
    tokens
        .save_device_token(PASSPORT_STS.to_owned(), token)
        .map_err(storage_error)
}

#[cfg(test)]
mod management_device_tests {
    use super::*;
    use crate::tokens::{backend::MemoryBackend, store::TokenBackend};
    use std::sync::Arc;

    struct Denied;
    impl TokenBackend for Denied {
        fn get(&self, _: &str) -> Result<Option<Vec<u8>>, TokenStoreError> {
            Err(TokenStoreError::Io(std::io::Error::from(
                std::io::ErrorKind::PermissionDenied,
            )))
        }
        fn set(&self, _: &str, _: &[u8]) -> Result<(), TokenStoreError> {
            panic!("must not provision after denied read")
        }
        fn remove(&self, _: &str) -> Result<(), TokenStoreError> {
            unreachable!()
        }
    }

    #[tokio::test]
    async fn denied_and_corrupt_device_reads_never_provision() {
        let denied = TokenManager::with_management_backend(Arc::new(Denied));
        assert!(matches!(
            ensure_device_credentials(&reqwest::Client::new(), &denied).await,
            Err(DeviceCredentialError::StorageUnavailable)
        ));
        let memory = Arc::new(MemoryBackend::default());
        memory.set("dev_license", b"{}").unwrap();
        let corrupt = TokenManager::with_management_backend(memory.clone());
        assert!(matches!(
            ensure_device_credentials(&reqwest::Client::new(), &corrupt).await,
            Err(DeviceCredentialError::InvalidStoredCredential)
        ));
        assert_eq!(memory.get("dev_license").unwrap().unwrap(), b"{}");
        assert!(memory.get("device-tokens").unwrap().is_none());
    }
}
