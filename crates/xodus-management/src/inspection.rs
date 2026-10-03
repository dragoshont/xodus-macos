use std::path::Path;

use sha2::{Digest, Sha256};
use uuid::Uuid;

use crate::wire::{ErrorCode, WireError};

pub const MARKER: &str = ".xodus-streaming.msixvc";
pub const TIMEOUT: std::time::Duration = std::time::Duration::from_secs(5);
const HEADER_BYTES: u64 = 4096;
const BASE_OFFSET: u64 = 0x200;
const IDENTITY_OFFSET: u64 = 0x39c;
const BASE_BYTES: usize = 156;
const IDENTITY_BYTES: usize = 40;

#[derive(Debug)]
pub struct MarkerMetadata {
    pub bytes: u64,
    pub observed_metadata_sha256: String,
    pub format_version: u32,
    pub xvd_type: u32,
    pub content_type_raw: u32,
    pub volume_flags_raw: u32,
    pub content_id: String,
    pub header_product_guid: String,
    pub header_pduid: String,
    pub observed_package_version: String,
}

fn malformed() -> WireError {
    WireError::new(
        ErrorCode::IntegrityFailed,
        "The selected marker has incomplete or unsupported structural XVD metadata. No files were modified.",
        false,
    )
}

fn unsupported() -> WireError {
    WireError::new(
        ErrorCode::UnsupportedConfiguration,
        "Read-only inspection requires an accessible local folder and a regular, unlinked marker; symlinks and special files are refused.",
        false,
    )
}

fn changed() -> WireError {
    WireError::new(
        ErrorCode::RevisionConflict,
        "The selected folder or marker changed during inspection. Retry without modifying files.",
        true,
    )
}

pub fn directory_valid(directory: &str) -> bool {
    directory.len() <= 1024
        && directory.starts_with('/')
        && directory != "/"
        && !directory.chars().any(char::is_control)
        && directory
            .split('/')
            .skip(1)
            .all(|component| !component.is_empty() && component != "." && component != "..")
        && directory.split('/').count() <= 65
}

fn parse_metadata(
    base: &[u8; BASE_BYTES],
    identity: &[u8; IDENTITY_BYTES],
    bytes: u64,
) -> Result<MarkerMetadata, WireError> {
    let u32_at = |offset: usize| {
        u32::from_le_bytes([
            base[offset],
            base[offset + 1],
            base[offset + 2],
            base[offset + 3],
        ])
    };
    let xvd_type = u32_at(128);
    let content_type_raw = u32_at(132);
    if &base[..8] != b"msft-xvd"
        || !(HEADER_BYTES..=9_007_199_254_740_991).contains(&bytes)
        || xvd_type > 1
        || !(content_type_raw <= 0x1e || (0x20..=0x25).contains(&content_type_raw))
    {
        return Err(malformed());
    }
    let guid = |slice: &[u8]| -> Result<Uuid, WireError> {
        let array: [u8; 16] = slice.try_into().map_err(|_| malformed())?;
        Ok(Uuid::from_bytes_le(array))
    };
    let content_id = guid(&base[32..48])?;
    if content_id.is_nil() {
        return Err(malformed());
    }
    let component = |offset: usize| u16::from_le_bytes([identity[offset], identity[offset + 1]]);
    let mut digest = Sha256::new();
    digest.update(base);
    digest.update(identity);
    Ok(MarkerMetadata {
        bytes,
        observed_metadata_sha256: crate::staging::digest_hex(&digest.finalize()),
        format_version: u32_at(12),
        xvd_type,
        content_type_raw,
        volume_flags_raw: u32_at(8),
        content_id: content_id.to_string(),
        header_product_guid: guid(&identity[..16])?.to_string(),
        header_pduid: guid(&identity[16..32])?.to_string(),
        observed_package_version: format!(
            "{}.{}.{}.{}",
            component(38),
            component(36),
            component(34),
            component(32)
        ),
    })
}

