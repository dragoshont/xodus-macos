use quick_xml::events::Event;
use quick_xml::name::ResolveResult;
use quick_xml::reader::NsReader;
use serde::{Deserialize, Serialize};
use thiserror::Error;

pub const MAX_MANIFEST_BYTES: usize = 1024 * 1024;

#[derive(Debug, Error)]
pub enum ManifestError {
    #[error("Manifest exceeds the 1 MiB inspection limit")]
    TooLarge,
    #[error("Manifest must be UTF-8 XML")]
    Encoding,
    #[error("Manifest XML is malformed")]
    Xml,
    #[error("Manifest must have one supported Package or Bundle root")]
    Root,
    #[error("DTD declarations and custom entity references are not supported")]
    Entities,
    #[error("Manifest nesting exceeds the inspection limit")]
    Depth,
    #[error("Manifest is missing required metadata or contains duplicate fields")]
    Metadata(#[source] quick_xml::DeError),
    #[error("Manifest contains an unsafe relative filename")]
    Path,
    #[error("Bundle contains duplicate filenames")]
    DuplicateFilename,
    #[error("Bundle contains an unsupported package type")]
    PackageType,
    #[error("Architecture must be x86, x64, arm, arm64 or neutral")]
    Architecture,
    #[error("Bundle has no non-stub application for the requested architecture")]
    NoApplication,
    #[error("Bundle has multiple applications for the requested architecture")]
    AmbiguousApplication,
}

#[derive(Debug, Serialize)]
#[serde(tag = "Kind", content = "Manifest")]
pub enum Manifest {
    Package(PackageManifest),
    Bundle(BundleManifest),
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(rename_all = "PascalCase")]
pub struct Identity {
    #[serde(rename(deserialize = "@Name"))]
    pub name: String,
    #[serde(rename(deserialize = "@Publisher"))]
    pub publisher: String,
    #[serde(rename(deserialize = "@Version"))]
    pub version: String,
    #[serde(default, rename(deserialize = "@ProcessorArchitecture"))]
    pub processor_architecture: Option<String>,
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(rename_all = "PascalCase")]
pub struct PackageManifest {
    pub identity: Identity,
    #[serde(default)]
    pub dependencies: Dependencies,
    #[serde(default)]
    pub applications: Applications,
}

#[derive(Debug, Default, Serialize, Deserialize)]
#[serde(rename_all = "PascalCase")]
pub struct Dependencies {
    #[serde(default, rename(deserialize = "TargetDeviceFamily"))]
    pub target_device_families: Vec<TargetDeviceFamily>,
    #[serde(default, rename(deserialize = "PackageDependency"))]
    pub packages: Vec<PackageDependency>,
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(rename_all = "PascalCase")]
pub struct TargetDeviceFamily {
    #[serde(rename(deserialize = "@Name"))]
    pub name: String,
    #[serde(rename(deserialize = "@MinVersion"))]
    pub min_version: String,
    #[serde(default, rename(deserialize = "@MaxVersionTested"))]
    pub max_version_tested: Option<String>,
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(rename_all = "PascalCase")]
pub struct PackageDependency {
    #[serde(rename(deserialize = "@Name"))]
    pub name: String,
    #[serde(default, rename(deserialize = "@Publisher"))]
    pub publisher: Option<String>,
    #[serde(default, rename(deserialize = "@MinVersion"))]
    pub min_version: Option<String>,
    #[serde(default, rename(deserialize = "@ProcessorArchitecture"))]
    pub processor_architecture: Option<String>,
}

#[derive(Debug, Default, Serialize, Deserialize)]
#[serde(rename_all = "PascalCase")]
pub struct Applications {
    #[serde(default, rename(deserialize = "Application"))]
    pub applications: Vec<Application>,
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(rename_all = "PascalCase")]
pub struct Application {
    #[serde(rename(deserialize = "@Id"))]
    pub id: String,
    #[serde(default, rename(deserialize = "@Executable"))]
    pub executable: Option<String>,
    #[serde(default, rename(deserialize = "@EntryPoint"))]
    pub entry_point: Option<String>,
    #[serde(default, rename(deserialize = "@RuntimeBehavior"))]
    pub runtime_behavior: Option<String>,
    #[serde(default, rename(deserialize = "@TrustLevel"))]
    pub trust_level: Option<String>,
    #[serde(default, rename(deserialize = "@StartPage"))]
    pub start_page: Option<String>,
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(rename_all = "PascalCase")]
pub struct BundleManifest {
    pub identity: Identity,
    pub packages: BundlePackages,
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(rename_all = "PascalCase")]
pub struct BundlePackages {
    #[serde(default, rename(deserialize = "Package"))]
    pub packages: Vec<BundlePackage>,
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(rename_all = "PascalCase")]
pub struct BundlePackage {
    #[serde(default = "resource_type", rename(deserialize = "@Type"))]
    pub package_type: String,
    #[serde(rename(deserialize = "@Version"))]
    pub version: String,
    #[serde(
        default = "neutral_architecture",
        rename(deserialize = "@Architecture")
    )]
    pub architecture: String,
    #[serde(rename(deserialize = "@FileName"))]
    pub file_name: String,
    #[serde(rename(deserialize = "@Offset"))]
    pub offset: u64,
    #[serde(rename(deserialize = "@Size"))]
    pub size: u64,
    #[serde(default, rename(deserialize = "@IsStub"))]
    pub is_stub: bool,
    #[serde(default, rename(deserialize = "@ResourceId"))]
    pub resource_id: Option<String>,
}

