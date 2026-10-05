use tokio::io::{AsyncBufRead, AsyncBufReadExt};

use crate::state::identifier_valid;
use crate::wire::*;

pub enum Line {
    Frame(Vec<u8>),
    Oversized,
    Truncated,
    Eof,
}

pub async fn read_line<R: AsyncBufRead + Unpin>(reader: &mut R) -> std::io::Result<Line> {
    let mut line = Vec::new();
    let mut oversized = false;
    loop {
        let buffer = reader.fill_buf().await?;
        if buffer.is_empty() {
            return Ok(if oversized {
                Line::Oversized
            } else if line.is_empty() {
                Line::Eof
            } else {
                Line::Truncated
            });
        }
        let end = buffer.iter().position(|byte| *byte == b'\n');
        let length = end.map_or(buffer.len(), |position| position + 1);
        let content_length = length - usize::from(end.is_some());
        if !oversized {
            if line.len().saturating_add(content_length) > MAX_LINE_BYTES {
                oversized = true;
                line.clear();
            } else {
                line.extend_from_slice(&buffer[..content_length]);
            }
        }
        reader.consume(length);
        if end.is_some() {
            return Ok(if oversized {
                Line::Oversized
            } else {
                Line::Frame(line)
            });
        }
    }
}

struct UniqueJson(serde_json::Value);

impl<'de> serde::Deserialize<'de> for UniqueJson {
    fn deserialize<D: serde::Deserializer<'de>>(deserializer: D) -> Result<Self, D::Error> {
        struct Visitor;
        impl<'de> serde::de::Visitor<'de> for Visitor {
            type Value = UniqueJson;
            fn expecting(&self, formatter: &mut std::fmt::Formatter) -> std::fmt::Result {
                formatter.write_str("JSON without duplicate keys")
            }
            fn visit_bool<E: serde::de::Error>(self, value: bool) -> Result<Self::Value, E> {
                Ok(UniqueJson(value.into()))
            }
            fn visit_i64<E: serde::de::Error>(self, value: i64) -> Result<Self::Value, E> {
                Ok(UniqueJson(value.into()))
            }
            fn visit_u64<E: serde::de::Error>(self, value: u64) -> Result<Self::Value, E> {
                Ok(UniqueJson(value.into()))
            }
            fn visit_f64<E: serde::de::Error>(self, value: f64) -> Result<Self::Value, E> {
                serde_json::Number::from_f64(value)
                    .map(|number| UniqueJson(number.into()))
                    .ok_or_else(|| E::custom("non-finite number"))
            }
            fn visit_str<E: serde::de::Error>(self, value: &str) -> Result<Self::Value, E> {
                Ok(UniqueJson(value.into()))
            }
            fn visit_string<E: serde::de::Error>(self, value: String) -> Result<Self::Value, E> {
                Ok(UniqueJson(value.into()))
            }
            fn visit_unit<E: serde::de::Error>(self) -> Result<Self::Value, E> {
                Ok(UniqueJson(serde_json::Value::Null))
            }
            fn visit_seq<A: serde::de::SeqAccess<'de>>(
                self,
                mut sequence: A,
            ) -> Result<Self::Value, A::Error> {
                let mut values = Vec::new();
                while let Some(UniqueJson(value)) = sequence.next_element()? {
                    values.push(value);
                }
                Ok(UniqueJson(values.into()))
            }
            fn visit_map<A: serde::de::MapAccess<'de>>(
                self,
                mut map: A,
            ) -> Result<Self::Value, A::Error> {
                let mut values = serde_json::Map::new();
                while let Some(key) = map.next_key::<String>()? {
                    if values.contains_key(&key) {
                        return Err(serde::de::Error::custom("duplicate JSON property"));
                    }
                    let UniqueJson(value) = map.next_value()?;
                    values.insert(key, value);
                }
                Ok(UniqueJson(values.into()))
            }
        }
        deserializer.deserialize_any(Visitor)
    }
}

pub fn parse(bytes: &[u8]) -> Result<Request, WireError> {
    let UniqueJson(value) = serde_json::from_slice(bytes).map_err(|_| invalid())?;
    if value.get("protocol") != Some(&serde_json::json!({"major":1,"minor":0})) {
        return Err(WireError::new(
            ErrorCode::ProtocolMismatch,
            "Only management protocol 1.0 is supported. Update the app/backend pairing.",
            false,
        ));
    }
    let command = value
        .get("command")
        .and_then(serde_json::Value::as_str)
        .ok_or_else(invalid)?;
    if !COMMANDS.contains(&command) {
        return Err(WireError::new(
            ErrorCode::UnknownCommand,
            "This command is not part of management protocol 1.0.",
            false,
        ));
    }
    if matches!(
        command,
        "catalog.search" | "catalog.discover" | "catalog.query"
    ) && value["params"].get("cursor").is_none()
    {
        return Err(invalid());
    }
    let request: Request = serde_json::from_slice(bytes).map_err(|_| invalid())?;
    if !identifier_valid(&request.request_id) || request.request_id.starts_with("invalid-") {
        return Err(invalid());
    }
    validate_operation(&request.operation)?;
    Ok(request)
}