pub fn inspect(directory: &str) -> Result<MarkerMetadata, WireError> {
    if !directory_valid(directory) {
        return Err(crate::transport::invalid());
    }
    #[cfg(unix)]
    {
        inspect_checked(Path::new(directory), || {})
    }
    #[cfg(not(unix))]
    {
        Err(unsupported())
    }
}

pub async fn inspect_async(
    directory: String,
    permits: std::sync::Arc<tokio::sync::Semaphore>,
) -> Result<crate::wire::InspectionData, WireError> {
    let selected = directory.clone();
    let marker = bounded_read(permits, TIMEOUT, move || inspect(&selected)).await?;
    Ok(crate::wire::InspectionData {
        scope: "userSelectedDirectory".to_owned(), completeness: crate::wire::Completeness::Partial,
        freshness: crate::wire::Freshness::Live, checked_at: crate::state::now(), directory,
        marker: crate::wire::InspectionMarker {
            relative_path: MARKER.to_owned(), bytes: marker.bytes,
            observed_metadata_sha256: marker.observed_metadata_sha256,
            format: "msft-xvd".to_owned(), format_version: marker.format_version,
            xvd_type: marker.xvd_type, content_type_raw: marker.content_type_raw,
            volume_flags_raw: marker.volume_flags_raw, content_id: marker.content_id,
            header_product_guid: marker.header_product_guid, header_pduid: marker.header_pduid,
            observed_package_version: marker.observed_package_version,
        },
        assessment: crate::wire::InspectionAssessment {
            kind: "externalMarkerDetected".to_owned(), registered: false,
            retail_identity: "unknown".to_owned(), file_verification: "notPerformed".to_owned(),
            entitlement: "unknown".to_owned(), compatibility: "unknown".to_owned(), launchable: false,
            reason: "Marker metadata is not verified files, retail identity, authorization or a certified runtime.".to_owned(),
        },
    })
}

async fn bounded_read<T: Send + 'static>(
    permits: std::sync::Arc<tokio::sync::Semaphore>,
    timeout: std::time::Duration,
    read: impl FnOnce() -> Result<T, WireError> + Send + 'static,
) -> Result<T, WireError> {
    let unavailable = || {
        WireError::new(
            ErrorCode::UnsupportedConfiguration,
            "Read-only folder inspection did not finish its local metadata read within the bounded deadline. No files were modified.",
            true,
        )
    };
    let deadline = tokio::time::Instant::now() + timeout;
    let permit = tokio::time::timeout_at(deadline, permits.acquire_owned())
        .await
        .map_err(|_| unavailable())?
        .map_err(|_| unsupported())?;
    let work = tokio::task::spawn_blocking(move || {
        let _permit = permit;
        read()
    });
    tokio::time::timeout_at(deadline, work)
        .await
        .map_err(|_| unavailable())?
        .map_err(|_| {
            WireError::new(
                ErrorCode::InternalError,
                "Read-only inspection worker failed unexpectedly. No files were modified.",
                false,
            )
        })?
}

#[cfg(unix)]
fn open_at(
    parent: Option<&std::fs::File>,
    name: &std::ffi::CStr,
    directory: bool,
) -> Result<std::fs::File, WireError> {
    use std::os::fd::{AsRawFd, FromRawFd};
    let mut flags = libc::O_RDONLY | libc::O_NOFOLLOW | libc::O_CLOEXEC | libc::O_NONBLOCK;
    if directory {
        flags |= libc::O_DIRECTORY;
    }
    // SAFETY: names are NUL-terminated; descriptors are borrowed and no creating flags are used.
    let fd = unsafe {
        match parent {
            Some(parent) => libc::openat(parent.as_raw_fd(), name.as_ptr(), flags),
            None => libc::open(name.as_ptr(), flags),
        }
    };
    if fd < 0 {
        return Err(
            if std::io::Error::last_os_error().kind() == std::io::ErrorKind::NotFound {
                WireError::new(
                    ErrorCode::NotFound,
                    "The selected folder has no readable supported Xodus marker. No installation or ownership was inferred.",
                    false,
                )
            } else {
                unsupported()
            },
        );
    }
    // SAFETY: open/openat returned a new owned descriptor, transferred once to File.
    Ok(unsafe { std::fs::File::from_raw_fd(fd) })
}

