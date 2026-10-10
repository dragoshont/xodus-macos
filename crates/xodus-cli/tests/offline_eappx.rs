use std::process::{Command, Stdio};
use std::time::{Duration, Instant};

fn bounded_output(command: &mut Command) -> std::process::Output {
    let mut child = command.spawn().unwrap();
    let deadline = Instant::now() + Duration::from_secs(20);
    while child.try_wait().unwrap().is_none() {
        if Instant::now() >= deadline {
            child.kill().unwrap();
            child.wait().unwrap();
            panic!("Credential-free command did not exit within 20 seconds");
        }
        std::thread::sleep(Duration::from_millis(25));
    }
    child.wait_with_output().unwrap()
}

#[test]
fn local_eappx_extraction_reaches_file_errors_without_credentials() {
    let home = tempfile::tempdir().unwrap();
    let missing_package = home.path().join("missing.eappx");
    let destination = home.path().join("output");
    let missing_keys = home.path().join("missing-keys.txt");

    for with_key_file in [false, true] {
        let mut command = Command::new(env!("CARGO_BIN_EXE_xodus-cli"));
        command
            .current_dir(home.path())
            .env("HOME", home.path())
            .env("USERPROFILE", home.path())
            .env("XODUS_LOG", "off")
            .env(
                "DBUS_SESSION_BUS_ADDRESS",
                "unix:path=/nonexistent-xodus-test-bus",
            )
            .env("HTTP_PROXY", "http://127.0.0.1:9")
            .env("HTTPS_PROXY", "http://127.0.0.1:9")
            .env("ALL_PROXY", "http://127.0.0.1:9")
            .env_remove("NO_PROXY")
            .env_remove("no_proxy")
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .arg("extract-eappx")
            .arg(&missing_package)
            .arg(&destination);
        if with_key_file {
            command.arg("--key-file").arg(&missing_keys);
        }

        let output = bounded_output(&mut command);
        let stderr = String::from_utf8_lossy(&output.stderr);

        assert_eq!(output.status.code(), Some(1), "{stderr}");
        assert!(
            stderr.starts_with("Failed to open "),
            "Expected a local file error, not credential initialization: {stderr}"
        );
        assert!(!destination.exists());
        assert!(!home.path().join(".xodus-keyring.ron").exists());
    }
}

#[test]
fn public_product_inspection_reports_network_errors_without_credentials() {
    let home = tempfile::tempdir().unwrap();
    let mut command = Command::new(env!("CARGO_BIN_EXE_xodus-cli"));
    command
        .current_dir(home.path())
        .env("HOME", home.path())
        .env("USERPROFILE", home.path())
        .env("XODUS_LOG", "off")
        .env(
            "DBUS_SESSION_BUS_ADDRESS",
            "unix:path=/nonexistent-xodus-test-bus",
        )
        .env("HTTP_PROXY", "http://127.0.0.1:9")
        .env("HTTPS_PROXY", "http://127.0.0.1:9")
        .env("ALL_PROXY", "http://127.0.0.1:9")
        .env_remove("NO_PROXY")
        .env_remove("no_proxy")
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .arg("inspect-product")
        .arg("BWMQL2RPWBHB")
        .arg("--market")
        .arg("US");
    let output = bounded_output(&mut command);
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert_eq!(output.status.code(), Some(1), "{stderr}");
    assert!(
        stderr.starts_with("Failed to inspect public product metadata: "),
        "Expected a public catalog error, not credential initialization: {stderr}"
    );
    assert!(output.stdout.is_empty());
    assert!(!home.path().join(".xodus-keyring.ron").exists());
}
