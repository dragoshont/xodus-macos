use std::sync::Arc;

use tokio::io::AsyncReadExt;
use tokio_util::sync::CancellationToken;
use xodus::models::secrets::LegacyToken;
use xodus::tokens::TokenManager;

use crate::simple_context::SimpleContext;

pub async fn route(
    mut socket: tokio::net::UnixStream,
    token: CancellationToken,
    device_token: LegacyToken,
    tokens: Arc<TokenManager>,
) {
    let cred = socket.peer_cred().ok().and_then(|cred| cred.pid());
    tracing::debug!("Connection from pid {cred:?}");

    let mut context = SimpleContext::new(device_token, tokens);
    loop {
        let mut read_magic = [0; 4];
        let read = tokio::select! {
            biased;
            _ = token.cancelled() => return,
            read = socket.read_exact(&mut read_magic) => read,
        };
        if let Err(err) = read {
            tracing::error!("Failed to read magic: {err:?}");
            return;
        }

        let magic = u32::from_le_bytes(read_magic);
        let res = tokio::select! {
            biased;
            _ = token.cancelled() => return,
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
            tracing::error!("There was an error handling the message: {err}");
            return;
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
}
