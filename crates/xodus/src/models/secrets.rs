use serde::{Deserialize, Serialize};

use crate::models::soap::{self, Timestamp};

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Device {
    pub puid: String,
    pub hwid: String,
    pub device_id: String,
    pub splicense: String,
    pub username: String,
    pub password: String,
}

#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct PendingManagementExchange {
    pub flow_id: String,
    pub device: Device,
    pub device_token: LegacyToken,
    pub property: crate::models::live::DAProperty,
    pub after_continuation: bool,
    pub created_at: chrono::DateTime<chrono::Utc>,
    pub expires_at: chrono::DateTime<chrono::Utc>,
}

impl PendingManagementExchange {
    pub fn new(
        flow_id: String,
        device: Device,
        device_token: LegacyToken,
        property: crate::models::live::DAProperty,
        after_continuation: bool,
    ) -> Option<Self> {
        let created_at = chrono::Utc::now();
        let expires_at = (created_at + chrono::Duration::seconds(300))
            .min(
                chrono::DateTime::parse_from_rfc3339(&property.da_expires)
                    .ok()?
                    .with_timezone(&chrono::Utc),
            )
            .min(
                chrono::DateTime::parse_from_rfc3339(&device_token.lifetime.expires)
                    .ok()?
                    .with_timezone(&chrono::Utc),
            );
        let pending = Self {
            flow_id,
            device,
            device_token,
            property,
            after_continuation,
            created_at,
            expires_at,
        };
        pending.valid().then_some(pending)
    }

    pub fn valid(&self) -> bool {
        let now = chrono::Utc::now();
        !self.flow_id.is_empty()
            && self.created_at <= now
            && now < self.expires_at
            && self.expires_at <= self.created_at + chrono::Duration::seconds(300)
            && !self.device.username.is_empty()
            && !self.device.password.is_empty()
            && !self.device.puid.is_empty()
            && !self.device.splicense.is_empty()
            && !self.device.hwid.is_empty()
            && !self.device.device_id.is_empty()
            && device_token_structurally_valid(&self.device_token)
            && legacy_token_valid(&self.device_token)
            && !self.property.da_token.is_empty()
            && !self.property.username.is_empty()
            && !self.property.puid.is_empty()
            && chrono::DateTime::parse_from_rfc3339(&self.property.da_start_time)
                .is_ok_and(|started| started <= now && started < self.expires_at)
            && chrono::DateTime::parse_from_rfc3339(&self.property.da_expires)
                .is_ok_and(|expires| self.expires_at <= expires)
            && serde_json::to_vec(self).is_ok_and(|bytes| bytes.len() <= 192 * 1024)
    }
}

#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ManagementStoreSession {
    pub flow_id: String,
    pub user: User,
    pub tokens: std::collections::HashMap<String, Token>,
    pub device: Device,
    pub device_token: LegacyToken,
}

impl ManagementStoreSession {
    pub fn structurally_valid(&self) -> bool {
        !self.flow_id.is_empty()
            && !self.user.puid.trim().is_empty()
            && !self.user.username.trim().is_empty()
            && !self.device.username.is_empty()
            && !self.device.password.is_empty()
            && !self.device.puid.is_empty()
            && !self.device.hwid.is_empty()
            && !self.device.splicense.is_empty()
            && device_token_structurally_valid(&self.device_token)
            && matches!(self.tokens.get(crate::tokens::PASSPORT_STS),
                Some(Token::Legacy(token)) if legacy_token_structurally_valid(token))
    }

    pub fn valid(&self) -> bool {
        self.structurally_valid()
            && legacy_token_valid(&self.device_token)
            && matches!(self.tokens.get(crate::tokens::PASSPORT_STS),
                Some(Token::Legacy(token)) if legacy_token_valid(token))
    }
}

pub fn device_token_structurally_valid(token: &LegacyToken) -> bool {
    device_token_structure_failure(token).is_none()
}

