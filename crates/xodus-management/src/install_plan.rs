use crate::wire::{
    ErrorCode, InstallPlanFailureCategory, InstallPlanFailureDetails,
    InstallPlanFailureReason as Reason, InstallPlanFailureStage as Stage, PlanParams, WireError,
};

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum PlanFailure {
    CredentialUnavailable,
    ProfileChanged,
    AuthRejected,
    Ambiguous,
    PackageUnavailable,
    ApplicabilityUnproven,
    SelectionUnsupported,
    FormatUnsupported,
    IntegrityUnproven,
    LayoutIncomplete,
    LicenseRequired,
    DestinationUnsupported,
    DestinationChanged,
    InsufficientSpace,
    ProviderUnavailable,
    ResponseInvalid,
    Expired,
}

impl PlanFailure {
    pub fn wire_error(self) -> WireError {
        let (stage, reason, code, retryable, message) = match self {
            Self::CredentialUnavailable => (
                Stage::Credentials,
                Reason::Unavailable,
                ErrorCode::AuthInvalid,
                false,
                "Install planning requires a current saved launcher account.",
            ),
            Self::ProfileChanged => (
                Stage::Credentials,
                Reason::ProfileChanged,
                ErrorCode::AuthInvalid,
                false,
                "The launcher account changed during install planning. Select the edition again.",
            ),
            Self::AuthRejected => (
                Stage::Credentials,
                Reason::Rejected,
                ErrorCode::AccessRevoked,
                false,
                "The package metadata provider rejected this account. No license was acquired.",
            ),
            Self::Ambiguous => (
                Stage::Selection,
                Reason::Ambiguous,
                ErrorCode::PackageAmbiguous,
                false,
                "The selected edition does not resolve to exactly one PC package.",
            ),
            Self::PackageUnavailable => (
                Stage::Package,
                Reason::Unavailable,
                ErrorCode::PackageUnavailable,
                false,
                "The selected edition has no available matching PC base package.",
            ),
            Self::ApplicabilityUnproven => (
                Stage::Selection,
                Reason::ApplicabilityUnproven,
                ErrorCode::UnsupportedConfiguration,
                false,
                "Package architecture, language and dependency applicability are not established by the available metadata.",
            ),
            Self::SelectionUnsupported => (
                Stage::Selection,
                Reason::Unsupported,
                ErrorCode::UnsupportedConfiguration,
                false,
                "The selected edition has no package declared for the requested architecture and language.",
            ),
            Self::FormatUnsupported => (
                Stage::Format,
                Reason::Unsupported,
                ErrorCode::UnsupportedConfiguration,
                false,
                "A supported authenticated pre-key package format has not been established.",
            ),
            Self::IntegrityUnproven => (
                Stage::Integrity,
                Reason::Unproven,
                ErrorCode::IntegrityFailed,
                false,
                "The provider digest algorithm, encoding and coverage are not established.",
            ),
            Self::LayoutIncomplete => (
                Stage::Layout,
                Reason::Incomplete,
                ErrorCode::PackageUnavailable,
                false,
                "The pre-key expanded layout, padded file sizes and scratch bound are incomplete.",
            ),
            Self::LicenseRequired => (
                Stage::License,
                Reason::AcquisitionRequired,
                ErrorCode::AccessUnknown,
                false,
                "Further planning would require a separately authorized license or key operation.",
            ),
            Self::DestinationUnsupported => (
                Stage::Destination,
                Reason::Unsupported,
                ErrorCode::UnsupportedConfiguration,
                false,
                "Install planning currently supports only the existing private managed state root.",
            ),
            Self::DestinationChanged => (
                Stage::Destination,
                Reason::Changed,
                ErrorCode::PlanChanged,
                false,
                "The managed destination changed during install planning. No files were created.",
            ),
            Self::InsufficientSpace => (
                Stage::Space,
                Reason::Insufficient,
                ErrorCode::InsufficientSpace,
                false,
                "The selected volume has insufficient space for the established peak allocation.",
            ),
            Self::ProviderUnavailable => (
                Stage::Provider,
                Reason::Unavailable,
                ErrorCode::NetworkUnavailable,
                true,
                "The bounded package metadata read did not complete. No license or payload was requested.",
            ),
            Self::ResponseInvalid => (
                Stage::Provider,
                Reason::InvalidResponse,
                ErrorCode::IntegrityFailed,
                false,
                "Required package metadata is malformed or inconsistent.",
            ),
            Self::Expired => (
                Stage::Selection,
                Reason::Expired,
                ErrorCode::PlanExpired,
                false,
                "The in-memory install planning observation expired. Resolve the selected edition again.",
            ),
        };
        let details = InstallPlanFailureDetails {
            category: InstallPlanFailureCategory::InstallPlanFailure,
            stage,
            reason,
        };
        let mut error = WireError::new(code, message, retryable);
        error.details = Some(
            [
                ("category".to_owned(), serde_json::json!(details.category)),
                ("stage".to_owned(), serde_json::json!(details.stage)),
                ("reason".to_owned(), serde_json::json!(details.reason)),
            ]
            .into_iter()
            .collect(),
        );
        error
    }
}

