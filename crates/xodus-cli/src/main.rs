use std::process::ExitCode;

use clap::{Parser, Subcommand};
use tracing_subscriber::Layer;
use tracing_subscriber::layer::SubscriberExt;
use tracing_subscriber::util::SubscriberInitExt;
use xodus::tokens::TokenManager;

mod auth_verify;
#[cfg(debug_assertions)]
mod collections_diagnostic;
mod commands;
mod license;
#[cfg(target_os = "macos")]
mod management_auth;
#[cfg(target_os = "macos")]
mod native_auth_host;
mod package;
mod provider_credentials;
mod recent_library;
mod runtime_plan;
mod webview;

#[derive(Subcommand)]
enum SubCommand {
    #[cfg(debug_assertions)]
    #[command(hide = true)]
    CollectionsDiagnostic {
        #[arg(long, value_parser = ["US"])]
        market: String,
    },
    #[command(about = "Pure JSON runtime configuration generation planning; no provider execution")]
    RuntimePlan,
    #[command(about = "Strict JSONL launcher management protocol")]
    Manage {
        #[arg(long)]
        protocol: u32,
        #[arg(long)]
        state_dir: std::path::PathBuf,
        #[arg(long, requires_all = ["native_auth_host_sha256", "native_auth_host_version"])]
        native_auth_host: Option<std::path::PathBuf>,
        #[arg(long, requires_all = ["native_auth_host", "native_auth_host_version"])]
        native_auth_host_sha256: Option<String>,
        #[arg(long, requires_all = ["native_auth_host", "native_auth_host_sha256"])]
        native_auth_host_version: Option<u32>,
    },
    #[cfg(target_os = "macos")]
    #[command(hide = true)]
    ManagementAuthWorker {
        #[arg(long)]
        flow_id: String,
    },
    #[command(about = "Download msixvc or xsp files fo given game")]
    Download {
        product: String,
        #[arg(short, long)]
        market: Option<String>,
        #[arg(
            long,
            default_value_t = false,
            help = "Display download URLs instead of downloading"
        )]
        dry_run: bool,
    },
    #[command(about = "Dump CIKs for use with XvdTool")]
    License {
        #[clap(help = "Content Id of a license")]
        content_id: String,
        #[clap(help = "A path where to dump CIKs")]
        ciks: String,
        #[arg(short, long)]
        market: Option<String>,
    },
    #[command(about = "Extract locally stored msixvc file")]
    Extract {
        path: String,
        destination: String,
        #[arg(short, long)]
        market: Option<String>,
    },
    #[command(
        about = "Extract a locally stored EAppx/EMSIX package (research task, see issue #91)"
    )]
    ExtractEappx {
        path: String,
        destination: String,
        #[arg(short, long, help = "Path to a keyfile with content decryption keys")]
        key_file: Option<String>,
    },
    Login,
    Logout {
        #[arg(long, default_value_t = false, help = "Remove device license")]
        device: bool,
    },
    #[command(about = "Download and extract the game through streaming algorithm")]
    Streaming {
        source: String,
        destination: String,
        #[arg(
            long,
            default_value_t = false,
            help = "Attempt to skip downloading NTFS metadata to be faste while missing some files"
        )]
        try_skip_ntfs: bool,
        #[arg(short, long)]
        parallel: Option<usize>,
        #[arg(short, long)]
        market: Option<String>,
    },
    #[cfg(unix)]
    #[command(about = "Run a Game with xodus wine")]
    Run {
        source: String,
        wine: String,
        #[arg(short, long)]
        exe: Option<String>,
        #[arg(short, long)]
        market: Option<String>,
    },
    #[command(about = "Generate or decrypt base64-encoded CLEP challenge data")]
    Clep {
        #[command(subcommand)]
        action: ClepAction,
    },
    #[command(about = "Decode SPLicenseBlock")]
    SpLicense {
        block: String,
    },
}