fn resource_type() -> String {
    "resource".to_owned()
}

fn neutral_architecture() -> String {
    "neutral".to_owned()
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "PascalCase")]
pub struct BundleSelection<'a> {
    pub application: &'a BundlePackage,
    pub resource_candidates: Vec<&'a BundlePackage>,
}

fn relative_filename(path: &str) -> Result<(), ManifestError> {
    if path.is_empty()
        || path.contains([':', '\0', '<', '>', '"', '|', '?', '*'])
        || path
            .split(['/', '\\'])
            .any(|part| part.is_empty() || part == "." || part == "..")
    {
        return Err(ManifestError::Path);
    }
    Ok(())
}

fn known_architecture(value: &str) -> bool {
    matches!(value, "x86" | "x64" | "arm" | "arm64" | "neutral")
}

impl BundleManifest {
    pub fn select(&self, architecture: &str) -> Result<BundleSelection<'_>, ManifestError> {
        if !known_architecture(architecture) {
            return Err(ManifestError::Architecture);
        }
        let applications: Vec<_> = self
            .packages
            .packages
            .iter()
            .filter(|package| {
                package.package_type == "application"
                    && package.architecture == architecture
                    && !package.is_stub
            })
            .collect();
        let application = match applications.as_slice() {
            [application] => *application,
            [] => return Err(ManifestError::NoApplication),
            _ => return Err(ManifestError::AmbiguousApplication),
        };
        let resource_candidates = self
            .packages
            .packages
            .iter()
            .filter(|package| {
                package.package_type == "resource"
                    && (package.architecture == architecture || package.architecture == "neutral")
                    && !package.is_stub
            })
            .collect();
        Ok(BundleSelection {
            application,
            resource_candidates,
        })
    }
}