#[cfg(unix)]
fn open_directory(path: &Path) -> Result<std::fs::File, WireError> {
    use std::ffi::CString;
    use std::os::unix::ffi::OsStrExt;
    let mut directory = open_at(None, c"/", true)?;
    for component in path.components().skip(1) {
        let std::path::Component::Normal(name) = component else {
            return Err(unsupported());
        };
        directory = open_at(
            Some(&directory),
            &CString::new(name.as_bytes()).map_err(|_| unsupported())?,
            true,
        )?;
        require_local(&directory)?;
    }
    Ok(directory)
}

#[cfg(all(unix, target_os = "macos"))]
fn require_local(directory: &std::fs::File) -> Result<(), WireError> {
    use std::os::fd::AsRawFd;
    let mut info = std::mem::MaybeUninit::<libc::statfs>::uninit();
    // SAFETY: a valid borrowed directory descriptor and one writable statfs are supplied.
    if unsafe { libc::fstatfs(directory.as_raw_fd(), info.as_mut_ptr()) } != 0 {
        return Err(unsupported());
    }
    // SAFETY: successful fstatfs initialized the complete result.
    if unsafe { info.assume_init() }.f_flags & libc::MNT_LOCAL as u32 == 0 {
        return Err(unsupported());
    }
    Ok(())
}

#[cfg(all(unix, not(target_os = "macos")))]
fn require_local(_: &std::fs::File) -> Result<(), WireError> {
    Ok(())
}

#[cfg(unix)]
fn regular(metadata: &std::fs::Metadata) -> Result<(), WireError> {
    use std::os::unix::fs::MetadataExt;
    if !metadata.is_file() || metadata.nlink() != 1 {
        return Err(unsupported());
    }
    Ok(())
}

#[cfg(unix)]
fn same_file(before: &std::fs::Metadata, after: &std::fs::Metadata) -> bool {
    use std::os::unix::fs::MetadataExt;
    before.dev() == after.dev()
        && before.ino() == after.ino()
        && before.len() == after.len()
        && before.mtime() == after.mtime()
        && before.mtime_nsec() == after.mtime_nsec()
        && before.ctime() == after.ctime()
        && before.ctime_nsec() == after.ctime_nsec()
        && before.nlink() == after.nlink()
}