#[derive(Subcommand)]
enum ClepAction {
    #[command(
        about = "Generate a base64-encoded CLEP challenge (V2 and V4) from SMBIOS/disk serial data"
    )]
    Generate {
        #[arg(
            long,
            help = "Base64-encoded SMBIOS data (up to 256 bytes, zero-padded)"
        )]
        smbios: Option<String>,
        #[arg(
            long,
            help = "Base64-encoded disk serial (up to 64 bytes, zero-padded)"
        )]
        disk_serial: Option<String>,
    },
    #[command(about = "Decrypt a base64-encoded CLEP challenge back into its plaintext fields")]
    Decrypt {
        #[clap(help = "Base64-encoded, obfuscated CLEP challenge data (2048 bytes)")]
        data: String,
    },
}

#[derive(Parser)]
#[command(version, about, long_about = None)]
struct CliArgs {
    #[command(subcommand)]
    command: SubCommand,
}

#[cfg(test)]
mod argument_tests {
    use super::*;

    #[cfg(debug_assertions)]
    #[test]
    fn collections_diagnostic_requires_explicit_development_market() {
        assert!(matches!(
            CliArgs::try_parse_from(["xodus", "collections-diagnostic", "--market", "US"])
                .unwrap()
                .command,
            SubCommand::CollectionsDiagnostic { market } if market == "US"
        ));
        for arguments in [
            vec!["xodus", "collections-diagnostic"],
            vec!["xodus", "collections-diagnostic", "--market", "TW"],
            vec![
                "xodus",
                "collections-diagnostic",
                "--market",
                "US",
                "--product",
                "fixture",
            ],
        ] {
            assert!(CliArgs::try_parse_from(arguments).is_err());
        }
    }

    #[test]
    fn runtime_planning_has_its_own_source_only_command() {
        assert!(matches!(
            CliArgs::try_parse_from(["xodus", "runtime-plan"])
                .unwrap()
                .command,
            SubCommand::RuntimePlan
        ));
    }

    #[test]
    fn native_helper_binding_is_all_or_none_and_standalone_login_is_unchanged() {
        for flags in [
            vec!["--native-auth-host", "fixture-helper"],
            vec!["--native-auth-host-sha256", "fixture-hash"],
            vec!["--native-auth-host-version", "1"],
        ] {
            let mut args = vec![
                "xodus",
                "manage",
                "--protocol",
                "1",
                "--state-dir",
                "fixture-state",
            ];
            args.extend(flags);
            assert!(CliArgs::try_parse_from(args).is_err());
        }
        assert!(
            CliArgs::try_parse_from([
                "xodus",
                "manage",
                "--protocol",
                "1",
                "--state-dir",
                "fixture-state",
                "--native-auth-host",
                "fixture-helper",
                "--native-auth-host-sha256",
                "fixture-hash",
                "--native-auth-host-version",
                "1",
            ])
            .is_ok()
        );
        assert!(
            CliArgs::try_parse_from([
                "xodus",
                "manage",
                "--protocol",
                "1",
                "--state-dir",
                "fixture-state",
            ])
            .is_ok()
        );
        assert!(matches!(
            CliArgs::try_parse_from(["xodus", "login"]).unwrap().command,
            SubCommand::Login
        ));
    }
}

