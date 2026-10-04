use std::ffi::{CStr, CString};
use std::io;
use std::os::fd::{AsRawFd, FromRawFd};
use std::os::unix::fs::MetadataExt;
use std::path::Path;
use std::sync::Arc;

use tokio::net::UnixListener;
use tokio::task::JoinSet;
use tokio_util::sync::CancellationToken;
use xodus::tokens::TokenManager;

pub const MAX_CONNECTIONS: usize = 8;

fn invalid() -> io::Error {
    io::Error::new(
        io::ErrorKind::InvalidInput,
        "An absolute normalized private runtime socket path of at most 103 bytes is required",
    )
}

fn private() -> io::Error {
    io::Error::new(
        io::ErrorKind::PermissionDenied,
        "Runtime socket requires an existing owned local 0700 directory without aliases",
    )
}

fn socket_error() -> io::Error {
    io::Error::new(
        io::ErrorKind::AddrInUse,
        "The private runtime socket could not be created; existing entries are never removed",
    )
}

fn open_directory(path: &Path) -> io::Result<std::fs::File> {
    let mut fd = open_directory_at(libc::AT_FDCWD, c"/")?;
    for component in path.components().skip(1) {
        let std::path::Component::Normal(name) = component else {
            return Err(private());
        };
        use std::os::unix::ffi::OsStrExt;
        fd = open_directory_at(
            fd.as_raw_fd(),
            &CString::new(name.as_bytes()).map_err(|_| private())?,
        )?;
    }
    Ok(fd)
}

fn open_directory_at(parent: i32, name: &CStr) -> io::Result<std::fs::File> {
    // SAFETY: a borrowed descriptor and NUL-terminated name are used without creating flags.
    let fd = unsafe {
        libc::openat(
            parent,
            name.as_ptr(),
            libc::O_RDONLY
                | libc::O_DIRECTORY
                | libc::O_NOFOLLOW
                | libc::O_CLOEXEC
                | libc::O_NONBLOCK,
        )
    };
    if fd < 0 {
        return Err(private());
    }
    // SAFETY: the new descriptor is transferred once to an owned File.
    let file = unsafe { std::fs::File::from_raw_fd(fd) };
    #[cfg(target_os = "macos")]
    {
        let mut info = std::mem::MaybeUninit::<libc::statfs>::uninit();
        // SAFETY: the descriptor is live and successful fstatfs initializes the supplied result.
        if unsafe { libc::fstatfs(fd, info.as_mut_ptr()) } != 0
            || unsafe { info.assume_init() }.f_flags & libc::MNT_LOCAL as u32 == 0
        {
            return Err(private());
        }
    }
    Ok(file)
}

struct Endpoint {
    listener: UnixListener,
    directory: std::fs::File,
    name: CString,
    parent: std::path::PathBuf,
    device: libc::dev_t,
    inode: libc::ino_t,
}

fn entry(directory: &std::fs::File, name: &CStr) -> io::Result<libc::stat> {
    let mut value = std::mem::MaybeUninit::<libc::stat>::uninit();
    // SAFETY: a live borrowed directory descriptor, valid name and writable stat are provided.
    if unsafe {
        libc::fstatat(
            directory.as_raw_fd(),
            name.as_ptr(),
            value.as_mut_ptr(),
            libc::AT_SYMLINK_NOFOLLOW,
        )
    } != 0
    {
        return Err(io::Error::last_os_error());
    }
    // SAFETY: successful fstatat initialized the result.
    Ok(unsafe { value.assume_init() })
}

impl Endpoint {
    fn bind(path: &str) -> io::Result<Self> {
        if path.len() > 103
            || !path.starts_with('/')
            || path.chars().any(char::is_control)
            || path
                .split('/')
                .skip(1)
                .any(|part| part.is_empty() || part == "." || part == "..")
        {
            return Err(invalid());
        }
        let path = Path::new(path);
        let parent = path.parent().ok_or_else(invalid)?;
        let name = CString::new(
            path.file_name()
                .and_then(|name| name.to_str())
                .ok_or_else(invalid)?,
        )
        .map_err(|_| invalid())?;
        let directory = open_directory(parent)?;
        let metadata = directory.metadata().map_err(|_| private())?;
        // SAFETY: geteuid takes no pointers and has no side effects.
        let uid = unsafe { libc::geteuid() };
        if metadata.uid() != uid || metadata.mode() & 0o7777 != 0o700 {
            return Err(private());
        }
        match entry(&directory, &name) {
            Err(error) if error.kind() == io::ErrorKind::NotFound => {}
            _ => return Err(socket_error()),
        }
        let current = open_directory(parent)?.metadata().map_err(|_| private())?;
        if current.dev() != metadata.dev() || current.ino() != metadata.ino() {
            return Err(private());
        }
        let listener = UnixListener::bind(path).map_err(|_| socket_error())?;
        let socket = entry(&directory, &name).map_err(|_| socket_error())?;
        if socket.st_mode & libc::S_IFMT != libc::S_IFSOCK || socket.st_uid != uid {
            return Err(socket_error());
        }
        let endpoint = Self {
            listener,
            directory,
            name,
            parent: parent.to_owned(),
            device: socket.st_dev,
            inode: socket.st_ino,
        };
        // SAFETY: chmod targets one verified socket name relative to its owned directory, without following links.
        if unsafe {
            libc::fchmodat(
                endpoint.directory.as_raw_fd(),
                endpoint.name.as_ptr(),
                0o600,
                libc::AT_SYMLINK_NOFOLLOW,
            )
        } != 0
        {
            return Err(private());
        }
        endpoint.verify()?;
        Ok(endpoint)
    }

