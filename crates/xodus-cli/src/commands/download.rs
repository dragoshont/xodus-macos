#[cfg(unix)]
use std::os::unix::fs::PermissionsExt;
use std::process::ExitCode;

use futures_util::StreamExt;
use indicatif::{ProgressBar, ProgressStyle};
use inquire::MultiSelect;
use inquire::validator::Validation;
use tokio::io::AsyncWriteExt;
use xodus::models::packagespc::PackageFile;
use xodus::tokens::TokenManager;

use crate::package::{checked_package_file_source, get_content_id, get_packages};

async fn download_file(
    client: &reqwest::Client,
    source: reqwest::Url,
    expected: u64,
    destination: &std::path::Path,
    progress: &ProgressBar,
) -> Result<(), &'static str> {
    let directory = destination
        .parent()
        .filter(|path| !path.as_os_str().is_empty())
        .unwrap_or(std::path::Path::new("."));
    match tokio::fs::symlink_metadata(destination).await {
        Ok(metadata) if !metadata.is_file() || metadata.file_type().is_symlink() => {
            return Err("Download destination must be a regular file");
        }
        Ok(_) => {}
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => {}
        Err(_) => return Err("Could not inspect the download destination"),
    }
    let response = client
        .get(source)
        .send()
        .await
        .map_err(|_| "Download request failed")?;
    if response.status() != reqwest::StatusCode::OK {
        return Err("Download request did not return a complete successful response");
    }
    if response
        .content_length()
        .is_some_and(|length| length != expected)
    {
        return Err("Download length does not match package metadata");
    }
    let mut staging_builder = tempfile::Builder::new();
    #[cfg(unix)]
    staging_builder.permissions(std::fs::Permissions::from_mode(0o700));
    let staging = staging_builder
        .tempdir_in(directory)
        .map_err(|_| "Could not create private download staging")?;
    let temporary = tempfile::NamedTempFile::new_in(staging.path())
        .map_err(|_| "Could not create the staged download file")?;
    let mut file = tokio::fs::File::from_std(
        temporary
            .as_file()
            .try_clone()
            .map_err(|_| "Could not open the staged download file")?,
    );
    let mut stream = response.bytes_stream();
    let mut received = 0u64;
    while let Some(chunk) = stream.next().await {
        let chunk = chunk.map_err(|_| "Download response stream failed")?;
        received = received
            .checked_add(chunk.len() as u64)
            .filter(|length| *length <= expected)
            .ok_or("Download exceeds the package file size")?;
        file.write_all(&chunk)
            .await
            .map_err(|_| "Could not write the staged download")?;
        progress.inc(chunk.len() as u64);
    }
    if received != expected {
        return Err("Download byte count does not match package metadata");
    }
    file.flush()
        .await
        .map_err(|_| "Could not flush the staged download")?;
    file.sync_all()
        .await
        .map_err(|_| "Could not synchronize the staged download")?;
    drop(file);
    temporary
        .persist(destination)
        .map_err(|_| "Could not commit the staged download")?;
    Ok(())
}

pub async fn run(
    client: &reqwest::Client,
    tokens: &TokenManager,
    product: String,
    market: Option<String>,
    dry_run: bool,
) -> ExitCode {
    let content_id_task = get_content_id(client, product, market).await;
    let Ok(content_id) = content_id_task else {
        let Err(err) = content_id_task else {
            eprintln!("Unknown Error");
            return ExitCode::FAILURE;
        };
        eprintln!("{}", err);
        return ExitCode::FAILURE;
    };

    let package_result = get_packages(client, tokens, content_id.clone()).await;
    let Ok(package) = package_result else {
        let Err(err) = package_result else {
            eprintln!("Unknown Error");
            return ExitCode::FAILURE;
        };
        eprintln!("{}", err);
        return ExitCode::FAILURE;
    };

    let Ok(files) = MultiSelect::new("Select files to download", package.package_files)
        .with_page_size(30)
        .with_validator(|input: &[inquire::list_option::ListOption<&PackageFile>]| {
            if !input.is_empty() {
                Ok(Validation::Valid)
            } else {
                Ok(Validation::Invalid(
                    "At least one item has to be selected".into(),
                ))
            }
        })
        .prompt()
    else {
        tracing::error!("Selection failed");
        return ExitCode::FAILURE;
    };
    println!();
    for file in files {
        let (url, expected) = match checked_package_file_source(&file) {
            Ok(source) => source,
            Err(error) => {
                eprintln!("{error}");
                return ExitCode::FAILURE;
            }
        };
        if dry_run {
            println!("{}", url);
            continue;
        }

        let progress_bar = ProgressBar::new(expected).with_style(
            ProgressStyle::with_template("[{elapsed_precise}] [{bar:40.cyan/blue}] {bytes}/{total_bytes} ({bytes_per_sec}) ({eta})").unwrap()
            .progress_chars("#>-")
        );

        if let Err(error) = download_file(
            client,
            url,
            expected,
            std::path::Path::new(&file.file_name),
            &progress_bar,
        )
        .await
        {
            progress_bar.abandon();
            eprintln!("{error}");
            return ExitCode::FAILURE;
        }
        progress_bar.finish();
    }

    println!("ContentID: {content_id}");

    ExitCode::SUCCESS
}

