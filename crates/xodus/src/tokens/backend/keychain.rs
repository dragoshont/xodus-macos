use crate::tokens::store::{TokenBackend, TokenStoreError};

pub struct KeychainBackend;

pub(crate) struct ManagementKeychainBackend {
    pub interactive_reads: bool,
}

#[derive(Clone, Copy)]
enum ReadBoundary {
    InteractionLock,
    InteractionState,
    InteractionEnable,
    InteractionDisable,
    InteractionRestore,
    EntryCreate,
    SecretRead,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum ReadClass {
    Busy,
    InteractionDenied,
    AccessDenied,
    StoreUnavailable,
    Corrupt,
    Unknown,
}

#[derive(Clone, Copy)]
struct ReadDiagnostic {
    saved_store: bool,
    requested_interactive: bool,
    process_allowed: Option<bool>,
}

impl ReadDiagnostic {
    fn line(self, boundary: ReadBoundary, class: ReadClass, status: Option<i32>) -> String {
        let mut value = serde_json::json!({
            "category": "managementCredentialReadFailure",
            "boundary": match boundary {
                ReadBoundary::InteractionLock => "interactionLock",
                ReadBoundary::InteractionState => "interactionState",
                ReadBoundary::InteractionEnable => "interactionEnable",
                ReadBoundary::InteractionDisable => "interactionDisable",
                ReadBoundary::InteractionRestore => "interactionRestore",
                ReadBoundary::EntryCreate => "entryCreate",
                ReadBoundary::SecretRead => "secretRead",
            },
            "role": if self.saved_store { "savedStore" } else { "otherManagement" },
            "class": match class {
                ReadClass::Busy => "busy",
                ReadClass::InteractionDenied => "interactionDenied",
                ReadClass::AccessDenied => "accessDenied",
                ReadClass::StoreUnavailable => "storeUnavailable",
                ReadClass::Corrupt => "corrupt",
                ReadClass::Unknown => "unknown",
            },
            "requestedInteractive": self.requested_interactive,
        });
        if let Some(allowed) = self.process_allowed {
            value["processAllowed"] = allowed.into();
        }
        if let Some(status) = status {
            value["osStatus"] = status.into();
        }
        value.to_string()
    }

    fn emit(self, boundary: ReadBoundary, class: ReadClass, status: Option<i32>) {
        eprintln!("{}", self.line(boundary, class, status));
    }
}

fn status_class(status: Option<i32>) -> ReadClass {
    match status {
        Some(-25308) => ReadClass::InteractionDenied,
        Some(-25293 | -61 | -25244) => ReadClass::AccessDenied,
        Some(-25291 | -25292 | -25294 | -25295) => ReadClass::StoreUnavailable,
        Some(-26275) => ReadClass::Corrupt,
        _ => ReadClass::Unknown,
    }
}

fn keyring_failure(error: &keyring_core::Error) -> (ReadClass, Option<i32>) {
    #[cfg(target_os = "macos")]
    let status = match error {
        keyring_core::Error::PlatformFailure(error)
        | keyring_core::Error::NoStorageAccess(error) => error
            .downcast_ref::<security_framework::base::Error>()
            .map(|error| error.code()),
        _ => None,
    };
    #[cfg(not(target_os = "macos"))]
    let status = None;
    let class = match error {
        keyring_core::Error::BadEncoding(_)
        | keyring_core::Error::BadDataFormat(_, _)
        | keyring_core::Error::BadStoreFormat(_) => ReadClass::Corrupt,
        keyring_core::Error::NoStorageAccess(_) | keyring_core::Error::NoDefaultStore
            if status.is_none() =>
        {
            ReadClass::StoreUnavailable
        }
        _ => status_class(status),
    };
    (class, status)
}

fn management_operation<T>(
    interactive: bool,
    operation: impl FnOnce() -> Result<T, TokenStoreError>,
) -> Result<T, TokenStoreError> {
    management_guard(interactive, None, |_, _| operation())
}

#[cfg(target_os = "macos")]
#[link(name = "Security", kind = "framework")]
unsafe extern "C" {
    fn SecKeychainSetUserInteractionAllowed(state: u8) -> i32;
}

#[cfg(target_os = "macos")]
fn set_interaction(allowed: bool) -> Result<(), i32> {
    let status = unsafe { SecKeychainSetUserInteractionAllowed(u8::from(allowed)) };
    if status == 0 { Ok(()) } else { Err(status) }
}

#[cfg(target_os = "macos")]
fn native_read_error(status: i32) -> TokenStoreError {
    keyring_core::Error::PlatformFailure(Box::new(security_framework::base::Error::from_code(
        status,
    )))
    .into()
}

#[cfg(target_os = "macos")]
struct ReadInteraction<'a> {
    previous: bool,
    set: &'a mut dyn FnMut(bool) -> Result<(), i32>,
    diagnostic: ReadDiagnostic,
    finished: bool,
}

