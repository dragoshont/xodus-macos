use crate::hardware;
use crate::licensing::splicense::SPLicense;
use crate::licensing::utils::{generate_string, parse_bcrypt_rsa_private};
use crate::models::devicecredential::{Authentication, ClientInfo, DeviceAddRequest, DeviceInfo};
use crate::models::secrets::{
    Device, DeviceTokenStructureFailure, Token, device_token_structurally_valid,
    device_token_structure_failure, legacy_token_valid,
};
use crate::models::soap::BodyContent;
use crate::tokens::manager::{PASSPORT_STS, TokenManager};
use crate::tokens::store::TokenStoreError;

#[derive(Debug, thiserror::Error)]
pub enum DeviceCredentialError {
    #[error("Device credential storage is unavailable; review its permission")]
    StorageUnavailable,
    #[error("Stored device credentials are malformed; they were not replaced")]
    InvalidStoredCredential,
    #[error("Microsoft device credential request failed")]
    BrokerFailure,
    #[error("Microsoft device response did not contain complete valid credential proof")]
    InvalidBrokerProof,
    #[error("Microsoft device response did not contain complete valid credential proof")]
    InvalidBrokerProofAt(DeviceProofFailure),
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DeviceProofFailure {
    Registration,
    TokenResponse,
    TokenProof,
    TokenStructure,
    TokenKind,
    TokenAudience,
    TokenCipher,
    TokenXmlBound,
    TokenXmlParse,
    TokenCipherEncoding,
    TokenSecret,
}

fn storage_error(error: TokenStoreError) -> DeviceCredentialError {
    match error {
        TokenStoreError::Serde(_) | TokenStoreError::InvalidCredential => {
            DeviceCredentialError::InvalidStoredCredential
        }
        _ => DeviceCredentialError::StorageUnavailable,
    }
}

/// Called only by explicit legacy commands or the memory-only management login worker.
pub async fn ensure_device_credentials(
    client: &reqwest::Client,
    tokens: &TokenManager,
) -> Result<(), DeviceCredentialError> {
    let license = match tokens.get_device_license() {
        Ok(license) => license,
        Err(TokenStoreError::NotFound) => {
            match tokens.get_device_sts_token() {
                Err(TokenStoreError::NotFound) => {}
                Err(error) => return Err(storage_error(error)),
                Ok(_) => return Err(DeviceCredentialError::InvalidStoredCredential),
            }
            let device = provision_device(client, tokens).await?;
            return reauthenticate_device(client, tokens, device).await;
        }
        Err(error) => return Err(storage_error(error)),
    };
    match tokens.get_device_sts_token() {
        Ok(Token::Legacy(token))
            if device_token_structurally_valid(&token) && legacy_token_valid(&token) =>
        {
            return Ok(());
        }
        Ok(Token::Legacy(token)) if device_token_structurally_valid(&token) => {}
        Err(TokenStoreError::NotFound) => {}
        Err(error) => return Err(storage_error(error)),
        _ => return Err(DeviceCredentialError::InvalidStoredCredential),
    }
    reauthenticate_device(client, tokens, license).await
}

async fn provision_device(
    client: &reqwest::Client,
    tokens: &TokenManager,
) -> Result<Device, DeviceCredentialError> {
    let username = format!("02{}", generate_string(14));
    let password = generate_string(20);
    let provision = DeviceAddRequest {
        client_info: ClientInfo::default(),
        authentication: Authentication::new(username.clone(), password.clone()),
        device_info: Some(DeviceInfo {
            id: "DeviceInfo".to_string(),
            components: hardware::probe_provision_components(),
            tpm_info: None,
        }),
    };
    let dev = crate::api::live::login_device_credential(client, provision)
        .await
        .map_err(|_| DeviceCredentialError::BrokerFailure)?;
    validate_registration_proof(
        dev.success,
        &dev.puid,
        &dev.hw_device_id,
        &dev.license.splicense_block,
    )?;
    let device = Device {
        username,
        password,
        puid: dev.puid,
        hwid: dev.hw_device_id,
        device_id: dev.license.binding.device_id.unwrap_or_default(),
        splicense: dev.license.splicense_block,
    };
    tokens.save_device_license(&device).map_err(storage_error)?;
    Ok(device)
}

fn validate_registration_proof(
    success: bool,
    puid: &str,
    hwid: &str,
    splicense: &str,
) -> Result<(), DeviceCredentialError> {
    if !success || puid.is_empty() || hwid.is_empty() || splicense.is_empty() {
        return Err(DeviceCredentialError::InvalidBrokerProofAt(
            DeviceProofFailure::Registration,
        ));
    }
    Ok(())
}

async fn reauthenticate_device(
    client: &reqwest::Client,
    tokens: &TokenManager,
    license: Device,
) -> Result<(), DeviceCredentialError> {
    let sp_license = SPLicense::parse_base64(&license.splicense)
        .map_err(|_| DeviceCredentialError::InvalidStoredCredential)?;
    let key = sp_license
        .clep_sign_state
        .ok_or(DeviceCredentialError::InvalidStoredCredential)?
        .try_get_rsa_key()
        .map_err(|_| DeviceCredentialError::InvalidStoredCredential)?;
    let private_key = parse_bcrypt_rsa_private(&key)
        .map_err(|_| DeviceCredentialError::InvalidStoredCredential)?;
    let resp = crate::api::live::authenticate_device(client, license.username, private_key)
        .await
        .map_err(|_| DeviceCredentialError::BrokerFailure)?;
    save_device_response(tokens, resp.body.body)
}

fn save_device_response(
    tokens: &TokenManager,
    body: BodyContent,
) -> Result<(), DeviceCredentialError> {
    let response = crate::api::live::single_device_response(body).map_err(|_| {
        DeviceCredentialError::InvalidBrokerProofAt(DeviceProofFailure::TokenResponse)
    })?;
    let token = Token::from_response_checked(response)
        .map_err(|_| DeviceCredentialError::InvalidBrokerProofAt(DeviceProofFailure::TokenProof))?;
    validate_device_token(&token)?;
    tokens
        .save_device_token(PASSPORT_STS.to_owned(), token)
        .map_err(storage_error)
}

fn validate_device_token(token: &Token) -> Result<(), DeviceCredentialError> {
    let failure = match token {
        Token::Legacy(token) => {
            device_token_structure_failure(token).map(|failure| match failure {
                DeviceTokenStructureFailure::Audience => DeviceProofFailure::TokenAudience,
                DeviceTokenStructureFailure::Cipher => DeviceProofFailure::TokenCipher,
                DeviceTokenStructureFailure::XmlBound => DeviceProofFailure::TokenXmlBound,
                DeviceTokenStructureFailure::XmlParse => DeviceProofFailure::TokenXmlParse,
                DeviceTokenStructureFailure::CipherEncoding => {
                    DeviceProofFailure::TokenCipherEncoding
                }
                DeviceTokenStructureFailure::Secret => DeviceProofFailure::TokenSecret,
            })
        }
        Token::Compact(_) => Some(DeviceProofFailure::TokenKind),
    };
    if let Some(failure) = failure {
        return Err(DeviceCredentialError::InvalidBrokerProofAt(failure));
    }
    Ok(())
}

#[cfg(test)]
mod management_device_tests {
    use super::*;
    use crate::models::soap;
    use crate::tokens::{backend::MemoryBackend, store::TokenBackend};
    use base64::prelude::*;
    use std::sync::Arc;