#[cfg(unix)]
fn inspect_checked(path: &Path, after_read: impl FnOnce()) -> Result<MarkerMetadata, WireError> {
    use std::os::unix::fs::{FileExt, MetadataExt};
    let directory = open_directory(path)?;
    let directory_before = directory.metadata().map_err(|_| unsupported())?;
    let file = open_at(Some(&directory), c".xodus-streaming.msixvc", false)?;
    let before = file.metadata().map_err(|_| unsupported())?;
    regular(&before)?;
    let mut base = [0; BASE_BYTES];
    let mut identity = [0; IDENTITY_BYTES];
    file.read_exact_at(&mut base, BASE_OFFSET)
        .map_err(|_| malformed())?;
    file.read_exact_at(&mut identity, IDENTITY_OFFSET)
        .map_err(|_| malformed())?;
    let result = parse_metadata(&base, &identity, before.len())?;
    after_read();
    let after = file.metadata().map_err(|_| changed())?;
    regular(&after)?;
    if !same_file(&before, &after) {
        return Err(changed());
    }
    let current_directory = open_directory(path).map_err(|_| changed())?;
    let current = current_directory.metadata().map_err(|_| changed())?;
    if directory_before.dev() != current.dev() || directory_before.ino() != current.ino() {
        return Err(changed());
    }
    let current_file = open_at(Some(&current_directory), c".xodus-streaming.msixvc", false)
        .map_err(|_| changed())?;
    if !same_file(&before, &current_file.metadata().map_err(|_| changed())?) {
        return Err(changed());
    }
    Ok(result)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn header() -> ([u8; BASE_BYTES], [u8; IDENTITY_BYTES]) {
        let mut base = [0; BASE_BYTES];
        base[..8].copy_from_slice(b"msft-xvd");
        base[12..16].copy_from_slice(&2u32.to_le_bytes());
        base[32..48].copy_from_slice(&Uuid::from_u128(1).to_bytes_le());
        base[132..136].copy_from_slice(&1u32.to_le_bytes());
        let mut identity = [0; IDENTITY_BYTES];
        identity[..16].copy_from_slice(&Uuid::from_u128(2).to_bytes_le());
        identity[16..32].copy_from_slice(&Uuid::from_u128(3).to_bytes_le());
        for (offset, value) in [(32, 4u16), (34, 3), (36, 2), (38, 1)] {
            identity[offset..offset + 2].copy_from_slice(&value.to_le_bytes());
        }
        (base, identity)
    }

    #[test]
    fn metadata_is_fixed_bounded_and_does_not_treat_header_guids_as_retail_ids() {
        let (base, identity) = header();
        let parsed = parse_metadata(&base, &identity, HEADER_BYTES).unwrap();
        assert_eq!(parsed.content_id, Uuid::from_u128(1).to_string());
        assert_eq!(parsed.header_product_guid, Uuid::from_u128(2).to_string());
        assert_eq!(parsed.observed_package_version, "1.2.3.4");
        assert_eq!(parsed.observed_metadata_sha256.len(), 64);
        assert_eq!(BASE_BYTES + IDENTITY_BYTES, 196);
        let mut malformed_base = base;
        malformed_base[128..132].copy_from_slice(&256u32.to_le_bytes());
        assert!(parse_metadata(&malformed_base, &identity, HEADER_BYTES).is_err());
        assert!(parse_metadata(&base, &identity, HEADER_BYTES - 1).is_err());
        assert!(parse_metadata(&base, &identity, 9_007_199_254_740_992).is_err());
        let mut malformed_base = base;
        malformed_base[132..136].copy_from_slice(&0x10001u32.to_le_bytes());
        assert!(parse_metadata(&malformed_base, &identity, HEADER_BYTES).is_err());
        let mut extreme_time = base;
        extreme_time[16..24].fill(255);
        parse_metadata(&extreme_time, &identity, HEADER_BYTES).unwrap();
    }

    #[test]
    fn directory_syntax_rejects_global_root_aliases_controls_and_traversal() {
        for directory in [
            "/", "relative", "/a/../b", "/a/./b", "/a//b", "/a/", "/a\nb",
        ] {
            assert!(!directory_valid(directory));
        }
        assert!(directory_valid("/Users/fixture/selected folder"));
    }

    #[cfg(unix)]
    fn selected() -> (tempfile::TempDir, std::path::PathBuf) {
        let temporary = tempfile::tempdir().unwrap();
        let directory = temporary.path().canonicalize().unwrap();
        let (base, identity) = header();
        let mut bytes = vec![0; HEADER_BYTES as usize];
        bytes[BASE_OFFSET as usize..BASE_OFFSET as usize + BASE_BYTES].copy_from_slice(&base);
        bytes[IDENTITY_OFFSET as usize..IDENTITY_OFFSET as usize + IDENTITY_BYTES]
            .copy_from_slice(&identity);
        std::fs::write(directory.join(MARKER), bytes).unwrap();
        (temporary, directory)
    }

    #[cfg(unix)]
    #[test]
    fn selected_folder_read_is_actual_regular_io_and_excludes_signature_and_key_material() {
        let (_temporary, directory) = selected();
        let before = std::fs::read(directory.join(MARKER)).unwrap();
        let first = inspect(directory.to_str().unwrap()).unwrap();
        assert_eq!(std::fs::read(directory.join(MARKER)).unwrap(), before);
        let mut updated = before;
        updated[..0x200].fill(0x55);
        updated[0x34c..0x36c].fill(0x66);
        std::fs::write(directory.join(MARKER), &updated).unwrap();
        let second = inspect(directory.to_str().unwrap()).unwrap();
        assert_eq!(
            first.observed_metadata_sha256,
            second.observed_metadata_sha256
        );
        assert_eq!(std::fs::read(directory.join(MARKER)).unwrap(), updated);
    }

    #[cfg(unix)]
    #[test]
    fn symlinks_hardlinks_special_files_and_changed_identity_fail_closed() {
        use std::os::unix::fs::symlink;
        let (_temporary, directory) = selected();
        std::fs::hard_link(directory.join(MARKER), directory.join("hardlink")).unwrap();
        assert_eq!(
            inspect(directory.to_str().unwrap()).unwrap_err().code,
            ErrorCode::UnsupportedConfiguration
        );
        std::fs::remove_file(directory.join("hardlink")).unwrap();
        let alias = directory.join("alias");
        symlink(&directory, &alias).unwrap();
        assert_eq!(
            inspect(alias.to_str().unwrap()).unwrap_err().code,
            ErrorCode::UnsupportedConfiguration
        );
        assert_eq!(
            inspect_checked(&directory, || {
                std::fs::rename(directory.join(MARKER), directory.join("old")).unwrap();
                std::fs::copy(directory.join("old"), directory.join(MARKER)).unwrap();
            })
            .unwrap_err()
            .code,
            ErrorCode::RevisionConflict
        );
        std::fs::remove_file(directory.join(MARKER)).unwrap();
        symlink(directory.join("old"), directory.join(MARKER)).unwrap();
        assert_eq!(
            inspect(directory.to_str().unwrap()).unwrap_err().code,
            ErrorCode::UnsupportedConfiguration
        );
        std::fs::remove_file(directory.join(MARKER)).unwrap();
        std::fs::create_dir(directory.join(MARKER)).unwrap();
        assert_eq!(
            inspect(directory.to_str().unwrap()).unwrap_err().code,
            ErrorCode::UnsupportedConfiguration
        );
    }

    #[tokio::test]
    async fn timed_out_read_keeps_its_permit_until_owned_io_finishes() {
        use std::sync::Arc;
        let permits = Arc::new(tokio::sync::Semaphore::new(1));
        let (release, waiting) = std::sync::mpsc::channel();
        let (started, began) = tokio::sync::oneshot::channel();
        let worker_permits = permits.clone();
        let task = tokio::spawn(bounded_read(
            worker_permits,
            std::time::Duration::from_millis(100),
            move || {
                started.send(()).unwrap();
                waiting.recv().unwrap();
                Ok(())
            },
        ));
        began.await.unwrap();
        assert_eq!(
            task.await.unwrap().unwrap_err().code,
            ErrorCode::UnsupportedConfiguration
        );
        assert_eq!(permits.available_permits(), 0);
        assert_eq!(
            bounded_read(
                permits.clone(),
                std::time::Duration::from_millis(10),
                || Ok(())
            )
            .await
            .unwrap_err()
            .code,
            ErrorCode::UnsupportedConfiguration
        );
        release.send(()).unwrap();
        tokio::time::timeout(std::time::Duration::from_secs(1), async {
            while permits.available_permits() == 0 {
                tokio::task::yield_now().await;
            }
        })
        .await
        .unwrap();
        assert_eq!(bounded_read(permits, TIMEOUT, || Ok(())).await.unwrap(), ());
    }
}