#[cfg(test)]
mod tests {
    use super::*;
    use tokio::io::{AsyncReadExt, AsyncWriteExt};
    use tokio::net::TcpListener;

    fn client() -> reqwest::Client {
        reqwest::Client::builder()
            .no_proxy()
            .redirect(reqwest::redirect::Policy::none())
            .build()
            .unwrap()
    }

    async fn peer(bytes: &[u8]) -> (reqwest::Url, tokio::task::JoinHandle<()>) {
        let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
        let address = listener.local_addr().unwrap();
        let bytes = bytes.to_vec();
        let task = tokio::spawn(async move {
            let (mut socket, _) = listener.accept().await.unwrap();
            let mut request = [0; 4096];
            assert!(socket.read(&mut request).await.unwrap() > 0);
            socket.write_all(&bytes).await.unwrap();
        });
        (
            reqwest::Url::parse(&format!("http://{address}/fixture?secret=fixture-only")).unwrap(),
            task,
        )
    }

    fn assert_only_destination(root: &std::path::Path) {
        let entries: Vec<_> = std::fs::read_dir(root)
            .unwrap()
            .map(|entry| entry.unwrap().file_name())
            .collect();
        assert_eq!(entries, ["fixture.msixvc"]);
    }

    #[cfg(unix)]
    async fn assert_private_staging_commits_exact_file(root: &std::path::Path) {
        use std::os::unix::fs::MetadataExt;

        let destination = root.join("fixture.msixvc");
        std::fs::write(&destination, b"known-good").unwrap();
        let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
        let address = listener.local_addr().unwrap();
        let (release, complete) = tokio::sync::oneshot::channel();
        let peer = tokio::spawn(async move {
            let (mut socket, _) = listener.accept().await.unwrap();
            let mut request = [0; 4096];
            socket.read(&mut request).await.unwrap();
            socket
                .write_all(
                    b"HTTP/1.1 200 OK\r\nContent-Length: 12\r\nConnection: close\r\n\r\nfixt",
                )
                .await
                .unwrap();
            tokio::time::timeout(std::time::Duration::from_secs(2), complete)
                .await
                .unwrap()
                .unwrap();
            socket.write_all(b"ure-only").await.unwrap();
        });
        let progress = ProgressBar::hidden();
        let observed = progress.clone();
        let target = destination.clone();
        let caller = tokio::spawn(async move {
            download_file(
                &client(),
                reqwest::Url::parse(&format!("http://{address}/fixture")).unwrap(),
                12,
                &target,
                &progress,
            )
            .await
        });
        tokio::time::timeout(std::time::Duration::from_secs(2), async {
            while observed.position() < 4 {
                tokio::time::sleep(std::time::Duration::from_millis(1)).await;
            }
        })
        .await
        .unwrap();
        let mut staged_directories = std::fs::read_dir(root)
            .unwrap()
            .map(|entry| entry.unwrap().path())
            .filter(|path| path != &destination);
        let staging = staged_directories.next().unwrap();
        assert!(staged_directories.next().is_none());
        let metadata = std::fs::symlink_metadata(&staging).unwrap();
        assert!(metadata.is_dir() && !metadata.file_type().is_symlink());
        assert_eq!(metadata.permissions().mode() & 0o777, 0o700);
        let mut staged_files = std::fs::read_dir(&staging)
            .unwrap()
            .map(|entry| entry.unwrap().path());
        let staged = staged_files.next().unwrap();
        assert!(staged_files.next().is_none());
        let original = std::fs::File::open(&staged).unwrap();
        let identity = original.metadata().unwrap();
        assert!(identity.is_file());
        assert_eq!(identity.permissions().mode() & 0o777, 0o600);
        assert_eq!(std::fs::read(&destination).unwrap(), b"known-good");
        release.send(()).unwrap();
        caller.await.unwrap().unwrap();
        peer.await.unwrap();
        let committed = std::fs::symlink_metadata(&destination).unwrap();
        assert!(committed.is_file() && !committed.file_type().is_symlink());
        assert_eq!(
            (committed.dev(), committed.ino()),
            (identity.dev(), identity.ino())
        );
        assert_eq!(committed.permissions().mode() & 0o777, 0o600);
        assert_eq!(std::fs::read(&destination).unwrap(), b"fixture-only");
        let mut contents = Vec::new();
        std::io::Read::read_to_end(&mut &original, &mut contents).unwrap();
        assert_eq!(contents, b"fixture-only");
        assert_only_destination(root);
    }

