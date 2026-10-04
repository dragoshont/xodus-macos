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
    Da {
        property: IssuerProperty,
    },
    Closed {
        disposition: Disposition,
    },
    Cancelled,
    Failed {
        reason: HostFailure,
    },
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
        && uuid::Uuid::parse_str(flow_id)
            .is_ok_and(|id| id.hyphenated().to_string() == flow_id)
}

pub fn encode<T: Serialize>(frame: &T) -> std::io::Result<Vec<u8>> {
    let bytes = serde_json::to_vec(frame).map_err(|_| invalid())?;
    if bytes.is_empty() || bytes.len() > MAX_FRAME_BYTES {
        return Err(invalid());
    }
    Ok(bytes)
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
    let length = reader.read_u32().await.map_err(|_| invalid())? as usize;
    if length == 0 || length > MAX_FRAME_BYTES {
        return Err(invalid());
    }
    let mut bytes = vec![0; length];
    reader.read_exact(&mut bytes).await.map_err(|_| invalid())?;
    serde_json::from_slice(&bytes).map_err(|_| invalid())
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
        assert!(read::<ResultFrame, _>(&mut bytes[..bytes.len() - 1].as_ref()).await.is_err());
        assert!(serde_json::from_str::<ResultFrame>(
            r#"{"version":1,"version":1,"flowID":"00000000-0000-0000-0000-000000000000","sessionID":1,"sequence":1,"replyTo":1,"message":{"kind":"ready"}}"#
        ).is_err());
    }

    #[test]
    fn navigation_matches_existing_https_default_port_and_origin_rules() {
        assert!(trusted_navigation("https://login.live.com:443/"));
        assert!(finish_navigation("https://login.live.com/ppsecure/post.srf?fixture=1"));
        for url in [
            "http://login.live.com/",
            "https://login.live.com:8443/",
            "https://user@login.live.com/",
            "https://login.live.com.attacker.invalid/",
        ] {
            assert!(!trusted_navigation(url));
        }
        assert!(!trusted_bridge("https://account.live.com/"));
        assert!(!finish_navigation("https://login.live.com/ppsecure/post.srf.attacker"));
        assert!(!valid_identity(2, &uuid::Uuid::nil().to_string(), 1, 1));
        assert!(!valid_identity(1, "foreign", 1, 1));
        assert!(!valid_identity(1, &uuid::Uuid::nil().to_string(), 2, 1));
        assert!(!valid_identity(1, &uuid::Uuid::nil().to_string(), 1, 0));
    }
}
