use std::time::Duration;

use serde::de::DeserializeOwned;

pub const AUTH_RESPONSE_LIMIT: usize = 1024 * 1024;
pub const PACKAGE_RESPONSE_LIMIT: usize = 4 * 1024 * 1024;
pub const LICENSE_RESPONSE_LIMIT: usize = 4 * 1024 * 1024;
const REQUEST_DEADLINE: Duration = Duration::from_secs(30);

#[derive(Debug, thiserror::Error, PartialEq, Eq)]
pub enum ProviderResponseError {
    #[error("Provider request failed")]
    RequestFailed,
    #[error("Provider request exceeded its deadline")]
    Deadline,
    #[error("Provider rejected the request with HTTP status {status}")]
    HttpRejected { status: u16 },
    #[error("Provider response exceeds the {limit}-byte limit")]
    TooLarge { limit: usize },
    #[error("Provider response is not valid for the requested JSON type")]
    InvalidJson,
    #[error("Provider response limits are invalid")]
    InvalidLimits,
}

pub struct ProviderJson<T> {
    status: reqwest::StatusCode,
    body: T,
}

impl<T> ProviderJson<T> {
    pub fn require_success(self) -> Result<T, ProviderResponseError> {
        if !self.status.is_success() {
            return Err(ProviderResponseError::HttpRejected {
                status: self.status.as_u16(),
            });
        }
        Ok(self.body)
    }

    pub fn into_parts(self) -> (reqwest::StatusCode, T) {
        (self.status, self.body)
    }
}

pub async fn request_json<T: DeserializeOwned>(
    request: reqwest::RequestBuilder,
    max_bytes: usize,
) -> Result<ProviderJson<T>, ProviderResponseError> {
    request_json_with_deadline(request, max_bytes, REQUEST_DEADLINE).await
}

async fn request_json_with_deadline<T: DeserializeOwned>(
    request: reqwest::RequestBuilder,
    max_bytes: usize,
    deadline: Duration,
) -> Result<ProviderJson<T>, ProviderResponseError> {
    if max_bytes == 0 || deadline.is_zero() {
        return Err(ProviderResponseError::InvalidLimits);
    }
    tokio::time::timeout(deadline, async {
        let mut response = request.send().await.map_err(request_error)?;
        let status = response.status();
        if response
            .content_length()
            .is_some_and(|length| length > max_bytes as u64)
        {
            return Err(ProviderResponseError::TooLarge { limit: max_bytes });
        }
        let mut bytes = Vec::new();
        while let Some(chunk) = response.chunk().await.map_err(request_error)? {
            if bytes
                .len()
                .checked_add(chunk.len())
                .is_none_or(|length| length > max_bytes)
            {
                return Err(ProviderResponseError::TooLarge { limit: max_bytes });
            }
            bytes.extend_from_slice(&chunk);
        }
        let body = serde_json::from_slice(&bytes).map_err(|_| {
            if status.is_success() {
                ProviderResponseError::InvalidJson
            } else {
                ProviderResponseError::HttpRejected {
                    status: status.as_u16(),
                }
            }
        })?;
        Ok(ProviderJson { status, body })
    })
    .await
    .map_err(|_| ProviderResponseError::Deadline)?
}

fn request_error(error: reqwest::Error) -> ProviderResponseError {
    if error.is_timeout() {
        ProviderResponseError::Deadline
    } else {
        ProviderResponseError::RequestFailed
    }
}

#[cfg(test)]
mod management_tests {
    use super::*;
    use tokio::io::{AsyncReadExt, AsyncWriteExt};
    use tokio::net::TcpListener;

    async fn peer(bytes: Vec<u8>) -> (reqwest::RequestBuilder, tokio::task::JoinHandle<()>) {
        let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
        let address = listener.local_addr().unwrap();
        let task = tokio::spawn(async move {
            let (mut socket, _) = listener.accept().await.unwrap();
            let mut request = [0; 4096];
            assert!(socket.read(&mut request).await.unwrap() > 0);
            socket.write_all(&bytes).await.unwrap();
        });
        let request = reqwest::Client::builder()
            .no_proxy()
            .redirect(reqwest::redirect::Policy::none())
            .build()
            .unwrap()
            .get(format!("http://{address}/synthetic"));
        (request, task)
    }