    fn response() -> soap::RequestSecurityTokenResponse {
        let mut secret = [0; 4096];
        secret[..4].copy_from_slice(&4u32.to_le_bytes());
        soap::RequestSecurityTokenResponse {
            token_type: "urn:passport:legacy".to_owned(),
            applies_to: soap::AppliesTo {
                endpoint_reference: soap::EndpointReference {
                    address: "http://Passport.NET/tb".to_owned(),
                },
            },
            lifetime: soap::Timestamp {
                id: None,
                created: "2000-01-01T00:00:00Z".to_owned(),
                expires: "2099-01-01T00:00:00Z".to_owned(),
            },
            requested_security_token: soap::RequestedSecurityToken {
                encrypted_data: Some(soap::EncryptedData::devicesoftware(
                    BASE64_STANDARD.encode(b"synthetic-not-a-device-ticket"),
                )),
                binary_security_token: None,
            },
            requested_proof_token: Some(soap::RequestedProofToken {
                binary_secret: BASE64_STANDARD.encode(secret),
                encrypted_key: None,
            }),
        }
    }

    fn decode_response(collection: bool) -> BodyContent {
        decode_fixture_response(collection, response())
    }

    fn decode_fixture_response(
        collection: bool,
        response: soap::RequestSecurityTokenResponse,
    ) -> BodyContent {
        let encrypted = quick_xml::se::to_string(
            response
                .requested_security_token
                .encrypted_data
                .as_ref()
                .unwrap(),
        )
        .unwrap();
        let proof = response.requested_proof_token.unwrap().binary_secret;
        let response = format!(
            "<RequestSecurityTokenResponse><TokenType>urn:passport:legacy</TokenType><AppliesTo><EndpointReference><Address>http://Passport.NET/tb</Address></EndpointReference></AppliesTo><Lifetime><Created>2000-01-01T00:00:00Z</Created><Expires>2099-01-01T00:00:00Z</Expires></Lifetime><RequestedSecurityToken>{encrypted}</RequestedSecurityToken><RequestedProofToken><BinarySecret>{proof}</BinarySecret></RequestedProofToken></RequestSecurityTokenResponse>"
        );
        let body = if collection {
            format!(
                "<RequestSecurityTokenResponseCollection>{response}</RequestSecurityTokenResponseCollection>"
            )
        } else {
            response
        };
        let xml = format!(
            "<Envelope xmlns:ds=\"http://www.w3.org/2000/09/xmldsig#\" xmlns:wsse=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd\"><Header><Action>fixture-only</Action><To>synthetic.invalid</To><Security><Timestamp><Created>2000-01-01T00:00:00Z</Created><Expires>2099-01-01T00:00:00Z</Expires></Timestamp></Security></Header><Body>{body}</Body></Envelope>"
        );
        quick_xml::de::from_str::<soap::Envelope>(&xml)
            .unwrap()
            .body
            .body
    }

