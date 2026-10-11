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

#[test]
fn manifest_inspection_is_local_and_reports_activation_metadata() {
    let home = tempfile::tempdir().unwrap();
    let path = home.path().join("AppxManifest.xml");
    std::fs::write(
        &path,
        r#"<Package xmlns="http://schemas.microsoft.com/appx/manifest/foundation/windows10" xmlns:uap10="http://schemas.microsoft.com/appx/manifest/uap/windows10/10">
            <Identity Name="Example.Game" Publisher="CN=Example" Version="1.0.0.0" ProcessorArchitecture="x64"/>
            <Applications><Application Id="App" Executable="Game.exe" EntryPoint="Example.App" uap10:RuntimeBehavior="windowsApp"/></Applications>
        </Package>"#,
    )
    .unwrap();
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
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .arg("inspect-manifest")
        .arg(&path);
    let output = bounded_output(&mut command);
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    let report: serde_json::Value = serde_json::from_slice(&output.stdout).unwrap();
    assert_eq!(report["Inspection"]["Kind"], "Package");
    assert_eq!(
        report["Inspection"]["Manifest"]["Applications"]["Applications"][0]["RuntimeBehavior"],
        "windowsApp"
    );
    assert!(report["Selection"].is_null());
    assert!(!home.path().join(".xodus-keyring.ron").exists());

    command.arg("--architecture").arg("x64");
    let output = bounded_output(&mut command);
    assert_eq!(output.status.code(), Some(1));
    assert!(output.stdout.is_empty());
    assert!(
        String::from_utf8_lossy(&output.stderr)
            .contains("Architecture selection requires a bundle manifest")
    );
}

#[test]
fn manifest_inspection_selects_bundle_payload_without_extracting_it() {
    let home = tempfile::tempdir().unwrap();
    let path = home.path().join("AppxBundleManifest.xml");
    std::fs::write(
        &path,
        r#"<Bundle xmlns="http://schemas.microsoft.com/appx/2013/bundle">
            <Identity Name="Example.Game" Publisher="CN=Example" Version="1.0.0.0"/>
            <Packages>
                <Package Type="application" Version="1.0.0.0" Architecture="x64" FileName="game.appx" Offset="100" Size="200"/>
                <Package Version="1.0.0.0" FileName="resources.appx" Offset="300" Size="100"/>
            </Packages>
        </Bundle>"#,
    )
    .unwrap();
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
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .arg("inspect-manifest")
        .arg(&path)
        .arg("--architecture")
        .arg("x64");
    let output = bounded_output(&mut command);
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    let report: serde_json::Value = serde_json::from_slice(&output.stdout).unwrap();
    assert_eq!(report["Selection"]["Application"]["FileName"], "game.appx");
    assert_eq!(
        report["Selection"]["ResourceCandidates"][0]["FileName"],
        "resources.appx"
    );
    assert!(!home.path().join("game.appx").exists());
    assert!(!home.path().join(".xodus-keyring.ron").exists());
}
