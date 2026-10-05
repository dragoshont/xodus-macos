use std::process::ExitCode;
use std::sync::{
    Arc,
    atomic::{AtomicU8, Ordering},
};
use std::time::{Duration, Instant};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use xodus::models::secrets::{ManagementStoreSession, PendingManagementExchange, Token};
use xodus::tokens::{PASSPORT_STS, TokenManager};
use xodus_management::adapter::{
    ConsentBootstrap, ConsentFailure, ConsentHandoff, ExchangeFailureStage, MAX_AUTH_HANDOFF_BYTES,
    NativeSignInFailure,
};

enum SessionFailure {
    Cancelled,
    Failed(ConsentFailure),
    NativeSignIn(NativeSignInFailure),
    ExchangeDeferred {
        reason: NativeSignInFailure,
        pending: Option<Box<PendingManagementExchange>>,
    },
}

const ACTIVE: u8 = 0;
const CANCELLED: u8 = 1;
const TERMINAL: u8 = 2;

struct ParentGuardian {
    state: Arc<AtomicU8>,
    cancelled: tokio::sync::watch::Receiver<bool>,
    task: tokio::task::JoinHandle<()>,
}

impl ParentGuardian {
    fn start(mut parent: tokio::net::unix::OwnedReadHalf) -> Self {
        let state = Arc::new(AtomicU8::new(ACTIVE));
        let shared = state.clone();
        let (sender, cancelled) = tokio::sync::watch::channel(false);
        let task = tokio::spawn(async move {
            let mut unexpected = [0];
            let _ = parent.read(&mut unexpected).await;
            if shared
                .compare_exchange(ACTIVE, CANCELLED, Ordering::SeqCst, Ordering::SeqCst)
                .is_ok()
            {
                let _ = sender.send(true);
            }
        });
        Self {
            state,
            cancelled,
            task,
        }
    }

    async fn publish(
        &mut self,
        writer: &mut tokio::net::unix::OwnedWriteHalf,
        outcome: &ConsentHandoff,
        deadline: Instant,
    ) -> std::io::Result<()> {
        if self.state.load(Ordering::SeqCst) != ACTIVE || Instant::now() >= deadline {
            return Err(std::io::Error::other("Consent parent is unavailable"));
        }
        tokio::select! {
            biased;
            _ = self.cancelled.changed() => return Err(std::io::Error::other("Consent parent is unavailable")),
            result = tokio::time::timeout_at(tokio::time::Instant::from_std(deadline),
                xodus_management::native_auth::write(writer, outcome)) => {
                result.map_err(|_| std::io::Error::other("Consent handoff expired"))??;
            }
        }
        self.state
            .compare_exchange(ACTIVE, TERMINAL, Ordering::SeqCst, Ordering::SeqCst)
            .map_err(|_| std::io::Error::other("Consent parent is unavailable"))?;
        self.task.abort();
        writer.shutdown().await
    }
}

impl Drop for ParentGuardian {
    fn drop(&mut self) {
        self.task.abort();
    }
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
        SessionFailure::NativeSignIn(reason) => (
            ConsentHandoff::FailedNativeSignIn { reason },
            ExitCode::FAILURE,
        ),
        SessionFailure::ExchangeDeferred { reason, pending } => (
            ConsentHandoff::ExchangeDeferred { reason, pending },
            ExitCode::FAILURE,
        ),
    }
}

fn host_unavailable(error: std::io::Error) -> SessionFailure {
    SessionFailure::NativeSignIn(if error.kind() == std::io::ErrorKind::UnexpectedEof {
        NativeSignInFailure::ChannelEof
    } else {
        NativeSignInFailure::Unclassified
    })
}