    fn xml_base64_whitespace(value: &str) -> String {
        value
            .as_bytes()
            .chunks(4)
            .map(|chunk| std::str::from_utf8(chunk).unwrap())
            .collect::<Vec<_>>()
            .join(" \t\r\n")
    }

    #[test]
    fn management_cipher_issued_legacy_xml_round_trip_preserves_supported_metadata() {
        for collection in [false, true] {
            for method_value in [None, Some(""), Some(" "), Some("\t\r\n")] {
                for local_namespace in [false, true] {
                    for reference in [false, true] {
                        let mut response = response();
                        let encrypted = response
                            .requested_security_token
                            .encrypted_data
                            .as_mut()
                            .unwrap();
                        encrypted.encryption_method.val = method_value.map(str::to_owned);
                        if !local_namespace {
                            encrypted.key_info.ds = None;
                        }
                        if reference {
                            encrypted.key_info.security_token_reference =
                                Some(soap::SecurityTokenReference {
                                    reference: soap::ReferenceUri {
                                        uri: "#SyntheticTicket".to_owned(),
                                    },
                                });
                        }
                        let parsed = crate::api::live::single_device_response(
                            decode_fixture_response(collection, response.clone()),
                        )
                        .unwrap();
                        let inherited = inherited_legacy_conversion(&parsed);
                        let checked = Token::from_response_checked(parsed).unwrap();
                        assert_eq!(
                            serde_json::to_value(&inherited).unwrap(),
                            serde_json::to_value(&checked).unwrap()
                        );
                        let memory = Arc::new(MemoryBackend::default());
                        let tokens = TokenManager::with_management_backend(memory.clone());
                        save_device_response(
                            &tokens,
                            decode_fixture_response(collection, response),
                        )
                        .unwrap();
                        let Token::Legacy(token) = tokens.get_device_sts_token().unwrap() else {
                            panic!("Expected only a synthetic legacy proof");
                        };
                        assert!(device_token_structurally_valid(&token));
                        assert_eq!(
                            serde_json::to_value(Token::Legacy(token)).unwrap(),
                            serde_json::to_value(inherited).unwrap()
                        );
                    }
                }
            }
        }
    }

    // Test-only oracle for the inherited 08b06d38 legacy conversion, not a provider proof.
    fn inherited_legacy_conversion(response: &soap::RequestSecurityTokenResponse) -> Token {
        let encrypted = response
            .requested_security_token
            .encrypted_data
            .as_ref()
            .unwrap();
        Token::Legacy(crate::models::secrets::LegacyToken {
            key_name: encrypted.key_info.key_name.clone(),
            token: quick_xml::se::to_string(encrypted).unwrap(),
            binary_secret: response
                .requested_proof_token
                .as_ref()
                .map(|proof| proof.binary_secret.clone()),
            tpm_key: response
                .requested_proof_token
                .as_ref()
                .and_then(|proof| proof.encrypted_key.as_ref())
                .map(|key| key.cipher_data.cipher_value.clone()),
            lifetime: response.lifetime.clone(),
        })
    }