pub const COMMANDS: &[&str] = &[
    "hello",
    "auth.begin",
    "auth.cancel",
    "auth.status",
    "auth.logout",
    "auth.verify",
    "inventory.snapshot",
    "catalog.search",
    "catalog.discover",
    "catalog.query",
    "product.detail",
    "install.plan",
    "jobs.enqueue",
    "jobs.pause",
    "jobs.resume",
    "jobs.cancel",
    "jobs.retry",
    "jobs.snapshot",
    "events.replay",
    "installed.snapshot",
    "installed.inspect",
    "game.launch",
    "game.update",
    "game.rollback",
    "game.remove",
    "diagnostics.export",
];

pub fn invalid() -> WireError {
    WireError::new(
        ErrorCode::InvalidRequest,
        "Malformed request or invalid parameters. Consult the strict protocol 1.0 schema.",
        false,
    )
}

pub fn product_valid(product: &ProductParams) -> bool {
    product.product_id.len() == 12
        && product
            .product_id
            .bytes()
            .all(|byte| byte.is_ascii_uppercase() || byte.is_ascii_digit())
        && locale_valid(&product.market, &product.language)
}

fn locale_valid(market: &str, language: &str) -> bool {
    let pieces: Vec<_> = language.split('-').collect();
    market.len() == 2
        && market.bytes().all(|byte| byte.is_ascii_uppercase())
        && (pieces.len() == 1 || pieces.len() == 2)
        && (pieces[0].len() == 2 || pieces[0].len() == 3)
        && pieces[0].bytes().all(|byte| byte.is_ascii_lowercase())
        && (pieces.len() == 1
            || (pieces[1].len() == 2 && pieces[1].bytes().all(|byte| byte.is_ascii_uppercase())))
}

pub fn validate_operation(operation: &Operation) -> Result<(), WireError> {
    let text = |value: &str| {
        !value.is_empty() && value.chars().count() <= 1024 && !value.chars().any(char::is_control)
    };
    let valid = match operation {
        Operation::Hello(params) => {
            text(&params.client)
                && text(&params.client_version)
                && params.required_capabilities.len() <= 32
                && params
                    .required_capabilities
                    .iter()
                    .all(|value| identifier_valid(value))
        }
        Operation::AuthBegin(params) => params.account_scope == "default",
        Operation::AuthCancel(params) => identifier_valid(&params.flow_id),
        Operation::AuthVerify(params) => uuid::Uuid::parse_str(&params.content_id)
            .is_ok_and(|id| !id.is_nil() && id.to_string() == params.content_id),
        Operation::InventorySnapshot(params) => {
            params.account_scope == "default" && locale_valid(&params.market, "en")
        }
        Operation::ProductDetail(params) => product_valid(params),
        Operation::CatalogSearch(params) => {
            params.query.chars().count() <= 256
                && !params.query.chars().any(char::is_control)
                && locale_valid(&params.market, &params.language)
                && (1..=100).contains(&params.limit)
                && params
                    .cursor
                    .as_ref()
                    .is_none_or(|value| identifier_valid(value))
        }
        Operation::CatalogDiscover(params) => {
            locale_valid(&params.market, &params.language)
                && (1..=16).contains(&params.limit)
                && params
                    .cursor
                    .as_ref()
                    .is_none_or(|value| identifier_valid(value))
        }
        Operation::CatalogQuery(params) => {
            !params.query.trim().is_empty()
                && params.query.chars().count() <= 256
                && !params.query.chars().any(char::is_control)
                && locale_valid(&params.market, &params.language)
                && (1..=16).contains(&params.limit)
                && params.cursor.as_ref().is_none_or(|cursor| {
                    cursor.len() <= 16384
                        && cursor.strip_prefix("q1-").is_some_and(|hex| {
                            !hex.is_empty()
                                && hex.len().is_multiple_of(2)
                                && hex.bytes().all(|byte| {
                                    byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte)
                                })
                        })
                })
        }
        Operation::InstalledInspect(params) => {
            crate::inspection::directory_valid(&params.directory)
        }
        Operation::InstallPlan(params) => {
            identifier_valid(&params.edition_id)
                && text(&params.destination)
                && product_valid(&ProductParams {
                    product_id: params.product_id.clone(),
                    market: params.market.clone(),
                    language: params.language.clone(),
                    refresh: Refresh::Network,
                })
        }
        Operation::JobsEnqueue(EnqueueParams::CatalogRefresh {
            idempotency_key,
            product,
        }) => {
            identifier_valid(idempotency_key)
                && product_valid(product)
                && product.refresh == Refresh::Network
        }
        Operation::JobsEnqueue(EnqueueParams::Install {
            idempotency_key,
            plan_id,
            plan_digest,
        }) => identifier_valid(idempotency_key) && identifier_valid(plan_id) && text(plan_digest),
        Operation::JobsPause(params)
        | Operation::JobsResume(params)
        | Operation::JobsCancel(params)
        | Operation::JobsRetry(params) => identifier_valid(&params.job_id),
        Operation::EventsReplay(params) => {
            identifier_valid(&params.session_id) && (1..=1000).contains(&params.limit)
        }
        Operation::GameLaunch(params) | Operation::GameRollback(params) => {
            identifier_valid(&params.installation_id)
        }
        Operation::GameUpdate(params) => {
            identifier_valid(&params.installation_id)
                && identifier_valid(&params.plan_id)
                && text(&params.plan_digest)
        }
        Operation::GameRemove(params) => identifier_valid(&params.installation_id),
        _ => true,
    };
    if valid { Ok(()) } else { Err(invalid()) }
}
