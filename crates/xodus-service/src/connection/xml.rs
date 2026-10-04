use tokio::io::{AsyncReadExt, AsyncWriteExt};
use xodus::models::xgameruntime::xuser::MSATokenRequest;
use xodus::proto::xodus::XodusMessageType;

use crate::XML_MAGIC;
use crate::simple_context::SimpleContext;

fn parse_request(buffer: &[u8]) -> Result<MSATokenRequest, tokio::io::Error> {
    use quick_xml::events::Event;
    let invalid = || {
        tokio::io::Error::new(
            tokio::io::ErrorKind::InvalidData,
            "Runtime MSA request XML has unsupported structure",
        )
    };
    if buffer.len() > 4096 {
        return Err(invalid());
    }
    let input = std::str::from_utf8(buffer).map_err(|_| invalid())?;
    let mut reader = quick_xml::Reader::from_str(input);
    let mut depth = 0;
    let mut started = false;
    let mut ended = false;
    let mut fields = std::collections::BTreeSet::new();
    loop {
        match reader.read_event().map_err(|_| invalid())? {
            Event::Start(node) => {
                if ended || node.attributes().next().is_some() {
                    return Err(invalid());
                }
                if depth == 0 {
                    if started || node.name().as_ref() != "MSATokenRequest" {
                        return Err(invalid());
                    }
                    started = true;
                } else if depth == 1 {
                    let name = match node.name().as_ref() {
                        "ClientId" => "client",
                        "AllowUi" => "ui",
                        "MsaFullTrust" | "MSAFullTrust" => "trust",
                        _ => return Err(invalid()),
                    };
                    if !fields.insert(name) {
                        return Err(invalid());
                    }
                } else {
                    return Err(invalid());
                }
                depth += 1;
            }
            Event::End(_) if depth > 0 => {
                depth -= 1;
                if depth == 0 {
                    ended = true;
                }
            }
            Event::Text(text) => {
                if depth != 2 && !text.as_ref().bytes().all(|byte| byte.is_ascii_whitespace()) {
                    return Err(invalid());
                }
            }
            Event::Decl(_) if !started => {}
            Event::Eof => break,
            _ => return Err(invalid()),
        }
    }
    if !started || !ended || depth != 0 || !fields.contains("client") {
        return Err(invalid());
    }
    quick_xml::de::from_str(input).map_err(|_| invalid())
}

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
            let req = parse_request(&buffer)?;
            let payload = crate::rps::exchange(context, req).await?;
            Ok(quick_xml::se::to_string(&payload)?.into_bytes())
        }

        _ => Err("Unimplemented".into()),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn strict_runtime_xml_accepts_only_the_flat_known_request() {
        let request = parse_request(
            b"<MSATokenRequest><ClientId>000000004424da1f</ClientId><AllowUi>false</AllowUi><MsaFullTrust>true</MsaFullTrust></MSATokenRequest>").unwrap();
        assert_eq!(request.client_id, "000000004424da1f");
        assert!(request.msa_full_trust && !request.allow_ui);
        assert!(parse_request(b"<MSATokenRequest><ClientId>000000004424da1f</ClientId><MSAFullTrust>true</MSAFullTrust></MSATokenRequest>").unwrap().msa_full_trust);
        for input in [
            "<Other><ClientId>000000004424da1f</ClientId></Other>",
            "<MSATokenRequest unknown=\"1\"><ClientId>000000004424da1f</ClientId></MSATokenRequest>",
            "<MSATokenRequest><ClientId><Nested>000000004424da1f</Nested></ClientId></MSATokenRequest>",
            "<MSATokenRequest><ClientId>000000004424da1f</ClientId><ClientId>000000004424da1f</ClientId></MSATokenRequest>",
            "<MSATokenRequest><ClientId>000000004424da1f</ClientId><MsaFullTrust>true</MsaFullTrust><MSAFullTrust>true</MSAFullTrust></MSATokenRequest>",
            "<!DOCTYPE MSATokenRequest [<!ENTITY account \"000000004424da1f\">]><MSATokenRequest><ClientId>&account;</ClientId></MSATokenRequest>",
        ] {
            assert!(parse_request(input.as_bytes()).is_err());
        }
        assert!(parse_request(&vec![b' '; 4097]).is_err());
    }
}