    #[test]
    fn inherited_conversion_serialization_is_not_checked_cipher_admission() {
        for collection in [false, true] {
            let mut response = response();
            response
                .requested_security_token
                .encrypted_data
                .as_mut()
                .unwrap()
                .cipher_data
                .cipher_value = "SYNTHETIC_INVALID_CIPHER!".to_owned();
            let parsed = crate::api::live::single_device_response(decode_fixture_response(
                collection,
                response.clone(),
            ))
            .unwrap();
            let inherited = inherited_legacy_conversion(&parsed);
            assert_eq!(
                serde_json::to_value(&inherited).unwrap(),
                serde_json::to_value(Token::from_response_checked(parsed).unwrap()).unwrap()
            );
            let Token::Legacy(inherited) = inherited else {
                panic!("Expected only the inherited synthetic legacy conversion");
            };
            assert!(!device_token_structurally_valid(&inherited));
            let memory = Arc::new(MemoryBackend::default());
            let tokens = TokenManager::with_management_backend(memory.clone());
            let error =
                save_device_response(&tokens, decode_fixture_response(collection, response))
                    .unwrap_err();
            assert!(matches!(
                error,
                DeviceCredentialError::InvalidBrokerProofAt(
                    DeviceProofFailure::TokenCipherEncoding
                )
            ));
            assert!(memory.get("device-tokens").unwrap().is_none());
        }
    }

    #[test]
    fn management_cipher_subsites_keep_rejection_guards_static_without_acceptance_changes() {
        let Token::Legacy(original) = Token::from_response_checked(response()).unwrap() else {
            panic!("Expected only a synthetic legacy proof");
        };
        assert!(validate_device_token(&Token::Legacy(original.clone())).is_ok());
        let mut boundary = original.clone();
        boundary
            .token
            .push_str(&" ".repeat(64 * 1024 - boundary.token.len()));
        assert_eq!(boundary.token.len(), 64 * 1024);
        assert!(device_token_structurally_valid(&boundary));
        assert!(validate_device_token(&Token::Legacy(boundary.clone())).is_ok());
        let mut cases = Vec::new();
        boundary.token.push(' ');
        cases.push((boundary, DeviceProofFailure::TokenXmlBound));
        let mut token = original.clone();
        token.token = "SECRET_SENTINEL_MALFORMED_XML".to_owned();
        cases.push((token, DeviceProofFailure::TokenXmlParse));
        let mut token = original.clone();
        let mut encrypted: soap::EncryptedData = quick_xml::de::from_str(&token.token).unwrap();
        encrypted.cipher_data.cipher_value = "AA\u{00a0}==".to_owned();
        token.token = quick_xml::se::to_string(&encrypted).unwrap();
        cases.push((token, DeviceProofFailure::TokenCipherEncoding));
        let mut token = original.clone();
        let mut encrypted: soap::EncryptedData = quick_xml::de::from_str(&token.token).unwrap();
        encrypted.cipher_data.cipher_value = " \t\r\n".to_owned();
        token.token = quick_xml::se::to_string(&encrypted).unwrap();
        cases.push((token, DeviceProofFailure::TokenCipher));
        let mut token = original;
        token.lifetime.expires = "SECRET_SENTINEL_INVALID_EXPIRY".to_owned();
        cases.push((token, DeviceProofFailure::TokenCipher));
        for (token, expected) in cases {
            assert!(!device_token_structurally_valid(&token));
            let error = validate_device_token(&Token::Legacy(token)).unwrap_err();
            assert!(matches!(
                error,
                DeviceCredentialError::InvalidBrokerProofAt(site) if site == expected
            ));
            assert!(!error.to_string().contains("SECRET_SENTINEL"));
            assert!(!format!("{error:?}").contains("SECRET_SENTINEL"));
        }
    }

