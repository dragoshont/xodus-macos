use std::collections::{BTreeMap, BTreeSet};

use serde_json::Value;

use crate::wire::{Artwork, ArtworkRole, ArtworkSource, ArtworkStatus};

pub const MAX_DIMENSION: u32 = 8192;
pub const MAX_PIXELS: u64 = 16_777_216;
const PREFIX: &str = "https://store-images.s-microsoft.com/image/";

pub fn normalize_url(value: &str) -> Option<String> {
    if value.len() > 2048 {
        return None;
    }
    let asset = value
        .strip_prefix(PREFIX)
        .or_else(|| value.strip_prefix("//store-images.s-microsoft.com/image/"))
        .or_else(|| value.strip_prefix("http://store-images.s-microsoft.com/image/"))?;
    if !asset.bytes().next()?.is_ascii_alphanumeric()
        || PREFIX.len() + asset.len() > 2048
        || !asset
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'.' | b'_' | b'-'))
    {
        return None;
    }
    Some(format!("{PREFIX}{asset}"))
}

pub fn dimensions_valid(width: Option<u32>, height: Option<u32>) -> bool {
    match (width, height) {
        (None, None) => true,
        (Some(width), Some(height)) => {
            (1..=MAX_DIMENSION).contains(&width)
                && (1..=MAX_DIMENSION).contains(&height)
                && u64::from(width) * u64::from(height) <= MAX_PIXELS
        }
        _ => false,
    }
}

pub fn valid(images: &[Artwork], status: &ArtworkStatus) -> bool {
    let mut roles = BTreeSet::new();
    images.len() <= 4
        && (matches!(status, ArtworkStatus::Available) != images.is_empty())
        && images.iter().all(|image| {
            normalize_url(&image.url).as_deref() == Some(image.url.as_str())
                && dimensions_valid(image.width, image.height)
                && roles.insert(image.role.clone())
        })
}

fn warning(reason: &'static str) {
    eprintln!("Public artwork metadata rejected: {reason}.");
}

pub fn catalog_images(value: &Value) -> (Vec<Artwork>, ArtworkStatus) {
    if value.is_null() {
        return (vec![], ArtworkStatus::Absent);
    }
    let Some(images) = value.as_array().filter(|images| images.len() <= 256) else {
        warning("invalid image collection");
        return (vec![], ArtworkStatus::Rejected);
    };
    let mut selected: BTreeMap<ArtworkRole, Artwork> = BTreeMap::new();
    let mut rejected = false;
    for image in images {
        let role = match image.get("ImagePurpose").and_then(Value::as_str) {
            Some("BoxArt") => ArtworkRole::BoxArt,
            Some("Poster") => ArtworkRole::Poster,
            Some("SuperHeroArt") => ArtworkRole::Hero,
            Some(_) => continue,
            None => {
                warning("missing image purpose");
                rejected = true;
                continue;
            }
        };
        let Some(url) = image
            .get("Uri")
            .and_then(Value::as_str)
            .and_then(normalize_url)
        else {
            warning("unsupported public image URL");
            rejected = true;
            continue;
        };
        let dimension = |key: &str| -> Result<Option<u32>, ()> {
            match image.get(key) {
                None | Some(Value::Null) => Ok(None),
                Some(value) => value
                    .as_u64()
                    .and_then(|number| u32::try_from(number).ok())
                    .map(Some)
                    .ok_or(()),
            }
        };
        let (Ok(width), Ok(height)) = (dimension("Width"), dimension("Height")) else {
            warning("invalid image dimensions");
            rejected = true;
            continue;
        };
        if !dimensions_valid(width, height) {
            warning("invalid image dimensions");
            rejected = true;
            continue;
        }
        let candidate = Artwork {
            role: role.clone(),
            url,
            width,
            height,
            source: ArtworkSource::DisplayCatalog,
        };
        let rank = |image: &Artwork| {
            (
                u64::from(image.width.unwrap_or(0)) * u64::from(image.height.unwrap_or(0)),
                image.url.clone(),
            )
        };
        if selected
            .get(&role)
            .is_none_or(|existing| rank(&candidate) > rank(existing))
        {
            selected.insert(role, candidate);
        }
    }
    let artwork: Vec<_> = selected.into_values().collect();
    let status = if !artwork.is_empty() {
        ArtworkStatus::Available
    } else if rejected {
        ArtworkStatus::Rejected
    } else {
        ArtworkStatus::Absent
    };
    (artwork, status)
}