pub fn inspect(bytes: &[u8]) -> Result<Manifest, ManifestError> {
    if bytes.len() > MAX_MANIFEST_BYTES {
        return Err(ManifestError::TooLarge);
    }
    let xml = std::str::from_utf8(bytes).map_err(|_| ManifestError::Encoding)?;
    let mut reader = NsReader::from_str(xml);
    let mut root = None;
    let mut depth: usize = 0;
    loop {
        let (namespace, event) = reader
            .read_resolved_event()
            .map_err(|_| ManifestError::Xml)?;
        match event {
            Event::Start(ref element) | Event::Empty(ref element) => {
                if depth == 0 {
                    if root.is_some() {
                        return Err(ManifestError::Root);
                    }
                    root = Some(match (element.local_name().as_ref(), namespace) {
                        ("Package", ResolveResult::Bound(ns))
                            if matches!(
                                ns.as_ref(),
                                "http://schemas.microsoft.com/appx/manifest/foundation/windows10"
                                    | "http://schemas.microsoft.com/appx/2010/manifest"
                                    | "http://schemas.microsoft.com/appx/2013/manifest"
                            ) =>
                        {
                            "Package"
                        }
                        ("Bundle", ResolveResult::Bound(ns))
                            if ns.as_ref() == "http://schemas.microsoft.com/appx/2013/bundle" =>
                        {
                            "Bundle"
                        }
                        _ => return Err(ManifestError::Root),
                    });
                }
                for attribute in element.attributes() {
                    attribute.map_err(|_| ManifestError::Xml)?;
                }
                if matches!(event, Event::Start(_)) {
                    depth += 1;
                    if depth > 64 {
                        return Err(ManifestError::Depth);
                    }
                }
            }
            Event::End(_) => {
                depth = depth.checked_sub(1).ok_or(ManifestError::Xml)?;
            }
            Event::DocType(_) => return Err(ManifestError::Entities),
            Event::GeneralRef(reference) => {
                let value = reference.as_ref();
                let numeric = value
                    .strip_prefix("#x")
                    .and_then(|number| u32::from_str_radix(number, 16).ok())
                    .or_else(|| {
                        value
                            .strip_prefix('#')
                            .and_then(|number| number.parse().ok())
                    })
                    .and_then(char::from_u32);
                if depth == 0
                    || (!matches!(value, "amp" | "lt" | "gt" | "quot" | "apos")
                        && numeric.is_none())
                {
                    return Err(ManifestError::Entities);
                }
            }
            Event::Text(text) if depth == 0 => {
                if !text.as_ref().bytes().all(|byte| byte.is_ascii_whitespace()) {
                    return Err(ManifestError::Root);
                }
            }
            Event::CData(_) if depth == 0 => return Err(ManifestError::Root),
            Event::Eof => break,
            _ => {}
        }
    }
    if depth != 0 {
        return Err(ManifestError::Xml);
    }
    match root {
        Some("Package") => {
            let package: PackageManifest =
                quick_xml::de::from_str(xml).map_err(ManifestError::Metadata)?;
            for application in &package.applications.applications {
                if let Some(executable) = &application.executable {
                    relative_filename(executable)?;
                }
            }
            Ok(Manifest::Package(package))
        }
        Some("Bundle") => {
            let bundle: BundleManifest =
                quick_xml::de::from_str(xml).map_err(ManifestError::Metadata)?;
            let mut names = std::collections::HashSet::new();
            for package in &bundle.packages.packages {
                relative_filename(&package.file_name)?;
                if !names.insert(package.file_name.to_lowercase()) {
                    return Err(ManifestError::DuplicateFilename);
                }
                if !known_architecture(&package.architecture) {
                    return Err(ManifestError::Architecture);
                }
                if !matches!(package.package_type.as_str(), "application" | "resource") {
                    return Err(ManifestError::PackageType);
                }
            }
            Ok(Manifest::Bundle(bundle))
        }
        _ => Err(ManifestError::Root),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const PACKAGE: &str = r#"<Package xmlns="http://schemas.microsoft.com/appx/manifest/foundation/windows10" xmlns:uap10="http://schemas.microsoft.com/appx/manifest/uap/windows10/10">
        <Identity Name="Example.Game" Publisher="CN=Example" Version="1.0.0.0" ProcessorArchitecture="x64"/>
        <Dependencies>
            <TargetDeviceFamily Name="Windows.Desktop" MinVersion="10.0.16299.0" MaxVersionTested="10.0.19041.0"/>
            <PackageDependency Name="Microsoft.NET.Native.Runtime.2.2" MinVersion="2.2.28604.0"/>
        </Dependencies>
        <Applications><Application Id="App" Executable="bin\Game.exe" EntryPoint="Example.App" uap10:RuntimeBehavior="windowsApp" uap10:TrustLevel="appContainer"/></Applications>
    </Package>"#;

    const BUNDLE: &str = r#"<Bundle xmlns="http://schemas.microsoft.com/appx/2013/bundle">
        <Identity Name="Example.Game" Publisher="CN=Example" Version="1.0.0.0"/>
        <Packages>
            <Package Type="application" Version="1.0.0.0" Architecture="x64" FileName="game_x64.appx" Offset="100" Size="200"/>
            <Package Type="application" Version="1.0.0.0" Architecture="arm64" FileName="game_arm64.appx" Offset="300" Size="200"/>
            <Package Version="1.0.0.0" FileName="resources.appx" Offset="500" Size="100"/>
            <Package Type="resource" Version="1.0.0.0" Architecture="arm64" FileName="resources_arm64.appx" Offset="600" Size="100"/>
        </Packages>
    </Bundle>"#;

    #[test]
    fn retains_activation_and_framework_metadata_without_inferring_support() {
        let Manifest::Package(package) = inspect(PACKAGE.as_bytes()).unwrap() else {
            panic!("Expected package");
        };
        let application = &package.applications.applications[0];
        assert_eq!(application.executable.as_deref(), Some(r"bin\Game.exe"));
        assert_eq!(application.runtime_behavior.as_deref(), Some("windowsApp"));
        assert_eq!(application.trust_level.as_deref(), Some("appContainer"));
        assert_eq!(
            package.dependencies.packages[0].name,
            "Microsoft.NET.Native.Runtime.2.2"
        );
        let output = serde_json::to_value(package).unwrap();
        assert!(output.get("Playable").is_none());
    }

    #[test]
    fn selects_exact_architecture_and_reports_neutral_resource_candidates() {
        let Manifest::Bundle(bundle) = inspect(BUNDLE.as_bytes()).unwrap() else {
            panic!("Expected bundle");
        };
        let selection = bundle.select("x64").unwrap();
        assert_eq!(selection.application.file_name, "game_x64.appx");
        assert_eq!(selection.resource_candidates.len(), 1);
        assert_eq!(selection.resource_candidates[0].file_name, "resources.appx");
        assert!(matches!(
            bundle.select("x86"),
            Err(ManifestError::NoApplication)
        ));
        assert!(matches!(
            bundle.select("auto"),
            Err(ManifestError::Architecture)
        ));
    }

    #[test]
    fn missing_activation_attributes_stay_unknown() {
        let xml = PACKAGE.replace(
            r#" EntryPoint="Example.App" uap10:RuntimeBehavior="windowsApp" uap10:TrustLevel="appContainer""#,
            "",
        );
        let Manifest::Package(package) = inspect(xml.as_bytes()).unwrap() else {
            panic!("Expected package");
        };
        assert!(package.applications.applications[0].entry_point.is_none());
        assert!(
            package.applications.applications[0]
                .runtime_behavior
                .is_none()
        );
        assert!(package.applications.applications[0].trust_level.is_none());
    }

    #[test]
    fn ordinary_xml_entities_and_ignored_visual_properties_are_supported() {
        let xml = PACKAGE.replace(
            "<Dependencies>",
            "<Properties><DisplayName>Example &amp; Game &#x26; Test</DisplayName></Properties><Dependencies>",
        );
        assert!(inspect(xml.as_bytes()).is_ok());
    }

    #[test]
    fn rejects_unsafe_bundle_and_executable_paths() {
        for path in [
            "../bad.appx",
            r"..\bad.appx",
            "/bad.appx",
            r"C:\bad.appx",
            r"\\host\bad.appx",
        ] {
            let xml = BUNDLE.replace("game_x64.appx", path);
            assert!(matches!(inspect(xml.as_bytes()), Err(ManifestError::Path)));
        }
        let xml = PACKAGE.replace(r"bin\Game.exe", r"..\Game.exe");
        assert!(matches!(inspect(xml.as_bytes()), Err(ManifestError::Path)));
    }

    #[test]
    fn rejects_duplicate_files_and_ambiguous_or_stub_applications() {
        let xml = BUNDLE.replace("game_arm64.appx", "GAME_X64.APPX");
        assert!(matches!(
            inspect(xml.as_bytes()),
            Err(ManifestError::DuplicateFilename)
        ));
        let xml = BUNDLE.replace(r#"Architecture="arm64""#, r#"Architecture="x64""#);
        let Manifest::Bundle(bundle) = inspect(xml.as_bytes()).unwrap() else {
            panic!("Expected bundle");
        };
        assert!(matches!(
            bundle.select("x64"),
            Err(ManifestError::AmbiguousApplication)
        ));
        let xml = BUNDLE.replace(
            r#"Type="application""#,
            r#"Type="application" IsStub="true""#,
        );
        let Manifest::Bundle(bundle) = inspect(xml.as_bytes()).unwrap() else {
            panic!("Expected bundle");
        };
        assert!(matches!(
            bundle.select("x64"),
            Err(ManifestError::NoApplication)
        ));
    }

    #[test]
    fn rejects_malformed_multi_root_dtd_and_large_xml() {
        for xml in [
            "<Package>",
            "<Package xmlns=\"https://example.invalid\"/>",
            &format!("{BUNDLE}{BUNDLE}"),
            &format!("<!DOCTYPE Bundle [<!ENTITY secret 'value'>]>{BUNDLE}"),
            &BUNDLE.replace(r#"Size="200""#, r#"Size="nope""#),
        ] {
            assert!(inspect(xml.as_bytes()).is_err());
        }
        assert!(matches!(
            inspect(&vec![b' '; MAX_MANIFEST_BYTES + 1]),
            Err(ManifestError::TooLarge)
        ));
    }
}