#[cfg(feature = "live")]
pub fn verification_failure(failure: crate::auth_verify::VerificationFailure) -> PlanFailure {
    use crate::auth_verify::VerificationFailure as Failure;
    match failure {
        Failure::CredentialUnavailable | Failure::AuthExchangeFailed => {
            PlanFailure::CredentialUnavailable
        }
        Failure::ProfileChanged => PlanFailure::ProfileChanged,
        Failure::AuthRejected => PlanFailure::AuthRejected,
        Failure::TransportFailed => PlanFailure::ProviderUnavailable,
        Failure::ResponseInvalid => PlanFailure::ResponseInvalid,
        Failure::PackageUnavailable => PlanFailure::PackageUnavailable,
    }
}

#[cfg(feature = "live")]
pub(crate) fn verification_error(error: WireError) -> WireError {
    use crate::auth_verify::VerificationFailure as Failure;
    let stage = error
        .details
        .as_ref()
        .and_then(|details| details.get("stage"))
        .and_then(serde_json::Value::as_str);
    let failure = match stage {
        Some("profileChanged") => Failure::ProfileChanged,
        Some("transportFailed") => Failure::TransportFailed,
        Some("authRejected") => Failure::AuthRejected,
        Some("responseInvalid") => Failure::ResponseInvalid,
        Some("packageUnavailable") => Failure::PackageUnavailable,
        _ => Failure::CredentialUnavailable,
    };
    verification_failure(failure).wire_error()
}

#[cfg(feature = "live")]
pub struct PlanRead {
    pub params: PlanParams,
    pub profile: xodus::tokens::ManagementProfileWitness,
    pub package: xodus::models::packagespc::PackageDetails,
    pub selected_package: xodus::models::displaycatalog::Package,
    pub package_id: String,
    pub content_id: String,
    pub expires_at: tokio::time::Instant,
}

#[cfg(feature = "live")]
pub fn declared_applicability(
    params: &PlanParams,
    package: &xodus::models::displaycatalog::Package,
) -> Result<bool, PlanFailure> {
    let Some(architectures) = package.architectures.as_ref() else {
        return Err(PlanFailure::ApplicabilityUnproven);
    };
    let Some(languages) = package.languages.as_ref() else {
        return Err(PlanFailure::ApplicabilityUnproven);
    };
    let architectures = architectures
        .as_array()
        .ok_or(PlanFailure::ResponseInvalid)?;
    let languages = languages.as_array().ok_or(PlanFailure::ResponseInvalid)?;
    if architectures.is_empty() || languages.is_empty() {
        return Err(PlanFailure::ApplicabilityUnproven);
    }
    if architectures.len() > 32
        || languages.len() > 256
        || architectures.iter().chain(languages).any(|value| {
            value.as_str().is_none_or(|value| {
                value.is_empty() || value.len() > 128 || value.chars().any(char::is_control)
            })
        })
    {
        return Err(PlanFailure::ResponseInvalid);
    }
    let requested = match params.architecture {
        crate::wire::Architecture::Arm64 => "arm64",
        crate::wire::Architecture::X86_64 => "x64",
    };
    Ok(architectures.iter().any(|value| {
        value
            .as_str()
            .is_some_and(|value| value.eq_ignore_ascii_case(requested))
    }) && languages.iter().any(|value| {
        value
            .as_str()
            .is_some_and(|value| value.eq_ignore_ascii_case(&params.language))
    }))
}

#[cfg(feature = "live")]
impl PlanRead {
    pub fn validate(
        &self,
        params: &PlanParams,
        now: tokio::time::Instant,
    ) -> Result<(), PlanFailure> {
        if &self.params != params {
            return Err(PlanFailure::ResponseInvalid);
        }
        if now >= self.expires_at {
            return Err(PlanFailure::Expired);
        }
        if !declared_applicability(params, &self.selected_package)? {
            return Err(PlanFailure::SelectionUnsupported);
        }
        if !crate::state::identifier_valid(&self.package_id)
            || self.selected_package.package_id.as_deref() != Some(&self.package_id)
            || self.selected_package.content_id.as_deref() != Some(&self.content_id)
            || self.package.content_id != self.content_id
            || !uuid::Uuid::parse_str(&self.content_id)
                .is_ok_and(|id| !id.is_nil() && id.to_string() == self.content_id)
            || !self.package.package_found
            || self.package.package_files.is_empty()
        {
            return Err(PlanFailure::ResponseInvalid);
        }
        Ok(())
    }