fn token_exchange_failure(error: xodus::api::live::rst::RSTError) -> SessionFailure {
    use xodus::api::live::rst::RSTError;
    let stage = match error {
        RSTError::Request(error) if error.is_timeout() => ExchangeFailureStage::RequestTimeout,
        RSTError::Request(error) => match error.status() {
            Some(status) if status.is_client_error() => ExchangeFailureStage::HttpClientError,
            Some(status) if status.is_server_error() => ExchangeFailureStage::HttpServerError,
            Some(_) => ExchangeFailureStage::HttpStatusRejected,
            None => ExchangeFailureStage::RequestTransport,
        },
        RSTError::Builder(_) => ExchangeFailureStage::RequestBuild,
        RSTError::Serialization(_) => ExchangeFailureStage::RequestSerialization,
        RSTError::Deserialization(_) => ExchangeFailureStage::ResponseParsing,
        RSTError::Bergshamra(_) | RSTError::InvalidResponseSignature(_) => {
            ExchangeFailureStage::ResponseSignature
        }
        RSTError::Base64(_) | RSTError::Utf8(_) => ExchangeFailureStage::ResponseEncoding,
        _ => ExchangeFailureStage::ResponseCryptography,
    };
    SessionFailure::NativeSignIn(NativeSignInFailure::TokenExchangeStage { stage })
}

fn helper_completion_failure(_: std::io::Error) -> SessionFailure {
    SessionFailure::NativeSignIn(NativeSignInFailure::HelperCompletionFailed)
}

fn private_channel(fd: std::os::fd::OwnedFd) -> std::io::Result<std::os::unix::net::UnixStream> {
    rustix::io::fcntl_setfd(&fd, rustix::io::FdFlags::CLOEXEC)?;
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
        || bootstrap.remaining_millis == 0
        || bootstrap.remaining_millis > xodus_management::native_auth::MAX_BUDGET_MILLIS
        || bootstrap
            .resume_exchange
            .as_ref()
            .is_some_and(|pending| !pending.valid())
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
    let bytes = xodus_management::native_auth::encode(outcome)?;
    channel.write_all(&(bytes.len() as u32).to_be_bytes())?;
    channel.write_all(&bytes)?;
    channel.flush()
}