    #[test]
    fn management_xml_base64_device_proof_preserves_xml_and_memory_admission() {
        for collection in [false, true] {
            for (wrap_cipher, wrap_secret) in
                [(false, false), (true, false), (false, true), (true, true)]
            {
                let mut response = response();
                let cipher = &mut response
                    .requested_security_token
                    .encrypted_data
                    .as_mut()
                    .unwrap()
                    .cipher_data
                    .cipher_value;
                if wrap_cipher {
                    *cipher = xml_base64_whitespace(cipher);
                }
                let secret = &mut response
                    .requested_proof_token
                    .as_mut()
                    .unwrap()
                    .binary_secret;
                if wrap_secret {
                    *secret = xml_base64_whitespace(secret);
                }
                let memory = Arc::new(MemoryBackend::default());
                let tokens = TokenManager::with_management_backend(memory.clone());
                save_device_response(&tokens, decode_fixture_response(collection, response))
                    .unwrap();
                let Token::Legacy(token) = tokens.get_device_sts_token().unwrap() else {
                    panic!("Expected only a synthetic legacy device proof");
                };
                assert!(device_token_structurally_valid(&token));
                let encrypted: soap::EncryptedData = quick_xml::de::from_str(&token.token).unwrap();
                assert_eq!(
                    encrypted.cipher_data.cipher_value.contains('\t'),
                    wrap_cipher
                );
                assert_eq!(token.binary_secret.unwrap().contains('\t'), wrap_secret);
            }
        }
    }

    #[test]
    fn single_device_response_forms_preserve_identical_memory_only_proof() {
        let mut saved = Vec::new();
        for collection in [false, true] {
            let memory = Arc::new(MemoryBackend::default());
            let tokens = TokenManager::with_management_backend(memory.clone());
            save_device_response(&tokens, decode_response(collection)).unwrap();
            let Token::Legacy(token) = tokens.get_device_sts_token().unwrap() else {
                panic!("Expected a synthetic legacy device proof");
            };
            assert!(device_token_structurally_valid(&token));
            assert!(legacy_token_valid(&token));
            saved.push(memory.get("device-tokens").unwrap().unwrap());
        }
        assert_eq!(saved[0], saved[1]);
    }

    #[test]
    fn ambiguous_empty_and_unexpected_device_bodies_preserve_memory_destination() {
        let memory = Arc::new(MemoryBackend::default());
        let tokens = TokenManager::with_management_backend(memory.clone());
        save_device_response(
            &tokens,
            BodyContent::RequestSecurityTokenResponse(Box::new(response())),
        )
        .unwrap();
        let original = memory.get("device-tokens").unwrap().unwrap();
        for body in [
            BodyContent::RequestSecurityTokenResponseCollection(
                soap::RequestSecurityTokenResponseCollection {
                    security_tokens: vec![],
                },
            ),
            BodyContent::RequestSecurityTokenResponseCollection(
                soap::RequestSecurityTokenResponseCollection {
                    security_tokens: vec![response(), response()],
                },
            ),
            BodyContent::EncryptedData(Box::new(soap::EncryptedData::devicesoftware(
                BASE64_STANDARD.encode(b"synthetic-not-decrypted"),
            ))),
            BodyContent::Fault(soap::Fault {}),
        ] {
            assert!(matches!(
                save_device_response(&tokens, body),
                Err(DeviceCredentialError::InvalidBrokerProofAt(
                    DeviceProofFailure::TokenResponse
                ))
            ));
            assert_eq!(memory.get("device-tokens").unwrap().unwrap(), original);
        }
    }