    pub fn readiness_failure(&self) -> PlanFailure {
        // Parsed hash strings do not establish authoritative MSIXVC/Update coverage.
        PlanFailure::IntegrityUnproven
    }
}

pub struct DestinationBinding {
    #[cfg(target_os = "macos")]
    root: std::path::PathBuf,
    #[cfg(target_os = "macos")]
    directory: std::fs::File,
}

impl DestinationBinding {
    pub fn read(root: &std::path::Path, requested: &str) -> Result<Self, PlanFailure> {
        if root.to_str() != Some(requested) || !crate::inspection::directory_valid(requested) {
            return Err(PlanFailure::DestinationUnsupported);
        }
        #[cfg(target_os = "macos")]
        {
            let directory = crate::staging::snapshot_directory(root)
                .map_err(|_| PlanFailure::DestinationUnsupported)?;
            Ok(Self {
                root: root.to_owned(),
                directory,
            })
        }
        #[cfg(not(target_os = "macos"))]
        {
            Err(PlanFailure::DestinationUnsupported)
        }
    }

    pub fn revalidate(&self) -> Result<(), PlanFailure> {
        #[cfg(target_os = "macos")]
        {
            use std::os::unix::fs::MetadataExt;
            let observed = self
                .directory
                .metadata()
                .map_err(|_| PlanFailure::DestinationChanged)?;
            let current = crate::staging::snapshot_directory(&self.root)
                .and_then(|directory| {
                    directory
                        .metadata()
                        .map_err(|_| PlanFailure::DestinationChanged.wire_error())
                })
                .map_err(|_| PlanFailure::DestinationChanged)?;
            if observed.dev() != current.dev() || observed.ino() != current.ino() {
                return Err(PlanFailure::DestinationChanged);
            }
            Ok(())
        }
        #[cfg(not(target_os = "macos"))]
        {
            Err(PlanFailure::DestinationUnsupported)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn install_plan_fixed_errors_match_all_frozen_tuples_without_private_fields() {
        let frames: Vec<serde_json::Value> = serde_json::from_str(include_str!(
            "../../../docs/contracts/fixtures/management-v1/positive.json"
        ))
        .unwrap();
        for failure in [
            PlanFailure::CredentialUnavailable,
            PlanFailure::ProfileChanged,
            PlanFailure::AuthRejected,
            PlanFailure::Ambiguous,
            PlanFailure::PackageUnavailable,
            PlanFailure::ApplicabilityUnproven,
            PlanFailure::SelectionUnsupported,
            PlanFailure::FormatUnsupported,
            PlanFailure::IntegrityUnproven,
            PlanFailure::LayoutIncomplete,
            PlanFailure::LicenseRequired,
            PlanFailure::DestinationUnsupported,
            PlanFailure::DestinationChanged,
            PlanFailure::InsufficientSpace,
            PlanFailure::ProviderUnavailable,
            PlanFailure::ResponseInvalid,
            PlanFailure::Expired,
        ] {
            let error = serde_json::to_value(failure.wire_error()).unwrap();
            assert!(
                frames.iter().any(|frame| frame["error"] == error),
                "{failure:?}"
            );
            assert!(!error.to_string().contains("PRIVATE_SENTINEL"));
            assert_eq!(error["details"].as_object().unwrap().len(), 3);
        }
    }

    #[cfg(target_os = "macos")]
    #[test]
    fn install_plan_destination_is_existing_exact_private_root_without_creation_and_detects_replacement()
     {
        use std::os::unix::fs::{PermissionsExt, symlink};
        let temporary = tempfile::tempdir().unwrap();
        let root = temporary.path().canonicalize().unwrap();
        std::fs::set_permissions(&root, std::fs::Permissions::from_mode(0o700)).unwrap();
        let binding = DestinationBinding::read(&root, root.to_str().unwrap()).unwrap();
        assert!(binding.revalidate().is_ok());
        assert_eq!(std::fs::read_dir(&root).unwrap().count(), 0);
        let missing = root.join("missing");
        assert!(DestinationBinding::read(&root, missing.to_str().unwrap()).is_err());
        assert!(!missing.exists());
        let retained = tempfile::tempdir().unwrap();
        std::fs::rename(&root, retained.path().join("retained")).unwrap();
        std::fs::create_dir(&root).unwrap();
        std::fs::set_permissions(&root, std::fs::Permissions::from_mode(0o700)).unwrap();
        assert_eq!(binding.revalidate(), Err(PlanFailure::DestinationChanged));
        let linked = root.join("linked");
        symlink(retained.path().join("retained"), &linked).unwrap();
        assert!(DestinationBinding::read(&linked, linked.to_str().unwrap()).is_err());
    }
}