    #[tokio::test]
    async fn management_provider_json_accepts_exact_byte_boundary_and_preserves_body() {
        let payload = br#"{"value":"fixture-only"}"#;
        let bytes = [
            format!(
                "HTTP/1.1 200 OK\r\nContent-Length: {}\r\nConnection: close\r\n\r\n",
                payload.len()
            )
            .into_bytes(),
            payload.to_vec(),
        ]
        .concat();
        let (request, task) = peer(bytes).await;
        let response = request_json::<serde_json::Value>(request, payload.len())
            .await
            .unwrap()
            .require_success()
            .unwrap();
        assert_eq!(response["value"], "fixture-only");
        task.await.unwrap();
    }

    #[tokio::test]
    async fn management_provider_json_rejects_declared_oversize_without_reading_body() {
        let (request, task) = peer(
            b"HTTP/1.1 200 OK\r\nContent-Length: 1000000000\r\nConnection: close\r\n\r\n".to_vec(),
        )
        .await;
        assert!(matches!(
            request_json::<serde_json::Value>(request, 64).await,
            Err(ProviderResponseError::TooLarge { limit: 64 })
        ));
        task.await.unwrap();
    }

    #[tokio::test]
    async fn management_provider_json_bounds_chunked_bodies_without_content_length() {
        let payload = br#"{"value":"fixture-only"}"#;
        let bytes = [
            b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n".to_vec(),
            format!("{:x}\r\n", payload.len()).into_bytes(),
            payload.to_vec(),
            b"\r\n0\r\n\r\n".to_vec(),
        ]
        .concat();
        let (request, task) = peer(bytes.clone()).await;
        assert!(matches!(
            request_json::<serde_json::Value>(request, payload.len() - 1).await,
            Err(ProviderResponseError::TooLarge { .. })
        ));
        task.await.unwrap();
        let (request, task) = peer(bytes).await;
        assert!(
            request_json::<serde_json::Value>(request, payload.len())
                .await
                .unwrap()
                .require_success()
                .is_ok()
        );
        task.await.unwrap();
    }

    #[tokio::test]
    async fn management_provider_json_preserves_http_failures_for_json_and_non_json() {
        for payload in [b"{}".as_slice(), b"fixture-only".as_slice()] {
            let bytes = [
                format!(
                    "HTTP/1.1 403 Forbidden\r\nContent-Length: {}\r\nConnection: close\r\n\r\n",
                    payload.len()
                )
                .into_bytes(),
                payload.to_vec(),
            ]
            .concat();
            let (request, task) = peer(bytes).await;
            let result = request_json::<serde_json::Value>(request, 64)
                .await
                .and_then(ProviderJson::require_success);
            assert!(matches!(
                result,
                Err(ProviderResponseError::HttpRejected { status: 403 })
            ));
            task.await.unwrap();
        }
    }

    #[tokio::test]
    async fn management_provider_json_sanitizes_malformed_and_truncated_body_failures() {
        for (declared_length, payload) in [(12, b"fixture-only".as_slice()), (64, b"{".as_slice())]
        {
            let bytes = [
                format!(
                    "HTTP/1.1 200 OK\r\nContent-Length: {declared_length}\r\nConnection: close\r\n\r\n"
                )
                .into_bytes(),
                payload.to_vec(),
            ]
            .concat();
            let (request, task) = peer(bytes).await;
            let error = match request_json::<serde_json::Value>(request, 64).await {
                Ok(_) => panic!("Malformed/truncated provider body succeeded"),
                Err(error) => error,
            };
            assert!(matches!(
                error,
                ProviderResponseError::InvalidJson | ProviderResponseError::RequestFailed
            ));
            assert!(!format!("{error:?} {error}").contains("fixture-only"));
            task.await.unwrap();
        }
    }

