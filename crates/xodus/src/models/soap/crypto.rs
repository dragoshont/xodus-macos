use serde::{Deserialize, Serialize};

use super::base::ReferenceUri;

pub(crate) const MAX_XML_RESPONSE_BYTES: usize = 1024 * 1024;

pub(crate) fn decode_xml_base64(value: &str) -> Result<Vec<u8>, &'static str> {
    use base64::prelude::*;

    if value.len() > MAX_XML_RESPONSE_BYTES {
        return Err("XML base64 value exceeds the response bound");
    }
    // XML Schema base64Binary permits only SP/TAB/CR/LF in its lexical form.
    let xml_whitespace = |byte: u8| matches!(byte, b' ' | b'\t' | b'\r' | b'\n');
    if value.bytes().any(xml_whitespace) {
        let encoded: Vec<_> = value
            .bytes()
            .filter(|byte| !xml_whitespace(*byte))
            .collect();
        BASE64_STANDARD.decode(encoded)
    } else {
        BASE64_STANDARD.decode(value)
    }
    .map_err(|_| "Invalid XML base64 encoding")
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct EncryptionMethod {
    #[serde(rename = "@Algorithm")]
    pub algorithm: String,
    #[serde(rename = "$value")]
    pub val: Option<String>,
}

impl Default for EncryptionMethod {
    fn default() -> Self {
        Self {
            algorithm: "http://www.w3.org/2001/04/xmlenc#tripledes-cbc".to_string(),
            val: None,
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "PascalCase")]
pub struct CipherData {
    pub cipher_value: String,
}

impl CipherData {
    pub fn new(key: String) -> Self {
        Self { cipher_value: key }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SecurityTokenReference {
    #[serde(rename = "wsse:Reference", alias = "Reference")]
    pub reference: ReferenceUri,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SignatureKeyInfo {
    #[serde(
        rename = "wsse:SecurityTokenReference",
        alias = "SecurityTokenReference"
    )]
    pub security_token_reference: SecurityTokenReference,
}

impl SignatureKeyInfo {
    #[must_use]
    pub fn sign_key() -> Self {
        Self {
            security_token_reference: SecurityTokenReference {
                reference: ReferenceUri {
                    uri: "#SignKey".to_string(),
                },
            },
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AlgorithmNode {
    #[serde(rename = "@Algorithm")]
    pub algorithm: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SignatureTransforms {
    #[serde(rename = "Transform")]
    pub transform: Vec<AlgorithmNode>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SignatureReference {
    #[serde(rename = "@URI")]
    pub uri: String,
    #[serde(rename = "Transforms")]
    pub transforms: SignatureTransforms,
    #[serde(rename = "DigestMethod")]
    pub digest_method: AlgorithmNode,
    #[serde(rename = "DigestValue")]
    pub digest_value: String,
}

impl SignatureReference {
    pub fn exclusive(uri: &str) -> Self {
        Self {
            uri: uri.to_string(),
            transforms: SignatureTransforms {
                transform: vec![AlgorithmNode {
                    algorithm: "http://www.w3.org/2001/10/xml-exc-c14n#".to_string(),
                }],
            },
            digest_method: AlgorithmNode {
                algorithm: "http://www.w3.org/2001/04/xmlenc#sha256".to_string(),
            },
            digest_value: String::new(),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SignedInfo {
    #[serde(rename = "CanonicalizationMethod")]
    pub canonicalization_method: AlgorithmNode,
    #[serde(rename = "SignatureMethod")]
    pub signature_method: AlgorithmNode,
    #[serde(rename = "Reference")]
    pub reference: Vec<SignatureReference>,
}

impl Default for SignedInfo {
    fn default() -> Self {
        Self {
            canonicalization_method: AlgorithmNode {
                algorithm: "http://www.w3.org/2001/10/xml-exc-c14n#".to_string(),
            },
            signature_method: AlgorithmNode {
                algorithm: "http://www.w3.org/2001/04/xmldsig-more#hmac-sha256".to_string(),
            },
            reference: vec![
                SignatureReference::exclusive("#RST0"),
                SignatureReference::exclusive("#Timestamp"),
                SignatureReference::exclusive("#PPAuthInfo"),
            ],
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Signature {
    #[serde(rename = "@xmlns")]
    pub xmlns: String,
    #[serde(rename = "SignedInfo")]
    pub signed_info: SignedInfo,
    #[serde(rename = "SignatureValue")]
    pub signature_value: String,
    #[serde(rename = "KeyInfo", skip_serializing_if = "Option::is_none")]
    pub key_info: Option<SignatureKeyInfo>,
}

impl Signature {
    pub fn empty_hmac() -> Self {
        Self {
            xmlns: "http://www.w3.org/2000/09/xmldsig#".to_string(),
            signed_info: SignedInfo::default(),
            signature_value: String::new(),
            key_info: Some(SignatureKeyInfo::sign_key()),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct KeyInfoWrap {
    #[serde(
        rename = "@xmlns:ds",
        alias = "@xmlns",
        skip_serializing_if = "Option::is_none"
    )]
    pub ds: Option<String>,
    #[serde(
        rename = "ds:KeyName",
        alias = "KeyName",
        skip_serializing_if = "Option::is_none"
    )]
    pub key_name: Option<String>,
    #[serde(
        rename = "wsse:SecurityTokenReference",
        alias = "SecurityTokenReference",
        skip_serializing_if = "Option::is_none"
    )]
    pub security_token_reference: Option<SecurityTokenReference>,
}

impl KeyInfoWrap {
    pub fn sts() -> Self {
        Self {
            ds: Some("http://www.w3.org/2000/09/xmldsig#".to_string()),
            key_name: Some("http://Passport.NET/STS".to_string()),
            security_token_reference: None,
        }
    }

    pub fn as_signature(self) -> SignatureKeyInfo {
        let Self {
            security_token_reference: Some(reference),
            ..
        } = self
        else {
            panic!("Key is not named");
        };

        SignatureKeyInfo {
            security_token_reference: reference,
        }
    }

    pub fn as_named(self) -> NamedKeyInfo {
        let Self {
            ds,
            key_name,
            security_token_reference: _,
        } = self;

        NamedKeyInfo {
            ds: ds.unwrap_or_else(|| "http://www.w3.org/2000/09/xmldsig#".to_string()),
            key_name: key_name.expect("Key is not named"),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct NamedKeyInfo {
    #[serde(rename = "@xmlns:ds")]
    pub ds: String,
    #[serde(rename = "ds:KeyName", alias = "KeyName")]
    pub key_name: String,
}

impl NamedKeyInfo {
    pub fn sts() -> Self {
        Self {
            ds: "http://www.w3.org/2000/09/xmldsig#".to_string(),
            key_name: "http://Passport.NET/STS".to_string(),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "PascalCase")]
pub struct EncryptedData {
    #[serde(rename = "@Id")]
    pub id: String,
    #[serde(rename = "@xmlns")]
    pub xmlns: String,
    #[serde(rename = "@Type")]
    pub el_type: String,

    pub encryption_method: EncryptionMethod,
    #[serde(rename = "ds:KeyInfo", alias = "KeyInfo")]
    pub key_info: KeyInfoWrap,
    pub cipher_data: CipherData,
}

impl EncryptedData {
    pub fn devicesoftware(key: String) -> Self {
        Self {
            id: "devicesoftware".to_string(),
            xmlns: "http://www.w3.org/2001/04/xmlenc#".to_string(),
            el_type: "http://www.w3.org/2001/04/xmlenc#Element".to_string(),
            encryption_method: EncryptionMethod::default(),
            key_info: KeyInfoWrap::sts(),
            cipher_data: CipherData::new(key),
        }
    }

    pub fn binary_da_token(key: String) -> Self {
        Self {
            id: "BinaryDAToken0".to_string(),
            xmlns: "http://www.w3.org/2001/04/xmlenc#".to_string(),
            el_type: "http://www.w3.org/2001/04/xmlenc#Element".to_string(),
            encryption_method: EncryptionMethod::default(),
            key_info: KeyInfoWrap::sts(),
            cipher_data: CipherData::new(key),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "PascalCase")]
pub struct EncryptedPP {
    pub encrypted_data: EncryptedData,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn management_xml_base64_preserves_bytes_and_canonical_input() {
        use base64::prelude::*;

        let bytes: Vec<_> = (0..=255).collect();
        let encoded = BASE64_STANDARD.encode(&bytes);
        let original = encoded.clone();
        for whitespace in [" ", "\t", "\r", "\n", " \t\r\n"] {
            let wrapped = encoded
                .as_bytes()
                .chunks(4)
                .map(|chunk| std::str::from_utf8(chunk).unwrap())
                .collect::<Vec<_>>()
                .join(whitespace);
            assert_eq!(decode_xml_base64(&wrapped).unwrap(), bytes);
        }
        assert_eq!(decode_xml_base64(&encoded).unwrap(), bytes);
        assert_eq!(encoded, original);
        assert_eq!(decode_xml_base64(" \tA A = = \r\n").unwrap(), [0]);
        assert!(decode_xml_base64(" \t\r\n").unwrap().is_empty());
    }

    #[test]
    fn management_xml_base64_rejects_invalid_alphabet_padding_and_non_xml_whitespace() {
        for encoded in [
            "AA",
            "AA=",
            "AA===",
            "AB==",
            "AAB=",
            "AA==AAAA",
            "AA-_",
            "AA\u{00a0}==",
            "AA\u{2003}==",
            "AA\u{000b}==",
            "AA\u{000c}==",
            "AA==SECRET_SENTINEL_XML_BASE64",
        ] {
            assert_eq!(
                decode_xml_base64(encoded),
                Err("Invalid XML base64 encoding")
            );
        }
    }

    #[test]
    fn management_xml_base64_checks_raw_bound_before_whitespace_normalization() {
        let at_bound = " ".repeat(MAX_XML_RESPONSE_BYTES);
        assert!(decode_xml_base64(&at_bound).unwrap().is_empty());
        let over_bound = format!("{at_bound} ");
        assert_eq!(
            decode_xml_base64(&over_bound),
            Err("XML base64 value exceeds the response bound")
        );
        let canonical_at_bound = "A".repeat(MAX_XML_RESPONSE_BYTES);
        assert_eq!(
            decode_xml_base64(&canonical_at_bound).unwrap().len(),
            MAX_XML_RESPONSE_BYTES / 4 * 3
        );
    }

    #[test]
    fn key_info_wrap_deserializes_ds_key_info_key_name() {
        let xml = r#"<ds:KeyInfo xmlns:ds="http://www.w3.org/2000/09/xmldsig#">
                        <ds:KeyName>http://Passport.NET/STS</ds:KeyName>
                    </ds:KeyInfo>"#;

        let key_info: KeyInfoWrap =
            quick_xml::de::from_str(xml).expect("failed to deserialize key info");

        let named = key_info.as_named();
        assert_eq!(named.ds, "http://www.w3.org/2000/09/xmldsig#");
        assert_eq!(named.key_name, "http://Passport.NET/STS");
    }

    #[test]
    fn key_info_wrap_deserializes_wsse_security_token_reference() {
        let xml = r##"<KeyInfo xmlns:wsse="http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd">
                        <wsse:SecurityTokenReference>
                            <wsse:Reference URI="#SignKey"></wsse:Reference>
                        </wsse:SecurityTokenReference>
                    </KeyInfo>"##;

        let key_info: KeyInfoWrap =
            quick_xml::de::from_str(xml).expect("failed to deserialize key info");

        let signature = key_info.as_signature();
        assert_eq!(signature.security_token_reference.reference.uri, "#SignKey");
    }
}