    fn verify(&self) -> io::Result<()> {
        let original = self.directory.metadata().map_err(|_| private())?;
        let current = open_directory(&self.parent)?
            .metadata()
            .map_err(|_| private())?;
        // SAFETY: geteuid takes no pointers and has no side effects.
        let uid = unsafe { libc::geteuid() };
        if original.dev() != current.dev()
            || original.ino() != current.ino()
            || current.uid() != uid
            || current.mode() & 0o7777 != 0o700
        {
            return Err(private());
        }
        let socket = entry(&self.directory, &self.name).map_err(|_| socket_error())?;
        if socket.st_dev != self.device
            || socket.st_ino != self.inode
            || socket.st_uid != uid
            || socket.st_mode & libc::S_IFMT != libc::S_IFSOCK
            || socket.st_mode & 0o7777 != 0o600
            || socket.st_nlink != 1
        {
            return Err(socket_error());
        }
        Ok(())
    }
}

impl Drop for Endpoint {
    fn drop(&mut self) {
        match entry(&self.directory, &self.name) {
            Ok(current)
                if current.st_dev == self.device
                    && current.st_ino == self.inode
                    && current.st_mode & libc::S_IFMT == libc::S_IFSOCK =>
            {
                // SAFETY: only this endpoint's unchanged socket name is removed via its retained directory descriptor.
                if unsafe { libc::unlinkat(self.directory.as_raw_fd(), self.name.as_ptr(), 0) } != 0
                {
                    eprintln!("Owned runtime socket cleanup failed");
                }
            }
            Ok(_) => eprintln!("Runtime socket identity changed; replacement was not removed"),
            Err(error) if error.kind() == io::ErrorKind::NotFound => {}
            Err(_) => eprintln!("Owned runtime socket cleanup inspection failed"),
        }
    }
}