#[tokio::main]
async fn main() -> ExitCode {
    let args = CliArgs::parse();
    #[cfg(debug_assertions)]
    if let SubCommand::CollectionsDiagnostic { market } = &args.command {
        return collections_diagnostic::run(market).await;
    }
    if matches!(args.command, SubCommand::RuntimePlan) {
        return runtime_plan::run().await;
    }
    if let SubCommand::Manage {
        protocol,
        state_dir,
        native_auth_host,
        native_auth_host_sha256,
        native_auth_host_version,
    } = &args.command
    {
        let binding = match (
            native_auth_host,
            native_auth_host_sha256,
            native_auth_host_version,
        ) {
            (Some(executable), Some(sha256), Some(version)) => {
                Some(xodus_management::native_auth::HostBinding {
                    executable: executable.clone(),
                    sha256: sha256.clone(),
                    version: *version,
                })
            }
            (None, None, None) => None,
            _ => return ExitCode::FAILURE,
        };
        let verifier = match auth_verify::PackageVerifier::new() {
            Ok(provider) => std::sync::Arc::new(provider),
            Err(_) => {
                eprintln!("Authenticated read provider could not be initialized.");
                return ExitCode::FAILURE;
            }
        };
        return xodus_management::adapter::run_with_auth_verifier(
            state_dir,
            *protocol,
            binding,
            Some(verifier),
        )
        .await;
    }
    #[cfg(target_os = "macos")]
    if let SubCommand::ManagementAuthWorker { flow_id } = &args.command {
        return management_auth::run(flow_id.clone()).await;
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
    let client = reqwest::ClientBuilder::new()
        .user_agent(format!("xodus-cli/{}", env!("CARGO_PKG_VERSION")))
        .connection_verbose(true)
        .build()
        .unwrap();

    xodus::secrets::init_secrets().expect("Unable to initialize credentials");
    let tokens = TokenManager::with_keychain_and_memory();

    // Clep/SpLicense are pure local data transforms and Logout only removes
    // stored credentials - none of them need a device identity, so don't
    // force provisioning (network + keychain access) just to run them. This
    // matters in practice: on a session with no usable secret-service
    // keychain, provisioning fails outright, which previously meant even
    // these fully offline commands were unusable.
    let needs_device_credentials = !matches!(
        args.command,
        SubCommand::Clep { .. } | SubCommand::SpLicense { .. } | SubCommand::Logout { .. }
    );
    if needs_device_credentials
        && let Err(error) = xodus::tokens::device::ensure_device_credentials(&client, &tokens).await
    {
        eprintln!("{error}");
        return ExitCode::FAILURE;
    }

    let code = match args.command {
        #[cfg(debug_assertions)]
        SubCommand::CollectionsDiagnostic { .. } => {
            unreachable!("development diagnostic returns before legacy initialization")
        }
        SubCommand::Manage { .. } | SubCommand::RuntimePlan => {
            unreachable!("management and pure planning return before legacy initialization")
        }
        #[cfg(target_os = "macos")]
        SubCommand::ManagementAuthWorker { .. } => {
            unreachable!("auth worker returns before legacy initialization")
        }
        SubCommand::Download {
            product,
            market,
            dry_run,
        } => commands::download::run(&client, &tokens, product, market, dry_run).await,
        SubCommand::License {
            content_id,
            market,
            ciks,
        } => {
            commands::license::run(
                &client,
                &tokens,
                content_id,
                market.unwrap_or("neutral".to_string()),
                ciks,
            )
            .await
        }
        SubCommand::Login => commands::login::run(&client, &tokens).await,
        SubCommand::Logout { device } => commands::logout::run(&tokens, device).await,
        SubCommand::Extract {
            path,
            destination,
            market,
        } => {
            commands::extract::run(
                &client,
                &tokens,
                path,
                destination,
                market.unwrap_or("neutral".to_string()),
            )
            .await
        }
        SubCommand::ExtractEappx {
            path,
            destination,
            key_file,
        } => commands::extract_eappx::run(path, destination, key_file).await,
        SubCommand::Streaming {
            source,
            destination,
            try_skip_ntfs,
            market,
            parallel,
        } => {
            commands::streaming::run(
                &client,
                &tokens,
                source,
                destination,
                try_skip_ntfs,
                parallel,
                market,
            )
            .await
        }
        #[cfg(unix)]
        SubCommand::Run {
            source,
            wine,
            exe,
            market,
        } => commands::run::run(&client, &tokens, source, wine, exe, market).await,
        SubCommand::Clep { action } => match action {
            ClepAction::Generate {
                smbios,
                disk_serial,
            } => commands::clep::generate(smbios, disk_serial),
            ClepAction::Decrypt { data } => commands::clep::decrypt(data),
        },
        SubCommand::SpLicense { block } => commands::splicense::run(block),
    };

    xodus::secrets::destroy_secrets();

    code
}