#[cfg(target_os = "macos")]
impl ReadInteraction<'_> {
    fn finish(mut self) -> Result<(), i32> {
        self.finished = true;
        (self.set)(self.previous)
    }
}

#[cfg(target_os = "macos")]
impl Drop for ReadInteraction<'_> {
    fn drop(&mut self) {
        if !self.finished {
            if let Err(status) = (self.set)(self.previous) {
                let diagnostic = ReadDiagnostic {
                    process_allowed: None,
                    ..self.diagnostic
                };
                diagnostic.emit(
                    ReadBoundary::InteractionRestore,
                    status_class(Some(status)),
                    Some(status),
                );
            }
        }
    }
}

fn emit_read_error(diagnostic: ReadDiagnostic, boundary: ReadBoundary, error: &TokenStoreError) {
    let (class, status) = match error {
        TokenStoreError::Keychain(error) => keyring_failure(error),
        _ => (ReadClass::Unknown, None),
    };
    diagnostic.emit(boundary, class, status);
}

#[cfg(target_os = "macos")]
fn read_interaction<T>(
    mut diagnostic: ReadDiagnostic,
    previous: bool,
    mut set: impl FnMut(bool) -> Result<(), i32>,
    operation: impl FnOnce(Option<bool>, &mut ReadBoundary) -> Result<T, TokenStoreError>,
) -> Result<T, TokenStoreError> {
    diagnostic.process_allowed = Some(previous);
    let requested = diagnostic.requested_interactive;
    let restore = if requested || previous {
        set(requested).map_err(|status| {
            diagnostic.emit(
                if requested {
                    ReadBoundary::InteractionEnable
                } else {
                    ReadBoundary::InteractionDisable
                },
                status_class(Some(status)),
                Some(status),
            );
            native_read_error(status)
        })?;
        Some(ReadInteraction {
            previous,
            set: &mut set,
            diagnostic,
            finished: false,
        })
    } else {
        None
    };
    diagnostic.process_allowed = Some(requested);
    let mut boundary = ReadBoundary::EntryCreate;
    let result = operation(Some(requested), &mut boundary);
    if let Some(restore) = restore {
        if let Err(status) = restore.finish() {
            diagnostic.process_allowed = None;
            diagnostic.emit(
                ReadBoundary::InteractionRestore,
                status_class(Some(status)),
                Some(status),
            );
            return result.and(Err(native_read_error(status)));
        }
    }
    if let Err(error) = &result {
        emit_read_error(diagnostic, boundary, error);
    }
    result
}

#[cfg(target_os = "macos")]
fn management_guard<T>(
    interactive: bool,
    diagnostic: Option<ReadDiagnostic>,
    operation: impl FnOnce(Option<bool>, &mut ReadBoundary) -> Result<T, TokenStoreError>,
) -> Result<T, TokenStoreError> {
    use security_framework::os::macos::keychain::SecKeychain;
    static INTERACTION: std::sync::Mutex<()> = std::sync::Mutex::new(());
    let unavailable = || {
        TokenStoreError::Io(std::io::Error::other(
            "Launcher Keychain access is unavailable or another explicit operation is pending",
        ))
    };
    let _exclusive = INTERACTION.try_lock().map_err(|_| {
        if let Some(diagnostic) = diagnostic {
            diagnostic.emit(ReadBoundary::InteractionLock, ReadClass::Busy, None);
        }
        unavailable()
    })?;
    let allowed = SecKeychain::user_interaction_allowed().map_err(|error| {
        if let Some(diagnostic) = diagnostic {
            diagnostic.emit(
                ReadBoundary::InteractionState,
                status_class(Some(error.code())),
                Some(error.code()),
            );
        }
        unavailable()
    })?;
    if let Some(diagnostic) = diagnostic {
        return read_interaction(diagnostic, allowed, set_interaction, operation);
    }
    let _no_prompt = if !interactive && allowed {
        Some(SecKeychain::disable_user_interaction().map_err(|_| unavailable())?)
    } else {
        None
    };
    operation(Some(interactive && allowed), &mut ReadBoundary::EntryCreate)
}

