use std::collections::HashMap;

use base64::prelude::*;

use crate::api::live::utils;
use crate::models::soap;

pub struct RSTRequest<'a> {
    pub signed_xml: String,
    pub signature: Option<super::RSTSignature<'a>>,
}

pub(crate) async fn bounded_response_text(
    mut response: reqwest::Response,
) -> Result<String, super::RSTError> {
    const MAX: usize = 1024 * 1024;
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
        let nonce = BASE64_STANDARD.decode(nonce)?;
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
        let nonce = [7; 32];
        let payload = b"<RequestSecurityTokenResponse><TokenType>urn:passport:compact</TokenType><AppliesTo><EndpointReference><Address>synthetic.invalid</Address></EndpointReference></AppliesTo><Lifetime><Created>2000-01-01T00:00:00Z</Created><Expires>2099-01-01T00:00:00Z</Expires></Lifetime><RequestedSecurityToken><BinarySecurityToken Id=\"synthetic-ticket\">fixture-not-a-ticket</BinarySecurityToken></RequestedSecurityToken></RequestSecurityTokenResponse>";
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