async fn issue_session(
    mut bootstrap: ConsentBootstrap,
    native: &mut Option<crate::native_auth_host::NativeHost>,
    deadline: Instant,
) -> Result<ManagementStoreSession, SessionFailure> {
    if let Some(mut pending) = bootstrap.resume_exchange.take() {
        if !pending.valid() {
            return Err(SessionFailure::NativeSignIn(
                NativeSignInFailure::ExchangeRetentionFailed,
            ));
        }
        pending.flow_id = bootstrap.flow_id;
        let client = reqwest::Client::builder()
            .timeout(Duration::from_secs(30))
            .redirect(reqwest::redirect::Policy::none())
            .build()
            .map_err(|_| SessionFailure::Failed(ConsentFailure::ClientInitialization))?;
        let exchanged = crate::commands::login::exchange_user_property(
            client,
            pending.device_token.clone(),
            crate::commands::login::CLIENT_ID.to_owned(),
            pending.property.clone(),
            pending.after_continuation,
        )
        .await;
        return match exchanged {
            Err(error) => {
                let SessionFailure::NativeSignIn(reason) = token_exchange_failure(error) else {
                    unreachable!()
                };
                Err(SessionFailure::ExchangeDeferred {
                    reason,
                    pending: Some(pending),
                })
            }
            Ok(xodus::models::live::ExchangeUserTokenOutcome::Fault(_)) => {
                Err(SessionFailure::ExchangeDeferred {
                    reason: NativeSignInFailure::TokenExchangeStage {
                        stage: ExchangeFailureStage::ContinuationRequired,
                    },
                    pending: None,
                })
            }
            Ok(xodus::models::live::ExchangeUserTokenOutcome::Issued(body)) => {
                let user = xodus::models::secrets::User {
                    puid: pending.property.puid.clone(),
                    username: pending.property.username.clone(),
                };
                let (tokens, user) = crate::commands::login::finish_issued(Some(
                    crate::commands::login::LoginOutput { body, user },
                ))
                .map_err(login_failure)?;
                Ok(ManagementStoreSession {
                    flow_id: pending.flow_id,
                    user,
                    tokens,
                    device: pending.device,
                    device_token: pending.device_token,
                })
            }
        };
    }
    let binding = bootstrap
        .native_host
        .ok_or(SessionFailure::Failed(ConsentFailure::NativeSignIn))?;
    xodus_management::native_auth::verify_binding(&binding)
        .map_err(|_| SessionFailure::Failed(ConsentFailure::NativeSignIn))?;
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
    crate::commands::login::validate_device(&device_token).map_err(login_failure)?;
    *native = Some(
        crate::native_auth_host::NativeHost::spawn(&binding, &bootstrap.flow_id)
            .map_err(|_| SessionFailure::Failed(ConsentFailure::NativeSignIn))?,
    );
    let host = native
        .as_mut()
        .ok_or(SessionFailure::Failed(ConsentFailure::NativeSignIn))?;
    let remaining = deadline
        .saturating_duration_since(Instant::now())
        .as_millis() as u64;
    if remaining == 0 {
        return Err(SessionFailure::Failed(ConsentFailure::NativeSignIn));
    }
    host.send(
        crate::webview::login_request(
            crate::commands::login::CLIENT_ID.to_owned(),
            crate::commands::login::LOGIN_MARKET.to_owned(),
            true,
        )
        .native_open(remaining),
    )
    .await
    .map_err(|_| SessionFailure::Failed(ConsentFailure::NativeSignIn))?;
    let mut captured = None;
    let (issued, user) =
        match issue_with_host(host, client, device_token.clone(), &mut captured).await {
            Ok(credentials) => credentials,
            Err(SessionFailure::NativeSignIn(
                reason @ NativeSignInFailure::TokenExchangeStage { .. },
            )) => {
                if let Some((property, after_continuation)) = captured
                    && let Some(pending) = PendingManagementExchange::new(
                        bootstrap.flow_id.clone(),
                        device,
                        device_token,
                        property,
                        after_continuation,
                    )
                {
                    return Err(SessionFailure::ExchangeDeferred {
                        reason,
                        pending: Some(Box::new(pending)),
                    });
                }
                return Err(SessionFailure::NativeSignIn(reason));
            }
            Err(error) => return Err(error),
        };
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

fn host_terminal(message: xodus_management::native_auth::HostResult) -> SessionFailure {
    match message {
        xodus_management::native_auth::HostResult::Cancelled => SessionFailure::Cancelled,
        xodus_management::native_auth::HostResult::Failed { reason } => {
            SessionFailure::NativeSignIn(NativeSignInFailure::Host { reason })
        }
        _ => SessionFailure::NativeSignIn(NativeSignInFailure::Unclassified),
    }
}

async fn issue_with_host(
    host: &mut crate::native_auth_host::NativeHost,
    client: reqwest::Client,
    device: xodus::models::secrets::LegacyToken,
    captured: &mut Option<(xodus::models::live::DAProperty, bool)>,
) -> Result<
    (
        std::collections::HashMap<String, Token>,
        xodus::models::secrets::User,
    ),
    SessionFailure,
> {
    use xodus_management::native_auth::{Command, HostResult};
    let unavailable = || SessionFailure::NativeSignIn(NativeSignInFailure::Unclassified);
    let mut continuations = 0;
    loop {
        match host.receive().await.map_err(host_unavailable)? {
            HostResult::Ready => {}
            other => return Err(host_terminal(other)),
        }
        let property = match host.receive().await.map_err(host_unavailable)? {
            HostResult::Da { property } => xodus::models::live::DAProperty {
                da_token: property.da_token,
                da_session_key: property.da_session_key,
                da_start_time: property.da_start_time,
                da_expires: property.da_expires,
                sts_inline_flow_token: property.sts_inline_flow_token,
                username: property.username,
                puid: property.puid,
            },
            other => return Err(host_terminal(other)),
        };
        *captured = Some((property.clone(), continuations > 0));
        let exchange = tokio::select! {
            biased;
            reply = host.receive() => return Err(host_terminal(reply.map_err(host_unavailable)?)),
            result = crate::commands::login::exchange_user_property(client.clone(), device.clone(),
                crate::commands::login::CLIENT_ID.to_owned(), property.clone(), continuations > 0) =>
                result.map_err(token_exchange_failure)?,
        };
        match exchange {
            xodus::models::live::ExchangeUserTokenOutcome::Issued(body) => {
                let output = crate::commands::login::LoginOutput {
                    body,
                    user: xodus::models::secrets::User {
                        puid: property.puid,
                        username: property.username,
                    },
                };
                let credentials =
                    crate::commands::login::finish_issued(Some(output)).map_err(login_failure)?;
                if !host.completed().await.map_err(helper_completion_failure)? {
                    return Err(SessionFailure::Cancelled);
                }
                return Ok(credentials);
            }
            xodus::models::live::ExchangeUserTokenOutcome::Fault(fault) => {
                let Some(url) = fault.and_then(|fault| fault.inline_auth_url) else {
                    return Err(SessionFailure::NativeSignIn(
                        NativeSignInFailure::TokenExchangeStage {
                            stage: ExchangeFailureStage::FaultWithoutContinuation,
                        },
                    ));
                };
                if continuations >= 4 || !crate::webview::trusted_login_url(&url) {
                    return Err(SessionFailure::NativeSignIn(
                        NativeSignInFailure::TokenExchangeStage {
                            stage: ExchangeFailureStage::ContinuationRejected,
                        },
                    ));
                }
                continuations += 1;
                host.send(Command::Navigate { url })
                    .await
                    .map_err(|_| unavailable())?;
            }
        }
    }
}

pub async fn run(flow_id: String) -> ExitCode {
    let started = Instant::now();
    std::panic::set_hook(Box::new(|_| {}));
    if !xodus_management::native_auth::valid_identity(1, &flow_id, 1, 1)
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
    if xodus_management::native_auth::disable_core_dumps().is_err()
        || channel.set_nonblocking(true).is_err()
    {
        return ExitCode::FAILURE;
    }
    let deadline = started + Duration::from_millis(bootstrap.remaining_millis);
    let Ok(channel) = tokio::net::UnixStream::from_std(channel) else {
        return ExitCode::FAILURE;
    };
    let (reader, mut writer) = channel.into_split();
    let mut guardian = ParentGuardian::start(reader);
    let mut native = None;
    let result = tokio::select! {
        biased;
        _ = guardian.cancelled.changed() => Err(SessionFailure::Failed(ConsentFailure::NativeSignIn)),
        _ = tokio::time::sleep_until(tokio::time::Instant::from_std(deadline)) =>
            Err(SessionFailure::Failed(ConsentFailure::NativeSignIn)),
        result = issue_session(bootstrap, &mut native, deadline) => result,
    };
    let result = if result.is_err() {
        if let Some(host) = native.as_mut()
            && host.abort().await.is_err()
        {
            return ExitCode::FAILURE;
        }
        result
    } else {
        result
    };
    drop(native);
    let (outcome, code) = match result {
        Ok(session) => (
            ConsentHandoff::StoreCompleted {
                session: Box::new(session),
            },
            ExitCode::SUCCESS,
        ),
        Err(error) => failure_handoff(error),
    };
    if guardian
        .publish(&mut writer, &outcome, deadline)
        .await
        .is_ok()
    {
        code
    } else {
        ExitCode::FAILURE
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::{Read, Write};

    #[tokio::test]
    async fn actual_native_pipeline_keeps_validated_failure_before_and_after_ready_vs_eof() {
        use crate::native_auth_host::{NativeHost, fixtures};
        use xodus_management::native_auth::HostFailure;
        for (mode, expected) in [
            (
                "failedReady",
                NativeSignInFailure::Host {
                    reason: HostFailure::NavigationFailed,
                },
            ),
            (
                "failedDA",
                NativeSignInFailure::Host {
                    reason: HostFailure::JavaScriptFailed,
                },
            ),
            ("eofReady", NativeSignInFailure::ChannelEof),
            ("version", NativeSignInFailure::Unclassified),
            (
                "da",
                NativeSignInFailure::TokenExchangeStage {
                    stage: ExchangeFailureStage::ResponseCryptography,
                },
            ),
        ] {
            let (_dir, binding) = fixtures::helper(mode);
            let mut host = NativeHost::spawn(&binding, &uuid::Uuid::nil().to_string()).unwrap();
            fixtures::open(&mut host, 1000).await.unwrap();
            let device = xodus::models::secrets::LegacyToken {
                key_name: None,
                token: "NEUTRAL_NOT_TOKEN".to_owned(),
                binary_secret: None,
                tpm_key: None,
                lifetime: xodus::models::soap::Timestamp {
                    id: None,
                    created: "2030-01-01T00:00:00Z".to_owned(),
                    expires: "2030-01-01T01:00:00Z".to_owned(),
                },
            };
            let failure =
                match issue_with_host(&mut host, reqwest::Client::new(), device, &mut None).await {
                    Err(failure) => failure,
                    Ok(_) => panic!("Neutral helper must never produce a Store session"),
                };
            host.abort().await.unwrap();
            let (handoff, _) = failure_handoff(failure);
            assert!(
                matches!(handoff, ConsentHandoff::FailedNativeSignIn { reason }
                if reason == expected)
            );
        }
    }

    #[tokio::test]
    async fn completion_failure_is_distinct_from_exchange_without_retaining_error_text() {
        use crate::native_auth_host::{NativeHost, fixtures};
        use xodus_management::native_auth::HostResult;
        let (_dir, binding) = fixtures::helper("nonzero");
        let mut host = NativeHost::spawn(&binding, &uuid::Uuid::nil().to_string()).unwrap();
        fixtures::open(&mut host, 1000).await.unwrap();
        assert!(matches!(host.receive().await.unwrap(), HostResult::Ready));
        let failure = host
            .completed()
            .await
            .map_err(helper_completion_failure)
            .err()
            .expect("Neutral helper nonzero exit must fail the completion fence");
        host.abort().await.unwrap();
        let (handoff, _) = failure_handoff(failure);
        assert!(matches!(
            handoff,
            ConsentHandoff::FailedNativeSignIn {
                reason: NativeSignInFailure::HelperCompletionFailed,
            }
        ));
        for (failure, expected) in [
            (
                token_exchange_failure(xodus::api::live::rst::RSTError::InvalidResponseSignature(
                    "PRIVATE_SENTINEL".to_owned(),
                )),
                NativeSignInFailure::TokenExchangeStage {
                    stage: ExchangeFailureStage::ResponseSignature,
                },
            ),
            (
                helper_completion_failure(std::io::Error::other("PRIVATE_SENTINEL")),
                NativeSignInFailure::HelperCompletionFailed,
            ),
        ] {
            let (handoff, _) = failure_handoff(failure);
            assert!(
                matches!(handoff, ConsentHandoff::FailedNativeSignIn { reason }
                if reason == expected)
            );
            assert!(
                !serde_json::to_string(&handoff)
                    .unwrap()
                    .contains("PRIVATE_SENTINEL")
            );
            assert!(!expected.wire_error().message.contains("PRIVATE_SENTINEL"));
        }
    }

    #[test]
    fn exchange_error_categories_never_retain_provider_response_or_exception() {
        use xodus::api::live::rst::{RSTBuilderError, RSTError};
        for (error, expected) in [
            (
                RSTError::Builder(RSTBuilderError::UnsupportedTokenCombination),
                ExchangeFailureStage::RequestBuild,
            ),
            (
                RSTError::MissingNonce,
                ExchangeFailureStage::ResponseCryptography,
            ),
            (
                RSTError::InvalidResponseSignature("PRIVATE_SENTINEL".to_owned()),
                ExchangeFailureStage::ResponseSignature,
            ),
            (
                RSTError::Request(
                    reqwest::Client::new()
                        .get("http://[PRIVATE_SENTINEL")
                        .build()
                        .err()
                        .unwrap(),
                ),
                ExchangeFailureStage::RequestTransport,
            ),
        ] {
            let (handoff, _) = failure_handoff(token_exchange_failure(error));
            assert!(matches!(handoff, ConsentHandoff::FailedNativeSignIn {
                reason: NativeSignInFailure::TokenExchangeStage { stage },
            } if stage == expected));
            assert!(
                !serde_json::to_string(&handoff)
                    .unwrap()
                    .contains("PRIVATE_SENTINEL")
            );
        }
    }

    #[tokio::test]
    async fn pending_exchange_retry_fails_locally_without_helper_or_expiry_renewal() {
        let device_token = xodus::models::secrets::LegacyToken {
            key_name: Some(PASSPORT_STS.to_owned()),
            token: "<EncryptedData Id=\"fixture\" xmlns=\"http://www.w3.org/2001/04/xmlenc#\" Type=\"http://www.w3.org/2001/04/xmlenc#Element\"><EncryptionMethod Algorithm=\"http://www.w3.org/2001/04/xmlenc#tripledes-cbc\"/><KeyInfo><KeyName>http://Passport.NET/STS</KeyName></KeyInfo><CipherData><CipherValue>Zml4dHVyZQ==</CipherValue></CipherData></EncryptedData>".to_owned(),
            binary_secret: Some(format!("BAAA{}AA==", "AAAA".repeat(1364))),
            tpm_key: None,
            lifetime: xodus::models::soap::Timestamp {
                id: None, created: "2026-01-01T00:00:00Z".to_owned(),
                expires: "2099-01-01T00:00:00Z".to_owned(),
            },
        };
        let pending = PendingManagementExchange::new(
            "original-flow".to_owned(),
            xodus::models::secrets::Device {
                puid: "fixture".to_owned(),
                hwid: "fixture".to_owned(),
                device_id: "fixture".to_owned(),
                splicense: "fixture".to_owned(),
                username: "fixture".to_owned(),
                password: "fixture".to_owned(),
            },
            device_token,
            xodus::models::live::DAProperty {
                da_token: "NEUTRAL_NOT_XML".to_owned(),
                da_session_key: "fixture".to_owned(),
                da_start_time: "2026-01-01T00:00:00Z".to_owned(),
                da_expires: "2099-01-01T00:00:00Z".to_owned(),
                sts_inline_flow_token: "fixture".to_owned(),
                username: "fixture".to_owned(),
                puid: "fixture".to_owned(),
            },
            false,
        )
        .unwrap();
        let expires_at = pending.expires_at;
        let mut native = None;
        let result = issue_session(
            ConsentBootstrap {
                flow_id: "retry-flow".to_owned(),
                device: None,
                device_token: None,
                native_host: None,
                remaining_millis: 1000,
                resume_exchange: Some(Box::new(pending)),
            },
            &mut native,
            Instant::now() + Duration::from_secs(1),
        )
        .await;
        let Err(SessionFailure::ExchangeDeferred {
            reason,
            pending: Some(pending),
        }) = result
        else {
            panic!(
                "Neutral invalid XML must fail before any request and retain the bounded inputs"
            );
        };
        assert!(matches!(
            reason,
            NativeSignInFailure::TokenExchangeStage {
                stage: ExchangeFailureStage::RequestBuild
                    | ExchangeFailureStage::ResponseCryptography,
            }
        ));
        assert!(native.is_none());
        assert_eq!(pending.expires_at, expires_at);
        assert_eq!(pending.flow_id, "retry-flow");
    }

    #[test]
    fn helper_terminal_names_survive_handoff_without_provider_text() {
        use xodus_management::native_auth::{HostFailure, HostResult};
        for reason in [
            HostFailure::InvalidFrame,
            HostFailure::InvalidNavigation,
            HostFailure::NavigationFailed,
            HostFailure::PopupUnsupported,
            HostFailure::ContentTerminated,
            HostFailure::JavaScriptFailed,
            HostFailure::BridgeInvalid,
            HostFailure::DeadlineExpired,
            HostFailure::ParentUnavailable,
        ] {
            let (handoff, _) = failure_handoff(host_terminal(HostResult::Failed { reason }));
            assert!(matches!(handoff, ConsentHandoff::FailedNativeSignIn {
                reason: NativeSignInFailure::Host { reason: actual },
            } if actual == reason));
        }
        for (error, expected) in [
            (
                std::io::Error::new(std::io::ErrorKind::UnexpectedEof, "PRIVATE_SENTINEL"),
                NativeSignInFailure::ChannelEof,
            ),
            (
                std::io::Error::other("PRIVATE_SENTINEL"),
                NativeSignInFailure::Unclassified,
            ),
        ] {
            let (handoff, _) = failure_handoff(host_unavailable(error));
            assert!(
                matches!(handoff, ConsentHandoff::FailedNativeSignIn { reason }
                if reason == expected)
            );
            assert!(
                !serde_json::to_string(&handoff)
                    .unwrap()
                    .contains("PRIVATE_SENTINEL")
            );
        }
    }

    fn guardian_pair() -> (
        tokio::net::UnixStream,
        ParentGuardian,
        tokio::net::unix::OwnedWriteHalf,
    ) {
        let (parent, worker) = std::os::unix::net::UnixStream::pair().unwrap();
        parent.set_nonblocking(true).unwrap();
        worker.set_nonblocking(true).unwrap();
        let parent = tokio::net::UnixStream::from_std(parent).unwrap();
        let (reader, writer) = tokio::net::UnixStream::from_std(worker)
            .unwrap()
            .into_split();
        (parent, ParentGuardian::start(reader), writer)
    }

    #[tokio::test]
    async fn parent_guardian_retained_write_half_and_normal_terminal_drop() {
        let (mut parent, mut guardian, mut writer) = guardian_pair();
        assert!(
            tokio::time::timeout(Duration::from_millis(20), guardian.cancelled.changed())
                .await
                .is_err()
        );
        guardian
            .publish(
                &mut writer,
                &ConsentHandoff::Cancelled,
                Instant::now() + Duration::from_secs(1),
            )
            .await
            .unwrap();
        let outcome: ConsentHandoff = xodus_management::native_auth::read(&mut parent)
            .await
            .unwrap();
        assert!(matches!(outcome, ConsentHandoff::Cancelled));
        drop(parent);
        assert_eq!(guardian.state.load(Ordering::SeqCst), TERMINAL);
    }

    #[tokio::test]
    async fn parent_guardian_eof_halfclose_and_unexpected_bytes_fence_late_completion() {
        for mode in ["eof", "halfclose", "bytes"] {
            let (mut parent, mut guardian, mut writer) = guardian_pair();
            match mode {
                "halfclose" => parent.shutdown().await.unwrap(),
                "bytes" => parent.write_all(b"x").await.unwrap(),
                _ => drop(parent),
            }
            tokio::time::timeout(Duration::from_secs(1), guardian.cancelled.changed())
                .await
                .unwrap()
                .unwrap();
            assert_eq!(guardian.state.load(Ordering::SeqCst), CANCELLED);
            assert!(
                guardian
                    .publish(
                        &mut writer,
                        &ConsentHandoff::Cancelled,
                        Instant::now() + Duration::from_secs(1)
                    )
                    .await
                    .is_err()
            );
        }
    }

    #[tokio::test]
    async fn parent_guardian_expired_budget_prevents_publication() {
        let (_parent, mut guardian, mut writer) = guardian_pair();
        assert!(
            guardian
                .publish(
                    &mut writer,
                    &ConsentHandoff::Cancelled,
                    Instant::now() - Duration::from_millis(1)
                )
                .await
                .is_err()
        );
    }

    #[tokio::test]
    #[ignore = "internal owned neutral child, invoked only by lifecycle tests"]
    async fn neutral_worker_entry() {
        assert_eq!(std::env::var("XODUS_NEUTRAL_HOST_TEST").unwrap(), "1");
        use std::os::fd::AsFd;
        let channel = private_channel(rustix::io::dup(std::io::stdin().as_fd()).unwrap()).unwrap();
        channel.set_nonblocking(true).unwrap();
        let (reader, mut writer) = tokio::net::UnixStream::from_std(channel)
            .unwrap()
            .into_split();
        let mut guardian = ParentGuardian::start(reader);
        let binding = xodus_management::native_auth::HostBinding {
            version: 1,
            executable: std::env::var_os("XODUS_NEUTRAL_HELPER").unwrap().into(),
            sha256: std::env::var("XODUS_NEUTRAL_HELPER_SHA").unwrap(),
        };
        let mut host =
            crate::native_auth_host::NativeHost::spawn(&binding, &uuid::Uuid::nil().to_string())
                .unwrap();
        let budget = std::env::var("XODUS_NEUTRAL_BUDGET")
            .unwrap()
            .parse::<u64>()
            .unwrap();
        let deadline = Instant::now() + Duration::from_millis(budget);
        crate::native_auth_host::fixtures::open(&mut host, budget)
            .await
            .unwrap();
        assert!(matches!(
            host.receive().await.unwrap(),
            xodus_management::native_auth::HostResult::Ready
        ));
        writer.write_u32(std::process::id()).await.unwrap();
        writer.write_u32(host.pid()).await.unwrap();
        writer.flush().await.unwrap();
        tokio::select! {
            biased;
            _ = guardian.cancelled.changed() => {}
            _ = tokio::time::sleep_until(tokio::time::Instant::from_std(deadline)) => {}
        }
        host.abort().await.unwrap();
    }

    #[tokio::test]
    async fn actual_neutral_engine_death_worker_sigkill_and_budget_close_owned_helper() {
        use std::os::fd::OwnedFd;
        use std::process::Stdio;
        use tokio::process::Command;
        for mode in ["engineDeath", "workerDeath", "deadline"] {
            let (_helper_dir, binding) = crate::native_auth_host::fixtures::helper("normal");
            let engine_dir = tempfile::tempdir().unwrap();
            let engine_path = engine_dir.path().join("neutral-engine.py");
            std::fs::write(
                &engine_path,
                r#"import socket,subprocess,sys,os
control=socket.socket(fileno=0)
parent,child=socket.socketpair()
env=dict(os.environ,XODUS_NEUTRAL_HOST_TEST='1',XODUS_NEUTRAL_HELPER=sys.argv[2],
 XODUS_NEUTRAL_HELPER_SHA=sys.argv[3],XODUS_NEUTRAL_BUDGET=sys.argv[4])
p=subprocess.Popen([sys.argv[1],'--exact','management_auth::tests::neutral_worker_entry',
 '--ignored','--test-threads=1'],stdin=child,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,
 env=env,close_fds=True)
child.close()
b=b''
while len(b)<8:
 x=parent.recv(8-len(b))
 if not x:raise RuntimeError('neutral worker did not become ready')
 b+=x
control.sendall(b)
p.wait(timeout=10)
"#,
            )
            .unwrap();
            let (control, input) = std::os::unix::net::UnixStream::pair().unwrap();
            control.set_nonblocking(true).unwrap();
            let mut control = tokio::net::UnixStream::from_std(control).unwrap();
            let mut engine = Command::new("/usr/bin/python3")
                .arg(&engine_path)
                .arg(std::env::current_exe().unwrap())
                .arg(&binding.executable)
                .arg(&binding.sha256)
                .arg(if mode == "deadline" { "200" } else { "5000" })
                .stdin(Stdio::from(OwnedFd::from(input)))
                .stdout(Stdio::null())
                .stderr(Stdio::null())
                .kill_on_drop(true)
                .spawn()
                .unwrap();
            let worker = tokio::time::timeout(Duration::from_secs(5), control.read_u32())
                .await
                .unwrap()
                .unwrap();
            let helper = control.read_u32().await.unwrap();
            if mode == "engineDeath" {
                engine.kill().await.unwrap();
            } else if mode == "workerDeath" {
                let exit = Command::new("/bin/kill")
                    .args(["-KILL", &worker.to_string()])
                    .stdout(Stdio::null())
                    .stderr(Stdio::null())
                    .status()
                    .await
                    .unwrap();
                assert!(exit.success());
                tokio::time::timeout(Duration::from_secs(5), engine.wait())
                    .await
                    .unwrap()
                    .unwrap();
            } else {
                let exit = tokio::time::timeout(Duration::from_secs(5), engine.wait())
                    .await
                    .unwrap()
                    .unwrap();
                assert!(exit.success());
            }
            for pid in [worker, helper] {
                let mut alive = true;
                for _ in 0..40 {
                    alive = Command::new("/bin/kill")
                        .args(["-0", &pid.to_string()])
                        .stdout(Stdio::null())
                        .stderr(Stdio::null())
                        .status()
                        .await
                        .unwrap()
                        .success();
                    if !alive {
                        break;
                    }
                    tokio::time::sleep(Duration::from_millis(50)).await;
                }
                assert!(!alive, "Owned neutral descendant was not reaped");
            }
        }
    }

    #[test]
    fn handoff_is_anonymous_bounded_private_socket_not_public_output() {
        let (mut parent, child) = std::os::unix::net::UnixStream::pair().unwrap();
        let mut channel = private_channel(child.into()).unwrap();
        use std::os::fd::AsFd;
        assert!(
            rustix::io::fcntl_getfd(channel.as_fd())
                .unwrap()
                .contains(rustix::io::FdFlags::CLOEXEC)
        );
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
        for budget in [0, 1, 600_000, 600_001] {
            let payload = serde_json::json!({"flow_id":"fixture","device":null,"device_token":null,
                "native_host":null,"remaining_millis":budget});
            let bytes = serde_json::to_vec(&payload).unwrap();
            let (mut parent, mut child) = std::os::unix::net::UnixStream::pair().unwrap();
            parent
                .write_all(&(bytes.len() as u32).to_be_bytes())
                .unwrap();
            parent.write_all(&bytes).unwrap();
            assert_eq!(
                read_bootstrap(&mut child, "fixture").is_ok(),
                (1..=600_000).contains(&budget)
            );
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
