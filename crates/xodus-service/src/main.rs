use std::fs::Permissions;
use std::os::unix::fs::PermissionsExt;
use std::sync::Arc;

use tokio::net::UnixListener;
use tokio_util::sync::CancellationToken;
use tracing_subscriber::Layer;
use tracing_subscriber::layer::SubscriberExt;
use tracing_subscriber::util::SubscriberInitExt;
use xodus::tokens::TokenManager;

mod utils;
use xodus_service::connection;

#[tokio::main]
async fn main() {
    let arguments: Vec<_> = std::env::args_os().skip(1).collect();
    if !arguments.is_empty() {
        if arguments.len() != 2 || arguments[0] != "--management-socket" {
            eprintln!("Expected --management-socket with one absolute private Unix path");
            std::process::exit(1);
        }
        let Some(path) = arguments[1].to_str() else {
            eprintln!("Private runtime socket path must be valid UTF-8");
            std::process::exit(1);
        };
        if !xodus::secrets::management_native_keychain_enabled() {
            eprintln!(
                "Isolated runtime broker requires native macOS Keychain without plaintext fallback"
            );
            std::process::exit(1);
        }
        if xodus::secrets::init_secrets().is_err() {
            eprintln!("Isolated native credential storage initialization failed");
            std::process::exit(1);
        }
        let cancellation = CancellationToken::new();
        let trigger = cancellation.clone();
        tokio::spawn(async move {
            if tokio::signal::ctrl_c().await.is_err() {
                eprintln!("Private runtime shutdown signal handler failed");
            }
            trigger.cancel();
        });
        let tokens = Arc::new(TokenManager::with_management_keychain_and_memory());
        if let Err(error) = xodus_service::isolated::serve(path, tokens, cancellation).await {
            eprintln!("Isolated runtime broker failed: {error}");
            std::process::exit(1);
        }
        return;
    }
    let filter = tracing_subscriber::EnvFilter::from_env("XODUS_LOG");
    let registry =
        tracing_subscriber::registry().with(tracing_subscriber::fmt::layer().with_filter(filter));

    #[cfg(feature = "tokio_console")]
    {
        use tracing::level_filters::LevelFilter;
        use tracing_subscriber::filter::Targets;

        let console_filter = Targets::new()
            .with_target("tokio", LevelFilter::TRACE)
            .with_target("runtime", LevelFilter::TRACE);
        let console_layer = console_subscriber::spawn().with_filter(console_filter);
        registry.with(console_layer).init();
    }
    #[cfg(not(feature = "tokio_console"))]
    {
        registry.init();
    }

    xodus::secrets::init_secrets().expect("Failed to init keychain");
    let tokens = Arc::new(TokenManager::with_keychain_and_memory());
    if let Err(error) =
        xodus::tokens::device::ensure_device_credentials(&reqwest::Client::new(), &tokens).await
    {
        eprintln!("{error}");
        std::process::exit(1);
    }
    let xodus::models::secrets::Token::Legacy(device_token) =
        tokens.get_device_sts_token().unwrap()
    else {
        panic!("Device token isnt legacy")
    };

    let runtime_dir = utils::get_runtime_dir();
    let cancellation = CancellationToken::new();
    let socket_path = format!("{runtime_dir}/xodus.sock");
    let trigger = cancellation.clone();
    tokio::spawn(async move {
        tokio::signal::ctrl_c()
            .await
            .expect("Failure to handle ctrl_c");
        trigger.cancel();
    });
    {
        let listener = UnixListener::bind(&socket_path).expect("Unable to bind to socket");
        let mode = 0o600;
        let perms = Permissions::from_mode(mode);
        _ = tokio::fs::set_permissions(&socket_path, perms).await;
        loop {
            let accept = tokio::select! {
                r = listener.accept() => r,
                _ = cancellation.cancelled() => break,
            }
            .expect("Failed to accept");

            let token = cancellation.clone();
            let device_token = device_token.clone();
            let tokens = tokens.clone();
            tokio::spawn(async move {
                connection::router::route(accept.0, token, device_token, tokens).await
            });
        }
    }

    _ = tokio::fs::remove_file(socket_path).await;
}
