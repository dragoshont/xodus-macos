use crate::simple_context::SimpleContext;

pub async fn handle(
    _socket: &mut tokio::net::UnixStream,
    _context: &mut SimpleContext,
) -> tokio::io::Result<()> {
    Err(tokio::io::Error::new(
        tokio::io::ErrorKind::Unsupported,
        "Protobuf runtime requests are not supported",
    ))
}
