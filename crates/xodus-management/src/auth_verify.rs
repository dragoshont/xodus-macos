use std::future::Future;
use std::pin::Pin;
use std::time::Duration;

use xodus::tokens::{ManagementProfileWitness, TokenManager};

use crate::wire::{ErrorCode, WireError};

pub const DEADLINE: Duration = Duration::from_secs(30);
const PUBLICATION_READ_DEADLINE: Duration = Duration::from_secs(2);
static PUBLICATION_READ_PERMITS: tokio::sync::Semaphore = tokio::sync::Semaphore::const_new(4);

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum VerificationFailure {
    CredentialUnavailable,
    ProfileChanged,
    AuthExchangeFailed,
    AuthRejected,
    TransportFailed,
    ResponseInvalid,
    PackageUnavailable,
}

impl VerificationFailure {
    pub fn recent_wire_error(self) -> WireError {
        let failure = if self == Self::PackageUnavailable {
            Self::ResponseInvalid
        } else {
            self
        };
        recent_error(failure.wire_error())
    }

    pub fn wire_error(self) -> WireError {
        let (stage, code, retryable) = match self {
            Self::CredentialUnavailable => ("credentialUnavailable", ErrorCode::AuthInvalid, false),
            Self::ProfileChanged => ("profileChanged", ErrorCode::AuthInvalid, false),
            Self::AuthExchangeFailed => ("authExchangeFailed", ErrorCode::AuthInvalid, true),
            Self::AuthRejected => ("authRejected", ErrorCode::AccessRevoked, false),
            Self::TransportFailed => ("transportFailed", ErrorCode::NetworkUnavailable, true),
            Self::ResponseInvalid => ("responseInvalid", ErrorCode::IntegrityFailed, false),
            Self::PackageUnavailable => {
                ("packageUnavailable", ErrorCode::PackageUnavailable, false)
            }
        };
        let mut error = WireError::new(
            code,
            &format!("Authenticated read failed: {stage}."),
            retryable,
        );
        error.details = Some(
            [
                (
                    "category".to_owned(),
                    serde_json::Value::from("authenticatedReadFailure"),
                ),
                ("stage".to_owned(), serde_json::Value::from(stage)),
            ]
            .into_iter()
            .collect(),
        );
        error
    }
}

pub fn recent_error(mut error: WireError) -> WireError {
    if let Some(details) = &mut error.details
        && details.get("category").and_then(serde_json::Value::as_str)
            == Some("authenticatedReadFailure")
    {
        if let Some(stage) = details.get("stage").and_then(serde_json::Value::as_str) {
            error.message = format!("Recent library read failed: {stage}.");
        }
        details.insert("category".to_owned(), "recentLibraryFailure".into());
    }
    error
}

pub struct RecentLibraryRead {
    pub data: crate::wire::RecentLibraryData,
    pub profile: ManagementProfileWitness,
}

pub async fn bounded_verification<T>(
    deadline: Duration,
    operation: impl Future<Output = Result<T, VerificationFailure>>,
) -> Result<T, VerificationFailure> {
    tokio::time::timeout(deadline, operation)
        .await
        .unwrap_or(Err(VerificationFailure::TransportFailed))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[tokio::test]
    async fn total_deadline_drops_the_pending_operation_without_retry() {
        struct Dropped(std::sync::Arc<std::sync::atomic::AtomicBool>);
        impl Drop for Dropped {
            fn drop(&mut self) {
                self.0.store(true, std::sync::atomic::Ordering::SeqCst);
            }
        }
        let dropped = std::sync::Arc::new(std::sync::atomic::AtomicBool::new(false));
        let owned = Dropped(dropped.clone());
        let result = bounded_verification(Duration::from_millis(20), async move {
            let _guard = owned;
            std::future::pending::<Result<(), VerificationFailure>>().await
        })
        .await;
        assert_eq!(result, Err(VerificationFailure::TransportFailed));
        assert!(dropped.load(std::sync::atomic::Ordering::SeqCst));
        assert_eq!(DEADLINE, Duration::from_secs(30));
    }
}

pub async fn revalidate_publication(
    tokens: TokenManager,
    witness: ManagementProfileWitness,
) -> Result<ManagementProfileWitness, VerificationFailure> {
    let deadline = tokio::time::Instant::now() + PUBLICATION_READ_DEADLINE;
    let permit = tokio::time::timeout_at(deadline, PUBLICATION_READ_PERMITS.acquire())
        .await
        .map_err(|_| VerificationFailure::TransportFailed)?
        .map_err(|_| VerificationFailure::CredentialUnavailable)?;
    let readonly = tokens
        .readonly_management_profile()
        .map_err(|_| VerificationFailure::CredentialUnavailable)?;
    let read = tokio::task::spawn_blocking(move || {
        let _permit = permit;
        readonly
            .verify_management_publication(&witness)
            .map_err(|error| match error {
                xodus::tokens::store::TokenStoreError::NotFound
                | xodus::tokens::store::TokenStoreError::InvalidCredential
                | xodus::tokens::store::TokenStoreError::Serde(_) => {
                    VerificationFailure::ProfileChanged
                }
                _ => VerificationFailure::CredentialUnavailable,
            })?;
        Ok(witness)
    });
    tokio::time::timeout_at(deadline, read)
        .await
        .map_err(|_| VerificationFailure::TransportFailed)?
        .map_err(|_| VerificationFailure::CredentialUnavailable)?
}

pub trait AuthVerifier: Send + Sync {
    fn plan_supported(&self) -> bool {
        false
    }

    fn plan_preflight(&self) -> Option<crate::install_plan::PlanFailure> {
        None
    }

    fn plan(
        &self,
        _: Option<TokenManager>,
        _: crate::wire::PlanParams,
    ) -> Pin<
        Box<
            dyn Future<
                    Output = Result<
                        crate::install_plan::PlanRead,
                        crate::install_plan::PlanFailure,
                    >,
                > + Send
                + '_,
        >,
    > {
        Box::pin(async { Err(crate::install_plan::PlanFailure::FormatUnsupported) })
    }

    fn recent_supported(&self) -> bool {
        false
    }

    fn recent(
        &self,
        _: TokenManager,
        _: crate::wire::RecentLibraryParams,
    ) -> Pin<Box<dyn Future<Output = Result<RecentLibraryRead, VerificationFailure>> + Send + '_>>
    {
        Box::pin(async { Err(VerificationFailure::ResponseInvalid) })
    }

    fn verify(
        &self,
        tokens: TokenManager,
        content_id: String,
    ) -> Pin<
        Box<dyn Future<Output = Result<ManagementProfileWitness, VerificationFailure>> + Send + '_>,
    >;
}