pub fn legacy_token_structurally_valid(token: &LegacyToken) -> bool {
    legacy_token_structure_failure(token).is_none()
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum DeviceTokenStructureFailure {
    Audience,
    Cipher,
    XmlBound,
    XmlParse,
    Secret,
}

fn legacy_token_structure_failure(token: &LegacyToken) -> Option<DeviceTokenStructureFailure> {
    if token.token.len() > 64 * 1024 {
        return Some(DeviceTokenStructureFailure::XmlBound);
    }
    let Ok(encrypted) = quick_xml::de::from_str::<soap::EncryptedData>(&token.token) else {
        return Some(DeviceTokenStructureFailure::XmlParse);
    };
    if encrypted.key_info.key_name.as_deref() != Some(crate::tokens::PASSPORT_STS)
        || token.key_name.as_deref() != Some(crate::tokens::PASSPORT_STS)
    {
        return Some(DeviceTokenStructureFailure::Audience);
    }
    // Passport consumers forward the issuer's ticket, not locally decrypted bytes.
    if encrypted.cipher_data.cipher_value.trim().is_empty()
        || chrono::DateTime::parse_from_rfc3339(&token.lifetime.expires).is_err()
    {
        return Some(DeviceTokenStructureFailure::Cipher);
    }
    None
}

pub(crate) fn device_token_structure_failure(
    token: &LegacyToken,
) -> Option<DeviceTokenStructureFailure> {
    legacy_token_structure_failure(token).or_else(|| {
        if token.binary_secret.as_ref().is_some_and(|secret| {
            soap::decode_xml_base64(secret)
                .is_ok_and(|bytes| bytes.len() == 4096 && bytes[..4] == 4u32.to_le_bytes())
        }) {
            None
        } else {
            Some(DeviceTokenStructureFailure::Secret)
        }
    })
}

pub fn legacy_token_valid(token: &LegacyToken) -> bool {
    legacy_token_structurally_valid(token)
        && chrono::DateTime::parse_from_rfc3339(&token.lifetime.expires)
            .is_ok_and(|expires| expires > chrono::Utc::now())
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct LegacyToken {
    pub key_name: Option<String>,
    pub token: String,
    pub binary_secret: Option<String>,
    #[serde(default)]
    pub tpm_key: Option<String>,
    pub lifetime: Timestamp,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
#[serde(untagged)]
pub enum Token {
    Legacy(LegacyToken),
    Compact(String),
}

impl Token {
    pub fn from_response_checked(
        value: soap::RequestSecurityTokenResponse,
    ) -> Result<Self, &'static str> {
        let invalid = "Missing or invalid credential proof";
        if value
            .applies_to
            .endpoint_reference
            .address
            .trim()
            .is_empty()
        {
            return Err(invalid);
        }
        let expires =
            chrono::DateTime::parse_from_rfc3339(&value.lifetime.expires).map_err(|_| invalid)?;
        if expires <= chrono::Utc::now() {
            return Err(invalid);
        }
        match value.token_type.as_str() {
            "urn:passport:legacy" => {
                let encrypted = value
                    .requested_security_token
                    .encrypted_data
                    .ok_or(invalid)?;
                if encrypted.cipher_data.cipher_value.trim().is_empty() {
                    return Err(invalid);
                }
                let key_name = encrypted.key_info.key_name.clone();
                let token = quick_xml::se::to_string(&encrypted).map_err(|_| invalid)?;
                let binary_secret = value
                    .requested_proof_token
                    .as_ref()
                    .map(|proof| proof.binary_secret.clone());
                let tpm_key = value
                    .requested_proof_token
                    .and_then(|proof| proof.encrypted_key.map(|key| key.cipher_data.cipher_value));
                Ok(Self::Legacy(LegacyToken {
                    key_name,
                    token,
                    binary_secret,
                    tpm_key,
                    lifetime: value.lifetime,
                }))
            }
            "urn:passport:compact" | "urn:passport:delegationcompact" => {
                let token = value
                    .requested_security_token
                    .binary_security_token
                    .ok_or(invalid)?
                    .value;
                if token.trim().is_empty() {
                    return Err(invalid);
                }
                Ok(Self::Compact(
                    if value.token_type == "urn:passport:delegationcompact" {
                        format!("d={token}")
                    } else {
                        token
                    },
                ))
            }
            _ => Err(invalid),
        }
    }
}
impl From<soap::RequestSecurityTokenResponse> for Token {
    fn from(value: soap::RequestSecurityTokenResponse) -> Self {
        match value.token_type.as_str() {
            "urn:passport:legacy" => {
                let encrypted_data = value.requested_security_token.encrypted_data.unwrap();
                let key_name = encrypted_data.key_info.key_name.clone();
                let token = quick_xml::se::to_string(&encrypted_data).unwrap();
                let binary_secret = value
                    .requested_proof_token
                    .as_ref()
                    .map(|t| t.binary_secret.clone());
                let tpm_key = value
                    .requested_proof_token
                    .and_then(|t| t.encrypted_key.map(|k| k.cipher_data.cipher_value));
                Self::Legacy(LegacyToken {
                    key_name,
                    token,
                    binary_secret,
                    tpm_key,
                    lifetime: value.lifetime,
                })
            }
            "urn:passport:compact" => Self::Compact(
                value
                    .requested_security_token
                    .binary_security_token
                    .unwrap()
                    .value,
            ),
            "urn:passport:delegationcompact" => Self::Compact(format!(
                "d={}",
                value
                    .requested_security_token
                    .binary_security_token
                    .unwrap()
                    .value
            )),
            _ => unreachable!(),
        }
    }
}

#[derive(Debug, Serialize, Deserialize)]
pub struct TokenStore {
    #[serde(flatten)]
    pub tokens: std::collections::HashMap<String, Token>,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct User {
    pub puid: String,
    pub username: String,
}

#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct ManagementXalSession {
    #[serde(rename = "flowID")]
    pub flow_id: String,
    pub session: crate::xal::TokenStore,
}

#[cfg(test)]
mod management_token_tests {
    use super::*;

    fn response(proof: Option<&str>, expiration: &str) -> soap::RequestSecurityTokenResponse {
        soap::RequestSecurityTokenResponse {
            token_type: "urn:passport:compact".to_owned(),
            applies_to: soap::AppliesTo {
                endpoint_reference: soap::EndpointReference {
                    address: "fixture-audience".to_owned(),
                },
            },
            lifetime: Timestamp {
                id: None,
                created: "2026-01-01T00:00:00Z".to_owned(),
                expires: expiration.to_owned(),
            },
            requested_security_token: soap::RequestedSecurityToken {
                encrypted_data: None,
                binary_security_token: proof.map(|value| soap::BinarySecurityTokenRes {
                    id: "fixture".to_owned(),
                    value: value.to_owned(),
                    value_type: None,
                }),
            },
            requested_proof_token: None,
        }
    }

    #[test]
    fn empty_missing_and_expired_proofs_are_errors_not_panics() {
        assert!(Token::from_response_checked(response(None, "2099-01-01T00:00:00Z")).is_err());
        assert!(Token::from_response_checked(response(Some(""), "2099-01-01T00:00:00Z")).is_err());
        assert!(
            Token::from_response_checked(response(Some("fixture"), "2000-01-01T00:00:00Z"))
                .is_err()
        );
        assert!(Token::from_response_checked(response(Some("fixture"), "invalid")).is_err());
    }

    #[test]
    fn nonempty_unexpired_compact_proof_can_be_converted() {
        assert!(matches!(
            Token::from_response_checked(response(
                Some("fixture-not-a-token"),
                "2099-01-01T00:00:00Z"
            )),
            Ok(Token::Compact(_))
        ));
    }
}
