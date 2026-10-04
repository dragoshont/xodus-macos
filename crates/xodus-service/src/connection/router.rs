use std::sync::Arc;

use tokio::io::AsyncReadExt;
use tokio_util::sync::CancellationToken;
use xodus::models::secrets::LegacyToken;
use xodus::tokens::TokenManager;

use crate::simple_context::SimpleContext;

pub async fn route(
    socket: tokio::net::UnixStream,
    token: CancellationToken,
    device_token: LegacyToken,
    tokens: Arc<TokenManager>,
) {
    let context = match SimpleContext::new(device_token, tokens) {
        Ok(context) => context,
        Err(_) => {
            tracing::error!("Runtime HTTP client initialization failed");
            return;
        }
    };
    if route_context(socket, token, context).await.is_err() {
        tracing::error!("Runtime connection failed");
    }
}

pub async fn route_management(
    socket: tokio::net::UnixStream,
    token: CancellationToken,
    tokens: Arc<TokenManager>,
) -> std::io::Result<()> {
    let context = SimpleContext::management(tokens)?;
    route_context(socket, token, context).await
}

async fn route_context(
    mut socket: tokio::net::UnixStream,
    token: CancellationToken,
    mut context: SimpleContext,
) -> std::io::Result<()> {
    let cred = socket.peer_cred().ok().and_then(|cred| cred.pid());
    tracing::debug!("Connection from pid {cred:?}");
    loop {
        let mut read_magic = [0; 4];
        let read = tokio::select! {
            biased;
            _ = token.cancelled() => return Ok(()),
            read = async {
                if context.management_profile {
                    tokio::time::timeout(std::time::Duration::from_secs(10),
                        socket.read_exact(&mut read_magic)).await
                        .map_err(|_| std::io::Error::new(std::io::ErrorKind::TimedOut,
                            "Private runtime header deadline expired"))?
                } else { socket.read_exact(&mut read_magic).await }
            } => read,
        };
        if let Err(err) = read {
            if err.kind() == std::io::ErrorKind::UnexpectedEof {
                return Ok(());
            }
            return Err(std::io::Error::new(
                err.kind(),
                "Runtime header read failed",
            ));
        }

        let magic = u32::from_le_bytes(read_magic);
        let res = tokio::select! {
            biased;
            _ = token.cancelled() => return Ok(()),
            res = async {
                match magic {
                    crate::XML_MAGIC => super::xml::handle(&mut socket, &mut context).await,
                    crate::PROTO_MAGIC => super::proto::handle(&mut socket, &mut context).await,
                    _ => Err(tokio::io::Error::new(
                        tokio::io::ErrorKind::InvalidData,
                        "Unknown runtime frame magic",
                    )),
                }
            } => res,
        };

        if let Err(err) = res {
            return Err(std::io::Error::new(err.kind(), "Runtime request failed"));
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::Duration;
    use tokio::io::AsyncWriteExt;
    use tokio::net::UnixStream;
    use xodus::models::soap::Timestamp;

    fn device_token() -> LegacyToken {
        LegacyToken {
            key_name: None,
            token: "transport-test-only".to_owned(),
            binary_secret: None,
            tpm_key: None,
            lifetime: Timestamp {
                id: None,
                created: "2000-01-01T00:00:00Z".to_owned(),
                expires: "2000-01-02T00:00:00Z".to_owned(),
            },
        }
    }

    fn start_route() -> (UnixStream, CancellationToken, tokio::task::JoinHandle<()>) {
        let (client, server) = UnixStream::pair().unwrap();
        let token = CancellationToken::new();
        let task = tokio::spawn(route(
            server,
            token.clone(),
            device_token(),
            Arc::new(TokenManager::with_memory()),
        ));
        (client, token, task)
    }

    async fn closed_without_reply(client: &mut UnixStream, task: tokio::task::JoinHandle<()>) {
        tokio::time::timeout(Duration::from_secs(2), task)
            .await
            .expect("runtime connection did not close")
            .expect("runtime connection panicked");
        let mut data = Vec::new();
        tokio::time::timeout(Duration::from_secs(2), client.read_to_end(&mut data))
            .await
            .expect("runtime socket stayed open")
            .expect("runtime socket read failed");
        assert!(
            data.is_empty(),
            "invalid request received a success-shaped response"
        );
    }

    #[tokio::test]
    async fn ping_round_trip_uses_only_memory_backed_context() {
        let (mut client, token, task) = start_route();
        let request = super::super::encode_message(crate::XML_MAGIC, 1, b"ping".to_vec()).unwrap();
        client.write_all(&request).await.unwrap();
        let expected = super::super::encode_message(crate::XML_MAGIC, 2, b"ping".to_vec()).unwrap();
        let mut response = vec![0; expected.len()];
        tokio::time::timeout(Duration::from_secs(2), client.read_exact(&mut response))
            .await
            .expect("ping timed out")
            .unwrap();
        assert_eq!(response, expected);
        token.cancel();
        closed_without_reply(&mut client, task).await;
    }

    #[tokio::test]
    async fn cancellation_interrupts_a_partial_magic_read() {
        let (mut client, token, task) = start_route();
        client.write_all(&[0x58, 0x53]).await.unwrap();
        tokio::task::yield_now().await;
        token.cancel();
        closed_without_reply(&mut client, task).await;
    }

    #[tokio::test]
    async fn cancellation_interrupts_a_partial_payload_read() {
        let (mut client, token, task) = start_route();
        let request = super::super::encode_message(crate::XML_MAGIC, 1, b"ping".to_vec()).unwrap();
        client.write_all(&request[..9]).await.unwrap();
        tokio::task::yield_now().await;
        token.cancel();
        closed_without_reply(&mut client, task).await;
    }

    #[tokio::test]
    async fn invalid_request_types_close_without_a_reply() {
        for message_type in [0, 2, 4, u16::MAX] {
            let (mut client, _, task) = start_route();
            let request =
                super::super::encode_message(crate::XML_MAGIC, message_type, Vec::new()).unwrap();
            client.write_all(&request).await.unwrap();
            closed_without_reply(&mut client, task).await;
        }
    }

    #[tokio::test]
    async fn malformed_xml_closes_without_a_success_shaped_reply() {
        for payload in [vec![0xff], b"<MSATokenRequest".to_vec()] {
            let (mut client, _, task) = start_route();
            let request = super::super::encode_message(crate::XML_MAGIC, 3, payload).unwrap();
            client.write_all(&request).await.unwrap();
            closed_without_reply(&mut client, task).await;
        }
    }

    #[tokio::test]
    async fn truncated_payload_closes_without_a_reply() {
        let (mut client, _, task) = start_route();
        let request = super::super::encode_message(crate::XML_MAGIC, 1, b"ping".to_vec()).unwrap();
        client.write_all(&request[..9]).await.unwrap();
        client.shutdown().await.unwrap();
        closed_without_reply(&mut client, task).await;
    }

    #[tokio::test]
    async fn unsupported_protobuf_closes_without_panicking() {
        let (mut client, _, task) = start_route();
        client
            .write_all(&crate::PROTO_MAGIC.to_le_bytes())
            .await
            .unwrap();
        closed_without_reply(&mut client, task).await;
    }

    #[tokio::test]
    async fn unknown_magic_closes_without_a_reply() {
        let (mut client, _, task) = start_route();
        client.write_all(&0u32.to_le_bytes()).await.unwrap();
        closed_without_reply(&mut client, task).await;
    }

    #[tokio::test]
    #[cfg(target_os = "macos")]
    async fn isolated_route_pings_without_credentials_and_refuses_signed_out_rps() {
        if !xodus::secrets::management_native_keychain_enabled() {
            return;
        }
        let (mut client, server) = UnixStream::pair().unwrap();
        let tokens = Arc::new(TokenManager::with_management_backend(Arc::new(
            xodus::tokens::backend::MemoryBackend::default(),
        )));
        let task = tokio::spawn(async move {
            assert!(
                route_management(server, CancellationToken::new(), tokens)
                    .await
                    .is_err()
            );
        });
        let ping = super::super::encode_message(crate::XML_MAGIC, 1, b"isolated".to_vec()).unwrap();
        client.write_all(&ping).await.unwrap();
        let expected =
            super::super::encode_message(crate::XML_MAGIC, 2, b"isolated".to_vec()).unwrap();
        let mut reply = vec![0; expected.len()];
        tokio::time::timeout(Duration::from_secs(2), client.read_exact(&mut reply))
            .await
            .unwrap()
            .unwrap();
        assert_eq!(reply, expected);
        let request = super::super::encode_message(crate::XML_MAGIC, 3,
            b"<MSATokenRequest><ClientId>000000004424da1f</ClientId><MSAFullTrust>true</MSAFullTrust></MSATokenRequest>".to_vec()).unwrap();
        client.write_all(&request).await.unwrap();
        closed_without_reply(&mut client, task).await;
    }
}
