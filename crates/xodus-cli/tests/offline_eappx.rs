use std::process::{Command, Stdio};
use std::time::{Duration, Instant};

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
            .env("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent-xodus-test-bus")
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

        let mut child = command.spawn().unwrap();
        let deadline = Instant::now() + Duration::from_secs(20);
        while child.try_wait().unwrap().is_none() {
            if Instant::now() >= deadline {
                child.kill().unwrap();
                child.wait().unwrap();
                panic!("Local EAppx extraction did not exit within 20 seconds");
            }
            std::thread::sleep(Duration::from_millis(25));
        }
        let output = child.wait_with_output().unwrap();
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
