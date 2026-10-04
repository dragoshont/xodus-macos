use std::collections::HashMap;

use crate::api::live::utils;
use crate::models::soap;

pub struct RSTRequest<'a> {
    pub signed_xml: String,
    pub signature: Option<super::RSTSignature<'a>>,
}

pub(crate) async fn bounded_response_text(
    mut response: reqwest::Response,
) -> Result<String, super::RSTError> {
    const MAX: usize = soap::MAX_XML_RESPONSE_BYTES;
    let mut bytes = Vec::new();
    while let Some(chunk) = response.chunk().await? {
        if chunk.len() > MAX - bytes.len() {
            return Err(super::RSTError::InvalidEncryptedPayload);
        }
        bytes.extend_from_slice(&chunk);
    }
    String::from_utf8(bytes).map_err(|_| super::RSTError::InvalidEncryptedPayload)
}

impl<'a> RSTRequest<'a> {
    /// Makes a POST request with `reqwest::Client` and decrypts the envelope if applicable
    pub async fn request(
        self,
        client: &reqwest::Client,
    ) -> Result<soap::Envelope, super::RSTError> {
        tracing::trace!("Making RST2.srf request");
        let response = client
        .post("https://login.live.com/RST2.srf")
        .header("User-Agent", "MSAWindows/55 (OS 10.0.26100.0.0 ge_release; IDK 10.0.26100.5074 ge_release; Cfg 16.000.29325.00; Test 0)")
        .header("Content-Type", "application/soap+xml")
        .header("Host", "login.live.com")
        .body(self.signed_xml)
        .send()
        .await?;

        let status_error = response.error_for_status_ref().err();
        let response_text = bounded_response_text(response).await?;
        let envelope: soap::Envelope = quick_xml::de::from_str(&response_text)?;
        if let Some(error) = status_error
            && !matches!(envelope.body.body, soap::BodyContent::Fault(_))
        {
            return Err(error.into());
        }

        verify_and_decrypt_envelope(self.signature, response_text, envelope)
    }
}

fn verify_and_decrypt_envelope<'a>(
    signature: Option<super::RSTSignature<'a>>,
    xml_text: String,
    mut envelope: soap::Envelope,
) -> Result<soap::Envelope, super::RSTError> {
    let Some(signature) = signature else {
        tracing::debug!("No signature, returning raw envelope");
        return Ok(envelope);
    };
    tracing::trace!("Decrypting soap::Envelope");
    let mut nonces = HashMap::new();
    for token in &envelope.header.security.derived_key_tokens {
        if token.id.is_empty()
            || nonces
                .insert(token.id.clone(), token.nonce.clone())
                .is_some()
        {
            return Err(super::RSTError::InvalidEncryptedPayload);
        }
    }

    if let Some(security_signature) = &envelope.header.security.signature
        && let Some(key_info) = &security_signature.key_info
    {
        let id = &key_info.security_token_reference.reference.uri;
        let id = id
            .strip_prefix('#')
            .filter(|id| !id.is_empty())
            .ok_or(super::RSTError::MissingNonce)?;
        let nonce = nonces.get(id).ok_or(super::RSTError::MissingNonce)?;
        let nonce =
            soap::decode_xml_base64(nonce).map_err(|_| super::RSTError::InvalidEncryptedPayload)?;
        let key = signature.signing_key(&nonce)?;
        let mut kmgr = bergshamra::KeysManager::new();
        kmgr.add_key(bergshamra::Key::new(key, bergshamra::KeyUsage::Verify));
        let ctx = bergshamra::DsigContext::new(kmgr).with_strict_verification(false);
        let result = bergshamra::verify(&ctx, &xml_text)?;
        match result {
            bergshamra::VerifyResult::Invalid { reason } => {
                return Err(super::RSTError::InvalidResponseSignature(reason));
            }
            bergshamra::VerifyResult::Valid { .. } => tracing::debug!("Verification successful"),
        }
    }

    if envelope.header.pp.is_none()
        && let Some(enc_pp) = envelope.header.encrypted_pp.take()
    {
        tracing::trace!("Decrypting soap::PP");
        let pp = utils::decrypt_soap_encrypted_data(
            Box::new(enc_pp.encrypted_data),
            &signature,
            &nonces,
        )?;
        envelope.header.pp = pp;
    }

    if let soap::BodyContent::EncryptedData(enc_data) = envelope.body.body {
        tracing::trace!("Decrypting soap::Body");
        envelope.body.body = utils::decrypt_soap_encrypted_data(enc_data, &signature, &nonces)?;
    }

    Ok(envelope)
}

