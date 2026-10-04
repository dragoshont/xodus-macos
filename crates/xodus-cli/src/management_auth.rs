use std::process::ExitCode;
use xodus::models::secrets::{ManagementStoreSession, Token};
use xodus::tokens::{PASSPORT_STS, TokenManager};
use xodus_management::adapter::{
    ConsentBootstrap, ConsentFailure, ConsentHandoff, MAX_AUTH_HANDOFF_BYTES,
};

enum SessionFailure {
    Cancelled,
    Failed(ConsentFailure),
}

fn device_failure(error: xodus::tokens::device::DeviceCredentialError) -> SessionFailure {
    use xodus::tokens::device::{DeviceCredentialError, DeviceProofFailure};
    SessionFailure::Failed(match error {
        DeviceCredentialError::StorageUnavailable => ConsentFailure::DeviceStorage,
        DeviceCredentialError::InvalidStoredCredential => ConsentFailure::DeviceCredential,
        DeviceCredentialError::BrokerFailure => ConsentFailure::DeviceRequest,
        DeviceCredentialError::InvalidBrokerProof => ConsentFailure::DeviceResponse,
        DeviceCredentialError::InvalidBrokerProofAt(failure) => match failure {
            DeviceProofFailure::Registration => ConsentFailure::DeviceRegistrationProof,
            DeviceProofFailure::TokenResponse => ConsentFailure::DeviceTokenResponse,
            DeviceProofFailure::TokenProof => ConsentFailure::DeviceTokenProof,
            DeviceProofFailure::TokenStructure => ConsentFailure::DeviceTokenStructure,
            DeviceProofFailure::TokenKind => ConsentFailure::DeviceTokenKind,
            DeviceProofFailure::TokenAudience => ConsentFailure::DeviceTokenAudience,
            DeviceProofFailure::TokenCipher => ConsentFailure::DeviceTokenCipher,
            DeviceProofFailure::TokenXmlBound => ConsentFailure::DeviceTokenXmlBound,
            DeviceProofFailure::TokenXmlParse => ConsentFailure::DeviceTokenXmlParse,
            DeviceProofFailure::TokenCipherEncoding => ConsentFailure::DeviceTokenCipherEncoding,
            DeviceProofFailure::TokenSecret => ConsentFailure::DeviceTokenSecret,
        },
    })
}

fn login_failure(error: &'static str) -> SessionFailure {
    match error {
        "Sign-in was cancelled or no credentials were issued" => SessionFailure::Cancelled,
        "Device credential proof is invalid or expired" => {
            SessionFailure::Failed(ConsentFailure::DeviceProof)
        }
        "Sign-in did not return credential proof"
        | "Sign-in did not return nonempty credential proof"
        | "Sign-in returned duplicate credential audiences"
        | "Sign-in did not issue the required Passport store credential" => {
            SessionFailure::Failed(ConsentFailure::StoreProof)
        }
        _ => SessionFailure::Failed(ConsentFailure::NativeSignIn),
    }
}

fn failure_handoff(error: SessionFailure) -> (ConsentHandoff, ExitCode) {
    match error {
        SessionFailure::Cancelled => (ConsentHandoff::Cancelled, ExitCode::from(2)),
        SessionFailure::Failed(failure) => {
            (ConsentHandoff::FailedAt { failure }, ExitCode::FAILURE)
        }
    }
}

fn private_channel(fd: std::os::fd::OwnedFd) -> std::io::Result<std::os::unix::net::UnixStream> {
    let channel = std::os::unix::net::UnixStream::from(fd);
    if !channel.peer_addr()?.is_unnamed() || !channel.local_addr()?.is_unnamed() {
        return Err(std::io::Error::other(
            "Expected inherited anonymous parent channel",
        ));
    }
    channel.set_read_timeout(Some(std::time::Duration::from_secs(5)))?;
    channel.set_write_timeout(Some(std::time::Duration::from_secs(5)))?;
    Ok(channel)
}

