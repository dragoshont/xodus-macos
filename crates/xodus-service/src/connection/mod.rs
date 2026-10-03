pub mod proto;
pub mod router;
pub mod xml;

use std::io::{Error, ErrorKind, Result};
use xodus::proto::xodus::XodusMessageType;

pub fn request_type(message_type: u16) -> Result<XodusMessageType> {
    match XodusMessageType::try_from(i32::from(message_type)) {
        Ok(message_type @ (XodusMessageType::Ping | XodusMessageType::MsaTokenRequest)) => {
            Ok(message_type)
        }
        _ => Err(Error::new(
            ErrorKind::InvalidData,
            "Unsupported runtime request type",
        )),
    }
}

pub fn encode_message(magic: u32, msg_type: u16, message_buffer: Vec<u8>) -> Result<Vec<u8>> {
    let size = u16::try_from(message_buffer.len()).map_err(|_| {
        Error::new(
            ErrorKind::InvalidInput,
            "Runtime payload exceeds frame limit",
        )
    })?;
    let mut buffer = Vec::with_capacity(8 + message_buffer.len());
    buffer.extend(magic.to_le_bytes());
    buffer.extend(msg_type.to_le_bytes());
    buffer.extend(size.to_le_bytes());
    buffer.extend(message_buffer);

    Ok(buffer)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn frame_header_matches_the_little_endian_wire_format() {
        assert_eq!(
            encode_message(crate::XML_MAGIC, 2, vec![0x41, 0x42]).unwrap(),
            [0x58, 0x53, 0x44, 0x58, 2, 0, 2, 0, 0x41, 0x42]
        );
    }

    #[test]
    fn empty_payload_has_a_complete_header() {
        let frame = encode_message(crate::XML_MAGIC, 2, Vec::new()).unwrap();
        assert_eq!(frame.len(), 8);
        assert_eq!(&frame[6..8], &[0, 0]);
    }

    #[test]
    fn maximum_payload_length_is_encoded_without_truncation() {
        let frame = encode_message(crate::XML_MAGIC, 2, vec![0x41; usize::from(u16::MAX)]).unwrap();
        assert_eq!(frame.len(), 8 + usize::from(u16::MAX));
        assert_eq!(&frame[6..8], &u16::MAX.to_le_bytes());
    }

    #[test]
    fn oversized_payload_is_rejected_instead_of_wrapping() {
        let error =
            encode_message(crate::XML_MAGIC, 2, vec![0x41; usize::from(u16::MAX) + 1]).unwrap_err();
        assert_eq!(error.kind(), ErrorKind::InvalidInput);
    }

    #[test]
    fn only_defined_request_types_are_accepted() {
        assert_eq!(request_type(1).unwrap(), XodusMessageType::Ping);
        assert_eq!(request_type(3).unwrap(), XodusMessageType::MsaTokenRequest);
        for message_type in [0, 2, 4, 5, u16::MAX] {
            assert_eq!(
                request_type(message_type).unwrap_err().kind(),
                ErrorKind::InvalidData
            );
        }
    }
}