#[cfg(test)]
mod management_tests {
    use super::*;
    use aes::cipher::{BlockModeEncrypt, KeyIvInit, block_padding::Pkcs7};
    use base64::prelude::*;

    fn signature() -> super::super::RSTSignature<'static> {
        super::super::RSTSignature::Hmac {
            clep_secret: b"public-synthetic-envelope",
            tpm_secret: &[],
        }
    }

    fn encrypted(reference: &str, nonce: &[u8], plaintext: &[u8]) -> soap::EncryptedData {
        let key = signature().hmac_key(nonce).unwrap();
        let mut buffer = vec![0; plaintext.len() + 16];
        let ciphertext = cbc::Encryptor::<aes::Aes256>::new((&key).into(), (&[0; 16]).into())
            .encrypt_padded_b2b::<Pkcs7>(plaintext, &mut buffer)
            .unwrap();
        let mut cipher_value = vec![0; 16];
        cipher_value.extend_from_slice(ciphertext);
        soap::EncryptedData {
            id: "synthetic-ciphertext".to_owned(),
            xmlns: "http://www.w3.org/2001/04/xmlenc#".to_owned(),
            el_type: "http://www.w3.org/2001/04/xmlenc#Element".to_owned(),
            encryption_method: soap::EncryptionMethod {
                algorithm: "http://www.w3.org/2001/04/xmlenc#aes256-cbc".to_owned(),
                val: None,
            },
            key_info: soap::KeyInfoWrap {
                ds: None,
                key_name: None,
                security_token_reference: Some(soap::SecurityTokenReference {
                    reference: soap::ReferenceUri {
                        uri: format!("#{reference}"),
                    },
                }),
            },
            cipher_data: soap::CipherData::new(BASE64_STANDARD.encode(cipher_value)),
        }
    }

    fn response_xml(keys: &[(&str, &[u8])]) -> String {
        let payload = b"<RequestSecurityTokenResponse><TokenType>urn:passport:compact</TokenType><AppliesTo><EndpointReference><Address>synthetic.invalid</Address></EndpointReference></AppliesTo><Lifetime><Created>2000-01-01T00:00:00Z</Created><Expires>2099-01-01T00:00:00Z</Expires></Lifetime><RequestedSecurityToken><BinarySecurityToken Id=\"synthetic-ticket\">fixture-not-a-ticket</BinarySecurityToken></RequestedSecurityToken></RequestSecurityTokenResponse>";
        response_xml_with_payload(keys, payload)
    }

    fn response_xml_with_payload(keys: &[(&str, &[u8])], payload: &[u8]) -> String {
        let nonce = [7; 32];
        let body = quick_xml::se::to_string(&encrypted("UniqueFixture", &nonce, payload)).unwrap();
        let pp = quick_xml::se::to_string(&encrypted(
            "UniqueFixture",
            &nonce,
            b"<pp><reqstatus>fixture-only</reqstatus></pp>",
        ))
        .unwrap();
        let keys: String = keys.iter().map(|(id, nonce)| format!(
            "<DerivedKeyToken Id=\"{id}\" Algorithm=\"urn:liveid:SP800108_CTR_HMAC_SHA256_DOUBLEDERIVED\"><Nonce>{}</Nonce></DerivedKeyToken>",
            BASE64_STANDARD.encode(nonce),
        )).collect();
        format!(
            "<Envelope><Header><Action>synthetic-action</Action><To>synthetic.invalid</To><Security><Timestamp><Created>2000-01-01T00:00:00Z</Created><Expires>2099-01-01T00:00:00Z</Expires></Timestamp>{keys}</Security><EncryptedPP>{pp}</EncryptedPP></Header><Body>{body}</Body></Envelope>"
        )
    }

    fn parse_and_decrypt(xml: &str) -> Result<soap::Envelope, super::super::RSTError> {
        let envelope: soap::Envelope = quick_xml::de::from_str(xml).unwrap();
        verify_and_decrypt_envelope(Some(signature()), xml.to_owned(), envelope)
    }

    fn wrap_xml_base64(value: &str) -> String {
        value
            .as_bytes()
            .chunks(4)
            .map(|chunk| std::str::from_utf8(chunk).unwrap())
            .collect::<Vec<_>>()
            .join(" \t\r\n")
    }

    fn wrapped_envelope(xml: &str) -> String {
        let envelope: soap::Envelope = quick_xml::de::from_str(xml).unwrap();
        let soap::BodyContent::EncryptedData(body) = envelope.body.body else {
            panic!("Expected only a synthetic encrypted body");
        };
        let mut wrapped = xml.to_owned();
        for value in [
            envelope.header.security.derived_key_tokens[0]
                .nonce
                .as_str(),
            envelope
                .header
                .encrypted_pp
                .as_ref()
                .unwrap()
                .encrypted_data
                .cipher_data
                .cipher_value
                .as_str(),
            body.cipher_data.cipher_value.as_str(),
        ] {
            wrapped = wrapped.replace(value, &wrap_xml_base64(value));
        }
        wrapped
    }

    fn signed_fixture_envelope(xml: String) -> String {
        let xml = xml
            .replacen(
                "<Envelope>",
                "<Envelope xmlns:ds=\"http://www.w3.org/2000/09/xmldsig#\" xmlns:wsse=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd\">",
                1,
            )
            .replacen("Id=\"synthetic-ciphertext\"", "Id=\"SignedPP\"", 1)
            .replacen("Id=\"synthetic-ciphertext\"", "Id=\"SignedBody\"", 1);
        let mut signature_node = soap::Signature::empty_hmac();
        signature_node.signed_info.reference = vec![
            soap::SignatureReference::exclusive("#SignedPP"),
            soap::SignatureReference::exclusive("#SignedBody"),
        ];
        signature_node
            .key_info
            .as_mut()
            .unwrap()
            .security_token_reference
            .reference
            .uri = "#UniqueFixture".to_owned();
        let signature_xml = quick_xml::se::to_string(&signature_node).unwrap();
        let xml = xml.replace("</Security>", &format!("{signature_xml}</Security>"));
        utils::sign_xml(Some(&signature()), &[7; 32], xml).unwrap()
    }

    fn assert_static_rejection(xml: &str) {
        let error = parse_and_decrypt(xml).unwrap_err();
        assert!(matches!(
            error,
            super::super::RSTError::InvalidEncryptedPayload
        ));
        assert_eq!(
            error.to_string(),
            "Response contains an invalid encrypted payload"
        );
        assert_eq!(format!("{error:?}"), "InvalidEncryptedPayload");
    }

    #[test]
    fn management_xml_base64_device_proof_after_real_envelope_decryption() {
        use crate::models::secrets::{Token, device_token_structurally_valid};

        let mut secret = [0; 4096];
        secret[..4].copy_from_slice(&4u32.to_le_bytes());
        let encoded_secret = BASE64_STANDARD.encode(secret);
        let encoded_cipher = BASE64_STANDARD.encode(b"synthetic-not-a-provider-ticket");
        for (wrap_cipher, wrap_secret) in
            [(false, false), (true, false), (false, true), (true, true)]
        {
            let cipher = if wrap_cipher {
                wrap_xml_base64(&encoded_cipher)
            } else {
                encoded_cipher.clone()
            };
            let secret = if wrap_secret {
                wrap_xml_base64(&encoded_secret)
            } else {
                encoded_secret.clone()
            };
            let encrypted_ticket =
                quick_xml::se::to_string(&soap::EncryptedData::devicesoftware(cipher)).unwrap();
            let payload = format!(
                "<RequestSecurityTokenResponse><TokenType>urn:passport:legacy</TokenType><AppliesTo><EndpointReference><Address>http://Passport.NET/tb</Address></EndpointReference></AppliesTo><Lifetime><Created>2000-01-01T00:00:00Z</Created><Expires>2099-01-01T00:00:00Z</Expires></Lifetime><RequestedSecurityToken>{encrypted_ticket}</RequestedSecurityToken><RequestedProofToken><BinarySecret>{secret}</BinarySecret></RequestedProofToken></RequestSecurityTokenResponse>"
            );
            let xml = signed_fixture_envelope(response_xml_with_payload(
                &[("UniqueFixture", &[7; 32])],
                payload.as_bytes(),
            ));
            let envelope = parse_and_decrypt(&xml).unwrap();
            assert!(envelope.header.encrypted_pp.is_none());
            assert_eq!(
                envelope.header.pp.unwrap().req_status.as_deref(),
                Some("fixture-only")
            );
            let response = crate::api::live::single_device_response(envelope.body.body).unwrap();
            let Token::Legacy(token) = Token::from_response_checked(response).unwrap() else {
                panic!("Expected only a synthetic legacy proof");
            };
            assert!(
                device_token_structurally_valid(&token),
                "XML base64 lexical whitespace rejected after AES envelope decryption"
            );
        }
    }

    #[test]
    fn management_opaque_ticket_after_signed_decryption_preserves_real_consumers() {
        use crate::models::secrets::{Token, device_token_structurally_valid};

        #[derive(serde::Deserialize)]
        #[serde(rename_all = "PascalCase")]
        struct BuiltEnvelope {
            header: BuiltHeader,
        }
        #[derive(serde::Deserialize)]
        #[serde(rename_all = "PascalCase")]
        struct BuiltHeader {
            security: BuiltSecurity,
        }
        #[derive(serde::Deserialize)]
        #[serde(rename_all = "PascalCase")]
        struct BuiltSecurity {
            #[serde(default)]
            binary_security_token: Vec<soap::BinarySecurityTokenReq>,
            encrypted_data: Option<soap::EncryptedData>,
        }
        let opaque = "synthetic issuer ticket ! <&> +/=_-";
        let mut secret = [0; 4096];
        secret[..4].copy_from_slice(&4u32.to_le_bytes());
        let encoded_secret = BASE64_STANDARD.encode(secret);
        let encrypted_ticket =
            quick_xml::se::to_string(&soap::EncryptedData::devicesoftware(opaque.to_owned()))
                .unwrap();
        let payload = format!(
            "<RequestSecurityTokenResponse><TokenType>urn:passport:legacy</TokenType><AppliesTo><EndpointReference><Address>http://Passport.NET/tb</Address></EndpointReference></AppliesTo><Lifetime><Created>2000-01-01T00:00:00Z</Created><Expires>2099-01-01T00:00:00Z</Expires></Lifetime><RequestedSecurityToken>{encrypted_ticket}</RequestedSecurityToken><RequestedProofToken><BinarySecret>{encoded_secret}</BinarySecret></RequestedProofToken></RequestSecurityTokenResponse>"
        );
        let xml = signed_fixture_envelope(response_xml_with_payload(
            &[("UniqueFixture", &[7; 32])],
            payload.as_bytes(),
        ));
        let envelope = parse_and_decrypt(&xml).unwrap();
        let response = crate::api::live::single_device_response(envelope.body.body).unwrap();
        let Token::Legacy(device) = Token::from_response_checked(response).unwrap() else {
            panic!("Expected only a synthetic signed legacy issuer ticket");
        };
        assert!(device_token_structurally_valid(&device));
        let hmac = crate::api::live::device_hmac_secret(&device).unwrap();
        let user = crate::models::secrets::LegacyToken {
            token: encrypted_ticket,
            ..device.clone()
        };
        for with_user in [false, true] {
            let mut builder = super::super::RSTRequestBuilder::new()
                .device_token(device.clone())
                .signature(super::super::RSTSignature::Hmac {
                    clep_secret: &*hmac,
                    tpm_secret: &[],
                })
                .scope_policy("http://Passport.NET/tb", None);
            if with_user {
                builder = builder.user_token(Token::Legacy(user.clone()));
            }
            let request = builder.build().unwrap();
            let envelope: BuiltEnvelope = quick_xml::de::from_str(&request.signed_xml).unwrap();
            if with_user {
                assert_eq!(envelope.header.security.binary_security_token.len(), 1);
                assert_eq!(
                    envelope.header.security.binary_security_token[0].value,
                    device.token
                );
            } else {
                assert_eq!(
                    envelope
                        .header
                        .security
                        .encrypted_data
                        .unwrap()
                        .cipher_data
                        .cipher_value,
                    opaque
                );
            }
        }
        let parsed: soap::Envelope = quick_xml::de::from_str(&xml).unwrap();
        let soap::BodyContent::EncryptedData(cipher) = parsed.body.body else {
            panic!("Expected only a synthetic signed encrypted body");
        };
        let mut altered = cipher.cipher_data.cipher_value.clone();
        altered.replace_range(..1, if altered.starts_with('A') { "B" } else { "A" });
        let tampered = xml.replace(&cipher.cipher_data.cipher_value, &altered);
        assert!(matches!(
            parse_and_decrypt(&tampered),
            Err(super::super::RSTError::InvalidResponseSignature(_))
        ));
    }

    #[test]
    fn management_xml_base64_signed_envelope_preserves_original_verification_and_decryption() {
        let original = response_xml(&[("UniqueFixture", &[7; 32])]);
        for xml in [original.clone(), wrapped_envelope(&original)] {
            let signed = signed_fixture_envelope(xml);
            let envelope = parse_and_decrypt(&signed).unwrap();
            assert!(envelope.header.encrypted_pp.is_none());
            assert_eq!(
                envelope.header.pp.unwrap().req_status.as_deref(),
                Some("fixture-only")
            );
            assert!(matches!(
                envelope.body.body,
                soap::BodyContent::RequestSecurityTokenResponse(_)
            ));
            let parsed: soap::Envelope = quick_xml::de::from_str(&signed).unwrap();
            let soap::BodyContent::EncryptedData(cipher) = parsed.body.body else {
                panic!("Expected only a synthetic encrypted body");
            };
            let mut altered = cipher.cipher_data.cipher_value.clone();
            let replacement = if altered.starts_with('A') { "B" } else { "A" };
            altered.replace_range(..1, replacement);
            let tampered = signed.replace(&cipher.cipher_data.cipher_value, &altered);
            assert!(matches!(
                parse_and_decrypt(&tampered),
                Err(super::super::RSTError::InvalidResponseSignature(_))
            ));
        }
    }

    #[test]
    fn management_xml_base64_envelope_rejects_invalid_nonce_and_cipher_lexical_forms() {
        let xml = response_xml(&[("UniqueFixture", &[7; 32])]);
        let parsed: soap::Envelope = quick_xml::de::from_str(&xml).unwrap();
        let soap::BodyContent::EncryptedData(cipher) = parsed.body.body else {
            panic!("Expected only a synthetic encrypted body");
        };
        let nonce = &parsed.header.security.derived_key_tokens[0].nonce;
        for invalid in [
            "AB==",
            "AA===",
            "AA-_",
            "AA\u{00a0}==",
            "AA\u{000b}==",
            "AA\u{000c}==",
        ] {
            for value in [nonce.as_str(), cipher.cipher_data.cipher_value.as_str()] {
                assert_static_rejection(&xml.replace(value, invalid));
            }
        }
    }

    #[test]
    fn management_soap_unique_nonce_ids_preserve_full_header_and_body_decryption_in_either_order() {
        for keys in [
            vec![
                ("UniqueFixture", [7; 32].as_slice()),
                ("OtherFixture", [8; 32].as_slice()),
            ],
            vec![
                ("OtherFixture", [8; 32].as_slice()),
                ("UniqueFixture", [7; 32].as_slice()),
            ],
        ] {
            let envelope = parse_and_decrypt(&response_xml(&keys)).unwrap();
            assert!(envelope.header.encrypted_pp.is_none());
            assert_eq!(
                envelope.header.pp.unwrap().req_status.as_deref(),
                Some("fixture-only")
            );
            let soap::BodyContent::RequestSecurityTokenResponse(response) = envelope.body.body
            else {
                panic!("Synthetic response was not decrypted");
            };
            assert_eq!(
                response.applies_to.endpoint_reference.address,
                "synthetic.invalid"
            );
            assert_eq!(
                response
                    .requested_security_token
                    .binary_security_token
                    .unwrap()
                    .value,
                "fixture-not-a-ticket"
            );
        }
    }

    #[test]
    fn management_soap_identical_duplicate_nonce_ids_are_rejected_after_full_xml_decode() {
        assert_static_rejection(&response_xml(&[
            ("UniqueFixture", &[7; 32]),
            ("UniqueFixture", &[7; 32]),
        ]));
    }

    #[test]
    fn management_soap_conflicting_duplicate_nonce_ids_are_rejected_in_both_orders() {
        for keys in [
            [
                ("UniqueFixture", [8; 32].as_slice()),
                ("UniqueFixture", [7; 32].as_slice()),
            ],
            [
                ("UniqueFixture", [7; 32].as_slice()),
                ("UniqueFixture", [8; 32].as_slice()),
            ],
        ] {
            assert_static_rejection(&response_xml(&keys));
        }
    }

    #[test]
    fn management_soap_empty_nonce_id_is_rejected_instead_of_ignored() {
        assert_static_rejection(&response_xml(&[
            ("", &[8; 32]),
            ("UniqueFixture", &[7; 32]),
        ]));
    }
}