    #[tokio::test]
    async fn management_provider_json_rejects_valid_json_with_incompatible_typed_shape() {
        let payload = br#"{"Token":"fixture-only"}"#;
        let bytes = [
            format!(
                "HTTP/1.1 200 OK\r\nContent-Length: {}\r\nConnection: close\r\n\r\n",
                payload.len()
            )
            .into_bytes(),
            payload.to_vec(),
        ]
        .concat();
        let (request, task) = peer(bytes).await;
        assert!(matches!(
            request_json::<crate::models::xbox::XstsResponse>(request, 64).await,
            Err(ProviderResponseError::InvalidJson)
        ));
        task.await.unwrap();
    }

    #[tokio::test]
    async fn management_provider_json_preserves_structured_entitlement_denial_for_classification() {
        let payload = br#"{"satisfactionFailure":{"code":1,"description":"fixture-only"}}"#;
        let bytes = [
            format!(
                "HTTP/1.1 403 Forbidden\r\nContent-Length: {}\r\nConnection: close\r\n\r\n",
                payload.len()
            )
            .into_bytes(),
            payload.to_vec(),
        ]
        .concat();
        let (request, task) = peer(bytes).await;
        let response =
            request_json::<crate::models::licensing::LicenseContentResponse>(request, 128)
                .await
                .unwrap();
        let (status, body) = response.into_parts();
        assert_eq!(status, reqwest::StatusCode::FORBIDDEN);
        assert!(matches!(
            body,
            crate::models::licensing::LicenseContentResponse::SatisfactionFailure { .. }
        ));
        task.await.unwrap();
    }

    #[tokio::test]
    async fn management_provider_json_deadline_covers_headers_and_partial_body() {
        for send_headers in [false, true] {
            let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
            let address = listener.local_addr().unwrap();
            let task = tokio::spawn(async move {
                let (mut socket, _) = listener.accept().await.unwrap();
                let mut request = [0; 4096];
                socket.read(&mut request).await.unwrap();
                if send_headers {
                    socket
                        .write_all(b"HTTP/1.1 200 OK\r\nContent-Length: 64\r\n\r\n{")
                        .await
                        .unwrap();
                }
                std::future::pending::<()>().await;
            });
            let request = reqwest::Client::builder()
                .no_proxy()
                .build()
                .unwrap()
                .get(format!("http://{address}/synthetic"));
            assert!(matches!(
                request_json_with_deadline::<serde_json::Value>(
                    request,
                    64,
                    Duration::from_millis(100)
                )
                .await,
                Err(ProviderResponseError::Deadline)
            ));
            task.abort();
            assert!(task.await.unwrap_err().is_cancelled());
        }
    }

    #[tokio::test]
    async fn management_provider_json_rejects_invalid_limits_before_network() {
        for (limit, deadline) in [(0, REQUEST_DEADLINE), (64, Duration::ZERO)] {
            assert!(matches!(
                request_json_with_deadline::<serde_json::Value>(
                    reqwest::Client::new().get("http://127.0.0.1:1/synthetic"),
                    limit,
                    deadline,
                )
                .await,
                Err(ProviderResponseError::InvalidLimits)
            ));
        }
    }

    #[tokio::test]
    async fn management_provider_json_caller_abort_closes_partial_body() {
        let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
        let address = listener.local_addr().unwrap();
        let (started, ready) = tokio::sync::oneshot::channel();
        let peer = tokio::spawn(async move {
            let (mut socket, _) = listener.accept().await.unwrap();
            let mut request = [0; 4096];
            socket.read(&mut request).await.unwrap();
            socket
                .write_all(b"HTTP/1.1 200 OK\r\nContent-Length: 64\r\n\r\n{")
                .await
                .unwrap();
            started.send(()).unwrap();
            let mut next = [0; 1];
            match tokio::time::timeout(Duration::from_secs(2), socket.read(&mut next))
                .await
                .unwrap()
            {
                Ok(0) => {}
                Err(error) if error.kind() == std::io::ErrorKind::ConnectionReset => {}
                result => panic!("Cancelled provider did not close its peer: {result:?}"),
            }
        });
        let request = reqwest::Client::builder()
            .no_proxy()
            .build()
            .unwrap()
            .get(format!("http://{address}/synthetic"));
        let caller = tokio::spawn(request_json::<serde_json::Value>(request, 64));
        ready.await.unwrap();
        caller.abort();
        assert!(matches!(caller.await, Err(error) if error.is_cancelled()));
        peer.await.unwrap();
    }
}
