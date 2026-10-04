#![cfg(unix)]

pub mod connection;
pub mod isolated;
mod rps;
pub mod simple_context;

pub const XML_MAGIC: u32 = 0x58445358;
pub const PROTO_MAGIC: u32 = 0x58445350;