    #[cfg(unix)]
    #[test]
    fn download_staging_is_private_at_creation_under_permissive_umask() {
        const CHILD_ROOT: &str = "XODUS_TEST_DOWNLOAD_UMASK_ROOT";
        const CHILD_MASK: &str = "XODUS_TEST_DOWNLOAD_UMASK";
        if let Some(root) = std::env::var_os(CHILD_ROOT) {
            let root = std::path::PathBuf::from(root);
            let mask = match std::env::var(CHILD_MASK).unwrap().as_str() {
                "0002" => 0o002,
                "0000" => 0o000,
                _ => panic!("Unexpected isolated umask"),
            };
            let directory = root.join(format!("mask-{mask:o}"));
            std::fs::create_dir(&directory).unwrap();
            assert_eq!(
                std::fs::metadata(&directory).unwrap().permissions().mode() & 0o777,
                0o777 & !mask
            );
            let control = tempfile::tempdir_in(&directory).unwrap();
            assert_eq!(
                control.path().metadata().unwrap().permissions().mode() & 0o777,
                0o777 & !mask
            );
            drop(control);
            tokio::runtime::Builder::new_current_thread()
                .enable_all()
                .build()
                .unwrap()
                .block_on(assert_private_staging_commits_exact_file(&directory));
            download_size_and_stream_failures_preserve_known_good_file_and_clean_staging();
            download_caller_abort_cleans_partial_staging_and_preserves_destination();
            return;
        }
        let root = tempfile::Builder::new()
            .permissions(std::fs::Permissions::from_mode(0o700))
            .tempdir()
            .unwrap();
        for mask in ["0002", "0000"] {
            let output = std::process::Command::new("/bin/sh")
                .args(["-c", "umask \"$1\"; shift; exec \"$@\"", "xodus-isolated-umask", mask])
                .arg(std::env::current_exe().unwrap())
                .args(["--exact", "commands::download::tests::download_staging_is_private_at_creation_under_permissive_umask", "--test-threads=1"])
                .env(CHILD_ROOT, root.path())
                .env(CHILD_MASK, mask)
                .output().unwrap();
            assert!(
                output.status.success(),
                "Isolated umask {mask} regression failed:\n{}\n{}",
                String::from_utf8_lossy(&output.stdout),
                String::from_utf8_lossy(&output.stderr)
            );
            assert!(String::from_utf8_lossy(&output.stdout).contains("1 passed"));
        }
    }

    #[tokio::test]
    async fn download_commits_complete_exact_bytes_and_cleans_private_staging() {
        let root = tempfile::tempdir().unwrap();
        let destination = root.path().join("fixture.msixvc");
        std::fs::write(&destination, b"known-good").unwrap();
        let (url, task) =
            peer(b"HTTP/1.1 200 OK\r\nContent-Length: 12\r\nConnection: close\r\n\r\nfixture-only")
                .await;
        download_file(&client(), url, 12, &destination, &ProgressBar::hidden())
            .await
            .unwrap();
        task.await.unwrap();
        assert_eq!(std::fs::read(&destination).unwrap(), b"fixture-only");
        assert_only_destination(root.path());
    }

