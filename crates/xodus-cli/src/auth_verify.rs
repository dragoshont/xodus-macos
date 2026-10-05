use std::future::Future;
use std::pin::Pin;

use xodus::api::response::ProviderResponseError;
use xodus::api::xbox::XboxAuthError;
use xodus::tokens::{ManagementProfileWitness, TokenManager};
use xodus_management::auth_verify::{AuthVerifier, VerificationFailure};

use crate::package::PackageReadError;
use crate::provider_credentials::CredentialError;

pub struct PackageVerifier {
    client: reqwest::Client,
}

impl PackageVerifier {
    pub fn new() -> Result<Self, VerificationFailure> {
        let client = reqwest::Client::builder()
            .timeout(xodus_management::auth_verify::DEADLINE)
            .connect_timeout(std::time::Duration::from_secs(10))
            .redirect(reqwest::redirect::Policy::none())
            .build()
            .map_err(|_| VerificationFailure::TransportFailed)?;
        Ok(Self { client })
    }
}

fn provider_failure(error: ProviderResponseError) -> VerificationFailure {
    match error {
        ProviderResponseError::HttpRejected { status: 401 | 403 } => {
            VerificationFailure::AuthRejected
        }
        ProviderResponseError::HttpRejected { status: 404 } => {
            VerificationFailure::PackageUnavailable
        }
        ProviderResponseError::RequestFailed
        | ProviderResponseError::Deadline
        | ProviderResponseError::HttpRejected { .. } => VerificationFailure::TransportFailed,
        _ => VerificationFailure::ResponseInvalid,
    }
}

fn classify(error: PackageReadError) -> VerificationFailure {
    match error {
        PackageReadError::Credentials(CredentialError::ProfileChanged) => {
            VerificationFailure::ProfileChanged
        }
        PackageReadError::Credentials(CredentialError::Deadline) => {
            VerificationFailure::TransportFailed
        }
        PackageReadError::Credentials(_) => VerificationFailure::CredentialUnavailable,
        PackageReadError::Authentication(XboxAuthError::Provider(
            ProviderResponseError::HttpRejected { status: 404 },
        )) => VerificationFailure::AuthExchangeFailed,
        PackageReadError::Authentication(XboxAuthError::Provider(error)) => provider_failure(error),
        PackageReadError::Authentication(XboxAuthError::ExchangeFailed) => {
            VerificationFailure::AuthExchangeFailed
        }
        PackageReadError::Authentication(_)
        | PackageReadError::InvalidResponse
        | PackageReadError::InvalidContent => VerificationFailure::ResponseInvalid,
        PackageReadError::Provider(error) => provider_failure(error),
        PackageReadError::Unavailable => VerificationFailure::PackageUnavailable,
    }
}

impl AuthVerifier for PackageVerifier {
    fn verify(
        &self,
        tokens: TokenManager,
        content_id: String,
    ) -> Pin<
        Box<dyn Future<Output = Result<ManagementProfileWitness, VerificationFailure>> + Send + '_>,
    > {
        Box::pin(async move {
            let readonly = tokens
                .readonly_management_profile()
                .map_err(|_| VerificationFailure::CredentialUnavailable)?;
            crate::package::get_packages_verified(&self.client, &readonly, content_id)
                .await
                .map_err(classify)
                .and_then(|read| {
                    read.profile
                        .ok_or(VerificationFailure::CredentialUnavailable)
                })
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn credential_read_deadline_is_exact_retryable_transport_failure() {
        let error = classify(PackageReadError::Credentials(CredentialError::Deadline));
        assert_eq!(
            serde_json::to_value(error.wire_error()).unwrap(),
            serde_json::json!({
                "code":"NETWORK_UNAVAILABLE",
                "message":"Authenticated read failed: transportFailed.",
                "retryable":true,
                "details":{"category":"authenticatedReadFailure","stage":"transportFailed"},
            })
        );
    }

    #[test]
    fn authenticated_read_errors_are_closed_and_distinguish_unavailable_from_rejected() {
        for (error, expected) in [
            (
                PackageReadError::Credentials(CredentialError::ProfileChanged),
                VerificationFailure::ProfileChanged,
            ),
            (
                PackageReadError::Credentials(CredentialError::StoreUnavailable),
                VerificationFailure::CredentialUnavailable,
            ),
            (
                PackageReadError::Authentication(XboxAuthError::ExchangeFailed),
                VerificationFailure::AuthExchangeFailed,
            ),
            (
                PackageReadError::Provider(ProviderResponseError::HttpRejected { status: 401 }),
                VerificationFailure::AuthRejected,
            ),
            (
                PackageReadError::Provider(ProviderResponseError::HttpRejected { status: 403 }),
                VerificationFailure::AuthRejected,
            ),
            (
                PackageReadError::Provider(ProviderResponseError::HttpRejected { status: 404 }),
                VerificationFailure::PackageUnavailable,
            ),
            (
                PackageReadError::Unavailable,
                VerificationFailure::PackageUnavailable,
            ),
            (
                PackageReadError::Provider(ProviderResponseError::Deadline),
                VerificationFailure::TransportFailed,
            ),
            (
                PackageReadError::Provider(ProviderResponseError::InvalidJson),
                VerificationFailure::ResponseInvalid,
            ),
            (
                PackageReadError::Provider(ProviderResponseError::TooLarge { limit: 123 }),
                VerificationFailure::ResponseInvalid,
            ),
        ] {
            let result = classify(error);
            assert_eq!(result, expected);
            let wire = serde_json::to_string(&result.wire_error()).unwrap();
            for value in [
                "PRIVATE_SENTINEL",
                "Authorization",
                "http",
                "123",
                "401",
                "403",
                "404",
            ] {
                assert!(!wire.contains(value));
            }
        }
    }
}