#[cfg(not(target_os = "macos"))]
fn management_guard<T>(
    _interactive: bool,
    diagnostic: Option<ReadDiagnostic>,
    operation: impl FnOnce(Option<bool>, &mut ReadBoundary) -> Result<T, TokenStoreError>,
) -> Result<T, TokenStoreError> {
    let mut boundary = ReadBoundary::EntryCreate;
    let result = operation(None, &mut boundary);
    if let (Some(diagnostic), Err(error)) = (diagnostic, &result) {
        emit_read_error(diagnostic, boundary, error);
    }
    result
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
        let diagnostic = ReadDiagnostic {
            saved_store: key == crate::tokens::manager::keys::STORE_USER_SESSION,
            requested_interactive: self.interactive_reads,
            process_allowed: None,
        };
        management_guard(self.interactive_reads, Some(diagnostic), |_, boundary| {
            let entry =
                crate::secrets::get_entry_for(crate::secrets::MANAGEMENT_SERVICE_NAME, key)?;
            *boundary = ReadBoundary::SecretRead;
            match entry.get_secret() {
                Ok(bytes) => Ok(Some(bytes)),
                Err(keyring_core::Error::NoEntry) => Ok(None),
                Err(error) => Err(error.into()),
            }
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
    use std::cell::Cell;

    fn diagnostic(requested_interactive: bool) -> ReadDiagnostic {
        ReadDiagnostic {
            saved_store: true,
            requested_interactive,
            process_allowed: None,
        }
    }

    #[test]
    fn fixed_diagnostics_are_closed_bounded_and_never_format_platform_payloads() {
        #[derive(Debug)]
        struct PrivateError;
        impl std::fmt::Display for PrivateError {
            fn fmt(&self, _: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
                panic!("must not format private provider error");
            }
        }
        impl std::error::Error for PrivateError {}
        let sentinel = "PRIVATE-TOKEN-XUID-URL-ACCOUNT";
        for error in [
            keyring_core::Error::PlatformFailure(Box::new(PrivateError)),
            keyring_core::Error::NoStorageAccess(Box::new(PrivateError)),
            keyring_core::Error::BadEncoding(sentinel.as_bytes().to_vec()),
            keyring_core::Error::BadDataFormat(
                sentinel.as_bytes().to_vec(),
                Box::new(PrivateError),
            ),
            keyring_core::Error::BadStoreFormat(sentinel.into()),
            keyring_core::Error::Invalid(sentinel.into(), sentinel.into()),
        ] {
            let (class, status) = keyring_failure(&error);
            let line = diagnostic(true).line(ReadBoundary::SecretRead, class, status);
            assert!(!line.contains(sentinel));
            assert!(line.len() + 1 <= 512);
            let value: serde_json::Value = serde_json::from_str(&line).unwrap();
            assert_eq!(value["category"], "managementCredentialReadFailure");
            assert!(value.get("processAllowed").is_none());
            assert!(value.get("osStatus").is_none());
            assert_eq!(value.as_object().unwrap().len(), 5);
        }
        for boundary in [
            ReadBoundary::InteractionLock,
            ReadBoundary::InteractionState,
            ReadBoundary::InteractionEnable,
            ReadBoundary::InteractionDisable,
            ReadBoundary::InteractionRestore,
            ReadBoundary::EntryCreate,
            ReadBoundary::SecretRead,
        ] {
            for class in [
                ReadClass::Busy,
                ReadClass::InteractionDenied,
                ReadClass::AccessDenied,
                ReadClass::StoreUnavailable,
                ReadClass::Corrupt,
                ReadClass::Unknown,
            ] {
                for status in [i32::MIN, i32::MAX] {
                    let line = ReadDiagnostic {
                        process_allowed: Some(false),
                        ..diagnostic(false)
                    }
                    .line(boundary, class, Some(status));
                    assert!(line.len() + 1 <= 512);
                    let value: serde_json::Value = serde_json::from_str(&line).unwrap();
                    assert_eq!(value["osStatus"], status);
                    assert_eq!(value["processAllowed"], false);
                    assert_eq!(value.as_object().unwrap().len(), 7);
                }
            }
        }
    }

    #[test]
    fn boxed_native_status_is_preserved_and_mapping_does_not_invent_lock_state() {
        for (status, expected) in [
            (-25308, ReadClass::InteractionDenied),
            (-25293, ReadClass::AccessDenied),
            (-25291, ReadClass::StoreUnavailable),
            (-26275, ReadClass::Corrupt),
            (-128, ReadClass::Unknown),
            (i32::MIN, ReadClass::Unknown),
        ] {
            for error in [
                keyring_core::Error::PlatformFailure(Box::new(
                    security_framework::base::Error::from_code(status),
                )),
                keyring_core::Error::NoStorageAccess(Box::new(
                    security_framework::base::Error::from_code(status),
                )),
            ] {
                assert_eq!(keyring_failure(&error), (expected, Some(status)));
            }
        }
    }

    #[test]
    fn requested_read_policy_restores_exact_prior_state_on_success_and_failure() {
        for previous in [false, true] {
            for requested in [false, true] {
                for fail in [false, true] {
                    let state = Cell::new(previous);
                    let calls = Cell::new(0);
                    let result = read_interaction(
                        diagnostic(requested),
                        previous,
                        |allowed| {
                            calls.set(calls.get() + 1);
                            state.set(allowed);
                            Ok(())
                        },
                        |allowed, boundary| {
                            assert_eq!(allowed, Some(requested));
                            assert_eq!(state.get(), requested);
                            *boundary = ReadBoundary::SecretRead;
                            if fail {
                                Err(TokenStoreError::InvalidCredential)
                            } else {
                                Ok(())
                            }
                        },
                    );
                    assert_eq!(result.is_err(), fail);
                    assert_eq!(state.get(), previous);
                    assert_eq!(calls.get(), if requested || previous { 2 } else { 0 });
                }
            }
        }
    }

    #[test]
    fn setter_failure_skips_read_and_restore_failure_cannot_return_success_or_retry() {
        let called = Cell::new(false);
        let result = read_interaction(
            diagnostic(true),
            false,
            |_| Err(-25293),
            |_, _| {
                called.set(true);
                Ok(())
            },
        );
        assert!(result.is_err());
        assert!(!called.get());
        for read_fails in [false, true] {
            let calls = Cell::new(0);
            let result = read_interaction(
                diagnostic(true),
                false,
                |_| {
                    calls.set(calls.get() + 1);
                    if calls.get() == 1 {
                        Ok(())
                    } else {
                        Err(-25291)
                    }
                },
                |_, _| {
                    if read_fails {
                        Err(TokenStoreError::InvalidCredential)
                    } else {
                        Ok(())
                    }
                },
            );
            assert!(result.is_err());
            assert_eq!(calls.get(), 2);
            if read_fails {
                assert!(matches!(result, Err(TokenStoreError::InvalidCredential)));
            }
        }
    }

    #[test]
    fn unwind_restores_policy_without_an_extra_read() {
        let state = Cell::new(false);
        let calls = Cell::new(0);
        let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
            read_interaction::<()>(
                diagnostic(true),
                false,
                |allowed| {
                    calls.set(calls.get() + 1);
                    state.set(allowed);
                    Ok(())
                },
                |_, _| panic!("synthetic read panic"),
            )
        }));
        assert!(result.is_err());
        assert!(!state.get());
        assert_eq!(calls.get(), 2);
    }

    #[test]
    fn nested_and_concurrent_read_guards_fail_busy_without_entering_read() {
        management_operation(true, || {
            assert!(
                management_guard::<()>(true, Some(diagnostic(true)), |_, _| {
                    panic!("nested read must not enter")
                })
                .is_err()
            );
            std::thread::spawn(|| {
                assert!(
                    management_guard::<()>(true, Some(diagnostic(true)), |_, _| {
                        panic!("concurrent read must not enter")
                    })
                    .is_err()
                );
            })
            .join()
            .unwrap();
            Ok(())
        })
        .unwrap();
    }

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