pub fn history_image(value: Option<&Value>) -> (Vec<Artwork>, ArtworkStatus) {
    match value {
        None | Some(Value::Null) => (vec![], ArtworkStatus::Absent),
        Some(value) if value.as_str() == Some("") => (vec![], ArtworkStatus::Absent),
        Some(value) => {
            if let Some(url) = value.as_str().and_then(normalize_url) {
                (
                    vec![Artwork {
                        role: ArtworkRole::Tile,
                        url,
                        width: None,
                        height: None,
                        source: ArtworkSource::TitleHub,
                    }],
                    ArtworkStatus::Available,
                )
            } else {
                warning("unsupported recent title image URL");
                (vec![], ArtworkStatus::Rejected)
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn real_store_origin_only_and_exact_pixel_boundary() {
        assert_eq!(
            normalize_url("//store-images.s-microsoft.com/image/apps.fixture").unwrap(),
            "https://store-images.s-microsoft.com/image/apps.fixture"
        );
        assert_eq!(
            normalize_url("http://store-images.s-microsoft.com/image/apps.fixture"),
            Some("https://store-images.s-microsoft.com/image/apps.fixture".to_owned())
        );
        for url in [
            "https://store-images.s-microsoft.com:443/image/fixture",
            "http://store-images.s-microsoft.com:80/image/fixture",
            "http://store-images.s-microsoft.com:443/image/fixture",
            "http://user@store-images.s-microsoft.com/image/fixture",
            "http://store-images.s-microsoft.com/image/fixture?url=PRIVATE_SENTINEL",
            "http://store-images.s-microsoft.com/image/fixture?token=PRIVATE_SENTINEL",
            "http://store-images.s-microsoft.com/image/fixture#fragment",
            "http://store-images.s-microsoft.com.attacker.invalid/image/fixture",
            "http://images-eds.xboxlive.com/fixture?url=PRIVATE_SENTINEL",
            "https://images-eds-ssl.xboxlive.com/fixture",
            "http://store-images.s-microsoft.com/image/../fixture",
            "http://store-images.s-microsoft.com/image/%2e%2e",
            "http://store-images.s-microsoft.com/image/fixture/child",
            "http://store-images.s-microsoft.com/image/fixture\\child",
            "http://store-images.s-microsoft.com/other/fixture",
            "https://store-images.s-microsoft.com/image/fixture?token=PRIVATE_SENTINEL",
            "https://store-images.s-microsoft.com/image/fixture#fragment",
            "https://user@store-images.s-microsoft.com/image/fixture",
            "https://store-images.s-microsoft.com.attacker.invalid/image/fixture",
            "https://127.0.0.1/image/fixture",
            "https://store-images.s-microsoft.com/image/../fixture",
            "https://store-images.s-microsoft.com/image/%2e%2e",
            "file:///fixture",
        ] {
            assert!(normalize_url(url).is_none());
        }
        assert!(dimensions_valid(Some(8192), Some(2048)));
        assert!(!dimensions_valid(Some(8192), Some(2049)));
        assert!(dimensions_valid(Some(3000), Some(5000)));
        assert!(!dimensions_valid(Some(0), Some(1)));
        assert!(!dimensions_valid(Some(8193), Some(1)));
        assert!(!dimensions_valid(None, Some(1)));
    }

    #[test]
    fn history_store_scheme_upgrade_preserves_asset_and_canonical_wire_limits() {
        let asset = "apps.01234567-89ab-cdef-0123-456789abcdef.asset_segment-1";
        let input = format!("http://store-images.s-microsoft.com/image/{asset}");
        let (images, status) = history_image(Some(&json!(input)));
        assert_eq!(status, ArtworkStatus::Available);
        assert_eq!(images.len(), 1);
        assert_eq!(images[0].url, format!("{PREFIX}{asset}"));
        assert_eq!(images[0].role, ArtworkRole::Tile);
        assert_eq!(images[0].source, ArtworkSource::TitleHub);
        assert!(valid(&images, &status));
        let mut noncanonical = images.clone();
        noncanonical[0].url = input;
        assert!(!valid(&noncanonical, &status));
        for input_prefix in [
            PREFIX,
            "http://store-images.s-microsoft.com/image/",
            "//store-images.s-microsoft.com/image/",
        ] {
            let asset = "a".repeat(2048 - PREFIX.len());
            assert_eq!(
                normalize_url(&format!("{input_prefix}{asset}"))
                    .unwrap()
                    .len(),
                2048
            );
            assert!(normalize_url(&format!("{input_prefix}{asset}a")).is_none());
        }
    }

    #[test]
    fn real_purposes_are_lenient_deterministic_and_never_synthesized() {
        let mut images = json!([
            {"ImagePurpose":"BoxArt","Uri":"//store-images.s-microsoft.com/image/box","Width":1080,"Height":1080,"Unknown":true},
            {"ImagePurpose":"Poster","Uri":"//store-images.s-microsoft.com/image/poster","Width":1440,"Height":2160},
            {"ImagePurpose":"SuperHeroArt","Uri":"//store-images.s-microsoft.com/image/hero","Width":3840,"Height":2160},
            {"ImagePurpose":"Screenshot","Uri":"PRIVATE_SENTINEL"},
            {"ImagePurpose":"BoxArt","Uri":"https://attacker.invalid/PRIVATE_SENTINEL"}
        ]);
        let (artwork, status) = catalog_images(&images);
        assert_eq!(artwork.len(), 3);
        assert!(valid(&artwork, &status));
        images.as_array_mut().unwrap().reverse();
        assert_eq!(catalog_images(&images).0, artwork);
        assert!(
            !serde_json::to_string(&artwork)
                .unwrap()
                .contains("PRIVATE_SENTINEL")
        );
        assert_eq!(catalog_images(&json!([])).1, ArtworkStatus::Absent);
        assert_eq!(
            catalog_images(&json!({"Uri":"PRIVATE_SENTINEL"})).1,
            ArtworkStatus::Rejected
        );
        assert_eq!(catalog_images(&json!([
            {"ImagePurpose":"Poster","Uri":"//store-images.s-microsoft.com/image/fixture","Width":8192,"Height":8192}
        ])).1, ArtworkStatus::Rejected);
    }
}