    #[tokio::test]
    async fn download_http_errors_and_partial_status_preserve_known_good_file() {
        for status in ["403 Forbidden", "206 Partial Content", "302 Found"] {
            let root = tempfile::tempdir().unwrap();
            let destination = root.path().join("fixture.msixvc");
            std::fs::write(&destination, b"known-good").unwrap();
            let (url, task) = peer(format!("HTTP/1.1 {status}\r\nContent-Length: 12\r\nConnection: close\r\n\r\nfixture-only").as_bytes()).await;
            let error = download_file(&client(), url, 12, &destination, &ProgressBar::hidden())
                .await
                .unwrap_err();
            task.await.unwrap();
            assert_eq!(
                error,
                "Download request did not return a complete successful response"
            );
            assert!(!error.contains("fixture-only"));
            assert_eq!(std::fs::read(&destination).unwrap(), b"known-good");
            assert_only_destination(root.path());
        }
    }

    #[tokio::test]
    async fn download_size_and_stream_failures_preserve_known_good_file_and_clean_staging() {
        for bytes in [
            b"HTTP/1.1 200 OK\r\nContent-Length: 64\r\nConnection: close\r\n\r\nfixture-only".as_slice(),
            b"HTTP/1.1 200 OK\r\nContent-Length: 12\r\nConnection: close\r\n\r\nshort".as_slice(),
            b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n5\r\nshort\r\n0\r\n\r\n".as_slice(),
            b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n10\r\nfixture-overflow\r\n0\r\n\r\n".as_slice(),
        ] {
            let root = tempfile::tempdir().unwrap();
            let destination = root.path().join("fixture.msixvc");
            std::fs::write(&destination, b"known-good").unwrap();
            let (url, task) = peer(bytes).await;
            assert!(download_file(&client(), url, 12, &destination, &ProgressBar::hidden()).await.is_err());
            task.await.unwrap();
            assert_eq!(std::fs::read(&destination).unwrap(), b"known-good");
            assert_only_destination(root.path());
        }
    }

    #[tokio::test]
    async fn download_caller_abort_cleans_partial_staging_and_preserves_destination() {
        let root = tempfile::tempdir().unwrap();
        let destination = root.path().join("fixture.msixvc");
        std::fs::write(&destination, b"known-good").unwrap();
        let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
        let address = listener.local_addr().unwrap();
        let peer = tokio::spawn(async move {
            let (mut socket, _) = listener.accept().await.unwrap();
            let mut request = [0; 4096];
            socket.read(&mut request).await.unwrap();
            socket
                .write_all(
                    b"HTTP/1.1 200 OK\r\nContent-Length: 64\r\nConnection: close\r\n\r\npart",
                )
                .await
                .unwrap();
            let mut next = [0; 1];
            match tokio::time::timeout(std::time::Duration::from_secs(2), socket.read(&mut next))
                .await
                .unwrap()
            {
                Ok(0) => {}
                Err(error) if error.kind() == std::io::ErrorKind::ConnectionReset => {}
                result => panic!("Cancelled download did not close its peer: {result:?}"),
            }
        });
        let progress = ProgressBar::hidden();
        let observed = progress.clone();
        let target = destination.clone();
        let caller = tokio::spawn(async move {
            download_file(
                &client(),
                reqwest::Url::parse(&format!("http://{address}/fixture")).unwrap(),
                64,
                &target,
                &progress,
            )
            .await
        });
        tokio::time::timeout(std::time::Duration::from_secs(2), async {
            while observed.position() == 0 {
                tokio::time::sleep(std::time::Duration::from_millis(1)).await;
            }
        })
        .await
        .unwrap();
        caller.abort();
        assert!(caller.await.unwrap_err().is_cancelled());
        peer.await.unwrap();
        assert_eq!(std::fs::read(&destination).unwrap(), b"known-good");
        assert_only_destination(root.path());
    }

    #[cfg(unix)]
    #[tokio::test]
    async fn download_refuses_symlink_destination_before_request() {
        let root = tempfile::tempdir().unwrap();
        let target = root.path().join("known-good");
        std::fs::write(&target, b"known-good").unwrap();
        let destination = root.path().join("fixture.msixvc");
        std::os::unix::fs::symlink(&target, &destination).unwrap();
        assert_eq!(
            download_file(
                &client(),
                reqwest::Url::parse("http://127.0.0.1:1/fixture").unwrap(),
                12,
                &destination,
                &ProgressBar::hidden()
            )
            .await,
            Err("Download destination must be a regular file"),
        );
        assert_eq!(std::fs::read(&target).unwrap(), b"known-good");
        assert!(
            std::fs::symlink_metadata(destination)
                .unwrap()
                .file_type()
                .is_symlink()
        );
    }
}
