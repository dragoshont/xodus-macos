use crate::tokens::store::{TokenBackend, TokenStoreError};

pub struct KeychainBackend;

pub(crate) struct ManagementKeychainBackend {
    pub interactive_reads: bool,
}

#[cfg(target_os = "macos")]
fn management_operation<T>(
    interactive: bool,
    operation: impl FnOnce() -> Result<T, TokenStoreError>,
) -> Result<T, TokenStoreError> {
    use security_framework::os::macos::keychain::SecKeychain;
    static INTERACTION: std::sync::Mutex<()> = std::sync::Mutex::new(());
    let unavailable = || {
        TokenStoreError::Io(std::io::Error::other(
            "Launcher Keychain access is unavailable or another explicit operation is pending",
        ))
    };
    let _exclusive = INTERACTION.try_lock().map_err(|_| unavailable())?;
    let allowed = SecKeychain::user_interaction_allowed().map_err(|_| unavailable())?;
    let _no_prompt = if !interactive && allowed {
        Some(SecKeychain::disable_user_interaction().map_err(|_| unavailable())?)
    } else {
        None
    };
    operation()
}

#[cfg(not(target_os = "macos"))]
fn management_operation<T>(
    _interactive: bool,
    operation: impl FnOnce() -> Result<T, TokenStoreError>,
) -> Result<T, TokenStoreError> {
    operation()
}

fn get(service: &str, key: &str) -> Result<Option<Vec<u8>>, TokenStoreError> {
    match crate::secrets::get_entry_for(service, key)?.get_secret() {
        Ok(bytes) => Ok(Some(bytes)),
        Err(keyring_core::Error::NoEntry) => Ok(None),
        Err(error) => Err(error.into()),
    }
}

impl TokenBackend for KeychainBackend {
    fn get(&self, key: &str) -> Result<Option<Vec<u8>>, TokenStoreError> {
        get(crate::secrets::SERVICE_NAME, key)
    }

    fn set(&self, key: &str, value: &[u8]) -> Result<(), TokenStoreError> {
        Ok(crate::secrets::get_entry(key)?.set_secret(value)?)
    }

    fn remove(&self, key: &str) -> Result<(), TokenStoreError> {
        Ok(crate::secrets::get_entry(key)?.delete_credential()?)
    }
}

impl TokenBackend for ManagementKeychainBackend {
    fn get(&self, key: &str) -> Result<Option<Vec<u8>>, TokenStoreError> {
        management_operation(self.interactive_reads, || {
            get(crate::secrets::MANAGEMENT_SERVICE_NAME, key)
        })
    }
    fn set(&self, key: &str, value: &[u8]) -> Result<(), TokenStoreError> {
        management_operation(true, || {
            Ok(
                crate::secrets::get_entry_for(crate::secrets::MANAGEMENT_SERVICE_NAME, key)?
                    .set_secret(value)?,
            )
        })
    }
    fn remove(&self, key: &str) -> Result<(), TokenStoreError> {
        management_operation(
            key != crate::tokens::manager::keys::PENDING_STORE_EXCHANGE,
            || {
                Ok(
                    crate::secrets::get_entry_for(crate::secrets::MANAGEMENT_SERVICE_NAME, key)?
                        .delete_credential()?,
                )
            },
        )
    }
}

#[cfg(all(test, target_os = "macos"))]
mod management_keychain_tests {
    use super::*;
    use security_framework::os::macos::keychain::SecKeychain;
    #[test]
    fn read_policy_disables_only_process_interaction_and_restores_previous_setting() {
        let original = SecKeychain::user_interaction_allowed().unwrap();
        management_operation(false, || {
            assert!(!SecKeychain::user_interaction_allowed().unwrap());
            Ok(())
        })
        .unwrap();
        assert_eq!(SecKeychain::user_interaction_allowed().unwrap(), original);
        management_operation(true, || {
            assert_eq!(SecKeychain::user_interaction_allowed().unwrap(), original);
            assert!(management_operation(false, || Ok(())).is_err());
            Ok(())
        })
        .unwrap();
    }
}
