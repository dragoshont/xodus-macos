use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
use tokio::io::{AsyncRead, AsyncReadExt, AsyncWrite, AsyncWriteExt};

pub const VERSION: u32 = 1;
pub const MAX_FRAME_BYTES: usize = 256 * 1024;
pub const MAX_BUDGET_MILLIS: u64 = 600_000;
pub const SESSION_ID: u64 = 1;
pub const USER_AGENT: &str = "Mozilla/5.0 (Windows NT 10.0; Win64; x64; MSAppHost/3.0) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/70.0.3538.102 Safari/537.36 Edge/18.26100";
pub const CAPABILITIES: &str =
    r#"{"PrivatePropertyBag":1,"PasswordlessConnect":1,"PreferAssociate":1,"ChromelessUI":0}"#;

#[derive(Clone, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct HostBinding {
    pub version: u32,
    pub executable: std::path::PathBuf,
    pub sha256: String,
}

#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct CommandFrame {
    pub version: u32,
    #[serde(rename = "flowID")]
    pub flow_id: String,
    #[serde(rename = "sessionID")]
    pub session_id: u64,
    pub sequence: u64,
    pub message: Command,
}

#[derive(Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "camelCase", deny_unknown_fields)]
pub enum Command {
    Open {
        #[serde(rename = "remainingMillis")]
        remaining_millis: u64,
        url: String,
        headers: BTreeMap<String, String>,
        #[serde(rename = "userAgent")]
        user_agent: String,
    },
    Navigate {
        url: String,
    },
    Close {
        disposition: Disposition,
    },
}

#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct ResultFrame {
    pub version: u32,
    #[serde(rename = "flowID")]
    pub flow_id: String,
    #[serde(rename = "sessionID")]
    pub session_id: u64,
    pub sequence: u64,
    pub reply_to: u64,
    pub message: HostResult,
}

#[derive(Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "camelCase", deny_unknown_fields)]
pub enum HostResult {
    Ready,
    Da { property: IssuerProperty },
    Closed { disposition: Disposition },
    Cancelled,
    Failed { reason: HostFailure },
}