    #[test]
    fn malformed_device_proofs_cannot_replace_memory_destination_in_either_form() {
        let memory = Arc::new(MemoryBackend::default());
        let tokens = TokenManager::with_management_backend(memory.clone());
        save_device_response(
            &tokens,
            BodyContent::RequestSecurityTokenResponse(Box::new(response())),
        )
        .unwrap();
        let original = memory.get("device-tokens").unwrap().unwrap();
        let mut malformed = Vec::new();
        let mut token = response();
        token.lifetime.expires = "2000-01-01T00:00:00Z".to_owned();
        malformed.push(token);
        let mut token = response();
        token.lifetime.expires = "invalid".to_owned();
        malformed.push(token);
        let mut token = response();
        token.applies_to.endpoint_reference.address.clear();
        malformed.push(token);
        let mut token = response();
        token.token_type = "urn:passport:compact".to_owned();
        token.requested_security_token.encrypted_data = None;
        token.requested_security_token.binary_security_token = Some(soap::BinarySecurityTokenRes {
            id: "synthetic-only".to_owned(),
            value: "synthetic-not-a-device-ticket".to_owned(),
            value_type: None,
        });
        malformed.push(token);
        let mut token = response();
        token.token_type = "unexpected".to_owned();
        malformed.push(token);
        let mut token = response();
        token
            .requested_security_token
            .encrypted_data
            .as_mut()
            .unwrap()
            .key_info
            .key_name = None;
        malformed.push(token);
        let mut token = response();
        token.requested_security_token.encrypted_data = None;
        malformed.push(token);
        let mut token = response();
        token.requested_proof_token = None;
        malformed.push(token);
        let mut token = response();
        token.requested_proof_token.as_mut().unwrap().binary_secret = "!".to_owned();
        malformed.push(token);
        let mut token = response();
        token.requested_proof_token.as_mut().unwrap().binary_secret =
            BASE64_STANDARD.encode([4; 4095]);
        malformed.push(token);
        let mut token = response();
        token.requested_proof_token.as_mut().unwrap().binary_secret =
            BASE64_STANDARD.encode([0; 4096]);
        malformed.push(token);
        let mut token = response();
        token
            .requested_security_token
            .encrypted_data
            .as_mut()
            .unwrap()
            .key_info
            .key_name = Some("synthetic.invalid".to_owned());
        malformed.push(token);
        let mut token = response();
        token
            .requested_security_token
            .encrypted_data
            .as_mut()
            .unwrap()
            .cipher_data
            .cipher_value = "!".to_owned();
        malformed.push(token);
        let mut token = response();
        token
            .requested_security_token
            .encrypted_data
            .as_mut()
            .unwrap()
            .cipher_data
            .cipher_value
            .clear();
        malformed.push(token);
        for encoded in [
            "",
            " \t\r\n",
            "AA==!",
            "AB==",
            "AA===",
            "AA-_",
            "AA\u{00a0}==",
            "AA\u{000b}==",
            "AA\u{000c}==",
        ] {
            let mut token = response();
            token.requested_proof_token.as_mut().unwrap().binary_secret = encoded.to_owned();
            malformed.push(token);
            let mut token = response();
            token
                .requested_security_token
                .encrypted_data
                .as_mut()
                .unwrap()
                .cipher_data
                .cipher_value = encoded.to_owned();
            malformed.push(token);
        }
        for secret in [
            BASE64_STANDARD.encode([4; 4095]),
            BASE64_STANDARD.encode([0; 4096]),
        ] {
            let mut token = response();
            token.requested_proof_token.as_mut().unwrap().binary_secret =
                xml_base64_whitespace(&secret);
            malformed.push(token);
        }
        let mut token = response();
        token.requested_proof_token.as_mut().unwrap().binary_secret =
            " ".repeat(soap::MAX_XML_RESPONSE_BYTES + 1);
        malformed.push(token);
        let mut token = response();
        token
            .requested_security_token
            .encrypted_data
            .as_mut()
            .unwrap()
            .cipher_data
            .cipher_value = format!("{}AA==", " ".repeat(64 * 1024));
        malformed.push(token);
        for response in malformed {
            for body in [
                BodyContent::RequestSecurityTokenResponse(Box::new(response.clone())),
                BodyContent::RequestSecurityTokenResponseCollection(
                    soap::RequestSecurityTokenResponseCollection {
                        security_tokens: vec![response.clone()],
                    },
                ),
            ] {
                assert!(matches!(
                    save_device_response(&tokens, body),
                    Err(DeviceCredentialError::InvalidBrokerProofAt(
                        DeviceProofFailure::TokenProof
                            | DeviceProofFailure::TokenKind
                            | DeviceProofFailure::TokenAudience
                            | DeviceProofFailure::TokenCipher
                            | DeviceProofFailure::TokenXmlBound
                            | DeviceProofFailure::TokenXmlParse
                            | DeviceProofFailure::TokenCipherEncoding
                            | DeviceProofFailure::TokenSecret
                    ))
                ));
                assert_eq!(memory.get("device-tokens").unwrap().unwrap(), original);
            }
        }
    }

    #[test]
    fn device_registration_failure_is_static_and_preserves_the_same_field_guards() {
        let sentinel = "SECRET_SENTINEL_DEVICE_METADATA<XML>identity=synthetic";
        for (success, puid, hwid, license) in [
            (false, sentinel, sentinel, sentinel),
            (true, "", sentinel, sentinel),
            (true, sentinel, "", sentinel),
            (true, sentinel, sentinel, ""),
        ] {
            let error = validate_registration_proof(success, puid, hwid, license).unwrap_err();
            assert!(matches!(
                error,
                DeviceCredentialError::InvalidBrokerProofAt(DeviceProofFailure::Registration)
            ));
            assert!(!error.to_string().contains(sentinel));
            assert!(!format!("{error:?}").contains(sentinel));
        }
        assert!(validate_registration_proof(true, sentinel, sentinel, sentinel).is_ok());
    }

