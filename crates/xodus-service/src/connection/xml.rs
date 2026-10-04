use tokio::io::{AsyncReadExt, AsyncWriteExt};
use xodus::models::xgameruntime::xuser::MSATokenRequest;
use xodus::proto::xodus::XodusMessageType;

use crate::XML_MAGIC;
use crate::simple_context::SimpleContext;

pub async fn handle(
    socket: &mut tokio::net::UnixStream,
    context: &mut SimpleContext,
) -> tokio::io::Result<()> {
    tracing::debug!("Parsing XML");
    let read = async {
        let message_type = super::request_type(socket.read_u16_le().await?)?;
        let message_size = socket.read_u16_le().await?;
        let mut buffer = vec![0; message_size as usize];
        socket.read_exact(&mut buffer).await?;
        Ok::<_, tokio::io::Error>((message_type, buffer))
    };
    let (message_type, buffer) = if context.management_profile {
        tokio::time::timeout(std::time::Duration::from_secs(10), read)
            .await
            .map_err(|_| {
                tokio::io::Error::new(
                    tokio::io::ErrorKind::TimedOut,
                    "Private runtime payload deadline expired",
                )
            })??
    } else {
        read.await?
    };
    tracing::debug!("Read buffer");
    let out_buf = parse_message(context, message_type, buffer)
        .await
        .map_err(|_| {
            tokio::io::Error::new(tokio::io::ErrorKind::InvalidData, "Runtime request failed")
        })?;

    let data = super::encode_message(XML_MAGIC, message_type as u16 + 1, out_buf)?;
    if context.management_profile {
        tokio::time::timeout(std::time::Duration::from_secs(2), socket.write_all(&data))
            .await
            .map_err(|_| {
                tokio::io::Error::new(
                    tokio::io::ErrorKind::TimedOut,
                    "Private runtime response deadline expired",
                )
            })?
    } else {
        socket.write_all(&data).await
    }
}

pub async fn parse_message(
    context: &mut SimpleContext,
    message_type: XodusMessageType,
    buffer: Vec<u8>,
) -> Result<Vec<u8>, Box<dyn std::error::Error + Send + Sync>> {
    match message_type {
        XodusMessageType::Ping => Ok(buffer),
        XodusMessageType::MsaTokenRequest => {
            let string_buf = std::str::from_utf8(&buffer)?;
            let req = quick_xml::de::from_str::<MSATokenRequest>(string_buf)?;
            let payload = crate::rps::exchange(context, req).await?;
            Ok(quick_xml::se::to_string(&payload)?.into_bytes())
        }
        _ => Err("Unimplemented".into()),
    }
}