#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct IssuerProperty {
    #[serde(rename = "sDAToken")]
    pub da_token: String,
    #[serde(rename = "sDASessionKey")]
    pub da_session_key: String,
    #[serde(rename = "sDAStartTime")]
    pub da_start_time: String,
    #[serde(rename = "sDAExpires")]
    pub da_expires: String,
    #[serde(rename = "sSTSInlineFlowToken")]
    pub sts_inline_flow_token: String,
    #[serde(rename = "sSigninName")]
    pub username: String,
    #[serde(rename = "K")]
    pub puid: String,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub enum Disposition {
    Completed,
    Cancelled,
    Failed,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub enum HostFailure {
    InvalidFrame,
    InvalidNavigation,
    NavigationFailed,
    PopupUnsupported,
    ContentTerminated,
    JavaScriptFailed,
    BridgeInvalid,
    DeadlineExpired,
    ParentUnavailable,
}

fn invalid() -> std::io::Error {
    std::io::Error::other("Invalid private native authentication channel")
}

#[cfg(unix)]
pub fn disable_core_dumps() -> std::io::Result<()> {
    let mut limit = libc::rlimit {
        rlim_cur: 0,
        rlim_max: 0,
    };
    if unsafe { libc::getrlimit(libc::RLIMIT_CORE, &mut limit) } != 0 {
        return Err(std::io::Error::last_os_error());
    }
    limit.rlim_cur = 0;
    if unsafe { libc::setrlimit(libc::RLIMIT_CORE, &limit) } != 0 {
        return Err(std::io::Error::last_os_error());
    }
    Ok(())
}
pub fn trusted_navigation(value: &str) -> bool {
    reqwest::Url::parse(value).is_ok_and(|url| {
        url.scheme() == "https"
            && matches!(
                url.host_str(),
                Some("login.live.com" | "account.live.com" | "login.microsoftonline.com")
            )
            && url.port().is_none()
            && url.username().is_empty()
            && url.password().is_none()
    })
}

pub fn trusted_bridge(value: &str) -> bool {
    trusted_navigation(value)
        && reqwest::Url::parse(value).is_ok_and(|url| url.host_str() == Some("login.live.com"))
}

pub fn finish_navigation(value: &str) -> bool {
    trusted_bridge(value)
        && reqwest::Url::parse(value).is_ok_and(|url| url.path() == "/ppsecure/post.srf")
}

pub fn valid_identity(version: u32, flow_id: &str, session_id: u64, sequence: u64) -> bool {
    version == VERSION
        && session_id == SESSION_ID
        && sequence > 0
        && sequence <= 9_007_199_254_740_991
        && uuid::Uuid::parse_str(flow_id).is_ok_and(|id| id.hyphenated().to_string() == flow_id)
}

pub fn encode<T: Serialize>(frame: &T) -> std::io::Result<Vec<u8>> {
    let bytes = serde_json::to_vec(frame).map_err(|_| invalid())?;
    if bytes.is_empty() || bytes.len() > MAX_FRAME_BYTES {
        return Err(invalid());
    }
    Ok(bytes)
}

pub fn verify_binding(binding: &HostBinding) -> std::io::Result<()> {
    use sha2::{Digest, Sha256};
    use std::io::Read;

    if binding.version != VERSION
        || !binding.executable.is_absolute()
        || binding.sha256.len() != 64
        || !binding
            .sha256
            .bytes()
            .all(|b| b.is_ascii_digit() || (b'a'..=b'f').contains(&b))
        || binding.executable.canonicalize()? != binding.executable
    {
        return Err(invalid());
    }
    let attributes = std::fs::symlink_metadata(&binding.executable)?;
    if !attributes.is_file() || attributes.len() > 128 * 1024 * 1024 {
        return Err(invalid());
    }
    #[cfg(unix)]
    {
        use std::os::unix::fs::MetadataExt;
        if attributes.uid() != unsafe { libc::geteuid() }
            || attributes.mode() & 0o022 != 0
            || attributes.mode() & 0o100 == 0
        {
            return Err(invalid());
        }
    }
    let mut file = std::fs::File::open(&binding.executable)?;
    let mut hash = Sha256::new();
    let mut buffer = [0; 16 * 1024];
    let mut total = 0u64;
    loop {
        let count = file.read(&mut buffer)?;
        if count == 0 {
            break;
        }
        total += count as u64;
        if total > 128 * 1024 * 1024 {
            return Err(invalid());
        }
        hash.update(&buffer[..count]);
    }
    let after = file.metadata()?;
    if total != attributes.len()
        || attributes.len() != after.len()
        || attributes.modified()? != after.modified()?
        || crate::staging::digest_hex(&hash.finalize()) != binding.sha256
    {
        return Err(invalid());
    }
    #[cfg(unix)]
    {
        use std::os::unix::fs::MetadataExt;
        let current = std::fs::symlink_metadata(&binding.executable)?;
        if attributes.ino() != after.ino()
            || attributes.dev() != after.dev()
            || current.ino() != after.ino()
            || current.dev() != after.dev()
            || current.len() != after.len()
            || current.modified()? != after.modified()?
            || after.uid() != attributes.uid()
            || after.mode() != attributes.mode()
            || current.uid() != attributes.uid()
            || current.mode() != attributes.mode()
        {
            return Err(invalid());
        }
    }
    Ok(())
}

pub async fn write<T: Serialize, W: AsyncWrite + Unpin>(
    writer: &mut W,
    frame: &T,
) -> std::io::Result<()> {
    let bytes = encode(frame)?;
    writer.write_u32(bytes.len() as u32).await?;
    writer.write_all(&bytes).await?;
    writer.flush().await
}

pub async fn read<T: serde::de::DeserializeOwned, R: AsyncRead + Unpin>(
    reader: &mut R,
) -> std::io::Result<T> {
    read_optional(reader).await?.ok_or_else(invalid)
}

pub async fn read_optional<T: serde::de::DeserializeOwned, R: AsyncRead + Unpin>(
    reader: &mut R,
) -> std::io::Result<Option<T>> {
    let mut prefix = [0; 4];
    if reader.read(&mut prefix[..1]).await.map_err(|_| invalid())? == 0 {
        return Ok(None);
    }
    reader
        .read_exact(&mut prefix[1..])
        .await
        .map_err(|_| invalid())?;
    let length = u32::from_be_bytes(prefix) as usize;
    if length == 0 || length > MAX_FRAME_BYTES {
        return Err(invalid());
    }
    let mut bytes = vec![0; length];
    reader.read_exact(&mut bytes).await.map_err(|_| invalid())?;
    serde_json::from_slice(&bytes)
        .map(Some)
        .map_err(|_| invalid())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn fixture_frames_are_strict_and_round_trip_without_debug_credentials() {
        let fixtures: serde_json::Value = serde_json::from_str(include_str!(concat!(
            env!("CARGO_MANIFEST_DIR"),
            "/../../docs/fixtures/native-auth-host-v1.json"
        )))
        .unwrap();
        for value in fixtures["commands"].as_array().unwrap() {
            let frame: CommandFrame = serde_json::from_value(value.clone()).unwrap();
            assert!(valid_identity(
                frame.version,
                &frame.flow_id,
                frame.session_id,
                frame.sequence
            ));
            assert_eq!(serde_json::to_value(frame).unwrap(), *value);
        }
        for value in fixtures["results"].as_array().unwrap() {
            let frame: ResultFrame = serde_json::from_value(value.clone()).unwrap();
            assert!(valid_identity(
                frame.version,
                &frame.flow_id,
                frame.session_id,
                frame.sequence
            ));
            assert_eq!(serde_json::to_value(frame).unwrap(), *value);
        }
        for value in fixtures["invalidResults"].as_array().unwrap() {
            assert!(serde_json::from_value::<ResultFrame>(value.clone()).is_err());
        }
    }

    #[tokio::test]
    async fn framed_payload_bounds_include_the_entire_envelope() {
        for length in [0, MAX_FRAME_BYTES + 1] {
            let bytes = (length as u32).to_be_bytes();
            assert!(read::<ResultFrame, _>(&mut bytes.as_slice()).await.is_err());
        }
        let frame = ResultFrame {
            version: VERSION,
            flow_id: uuid::Uuid::nil().to_string(),
            session_id: SESSION_ID,
            sequence: 1,
            reply_to: 1,
            message: HostResult::Ready,
        };
        let mut bytes = Vec::new();
        write(&mut bytes, &frame).await.unwrap();
        let decoded: ResultFrame = read(&mut bytes.as_slice()).await.unwrap();
        assert_eq!(decoded.flow_id, frame.flow_id);
        assert!(
            read::<ResultFrame, _>(&mut bytes[..bytes.len() - 1].as_ref())
                .await
                .is_err()
        );
        assert!(serde_json::from_str::<ResultFrame>(
            r#"{"version":1,"version":1,"flowID":"00000000-0000-0000-0000-000000000000","sessionID":1,"sequence":1,"replyTo":1,"message":{"kind":"ready"}}"#
        ).is_err());
    }

    #[tokio::test]
    async fn exact_whole_frame_threshold_accepts_boundary_and_rejects_one_extra_byte() {
        let mut frame = ResultFrame {
            version: VERSION,
            flow_id: uuid::Uuid::nil().to_string(),
            session_id: SESSION_ID,
            sequence: 1,
            reply_to: 1,
            message: HostResult::Da {
                property: IssuerProperty {
                    da_token: String::new(),
                    da_session_key: String::new(),
                    da_start_time: String::new(),
                    da_expires: String::new(),
                    sts_inline_flow_token: String::new(),
                    username: String::new(),
                    puid: String::new(),
                },
            },
        };
        let overhead = encode(&frame).unwrap().len();
        if let HostResult::Da { property } = &mut frame.message {
            property.da_token = "x".repeat(MAX_FRAME_BYTES - overhead);
        }
        assert_eq!(encode(&frame).unwrap().len(), MAX_FRAME_BYTES);
        let mut bytes = Vec::new();
        write(&mut bytes, &frame).await.unwrap();
        let decoded: ResultFrame = read(&mut bytes.as_slice()).await.unwrap();
        let HostResult::Da { property } = decoded.message else {
            panic!("bounded property required");
        };
        assert_eq!(property.da_token.len(), MAX_FRAME_BYTES - overhead);
        if let HostResult::Da { property } = &mut frame.message {
            property.da_token.push('x');
        }
        assert!(encode(&frame).is_err());
    }

    #[test]
    fn navigation_matches_existing_https_default_port_and_origin_rules() {
        assert!(trusted_navigation("https://login.live.com:443/"));
        assert!(finish_navigation(
            "https://login.live.com/ppsecure/post.srf?fixture=1"
        ));
        for url in [
            "http://login.live.com/",
            "https://login.live.com:8443/",
            "https://user@login.live.com/",
            "https://login.live.com.attacker.invalid/",
        ] {
            assert!(!trusted_navigation(url));
        }
        assert!(!trusted_bridge("https://account.live.com/"));
        assert!(!finish_navigation(
            "https://login.live.com/ppsecure/post.srf.attacker"
        ));
        assert!(!valid_identity(2, &uuid::Uuid::nil().to_string(), 1, 1));
        assert!(!valid_identity(1, "foreign", 1, 1));
        assert!(!valid_identity(1, &uuid::Uuid::nil().to_string(), 2, 1));
        assert!(!valid_identity(1, &uuid::Uuid::nil().to_string(), 1, 0));
    }
}