    #[test]
    fn token_rejection_sites_remain_distinct_without_serializing_synthetic_proof() {
        for (response, expected) in [
            (
                {
                    let mut response = response();
                    response.requested_security_token.encrypted_data = None;
                    response
                },
                DeviceProofFailure::TokenProof,
            ),
            (
                {
                    let mut response = response();
                    response.requested_proof_token = None;
                    response
                },
                DeviceProofFailure::TokenSecret,
            ),
            (
                {
                    let mut response = response();
                    response.token_type = "urn:passport:compact".to_owned();
                    response.requested_security_token.encrypted_data = None;
                    response.requested_security_token.binary_security_token =
                        Some(soap::BinarySecurityTokenRes {
                            id: "synthetic-only".to_owned(),
                            value: "synthetic-not-a-device-ticket".to_owned(),
                            value_type: None,
                        });
                    response
                },
                DeviceProofFailure::TokenKind,
            ),
            (
                {
                    let mut response = response();
                    response
                        .requested_security_token
                        .encrypted_data
                        .as_mut()
                        .unwrap()
                        .key_info
                        .key_name = Some("SECRET_SENTINEL_CONTEXT".to_owned());
                    response
                },
                DeviceProofFailure::TokenAudience,
            ),
            (
                {
                    let mut response = response();
                    response
                        .requested_security_token
                        .encrypted_data
                        .as_mut()
                        .unwrap()
                        .cipher_data
                        .cipher_value = "SECRET_SENTINEL_BAD_CIPHER!".to_owned();
                    response
                },
                DeviceProofFailure::TokenCipherEncoding,
            ),
            (
                {
                    let mut response = response();
                    response
                        .requested_security_token
                        .encrypted_data
                        .as_mut()
                        .unwrap()
                        .cipher_data
                        .cipher_value = "AAAA".repeat(16 * 1024);
                    response
                },
                DeviceProofFailure::TokenXmlBound,
            ),
        ] {
            let memory = Arc::new(MemoryBackend::default());
            let tokens = TokenManager::with_management_backend(memory.clone());
            let error = save_device_response(
                &tokens,
                BodyContent::RequestSecurityTokenResponse(Box::new(response)),
            )
            .unwrap_err();
            assert!(matches!(
                error,
                DeviceCredentialError::InvalidBrokerProofAt(failure) if failure == expected
            ));
            assert_eq!(
                error.to_string(),
                "Microsoft device response did not contain complete valid credential proof"
            );
            assert!(!format!("{error:?}").contains("synthetic-not-a-device-ticket"));
            assert!(!format!("{error:?}").contains("SECRET_SENTINEL"));
            assert!(memory.get("device-tokens").unwrap().is_none());
        }
    }

    struct Denied;
    impl TokenBackend for Denied {
        fn get(&self, _: &str) -> Result<Option<Vec<u8>>, TokenStoreError> {
            Err(TokenStoreError::Io(std::io::Error::from(
                std::io::ErrorKind::PermissionDenied,
            )))
        }
        fn set(&self, _: &str, _: &[u8]) -> Result<(), TokenStoreError> {
            panic!("must not provision after denied read")
        }
        fn remove(&self, _: &str) -> Result<(), TokenStoreError> {
            unreachable!()
        }
    }

    #[tokio::test]
    async fn denied_and_corrupt_device_reads_never_provision() {
        let denied = TokenManager::with_management_backend(Arc::new(Denied));
        assert!(matches!(
            ensure_device_credentials(&reqwest::Client::new(), &denied).await,
            Err(DeviceCredentialError::StorageUnavailable)
        ));
        let memory = Arc::new(MemoryBackend::default());
        memory.set("dev_license", b"{}").unwrap();
        let corrupt = TokenManager::with_management_backend(memory.clone());
        assert!(matches!(
            ensure_device_credentials(&reqwest::Client::new(), &corrupt).await,
            Err(DeviceCredentialError::InvalidStoredCredential)
        ));
        assert_eq!(memory.get("dev_license").unwrap().unwrap(), b"{}");
        assert!(memory.get("device-tokens").unwrap().is_none());
    }
}
