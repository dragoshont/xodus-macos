use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use uuid::Uuid;

use crate::wire::{ErrorCode, WireError};

pub const VERSION: u32 = 1;
pub const MAX_CONFIGURATION_BYTES: usize = 16 * 1024;

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub enum Provider {
    Gptk3,
    Gptk4,
    #[serde(rename = "crossover")]
    CrossOver,
    StandaloneWine,
}

impl Provider {
    pub const ALL: [Self; 4] = [
        Self::Gptk3,
        Self::Gptk4,
        Self::CrossOver,
        Self::StandaloneWine,
    ];
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub enum EngineProvenance {
    AppleToolkit,
    UserInstalledCrossOver,
    UserSelectedWine,
    UserSelectedSourceBuild,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub enum EngineKind {
    Wine,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub enum GraphicsBackend {
    D3dMetal,
    Dxvk,
    WineD3d,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub enum GraphicsProvenance {
    AppleToolkit,
    CrossOverBundled,
    EngineBundled,
    UserSelected,
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct Engine {
    pub kind: EngineKind,
    pub provenance: EngineProvenance,
    #[serde(deserialize_with = "required_nullable")]
    pub version: Option<String>,
    #[serde(deserialize_with = "required_nullable")]
    pub artifact_sha256: Option<String>,
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct Graphics {
    #[serde(deserialize_with = "required_nullable")]
    pub backend: Option<GraphicsBackend>,
    #[serde(deserialize_with = "required_nullable")]
    pub provenance: Option<GraphicsProvenance>,
    #[serde(deserialize_with = "required_nullable")]
    pub version: Option<String>,
    #[serde(deserialize_with = "required_nullable")]
    pub artifact_sha256: Option<String>,
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct Configuration {
    pub version: u32,
    pub provider: Provider,
    #[serde(deserialize_with = "required_nullable")]
    pub provider_version: Option<String>,
    pub engine: Engine,
    pub graphics: Graphics,
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ConfigurationPlan {
    pub version: u32,
    pub configuration: Configuration,
    #[serde(rename = "generationID")]
    pub generation_id: String,
    pub prefix_relative_path: String,
    pub installation: InstallationAssessment,
    pub device_preflight: PreflightAssessment,
    pub game_verification: GameAssessment,
    pub launchable: bool,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub enum InstallationAssessment {
    NotInspected,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub enum PreflightAssessment {
    NotPerformed,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub enum GameAssessment {
    NotVerified,
}

fn invalid() -> WireError {
    WireError::new(
        ErrorCode::InvalidRequest,
        "Runtime configuration requires a supported provider and independent bounded engine/graphics metadata.",
        false,
    )
}

fn required_nullable<'de, T: Deserialize<'de>, D: serde::Deserializer<'de>>(
    deserializer: D,
) -> Result<Option<T>, D::Error> {
    Option::deserialize(deserializer)
}

fn version_valid(value: &Option<String>) -> bool {
    value.as_ref().is_none_or(|version| {
        !version.is_empty()
            && version.is_ascii()
            && version.len() <= 64
            && version.trim() == version
            && !version.chars().any(char::is_control)
    })
}

fn digest_valid(value: &Option<String>) -> bool {
    value.as_ref().is_none_or(|digest| {
        digest.len() == 64
            && digest
                .bytes()
                .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
    })
}

impl Configuration {
    pub fn preset(provider: Provider) -> Self {
        let (engine, backend, provenance) = match provider {
            Provider::Gptk3 | Provider::Gptk4 => (
                EngineProvenance::AppleToolkit,
                Some(GraphicsBackend::D3dMetal),
                Some(GraphicsProvenance::AppleToolkit),
            ),
            Provider::CrossOver => (EngineProvenance::UserInstalledCrossOver, None, None),
            Provider::StandaloneWine => (EngineProvenance::UserSelectedWine, None, None),
        };
        Self {
            version: VERSION,
            provider,
            provider_version: None,
            engine: Engine {
                kind: EngineKind::Wine,
                provenance: engine,
                version: None,
                artifact_sha256: None,
            },
            graphics: Graphics {
                backend,
                provenance,
                version: None,
                artifact_sha256: None,
            },
        }
    }

    pub fn validate(&self) -> Result<(), WireError> {
        let provenance_matches = match self.provider {
            Provider::Gptk3 | Provider::Gptk4 => {
                self.engine.provenance == EngineProvenance::AppleToolkit
            }
            Provider::CrossOver => {
                self.engine.provenance == EngineProvenance::UserInstalledCrossOver
            }
            Provider::StandaloneWine => matches!(
                self.engine.provenance,
                EngineProvenance::UserSelectedWine | EngineProvenance::UserSelectedSourceBuild
            ),
        };
        if self.version != VERSION
            || !provenance_matches
            || !version_valid(&self.provider_version)
            || !version_valid(&self.engine.version)
            || !version_valid(&self.graphics.version)
            || !digest_valid(&self.engine.artifact_sha256)
            || !digest_valid(&self.graphics.artifact_sha256)
            || (self.graphics.backend.is_none()
                && (self.graphics.provenance.is_some()
                    || self.graphics.version.is_some()
                    || self.graphics.artifact_sha256.is_some()))
            || (self.graphics.backend.is_some() && self.graphics.provenance.is_none())
        {
            return Err(invalid());
        }
        Ok(())
    }
}

pub fn parse(bytes: &[u8]) -> Result<Configuration, WireError> {
    if bytes.is_empty() || bytes.len() > MAX_CONFIGURATION_BYTES {
        return Err(invalid());
    }
    let configuration: Configuration = serde_json::from_slice(bytes).map_err(|_| invalid())?;
    configuration.validate()?;
    Ok(configuration)
}

pub fn plan(configuration: Configuration) -> Result<ConfigurationPlan, WireError> {
    configuration.validate()?;
    let bytes = serde_json::to_vec(&configuration).map_err(|_| invalid())?;
    let identity = crate::staging::digest_hex(&Sha256::digest(bytes));
    let generation_id = Uuid::new_v4().to_string();
    Ok(ConfigurationPlan {
        version: VERSION,
        configuration,
        prefix_relative_path: format!("runtime-prefixes/v1/{identity}/{generation_id}"),
        generation_id,
        installation: InstallationAssessment::NotInspected,
        device_preflight: PreflightAssessment::NotPerformed,
        game_verification: GameAssessment::NotVerified,
        launchable: false,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn all_four_presets_are_selectable_without_claiming_installation_or_verification() {
        for provider in Provider::ALL {
            let configuration = Configuration::preset(provider);
            let bytes = serde_json::to_vec(&configuration).unwrap();
            assert_eq!(parse(&bytes).unwrap(), configuration);
            let result = plan(configuration).unwrap();
            assert!(!result.launchable);
            assert_eq!(result.installation, InstallationAssessment::NotInspected);
            assert_eq!(result.device_preflight, PreflightAssessment::NotPerformed);
            assert_eq!(result.game_verification, GameAssessment::NotVerified);
        }
    }

    #[test]
    fn standalone_wine11_and_d3dmetal4_are_not_toolkit_or_old_engine_equivalence() {
        let mut configuration = Configuration::preset(Provider::StandaloneWine);
        configuration.engine.provenance = EngineProvenance::UserSelectedSourceBuild;
        configuration.engine.version = Some("11.0".to_owned());
        configuration.graphics.backend = Some(GraphicsBackend::D3dMetal);
        configuration.graphics.provenance = Some(GraphicsProvenance::UserSelected);
        configuration.graphics.version = Some("4.0".to_owned());
        let result = plan(configuration).unwrap();
        assert_eq!(result.configuration.provider, Provider::StandaloneWine);
        assert_eq!(result.configuration.engine.version.as_deref(), Some("11.0"));
        assert_eq!(
            result.configuration.graphics.version.as_deref(),
            Some("4.0")
        );
        assert!(!result.launchable);
    }

    #[test]
    fn generations_and_component_changes_never_reuse_a_prior_prefix_plan() {
        let configuration = Configuration::preset(Provider::Gptk4);
        let first = plan(configuration.clone()).unwrap();
        let second = plan(configuration.clone()).unwrap();
        assert_ne!(first.prefix_relative_path, second.prefix_relative_path);
        let mut changed = configuration;
        changed.engine.version = Some("11.0".to_owned());
        let third = plan(changed.clone()).unwrap();
        changed.graphics.version = Some("4.0".to_owned());
        let fourth = plan(changed).unwrap();
        assert_ne!(
            third.prefix_relative_path.split('/').nth(2),
            fourth.prefix_relative_path.split('/').nth(2)
        );
        assert!(crate::staging::valid_relative_path(
            &fourth.prefix_relative_path
        ));
        let mut changed = fourth.configuration;
        changed.engine.artifact_sha256 = Some("1".repeat(64));
        let fifth = plan(changed.clone()).unwrap();
        changed.graphics.artifact_sha256 = Some("2".repeat(64));
        changed.graphics.backend = Some(GraphicsBackend::Dxvk);
        let sixth = plan(changed).unwrap();
        assert_ne!(
            fifth.prefix_relative_path.split('/').nth(2),
            sixth.prefix_relative_path.split('/').nth(2)
        );
    }

    #[test]
    fn invalid_unknown_duplicate_and_unbounded_metadata_is_rejected_explicitly() {
        let mut configuration = Configuration::preset(Provider::CrossOver);
        configuration.engine.provenance = EngineProvenance::AppleToolkit;
        assert!(plan(configuration).is_err());
        let mut configuration = Configuration::preset(Provider::Gptk3);
        configuration.engine.artifact_sha256 = Some("not-a-digest".to_owned());
        assert!(plan(configuration).is_err());
        let mut configuration = Configuration::preset(Provider::StandaloneWine);
        configuration.graphics.version = Some("4.0".to_owned());
        assert!(plan(configuration).is_err());
        assert!(parse(&vec![b'x'; MAX_CONFIGURATION_BYTES + 1]).is_err());
        let bytes = serde_json::to_string(&Configuration::preset(Provider::Gptk4)).unwrap();
        assert!(
            parse(
                bytes
                    .replacen("\"version\":1", "\"version\":1,\"version\":1", 1)
                    .as_bytes()
            )
            .is_err()
        );
        assert!(
            parse(
                bytes
                    .replacen("\"version\":1", "\"version\":2", 1)
                    .as_bytes()
            )
            .is_err()
        );
        assert!(
            parse(
                bytes
                    .replacen("\"version\":1", "\"version\":1,\"installed\":true", 1)
                    .as_bytes()
            )
            .is_err()
        );
        for field in [
            "providerVersion",
            "engine.version",
            "engine.artifactSha256",
            "graphics.backend",
            "graphics.provenance",
            "graphics.version",
            "graphics.artifactSha256",
        ] {
            let mut value = serde_json::to_value(Configuration::preset(Provider::Gptk4)).unwrap();
            if let Some((parent, key)) = field.split_once('.') {
                value[parent].as_object_mut().unwrap().remove(key);
            } else {
                value.as_object_mut().unwrap().remove(field);
            }
            assert!(
                parse(&serde_json::to_vec(&value).unwrap()).is_err(),
                "{field}"
            );
        }
    }

    #[test]
    fn exact_byte_and_version_bounds_are_checked_not_approximated() {
        let mut configuration = Configuration::preset(Provider::StandaloneWine);
        configuration.engine.version = Some("x".repeat(64));
        configuration.validate().unwrap();
        configuration.engine.version = Some("x".repeat(65));
        assert!(configuration.validate().is_err());
        configuration.engine.version = Some("11.0\n".to_owned());
        assert!(configuration.validate().is_err());
        for digest in ["a".repeat(64) + "\n", "A".repeat(64), "a".repeat(63)] {
            let mut configuration = Configuration::preset(Provider::Gptk4);
            configuration.engine.artifact_sha256 = Some(digest);
            assert!(configuration.validate().is_err());
        }
        let mut bytes = serde_json::to_vec(&Configuration::preset(Provider::Gptk3)).unwrap();
        bytes.resize(MAX_CONFIGURATION_BYTES, b' ');
        parse(&bytes).unwrap();
        bytes.push(b' ');
        assert!(parse(&bytes).is_err());
    }

    #[test]
    fn canonical_configuration_fixtures_are_real_parser_inputs_not_installation_evidence() {
        let fixture: serde_json::Value = serde_json::from_str(include_str!(concat!(
            env!("CARGO_MANIFEST_DIR"),
            "/../../docs/fixtures/runtime-providers-v1.json"
        )))
        .unwrap();
        for value in fixture["configurations"].as_array().unwrap() {
            let configuration = parse(&serde_json::to_vec(value).unwrap()).unwrap();
            assert_eq!(serde_json::to_value(configuration.clone()).unwrap(), *value);
            assert!(!plan(configuration).unwrap().launchable);
        }
        for value in fixture["invalidConfigurations"].as_array().unwrap() {
            assert!(parse(&serde_json::to_vec(value).unwrap()).is_err());
        }
        for value in fixture["plans"].as_array().unwrap() {
            let configuration =
                parse(&serde_json::to_vec(&value["configuration"]).unwrap()).unwrap();
            let actual = plan(configuration).unwrap();
            assert_eq!(
                actual.prefix_relative_path.split('/').nth(2),
                value["prefixRelativePath"]
                    .as_str()
                    .unwrap()
                    .split('/')
                    .nth(2)
            );
        }
    }
}
