//! Developer fixture only: real private broker with an empty in-memory account.

#[cfg(unix)]
fn fixture_path(arguments: &[std::ffi::OsString]) -> Result<&str, &'static str> {
    if arguments.len() != 3
        || arguments[0] != "--empty-memory-fixture"
        || arguments[1] != "--management-socket"
    {
        return Err(
            "Expected --empty-memory-fixture --management-socket with one absolute private Unix path",
        );
    }
    arguments[2]
        .to_str()
        .ok_or("Empty-profile fixture socket path must be valid UTF-8")
}

#[cfg(unix)]
#[tokio::main]
async fn main() -> std::process::ExitCode {
    use std::sync::Arc;
    use tokio_util::sync::CancellationToken;
    use xodus::tokens::{TokenManager, backend::MemoryBackend};

    let arguments: Vec<_> = std::env::args_os().skip(1).collect();
    let path = match fixture_path(&arguments) {
        Ok(path) => path,
        Err(error) => {
            eprintln!("{error}");
            return std::process::ExitCode::FAILURE;
        }
    };
    let mut interrupt =
        match tokio::signal::unix::signal(tokio::signal::unix::SignalKind::interrupt()) {
            Ok(signal) => signal,
            Err(_) => {
                eprintln!("Empty-profile fixture interrupt handler initialization failed");
                return std::process::ExitCode::FAILURE;
            }
        };
    let cancellation = CancellationToken::new();
    let shutdown = cancellation.clone();
    let handler = tokio::spawn(async move {
        interrupt.recv().await;
        shutdown.cancel();
    });
    // No native store initialization, Keychain backend, seed credentials or provider call.
    let tokens = Arc::new(TokenManager::with_management_backend(Arc::new(
        MemoryBackend::default(),
    )));
    eprintln!("Developer fixture: empty in-memory account; no native credential access");
    let result = xodus_service::isolated::serve(path, tokens, cancellation).await;
    handler.abort();
    let _ = handler.await;
    match result {
        Ok(()) => std::process::ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("Empty-profile fixture failed: {error}");
            std::process::ExitCode::FAILURE
        }
    }
}

#[cfg(not(unix))]
fn main() -> std::process::ExitCode {
    eprintln!("Empty-profile fixture requires the native Unix broker library");
    std::process::ExitCode::FAILURE
}

#[cfg(all(test, unix))]
mod tests {
    use super::*;

    #[test]
    fn no_default_or_production_invocation_is_accepted() {
        for arguments in [
            vec![],
            vec!["--help"],
            vec!["--management-socket", "/owned/peer.sock"],
            vec!["--fixture-socket", "/owned/peer.sock"],
            vec!["--empty-memory-fixture"],
            vec!["--empty-memory-fixture", "--management-socket"],
            vec![
                "--management-socket",
                "/owned/peer.sock",
                "--empty-memory-fixture",
            ],
            vec![
                "--empty-memory-fixture",
                "--management-socket",
                "/owned/peer.sock",
                "--profile",
            ],
        ] {
            let arguments: Vec<_> = arguments.into_iter().map(Into::into).collect();
            assert!(fixture_path(&arguments).is_err());
        }
    }

    #[test]
    fn explicit_fixture_path_is_passed_unchanged_to_existing_endpoint_validation() {
        let arguments = [
            "--empty-memory-fixture".into(),
            "--management-socket".into(),
            "/owned/peer.sock".into(),
        ];
        assert_eq!(fixture_path(&arguments), Ok("/owned/peer.sock"));
    }

    #[test]
    fn production_source_rejects_fixture_argv_before_native_initialization() {
        let source = include_str!("../src/main.rs");
        let guard = r#"if arguments.len() != 2 || arguments[0] != "--management-socket" {
            eprintln!("Expected --management-socket with one absolute private Unix path");
            std::process::exit(1);
        }"#;
        let guard_offset = source
            .find(guard)
            .expect("Production early argv guard changed");
        let initialization = source.find("xodus::secrets::init_secrets()").unwrap();
        let profile = source
            .find("TokenManager::with_management_keychain_and_memory()")
            .unwrap();
        assert!(guard_offset < initialization && initialization < profile);
        let arguments = [
            "--empty-memory-fixture",
            "--management-socket",
            "/owned/peer.sock",
        ];
        assert!(arguments.len() != 2 || arguments[0] != "--management-socket");
    }

    #[test]
    fn non_utf8_path_is_rejected_before_the_broker() {
        use std::os::unix::ffi::OsStringExt;
        let arguments = [
            "--empty-memory-fixture".into(),
            "--management-socket".into(),
            std::ffi::OsString::from_vec(vec![0xff]),
        ];
        assert!(fixture_path(&arguments).is_err());
    }
}
