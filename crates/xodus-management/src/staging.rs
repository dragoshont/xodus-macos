//! Bounded local transaction primitive. It does not establish package authorization,
//! hash algorithm provenance, extract encrypted packages, or certify a runtime.
use std::collections::{BTreeMap, BTreeSet};
use std::fs::{self, File, OpenOptions};
use std::io::{Read, Write};
use std::path::{Path, PathBuf};

use fs2::FileExt;
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use uuid::Uuid;

use crate::state::{identifier_valid, validate_directory};
use crate::wire::{ErrorCode, InstallationRecord, Protocol, WireError};

pub const MAX_REGISTRY_INSTALLATIONS: usize = 256;
pub const MAX_STORAGE_JSON_BYTES: usize = 4 * 1024 * 1024;
const SNAPSHOT_TIMEOUT: std::time::Duration = std::time::Duration::from_secs(5);

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct Manifest {
    #[serde(rename = "productID")]
    pub product_id: String,
    #[serde(rename = "editionID")]
    pub edition_id: String,
    #[serde(rename = "packageID")]
    pub package_id: String,
    pub package_version: String,
    pub files: Vec<ManifestFile>,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct ManifestFile {
    pub path: String,
    pub bytes: u64,
    pub sha256: String,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct Transaction {
    #[serde(rename = "transactionID")]
    pub transaction_id: String,
    #[serde(rename = "installationID")]
    pub installation_id: String,
    pub expected_revision: u64,
    pub phase: Phase,
    pub manifest: Manifest,
}

#[derive(Clone, Debug, Deserialize, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub enum Phase {
    Prepared,
    Verified,
    Promoting,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct Version {
    #[serde(rename = "transactionID")]
    pub transaction_id: String,
    pub manifest: Manifest,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct LocalInstallation {
    #[serde(rename = "installationID")]
    pub installation_id: String,
    pub revision: u64,
    pub active: Version,
    pub rollback: Option<Version>,
}

#[derive(Clone, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
struct Registry {
    version: Protocol,
    installations: BTreeMap<String, LocalInstallation>,
}

pub struct StagingStore {
    root: PathBuf,
    _lock: File,
    registry: Registry,
}

fn error(code: ErrorCode, message: &str) -> WireError {
    WireError::new(code, message, false)
}

fn recovery() -> WireError {
    error(
        ErrorCode::RegistryRecoveryRequired,
        "Local staging transaction requires explicit recovery. No active version or saves were replaced.",
    )
}

pub(crate) fn digest_hex(bytes: &[u8]) -> String {
    const HEX: &[u8] = b"0123456789abcdef";
    let mut result = String::with_capacity(bytes.len() * 2);
    for byte in bytes {
        result.push(HEX[(byte >> 4) as usize] as char);
        result.push(HEX[(byte & 15) as usize] as char);
    }
    result
}

pub fn valid_relative_path(value: &str) -> bool {
    !value.is_empty()
        && value.len() <= 1024
        && !value.contains('\\')
        && !value.contains(':')
        && value.is_ascii()
        && value.split('/').count() <= 8
        && !value.chars().any(char::is_control)
        && value.split('/').all(|component| {
            !component.is_empty()
                && component != "."
                && component != ".."
                && !component.starts_with('.')
                && !component.ends_with('.')
                && !component.ends_with(' ')
        })
}

fn validate_manifest(manifest: &Manifest) -> Result<u64, WireError> {
    if !identifier_valid(&manifest.product_id)
        || !identifier_valid(&manifest.edition_id)
        || !identifier_valid(&manifest.package_id)
        || !identifier_valid(&manifest.package_version)
        || manifest.files.is_empty()
        || manifest.files.len() > 256
    {
        return Err(error(
            ErrorCode::InvalidRequest,
            "Invalid bounded package manifest.",
        ));
    }
    let mut paths = BTreeSet::new();
    let mut total = 0u64;
    for file in &manifest.files {
        if !valid_relative_path(&file.path)
            || file.sha256.len() != 64
            || !file
                .sha256
                .bytes()
                .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
            || !paths.insert(file.path.to_lowercase())
        {
            return Err(error(
                ErrorCode::InvalidRequest,
                "Invalid, duplicate or ambiguous package path/digest.",
            ));
        }
        total = total
            .checked_add(file.bytes)
            .ok_or_else(|| error(ErrorCode::InvalidRequest, "Package size overflow."))?;
    }
    for path in &paths {
        for other in &paths {
            if other.starts_with(&format!("{path}/")) {
                return Err(error(
                    ErrorCode::InvalidRequest,
                    "File/directory path collision.",
                ));
            }
        }
    }
    Ok(total)
}

fn bounded_id(value: &str) -> Result<(), WireError> {
    if Uuid::parse_str(value).is_err() || value.len() != 36 {
        return Err(error(
            ErrorCode::InvalidRequest,
            "Local transaction identity must be an opaque UUID.",
        ));
    }
    Ok(())
}

fn require_regular(path: &Path) -> Result<(), WireError> {
    let metadata = fs::symlink_metadata(path).map_err(|_| recovery())?;
    if !metadata.is_file() || metadata.file_type().is_symlink() {
        return Err(recovery());
    }
    #[cfg(unix)]
    {
        use std::os::unix::fs::MetadataExt;
        if metadata.nlink() != 1 {
            return Err(recovery());
        }
    }
    Ok(())
}

fn regular_or_missing(path: &Path) -> Result<(), WireError> {
    match fs::symlink_metadata(path) {
        Ok(_) => require_regular(path),
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => Ok(()),
        Err(_) => Err(recovery()),
    }
}

fn require_directory(path: &Path) -> Result<(), WireError> {
    let metadata = fs::symlink_metadata(path).map_err(|_| recovery())?;
    if !metadata.is_dir() || metadata.file_type().is_symlink() {
        return Err(recovery());
    }
    Ok(())
}

fn sync_directory(path: &Path) -> Result<(), WireError> {
    #[cfg(unix)]
    File::open(path)
        .and_then(|file| file.sync_all())
        .map_err(|_| recovery())?;
    Ok(())
}

fn atomic_json(path: &Path, value: &impl Serialize) -> Result<(), WireError> {
    atomic_bytes(path, &serialized_json(value)?)
}

fn serialized_json(value: &impl Serialize) -> Result<Vec<u8>, WireError> {
    let bytes = serde_json::to_vec(value).map_err(|_| recovery())?;
    if bytes.len() > MAX_STORAGE_JSON_BYTES {
        return Err(error(
            ErrorCode::LimitExceeded,
            "Local durable metadata would exceed its reloadable storage byte limit. Preserve active versions and saves.",
        ));
    }
    Ok(bytes)
}

fn registry_bytes(registry: &Registry) -> Result<Vec<u8>, WireError> {
    if registry.installations.len() > MAX_REGISTRY_INSTALLATIONS {
        return Err(error(
            ErrorCode::LimitExceeded,
            "Local installation registry is at capacity. No active version or saves were replaced.",
        ));
    }
    serialized_json(registry)
}

fn atomic_registry(path: &Path, registry: &Registry) -> Result<(), WireError> {
    atomic_bytes(path, &registry_bytes(registry)?)
}

fn atomic_bytes(path: &Path, bytes: &[u8]) -> Result<(), WireError> {
    regular_or_missing(path)?;
    let directory = path.parent().ok_or_else(recovery)?;
    require_directory(directory)?;
    let mut file = tempfile::NamedTempFile::new_in(directory).map_err(|_| recovery())?;
    file.write_all(bytes).map_err(|_| recovery())?;
    file.as_file().sync_all().map_err(|_| recovery())?;
    file.persist(path).map_err(|_| recovery())?;
    sync_directory(directory)
}

fn read_json<T: serde::de::DeserializeOwned>(path: &Path) -> Result<T, WireError> {
    require_regular(path)?;
    let file = File::open(path).map_err(|_| recovery())?;
    if file.metadata().map_err(|_| recovery())?.len() > MAX_STORAGE_JSON_BYTES as u64 {
        return Err(recovery());
    }
    serde_json::from_reader(file.take(MAX_STORAGE_JSON_BYTES as u64 + 1)).map_err(|_| recovery())
}

fn validate_registry(registry: &Registry) -> Result<(), WireError> {
    if registry.version != Protocol::default()
        || registry.installations.len() > MAX_REGISTRY_INSTALLATIONS
    {
        return Err(recovery());
    }
    for (id, installation) in &registry.installations {
        bounded_id(id)?;
        if id != &installation.installation_id || installation.revision == 0 {
            return Err(recovery());
        }
        for version in std::iter::once(&installation.active).chain(installation.rollback.iter()) {
            bounded_id(&version.transaction_id)?;
            validate_manifest(&version.manifest)?;
        }
    }
    Ok(())
}

pub async fn installed_snapshot(
    root: PathBuf,
    permits: std::sync::Arc<tokio::sync::Semaphore>,
) -> Result<Vec<InstallationRecord>, WireError> {
    crate::inspection::bounded_local_read(
        permits,
        SNAPSHOT_TIMEOUT,
        move || StagingStore::read_snapshot(&root),
        snapshot_timeout(),
        WireError::new(
            ErrorCode::RegistryRecoveryRequired,
            "Local registry snapshot worker is unavailable. No files were modified.",
            true,
        ),
    )
    .await
}

fn snapshot_timeout() -> WireError {
    WireError::new(
        ErrorCode::RegistryRecoveryRequired,
        "Local registry snapshot did not finish within its bounded deadline. No files were modified.",
        true,
    )
}

#[cfg(target_os = "macos")]
fn snapshot_directory(path: &Path) -> Result<File, WireError> {
    use std::os::unix::fs::{MetadataExt, PermissionsExt};
    if !path.is_absolute()
        || path.components().any(|component| {
            matches!(
                component,
                std::path::Component::ParentDir | std::path::Component::CurDir
            )
        })
    {
        return Err(recovery());
    }
    let directory = crate::inspection::open_directory(path).map_err(|_| recovery())?;
    let metadata = directory.metadata().map_err(|_| recovery())?;
    if metadata.uid() != unsafe { libc::geteuid() }
        || metadata.permissions().mode() & 0o777 != 0o700
    {
        return Err(recovery());
    }
    Ok(directory)
}

#[cfg(target_os = "macos")]
fn snapshot_open(
    parent: &File,
    name: &std::ffi::CStr,
    directory: bool,
) -> Result<Option<File>, WireError> {
    use std::os::unix::fs::{MetadataExt, PermissionsExt};
    match crate::inspection::open_at(Some(parent), name, directory) {
        Ok(file) => {
            let metadata = file.metadata().map_err(|_| recovery())?;
            let parent_metadata = parent.metadata().map_err(|_| recovery())?;
            if metadata.dev() != parent_metadata.dev()
                || metadata.uid() != unsafe { libc::geteuid() }
                || (directory && metadata.permissions().mode() & 0o777 != 0o700)
            {
                return Err(recovery());
            }
            Ok(Some(file))
        }
        Err(error) if error.code == ErrorCode::NotFound => Ok(None),
        Err(_) => Err(recovery()),
    }
}

#[cfg(target_os = "macos")]
fn snapshot_json<T: serde::de::DeserializeOwned>(
    parent: &File,
    name: &std::ffi::CStr,
) -> Result<Option<T>, WireError> {
    use std::os::unix::fs::MetadataExt;
    let Some(mut file) = snapshot_open(parent, name, false)? else {
        return Ok(None);
    };
    let before = file.metadata().map_err(|_| recovery())?;
    if !before.is_file()
        || before.nlink() != 1
        || before.uid() != unsafe { libc::geteuid() }
        || before.len() > MAX_STORAGE_JSON_BYTES as u64
    {
        return Err(recovery());
    }
    let mut bytes = Vec::new();
    (&mut file)
        .take(MAX_STORAGE_JSON_BYTES as u64 + 1)
        .read_to_end(&mut bytes)
        .map_err(|_| recovery())?;
    if bytes.len() > MAX_STORAGE_JSON_BYTES {
        return Err(recovery());
    }
    let after = file.metadata().map_err(|_| recovery())?;
    let current = snapshot_open(parent, name, false)?.ok_or_else(recovery)?;
    let current = current.metadata().map_err(|_| recovery())?;
    let unchanged = |value: &fs::Metadata| crate::inspection::same_file(&before, value);
    if !unchanged(&after) || !unchanged(&current) {
        return Err(recovery());
    }
    let value: crate::transport::UniqueJson =
        serde_json::from_slice(&bytes).map_err(|_| recovery())?;
    serde_json::from_value(value.0)
        .map(Some)
        .map_err(|_| recovery())
}

#[cfg(target_os = "macos")]
fn snapshot_journals(directory: &File) -> Result<Vec<std::ffi::CString>, WireError> {
    use std::os::fd::IntoRawFd;
    struct Listing(*mut libc::DIR);
    impl Drop for Listing {
        fn drop(&mut self) {
            // SAFETY: this scope exclusively owns the successful fdopendir result.
            unsafe {
                libc::closedir(self.0);
            }
        }
    }
    let fd = directory.try_clone().map_err(|_| recovery())?.into_raw_fd();
    // SAFETY: the duplicate is transferred to DIR only on successful fdopendir.
    let listing = unsafe { libc::fdopendir(fd) };
    if listing.is_null() {
        unsafe {
            libc::close(fd);
        }
        return Err(recovery());
    }
    let listing = Listing(listing);
    let mut names = Vec::new();
    loop {
        let mut entry = std::mem::MaybeUninit::<libc::dirent>::uninit();
        let mut result = std::ptr::null_mut();
        // SAFETY: native dirent storage and result pointer belong to this exclusive iterator.
        let status = unsafe { libc::readdir_r(listing.0, entry.as_mut_ptr(), &mut result) };
        if status != 0 {
            return Err(recovery());
        }
        if result.is_null() {
            return Ok(names);
        }
        // SAFETY: successful enumeration returned a NUL-terminated name in entry.
        let name = unsafe { std::ffi::CStr::from_ptr((*result).d_name.as_ptr()) };
        if name == c"." || name == c".." {
            continue;
        }
        if names.len() == 256 {
            return Err(recovery());
        }
        names.push(name.to_owned());
    }
}

impl StagingStore {
    pub fn read_snapshot(root: &Path) -> Result<Vec<InstallationRecord>, WireError> {
        #[cfg(target_os = "macos")]
        {
            Self::read_snapshot_macos(root)
        }
        #[cfg(not(target_os = "macos"))]
        {
            let _ = root;
            Err(error(
                ErrorCode::UnsupportedConfiguration,
                "Read-only registry snapshots require native macOS filesystem support.",
            ))
        }
    }

    #[cfg(target_os = "macos")]
    fn read_snapshot_macos(root: &Path) -> Result<Vec<InstallationRecord>, WireError> {
        use std::ffi::CString;
        use std::os::unix::fs::MetadataExt;
        let directory = snapshot_directory(root)?;
        let initial = directory.metadata().map_err(|_| recovery())?;
        let deadline = std::time::Instant::now() + SNAPSHOT_TIMEOUT;
        let lock = snapshot_open(&directory, c"staging.lock", false)?;
        let Some(lock) = lock else {
            for name in [
                c"registry.json",
                c"staging",
                c"versions",
                c"journals",
                c"saves",
            ] {
                if snapshot_open(&directory, name, false)?.is_some() {
                    return Err(recovery());
                }
            }
            let current = snapshot_directory(root)?
                .metadata()
                .map_err(|_| recovery())?;
            if !crate::inspection::same_file(&initial, &current) {
                return Err(WireError::new(
                    ErrorCode::StateLocked,
                    "Local staging scope changed during observation. Retry its snapshot later.",
                    true,
                ));
            }
            return Ok(Vec::new());
        };
        let lock_metadata = lock.metadata().map_err(|_| recovery())?;
        if !lock_metadata.is_file()
            || lock_metadata.nlink() != 1
            || lock_metadata.uid() != unsafe { libc::geteuid() }
        {
            return Err(recovery());
        }
        FileExt::try_lock_shared(&lock).map_err(|failure| {
            if failure.kind() != std::io::ErrorKind::WouldBlock {
                return recovery();
            }
            WireError::new(
                ErrorCode::StateLocked,
                "Local staging scope is being modified. Retry its snapshot later.",
                true,
            )
        })?;
        let registry: Registry =
            snapshot_json(&directory, c"registry.json")?.ok_or_else(recovery)?;
        validate_registry(&registry).map_err(|_| recovery())?;
        let journals = snapshot_open(&directory, c"journals", true)?.ok_or_else(recovery)?;
        let versions = snapshot_open(&directory, c"versions", true)?.ok_or_else(recovery)?;
        let initial_journals = journals.metadata().map_err(|_| recovery())?;
        let initial_versions = versions.metadata().map_err(|_| recovery())?;
        for name in [c"staging", c"saves"] {
            snapshot_open(&directory, name, true)?.ok_or_else(recovery)?;
        }
        let journal_names = snapshot_journals(&journals)?;
        let mut pending = BTreeSet::new();
        let staging = snapshot_open(&directory, c"staging", true)?.ok_or_else(recovery)?;
        for name in journal_names {
            if std::time::Instant::now() >= deadline {
                return Err(snapshot_timeout());
            }
            let id = name
                .to_str()
                .map_err(|_| recovery())?
                .strip_suffix(".json")
                .ok_or_else(recovery)?;
            bounded_id(id).map_err(|_| recovery())?;
            let transaction: Transaction = snapshot_json(&journals, &name)?.ok_or_else(recovery)?;
            if transaction.transaction_id != id || transaction.phase == Phase::Promoting {
                return Err(recovery());
            }
            bounded_id(&transaction.installation_id).map_err(|_| recovery())?;
            validate_manifest(&transaction.manifest).map_err(|_| recovery())?;
            let staged = snapshot_open(&staging, &CString::new(id).map_err(|_| recovery())?, true)?
                .ok_or_else(recovery)?;
            let owner: String = snapshot_json(&staged, c".owner.json")?.ok_or_else(recovery)?;
            if owner != id {
                return Err(recovery());
            }
            pending.insert(transaction.transaction_id);
        }
        let mut result = Vec::new();
        let mut owned_versions = BTreeSet::new();
        for installation in registry.installations.values() {
            if std::time::Instant::now() >= deadline {
                return Err(snapshot_timeout());
            }
            for version in std::iter::once(&installation.active).chain(installation.rollback.iter())
            {
                if pending.contains(&version.transaction_id)
                    || !owned_versions.insert(&version.transaction_id)
                {
                    return Err(recovery());
                }
                let name = CString::new(version.transaction_id.as_str()).map_err(|_| recovery())?;
                let version_directory =
                    snapshot_open(&versions, &name, true)?.ok_or_else(recovery)?;
                let owner: String =
                    snapshot_json(&version_directory, c".owner.json")?.ok_or_else(recovery)?;
                if owner != version.transaction_id {
                    return Err(recovery());
                }
            }
            let manifest = &installation.active.manifest;
            let serialized = serde_json::to_vec(manifest).map_err(|_| recovery())?;
            let managed_root = root
                .join("versions")
                .join(&installation.active.transaction_id);
            let managed_root = managed_root
                .to_str()
                .filter(|path| path.len() <= 1024 && !path.chars().any(char::is_control))
                .ok_or_else(recovery)?;
            result.push(InstallationRecord {
                installation_id: installation.installation_id.clone(),
                revision: installation.revision,
                product_id: manifest.product_id.clone(),
                edition_id: manifest.edition_id.clone(),
                package_id: manifest.package_id.clone(),
                package_version: manifest.package_version.clone(),
                package_digest: digest_hex(&Sha256::digest(serialized)),
                runtime_fingerprint: None,
                managed_root: managed_root.to_owned(),
                save_policy: "preserve".to_owned(),
                health: "notVerified".to_owned(),
            });
        }
        let current = snapshot_directory(root)?
            .metadata()
            .map_err(|_| recovery())?;
        let current_lock = snapshot_open(&directory, c"staging.lock", false)?
            .ok_or_else(recovery)?
            .metadata()
            .map_err(|_| recovery())?;
        let current_journals = snapshot_open(&directory, c"journals", true)?
            .ok_or_else(recovery)?
            .metadata()
            .map_err(|_| recovery())?;
        let current_versions = snapshot_open(&directory, c"versions", true)?
            .ok_or_else(recovery)?
            .metadata()
            .map_err(|_| recovery())?;
        if initial.dev() != current.dev()
            || initial.ino() != current.ino()
            || lock_metadata.dev() != current_lock.dev()
            || lock_metadata.ino() != current_lock.ino()
            || !crate::inspection::same_file(&initial_journals, &current_journals)
            || !crate::inspection::same_file(&initial_versions, &current_versions)
        {
            return Err(recovery());
        }
        Ok(result)
    }

    pub fn open(root: &Path) -> Result<Self, WireError> {
        validate_directory(root)?;
        for name in ["staging", "versions", "journals", "saves"] {
            validate_directory(&root.join(name))?;
        }
        let lock_path = root.join("staging.lock");
        regular_or_missing(&lock_path)?;
        let lock = OpenOptions::new()
            .read(true)
            .write(true)
            .create(true)
            .truncate(false)
            .open(lock_path)
            .map_err(|_| recovery())?;
        lock.try_lock_exclusive().map_err(|_| {
            error(
                ErrorCode::StateLocked,
                "Local staging scope is already open.",
            )
        })?;
        let registry_path = root.join("registry.json");
        let registry = match fs::symlink_metadata(&registry_path) {
            Ok(_) => {
                let registry: Registry = read_json(&registry_path)?;
                validate_registry(&registry)?;
                registry
            }
            Err(error) if error.kind() == std::io::ErrorKind::NotFound => Registry {
                version: Protocol::default(),
                installations: BTreeMap::new(),
            },
            Err(_) => return Err(recovery()),
        };
        atomic_registry(&registry_path, &registry)?;
        Ok(Self {
            root: root.to_owned(),
            _lock: lock,
            registry,
        })
    }

    pub fn snapshot(&self) -> Vec<LocalInstallation> {
        self.registry.installations.values().cloned().collect()
    }

    pub fn prepare(
        &mut self,
        manifest: Manifest,
        installation_id: Option<String>,
        expected_revision: u64,
        reserve_bytes: u64,
    ) -> Result<Transaction, WireError> {
        let bytes = validate_manifest(&manifest)?;
        let required = bytes
            .checked_add(reserve_bytes)
            .ok_or_else(|| error(ErrorCode::InvalidRequest, "Storage requirement overflow."))?;
        let available = fs2::available_space(&self.root).map_err(|_| recovery())?;
        if available < required {
            return Err(error(
                ErrorCode::InsufficientSpace,
                "Insufficient space for the staged file set.",
            ));
        }
        let installation_id = installation_id.unwrap_or_else(|| Uuid::new_v4().to_string());
        bounded_id(&installation_id)?;
        let actual = self
            .registry
            .installations
            .get(&installation_id)
            .map_or(0, |entry| entry.revision);
        if actual != expected_revision {
            return Err(error(
                ErrorCode::RevisionConflict,
                "Active local installation changed.",
            ));
        }
        if self
            .registry
            .installations
            .get(&installation_id)
            .is_some_and(|entry| {
                entry.active.manifest.product_id != manifest.product_id
                    || entry.active.manifest.edition_id != manifest.edition_id
            })
        {
            return Err(error(
                ErrorCode::InvalidRequest,
                "Update cannot change product or edition identity.",
            ));
        }
        let transaction = Transaction {
            transaction_id: Uuid::new_v4().to_string(),
            installation_id,
            expected_revision,
            phase: Phase::Prepared,
            manifest,
        };
        self.prospective_registry(&transaction)?;
        atomic_json(
            &self.journal_path(&transaction.transaction_id),
            &transaction,
        )?;
        let directory = self.root.join("staging").join(&transaction.transaction_id);
        validate_directory(&directory)?;
        atomic_json(&directory.join(".owner.json"), &transaction.transaction_id)?;
        Ok(transaction)
    }

    fn prospective_registry(
        &self,
        transaction: &Transaction,
    ) -> Result<(LocalInstallation, Registry), WireError> {
        let current = self
            .registry
            .installations
            .get(&transaction.installation_id);
        let installation = LocalInstallation {
            installation_id: transaction.installation_id.clone(),
            revision: transaction
                .expected_revision
                .checked_add(1)
                .ok_or_else(recovery)?,
            active: Version {
                transaction_id: transaction.transaction_id.clone(),
                manifest: transaction.manifest.clone(),
            },
            rollback: current.map(|entry| entry.active.clone()),
        };
        let mut registry = self.registry.clone();
        registry
            .installations
            .insert(installation.installation_id.clone(), installation.clone());
        registry_bytes(&registry)?;
        Ok((installation, registry))
    }

    fn journal_path(&self, id: &str) -> PathBuf {
        self.root.join("journals").join(format!("{id}.json"))
    }

    fn transaction(&self, id: &str) -> Result<Transaction, WireError> {
        bounded_id(id)?;
        let transaction: Transaction = read_json(&self.journal_path(id))?;
        if transaction.transaction_id != id {
            return Err(recovery());
        }
        bounded_id(&transaction.installation_id)?;
        validate_manifest(&transaction.manifest)?;
        Ok(transaction)
    }

    fn owned_directory(&self, group: &str, id: &str) -> Result<PathBuf, WireError> {
        bounded_id(id)?;
        let directory = self.root.join(group).join(id);
        require_directory(&self.root.join(group))?;
        require_directory(&directory)?;
        let owner: String = read_json(&directory.join(".owner.json"))?;
        if owner != id {
            return Err(recovery());
        }
        Ok(directory)
    }

    pub fn write_file(
        &mut self,
        id: &str,
        relative: &str,
        mut source: impl Read,
        cancelled: impl Fn() -> bool,
    ) -> Result<(), WireError> {
        let transaction = self.transaction(id)?;
        if transaction.phase != Phase::Prepared {
            return Err(error(
                ErrorCode::InvalidTransition,
                "Transaction is no longer writable.",
            ));
        }
        let expected = transaction
            .manifest
            .files
            .iter()
            .find(|file| file.path == relative)
            .ok_or_else(|| error(ErrorCode::InvalidRequest, "File is not manifest-owned."))?;
        let directory = self.owned_directory("staging", id)?;
        let target = directory.join(relative);
        let parent = target.parent().ok_or_else(recovery)?;
        let mut current = directory.clone();
        for component in relative.split('/').take(relative.split('/').count() - 1) {
            current.push(component);
            if !current.exists() {
                validate_directory(&current)?;
            } else {
                require_directory(&current)?;
            }
        }
        if fs::symlink_metadata(&target).is_ok() {
            return Err(error(
                ErrorCode::InvalidTransition,
                "A staged file already exists; re-plan instead of overwriting it.",
            ));
        }
        let available = fs2::available_space(parent).map_err(|_| recovery())?;
        if available < expected.bytes {
            return Err(error(
                ErrorCode::InsufficientSpace,
                "Space was lost before staging allocation.",
            ));
        }
        let mut temporary = tempfile::NamedTempFile::new_in(parent).map_err(|_| recovery())?;
        let mut digest = Sha256::new();
        let mut written = 0u64;
        let mut buffer = [0u8; 65536];
        loop {
            if cancelled() {
                return Err(error(
                    ErrorCode::Cancelled,
                    "Staging cancelled; no active version was replaced.",
                ));
            }
            let count = source
                .read(&mut buffer)
                .map_err(|_| error(ErrorCode::IntegrityFailed, "Staging source failed."))?;
            if count == 0 {
                break;
            }
            written = written.checked_add(count as u64).ok_or_else(recovery)?;
            if written > expected.bytes {
                return Err(error(
                    ErrorCode::IntegrityFailed,
                    "Source exceeds approved file size.",
                ));
            }
            temporary.write_all(&buffer[..count]).map_err(|_| {
                error(
                    ErrorCode::InsufficientSpace,
                    "Staging write failed; preserve prior active version.",
                )
            })?;
            digest.update(&buffer[..count]);
        }
        let actual = digest_hex(&digest.finalize());
        if written != expected.bytes || actual != expected.sha256 {
            return Err(error(
                ErrorCode::IntegrityFailed,
                "Staged file failed size/SHA256 verification.",
            ));
        }
        temporary.as_file().sync_all().map_err(|_| recovery())?;
        temporary
            .persist_noclobber(&target)
            .map_err(|_| recovery())?;
        sync_directory(parent)
    }

    pub fn verify(&mut self, id: &str) -> Result<(), WireError> {
        let mut transaction = self.transaction(id)?;
        if transaction.phase != Phase::Prepared {
            return Err(error(
                ErrorCode::InvalidTransition,
                "Transaction cannot be verified in this phase.",
            ));
        }
        let directory = self.owned_directory("staging", id)?;
        verify_tree(&directory, &transaction.manifest)?;
        transaction.phase = Phase::Verified;
        atomic_json(&self.journal_path(id), &transaction)
    }

    pub fn commit(&mut self, id: &str) -> Result<LocalInstallation, WireError> {
        let mut transaction = self.transaction(id)?;
        if transaction.phase != Phase::Verified {
            return Err(error(
                ErrorCode::InvalidTransition,
                "Only a fully verified staged version can commit.",
            ));
        }
        let current = self
            .registry
            .installations
            .get(&transaction.installation_id);
        if current.map_or(0, |entry| entry.revision) != transaction.expected_revision {
            return Err(error(
                ErrorCode::RevisionConflict,
                "Active version changed before commit.",
            ));
        }
        let (installation, registry) = self.prospective_registry(&transaction)?;
        let directory = self.owned_directory("staging", id)?;
        verify_tree(&directory, &transaction.manifest)?;
        let destination = self.root.join("versions").join(id);
        if fs::symlink_metadata(&destination).is_ok() {
            return Err(recovery());
        }
        transaction.phase = Phase::Promoting;
        atomic_json(&self.journal_path(id), &transaction)?;
        fs::rename(&directory, &destination).map_err(|_| recovery())?;
        sync_directory(&self.root.join("versions"))?;
        sync_directory(&self.root.join("staging"))?;
        atomic_registry(&self.root.join("registry.json"), &registry)?;
        self.registry = registry;
        fs::remove_file(self.journal_path(id)).map_err(|_| recovery())?;
        sync_directory(&self.root.join("journals"))?;
        Ok(installation)
    }

    pub fn recoveries(&self) -> Result<Vec<Transaction>, WireError> {
        let mut result = Vec::new();
        for entry in fs::read_dir(self.root.join("journals")).map_err(|_| recovery())? {
            if result.len() >= 256 {
                return Err(recovery());
            }
            let path = entry.map_err(|_| recovery())?.path();
            let transaction: Transaction = read_json(&path)?;
            if self.journal_path(&transaction.transaction_id) != path {
                return Err(recovery());
            }
            bounded_id(&transaction.transaction_id)?;
            bounded_id(&transaction.installation_id)?;
            validate_manifest(&transaction.manifest)?;
            result.push(transaction);
        }
        result.sort_by(|left, right| left.transaction_id.cmp(&right.transaction_id));
        Ok(result)
    }

    pub fn reconcile_committed(&mut self, id: &str) -> Result<(), WireError> {
        let transaction = self.transaction(id)?;
        let installation = self
            .registry
            .installations
            .get(&transaction.installation_id)
            .ok_or_else(recovery)?;
        if transaction.phase != Phase::Promoting || installation.active.transaction_id != id {
            return Err(recovery());
        }
        let directory = self.owned_directory("versions", id)?;
        verify_tree(&directory, &installation.active.manifest)?;
        fs::remove_file(self.journal_path(id)).map_err(|_| recovery())?;
        sync_directory(&self.root.join("journals"))
    }

    pub fn rollback(&mut self, id: &str, revision: u64) -> Result<LocalInstallation, WireError> {
        bounded_id(id)?;
        let current = self
            .registry
            .installations
            .get(id)
            .ok_or_else(|| error(ErrorCode::NotFound, "No local installation."))?;
        if current.revision != revision {
            return Err(error(
                ErrorCode::RevisionConflict,
                "Local installation changed.",
            ));
        }
        let previous = current.rollback.clone().ok_or_else(|| {
            error(
                ErrorCode::InvalidTransition,
                "No retained version is available.",
            )
        })?;
        let directory = self.owned_directory("versions", &previous.transaction_id)?;
        verify_tree(&directory, &previous.manifest)?;
        let installation = LocalInstallation {
            installation_id: id.to_owned(),
            revision: revision.checked_add(1).ok_or_else(recovery)?,
            active: previous,
            rollback: Some(current.active.clone()),
        };
        let mut registry = self.registry.clone();
        registry
            .installations
            .insert(id.to_owned(), installation.clone());
        atomic_registry(&self.root.join("registry.json"), &registry)?;
        self.registry = registry;
        Ok(installation)
    }

    pub fn discard_uncommitted(&mut self, id: &str) -> Result<(), WireError> {
        let transaction = self.transaction(id)?;
        if self.registry.installations.values().any(|entry| {
            entry.active.transaction_id == id
                || entry
                    .rollback
                    .as_ref()
                    .is_some_and(|version| version.transaction_id == id)
        }) {
            return Err(error(
                ErrorCode::InvalidTransition,
                "Committed or retained versions cannot be discarded as staging.",
            ));
        }
        let group = if self.root.join("staging").join(id).exists() {
            "staging"
        } else {
            "versions"
        };
        let directory = self.owned_directory(group, id)?;
        let actual = tree_files(&directory)?;
        let allowed: BTreeSet<_> = transaction
            .manifest
            .files
            .iter()
            .map(|file| file.path.clone())
            .chain(std::iter::once(".owner.json".to_owned()))
            .collect();
        if actual.iter().any(|path| !allowed.contains(path)) {
            return Err(recovery());
        }
        // The exact owned transaction root, not the registry, saves or a caller-supplied path.
        fs::remove_dir_all(&directory).map_err(|_| recovery())?;
        fs::remove_file(self.journal_path(id)).map_err(|_| recovery())?;
        sync_directory(&self.root.join(group))?;
        sync_directory(&self.root.join("journals"))
    }
}

fn tree_files(directory: &Path) -> Result<BTreeSet<String>, WireError> {
    let mut pending = vec![directory.to_owned()];
    let mut files = BTreeSet::new();
    let mut count = 0;
    while let Some(folder) = pending.pop() {
        require_directory(&folder)?;
        for entry in fs::read_dir(folder).map_err(|_| recovery())? {
            count += 1;
            if count > 4096 {
                return Err(recovery());
            }
            let entry = entry.map_err(|_| recovery())?;
            let metadata = fs::symlink_metadata(entry.path()).map_err(|_| recovery())?;
            if metadata.file_type().is_symlink() {
                return Err(recovery());
            }
            if metadata.is_dir() {
                pending.push(entry.path());
            } else if metadata.is_file() {
                let path = entry
                    .path()
                    .strip_prefix(directory)
                    .map_err(|_| recovery())?
                    .to_str()
                    .ok_or_else(recovery)?
                    .replace('\\', "/");
                files.insert(path);
            } else {
                return Err(recovery());
            }
        }
    }
    Ok(files)
}

fn verify_tree(directory: &Path, manifest: &Manifest) -> Result<(), WireError> {
    let expected: BTreeSet<_> = manifest
        .files
        .iter()
        .map(|file| file.path.clone())
        .chain(std::iter::once(".owner.json".to_owned()))
        .collect();
    if tree_files(directory)? != expected {
        return Err(error(
            ErrorCode::IntegrityFailed,
            "Staging contains missing or unexpected files.",
        ));
    }
    for expected in &manifest.files {
        let path = directory.join(&expected.path);
        require_regular(&path)?;
        let mut file = File::open(path).map_err(|_| recovery())?;
        if file.metadata().map_err(|_| recovery())?.len() != expected.bytes {
            return Err(error(
                ErrorCode::IntegrityFailed,
                "Staged length changed before commit.",
            ));
        }
        let mut digest = Sha256::new();
        let mut buffer = [0u8; 65536];
        let mut read = 0u64;
        loop {
            let count = file.read(&mut buffer).map_err(|_| recovery())?;
            if count == 0 {
                break;
            }
            read = read.checked_add(count as u64).ok_or_else(recovery)?;
            if read > expected.bytes {
                return Err(error(
                    ErrorCode::IntegrityFailed,
                    "Staged file grew during verification.",
                ));
            }
            digest.update(&buffer[..count]);
        }
        if read != expected.bytes || digest_hex(&digest.finalize()) != expected.sha256 {
            return Err(error(
                ErrorCode::IntegrityFailed,
                "Staged SHA256 changed before commit.",
            ));
        }
    }
    Ok(())
}