fn read_bootstrap(
    channel: &mut std::os::unix::net::UnixStream,
    flow_id: &str,
) -> std::io::Result<ConsentBootstrap> {
    use std::io::Read;
    let invalid = || std::io::Error::other("Invalid private consent bootstrap");
    let mut prefix = [0; 4];
    channel.read_exact(&mut prefix)?;
    let length = u32::from_be_bytes(prefix) as usize;
    if length == 0 || length > MAX_AUTH_HANDOFF_BYTES {
        return Err(invalid());
    }
    let mut bytes = vec![0; length];
    channel.read_exact(&mut bytes)?;
    let bootstrap: ConsentBootstrap = serde_json::from_slice(&bytes).map_err(|_| invalid())?;
    if bootstrap.flow_id != flow_id
        || (bootstrap.device.is_none() && bootstrap.device_token.is_some())
    {
        return Err(invalid());
    }
    Ok(bootstrap)
}

fn write_handoff(
    channel: &mut std::os::unix::net::UnixStream,
    outcome: &ConsentHandoff,
) -> std::io::Result<()> {
    use std::io::Write;
    let bytes = serde_json::to_vec(outcome)
        .map_err(|_| std::io::Error::other("Session serialization failed"))?;
    if bytes.len() > MAX_AUTH_HANDOFF_BYTES {
        return Err(std::io::Error::other(
            "Session exceeds private handoff bound",
        ));
    }
    channel.write_all(&(bytes.len() as u32).to_be_bytes())?;
    channel.write_all(&bytes)?;
    channel.flush()
}

async fn issue_session(
    bootstrap: ConsentBootstrap,
) -> Result<ManagementStoreSession, SessionFailure> {
    let tokens = TokenManager::with_memory();
    if let Some(device) = bootstrap.device {
        tokens
            .save_device_license(&device)
            .map_err(|_| SessionFailure::Failed(ConsentFailure::Bootstrap))?;
    }
    if let Some(token) = bootstrap.device_token {
        tokens
            .save_device_token(PASSPORT_STS.to_owned(), Token::Legacy(token))
            .map_err(|_| SessionFailure::Failed(ConsentFailure::Bootstrap))?;
    }
    let client = reqwest::Client::builder()
        .timeout(std::time::Duration::from_secs(30))
        .redirect(reqwest::redirect::Policy::none())
        .build()
        .map_err(|_| SessionFailure::Failed(ConsentFailure::ClientInitialization))?;
    xodus::tokens::device::ensure_device_credentials(&client, &tokens)
        .await
        .map_err(device_failure)?;
    let Token::Legacy(device_token) = tokens
        .get_device_sts_token()
        .map_err(|_| SessionFailure::Failed(ConsentFailure::DeviceProof))?
    else {
        return Err(SessionFailure::Failed(ConsentFailure::DeviceProof));
    };
    let device = tokens
        .get_device_license()
        .map_err(|_| SessionFailure::Failed(ConsentFailure::DeviceProof))?;
    let (issued, user) = crate::commands::login::issue_credentials(client, device_token.clone())
        .map_err(login_failure)?;
    let session = ManagementStoreSession {
        flow_id: bootstrap.flow_id,
        user,
        tokens: issued,
        device,
        device_token,
    };
    if !session.valid() {
        return Err(SessionFailure::Failed(ConsentFailure::StoreProof));
    }
    Ok(session)
}

