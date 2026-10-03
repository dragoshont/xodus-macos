use serde::{Deserialize, Serialize};

use crate::models::soap::{self, Timestamp};

#[derive(Debug, Serialize, Deserialize)]
pub struct Device {
    pub puid: String,
    pub hwid: String,
    pub device_id: String,
    pub splicense: String,
    pub username: String,
    pub password: String,
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