pub async fn serve(
    path: &str,
    tokens: Arc<TokenManager>,
    cancellation: CancellationToken,
) -> io::Result<()> {
    if !xodus::secrets::management_native_keychain_enabled() || !tokens.is_management_profile() {
        return Err(io::Error::new(
            io::ErrorKind::Unsupported,
            "Isolated runtime broker requires native macOS management credentials without plaintext fallback",
        ));
    }
    let endpoint = Endpoint::bind(path)?;
    let permits = Arc::new(tokio::sync::Semaphore::new(MAX_CONNECTIONS));
    let mut tasks = JoinSet::new();
    loop {
        tokio::select! {
            biased;
            _ = cancellation.cancelled() => break,
            completed = tasks.join_next(), if !tasks.is_empty() => {
                if !matches!(completed, Some(Ok(Ok(())))) {
                    eprintln!("Private runtime peer request failed");
                }
            }
            accepted = endpoint.listener.accept() => {
                let (socket, _) = accepted.map_err(|_| io::Error::other("Private runtime accept failed"))?;
                endpoint.verify()?;
                // SAFETY: geteuid takes no pointers and has no side effects.
                let uid = unsafe { libc::geteuid() };
                if socket.peer_cred().map(|credential| credential.uid()).ok() != Some(uid) {
                    continue;
                }
                let Ok(permit) = permits.clone().try_acquire_owned() else { continue; };
                let tokens = tokens.clone();
                let token = cancellation.clone();
                tasks.spawn(async move {
                    let _permit = permit;
                    crate::connection::router::route_management(socket, token, tokens).await
                });
            }
        }
    }
    cancellation.cancel();
    while let Some(result) = tasks.join_next().await {
        if !matches!(result, Ok(Ok(()))) {
            eprintln!("Private runtime peer shutdown failed");
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::os::unix::fs::{PermissionsExt, symlink};

    fn directory() -> std::path::PathBuf {
        static NEXT: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);
        let next = NEXT.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
        // SAFETY: geteuid takes no pointers and has no side effects.
        let uid = unsafe { libc::geteuid() };
        let path = std::env::temp_dir()
            .canonicalize()
            .unwrap()
            .join(format!("rps-{uid}-{}-{next}", std::process::id()));
        std::fs::create_dir(&path).unwrap();
        std::fs::set_permissions(&path, std::fs::Permissions::from_mode(0o700)).unwrap();
        path
    }

    #[tokio::test]
    async fn private_endpoint_lifecycle_refuses_aliases_existing_files_and_global_paths() {
        let root = directory();
        let path = root.join("private.sock");
        let endpoint = Endpoint::bind(path.to_str().unwrap()).unwrap();
        assert_eq!(
            std::fs::symlink_metadata(&path).unwrap().mode() & 0o777,
            0o600
        );
        assert!(Endpoint::bind(path.to_str().unwrap()).is_err());
        drop(endpoint);
        assert!(!path.exists());
        std::fs::write(&path, b"unrelated-owner-file").unwrap();
        assert!(Endpoint::bind(path.to_str().unwrap()).is_err());
        assert_eq!(std::fs::read(&path).unwrap(), b"unrelated-owner-file");
        std::fs::remove_file(&path).unwrap();
        let alias = root.join("alias");
        symlink(&root, &alias).unwrap();
        assert!(Endpoint::bind(alias.join("private.sock").to_str().unwrap()).is_err());
        for path in ["/tmp/xodus.sock", "relative.sock", "/tmp/../private.sock"] {
            assert!(Endpoint::bind(path).is_err());
        }
        std::fs::remove_file(alias).unwrap();
        std::fs::remove_dir(root).unwrap();
    }

    #[tokio::test]
    async fn private_endpoint_cleanup_never_removes_a_replacement() {
        let root = directory();
        let path = root.join("private.sock");
        let endpoint = Endpoint::bind(path.to_str().unwrap()).unwrap();
        std::fs::remove_file(&path).unwrap();
        std::fs::write(&path, b"replacement-owner-file").unwrap();
        assert!(endpoint.verify().is_err());
        drop(endpoint);
        assert_eq!(std::fs::read(&path).unwrap(), b"replacement-owner-file");
        std::fs::remove_file(path).unwrap();
        std::fs::remove_dir(root).unwrap();
    }

    #[tokio::test]
    async fn changed_parent_identity_fails_closed_and_cleanup_stays_on_original_descriptor() {
        let root = directory();
        let path = root.join("private.sock");
        let endpoint = Endpoint::bind(path.to_str().unwrap()).unwrap();
        let moved = root.with_extension("moved");
        std::fs::rename(&root, &moved).unwrap();
        std::fs::create_dir(&root).unwrap();
        std::fs::set_permissions(&root, std::fs::Permissions::from_mode(0o700)).unwrap();
        std::fs::write(&path, b"replacement-directory-owner-file").unwrap();
        assert!(endpoint.verify().is_err());
        drop(endpoint);
        assert!(!moved.join("private.sock").exists());
        assert_eq!(
            std::fs::read(&path).unwrap(),
            b"replacement-directory-owner-file"
        );
        std::fs::remove_file(path).unwrap();
        std::fs::remove_dir(root).unwrap();
        std::fs::remove_dir(moved).unwrap();
    }

    #[cfg(target_os = "macos")]
    #[tokio::test]
    async fn scoped_connection_capacity_and_cancellation_are_real_socket_behavior() {
        use tokio::io::{AsyncReadExt, AsyncWriteExt};
        if !xodus::secrets::management_native_keychain_enabled() {
            return;
        }
        let root = directory();
        let path = root.join("private.sock");
        let tokens = Arc::new(TokenManager::with_management_backend(Arc::new(
            xodus::tokens::backend::MemoryBackend::default(),
        )));
        let cancellation = CancellationToken::new();
        let owned_cancellation = cancellation.clone();
        let selected = path.to_str().unwrap().to_owned();
        let task = tokio::spawn(async move { serve(&selected, tokens, owned_cancellation).await });
        tokio::time::timeout(std::time::Duration::from_secs(2), async {
            while !path.exists() {
                tokio::task::yield_now().await;
            }
        })
        .await
        .unwrap();
        let request = crate::connection::encode_message(crate::XML_MAGIC, 1, vec![]).unwrap();
        let expected = crate::connection::encode_message(crate::XML_MAGIC, 2, vec![]).unwrap();
        let mut peers = Vec::new();
        for _ in 0..MAX_CONNECTIONS {
            let mut peer = tokio::net::UnixStream::connect(&path).await.unwrap();
            peer.write_all(&request).await.unwrap();
            let mut reply = [0; 8];
            tokio::time::timeout(
                std::time::Duration::from_secs(2),
                peer.read_exact(&mut reply),
            )
            .await
            .unwrap()
            .unwrap();
            assert_eq!(reply.as_slice(), expected);
            peers.push(peer);
        }
        let mut excess = tokio::net::UnixStream::connect(&path).await.unwrap();
        let mut reply = Vec::new();
        tokio::time::timeout(
            std::time::Duration::from_secs(2),
            excess.read_to_end(&mut reply),
        )
        .await
        .unwrap()
        .unwrap();
        assert!(reply.is_empty());
        cancellation.cancel();
        tokio::time::timeout(std::time::Duration::from_secs(2), task)
            .await
            .unwrap()
            .unwrap()
            .unwrap();
        assert!(!path.exists());
        for mut peer in peers {
            let mut reply = Vec::new();
            peer.read_to_end(&mut reply).await.unwrap();
            assert!(reply.is_empty());
        }
        std::fs::remove_dir(root).unwrap();
    }
}