pub async fn run(flow_id: String) -> ExitCode {
    std::panic::set_hook(Box::new(|_| {}));
    if uuid::Uuid::parse_str(&flow_id).is_err()
        || !xodus::secrets::management_native_keychain_enabled()
    {
        return ExitCode::FAILURE;
    }
    use std::os::fd::AsFd;
    let Ok(fd) = rustix::io::dup(std::io::stdin().as_fd()) else {
        return ExitCode::FAILURE;
    };
    let Ok(mut channel) = private_channel(fd) else {
        return ExitCode::FAILURE;
    };
    let Ok(bootstrap) = read_bootstrap(&mut channel, &flow_id) else {
        let _ = write_handoff(
            &mut channel,
            &ConsentHandoff::FailedAt {
                failure: ConsentFailure::Bootstrap,
            },
        );
        return ExitCode::FAILURE;
    };
    let (outcome, code) = match issue_session(bootstrap).await {
        Ok(session) => (
            ConsentHandoff::StoreCompleted {
                session: Box::new(session),
            },
            ExitCode::SUCCESS,
        ),
        Err(error) => failure_handoff(error),
    };
    if write_handoff(&mut channel, &outcome).is_ok() {
        code
    } else {
        ExitCode::FAILURE
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::{Read, Write};

    #[test]
    fn handoff_is_anonymous_bounded_private_socket_not_public_output() {
        let (mut parent, child) = std::os::unix::net::UnixStream::pair().unwrap();
        let mut channel = private_channel(child.into()).unwrap();
        write_handoff(&mut channel, &ConsentHandoff::Cancelled).unwrap();
        drop(channel);
        let mut bytes = Vec::new();
        parent.read_to_end(&mut bytes).unwrap();
        assert_eq!(
            u32::from_be_bytes(bytes[..4].try_into().unwrap()) as usize,
            bytes.len() - 4
        );
        assert!(matches!(
            serde_json::from_slice::<ConsentHandoff>(&bytes[4..]).unwrap(),
            ConsentHandoff::Cancelled
        ));
        assert!(private_channel(tempfile::tempfile().unwrap().into()).is_err());
    }

    #[test]
    fn bootstrap_rejects_mismatched_flow_oversize_and_unknown_fields() {
        for bytes in [
            0u32.to_be_bytes().to_vec(),
            ((MAX_AUTH_HANDOFF_BYTES + 1) as u32).to_be_bytes().to_vec(),
        ] {
            let (mut parent, mut child) = std::os::unix::net::UnixStream::pair().unwrap();
            parent.write_all(&bytes).unwrap();
            assert!(read_bootstrap(&mut child, "fixture").is_err());
        }
        for payload in [
            serde_json::json!({"flow_id":"foreign","device":null,"device_token":null}),
            serde_json::json!({"flow_id":"fixture","device":null,"device_token":null,"secret":"fixture-not-a-token"}),
        ] {
            let (mut parent, mut child) = std::os::unix::net::UnixStream::pair().unwrap();
            let bytes = serde_json::to_vec(&payload).unwrap();
            parent
                .write_all(&(bytes.len() as u32).to_be_bytes())
                .unwrap();
            parent.write_all(&bytes).unwrap();
            assert!(read_bootstrap(&mut child, "fixture").is_err());
        }
    }

    #[test]
    fn known_device_errors_remain_static_typed_failures_without_provider_details() {
        use xodus::tokens::device::{DeviceCredentialError, DeviceProofFailure};
        for (error, expected) in [
            (
                DeviceCredentialError::StorageUnavailable,
                ConsentFailure::DeviceStorage,
            ),
            (
                DeviceCredentialError::InvalidStoredCredential,
                ConsentFailure::DeviceCredential,
            ),
            (
                DeviceCredentialError::BrokerFailure,
                ConsentFailure::DeviceRequest,
            ),
            (
                DeviceCredentialError::InvalidBrokerProof,
                ConsentFailure::DeviceResponse,
            ),
            (
                DeviceCredentialError::InvalidBrokerProofAt(DeviceProofFailure::Registration),
                ConsentFailure::DeviceRegistrationProof,
            ),
            (
                DeviceCredentialError::InvalidBrokerProofAt(DeviceProofFailure::TokenResponse),
                ConsentFailure::DeviceTokenResponse,
            ),
            (
                DeviceCredentialError::InvalidBrokerProofAt(DeviceProofFailure::TokenProof),
                ConsentFailure::DeviceTokenProof,
            ),
            (
                DeviceCredentialError::InvalidBrokerProofAt(DeviceProofFailure::TokenStructure),
                ConsentFailure::DeviceTokenStructure,
            ),
            (
                DeviceCredentialError::InvalidBrokerProofAt(DeviceProofFailure::TokenKind),
                ConsentFailure::DeviceTokenKind,
            ),
            (
                DeviceCredentialError::InvalidBrokerProofAt(DeviceProofFailure::TokenAudience),
                ConsentFailure::DeviceTokenAudience,
            ),
            (
                DeviceCredentialError::InvalidBrokerProofAt(DeviceProofFailure::TokenCipher),
                ConsentFailure::DeviceTokenCipher,
            ),
            (
                DeviceCredentialError::InvalidBrokerProofAt(DeviceProofFailure::TokenXmlBound),
                ConsentFailure::DeviceTokenXmlBound,
            ),
            (
                DeviceCredentialError::InvalidBrokerProofAt(DeviceProofFailure::TokenXmlParse),
                ConsentFailure::DeviceTokenXmlParse,
            ),
            (
                DeviceCredentialError::InvalidBrokerProofAt(
                    DeviceProofFailure::TokenCipherEncoding,
                ),
                ConsentFailure::DeviceTokenCipherEncoding,
            ),
            (
                DeviceCredentialError::InvalidBrokerProofAt(DeviceProofFailure::TokenSecret),
                ConsentFailure::DeviceTokenSecret,
            ),
        ] {
            let (handoff, code) = failure_handoff(device_failure(error));
            assert_eq!(code, ExitCode::FAILURE);
            assert!(
                matches!(&handoff, ConsentHandoff::FailedAt { failure } if *failure == expected)
            );
            let (mut parent, child) = std::os::unix::net::UnixStream::pair().unwrap();
            let mut channel = private_channel(child.into()).unwrap();
            write_handoff(&mut channel, &handoff).unwrap();
            drop(channel);
            let mut bytes = Vec::new();
            parent.read_to_end(&mut bytes).unwrap();
            assert_eq!(
                u32::from_be_bytes(bytes[..4].try_into().unwrap()) as usize,
                bytes.len() - 4
            );
            assert!(matches!(
                serde_json::from_slice::<ConsentHandoff>(&bytes[4..]).unwrap(),
                ConsentHandoff::FailedAt { failure } if failure == expected
            ));
            let public = expected.wire_error();
            assert_eq!(public.code, xodus_management::wire::ErrorCode::AuthInvalid);
            let details = public.details.unwrap();
            assert_eq!(details.len(), 3);
            assert_eq!(details["category"], "nativeConsentFailure");
        }
    }

    #[test]
    fn secret_marked_unknown_error_cannot_reach_private_or_public_failure_payload() {
        let secret = "SECRET_SENTINEL_AUTH_DIAGNOSTIC<XML>ticket=user-secret&device=private";
        let (handoff, code) = failure_handoff(login_failure(secret));
        assert_eq!(code, ExitCode::FAILURE);
        let ConsentHandoff::FailedAt { failure } = handoff else {
            panic!("Expected a static failure category");
        };
        assert_eq!(failure, ConsentFailure::NativeSignIn);
        let (mut parent, child) = std::os::unix::net::UnixStream::pair().unwrap();
        let mut channel = private_channel(child.into()).unwrap();
        write_handoff(&mut channel, &ConsentHandoff::FailedAt { failure }).unwrap();
        drop(channel);
        let mut bytes = Vec::new();
        parent.read_to_end(&mut bytes).unwrap();
        assert!(
            !bytes
                .windows(secret.len())
                .any(|window| window == secret.as_bytes())
        );
        let public = serde_json::to_string(&failure.wire_error()).unwrap();
        assert!(!public.contains("SECRET_SENTINEL") && !public.contains("<XML>"));
        assert_eq!(
            failure.wire_error().details.unwrap()["reason"],
            "pipelineFailed"
        );
    }

    #[test]
    fn cancellation_and_known_store_proof_errors_keep_distinct_outcomes() {
        let (cancelled, code) = failure_handoff(login_failure(
            "Sign-in was cancelled or no credentials were issued",
        ));
        assert!(matches!(cancelled, ConsentHandoff::Cancelled));
        assert_eq!(code, ExitCode::from(2));
        for error in [
            "Sign-in did not return credential proof",
            "Sign-in did not return nonempty credential proof",
            "Sign-in returned duplicate credential audiences",
            "Sign-in did not issue the required Passport store credential",
        ] {
            let (failure, code) = failure_handoff(login_failure(error));
            assert!(matches!(
                failure,
                ConsentHandoff::FailedAt {
                    failure: ConsentFailure::StoreProof
                }
            ));
            assert_eq!(code, ExitCode::FAILURE);
        }
    }
}
