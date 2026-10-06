use std::collections::{BTreeMap, BTreeSet};

use serde_json::Value;
use xodus::api::xbox::titlehub;
use xodus::tokens::TokenManager;
use xodus_management::auth_verify::{RecentLibraryRead, VerificationFailure};
use xodus_management::wire::{
    Completeness, Freshness, HistoryPlatform, RecentLibraryData, RecentLibraryParams, RecentTitle,
};

use crate::auth_verify::classify;
use crate::package::PackageReadError;
use crate::provider_credentials::ProviderCredentials;

pub async fn read(
    client: &reqwest::Client,
    tokens: TokenManager,
    params: RecentLibraryParams,
) -> Result<RecentLibraryRead, VerificationFailure> {
    if !tokens.is_management_profile() {
        return Err(VerificationFailure::CredentialUnavailable);
    }
    let readonly = tokens
        .readonly_management_profile()
        .map_err(|_| VerificationFailure::CredentialUnavailable)?;
    let credentials = ProviderCredentials::read(&readonly)
        .await
        .map_err(|error| classify(PackageReadError::Credentials(error)))?;
    let token = xodus::api::xbox::run(
        client,
        credentials.device.clone(),
        credentials.user.clone(),
        credentials.account.username.clone(),
        titlehub::RELYING_PARTY,
    )
    .await
    .map_err(|error| classify(PackageReadError::Authentication(error)))?;
    let id = token
        .user_id()
        .ok_or(VerificationFailure::ResponseInvalid)?
        .to_owned();
    credentials
        .verify_current(&readonly)
        .await
        .map_err(|error| classify(PackageReadError::Credentials(error)))?;
    let response = titlehub::recent(client, token, params.limit)
        .await
        .map_err(|error| match error {
            xodus::api::xbox::XboxAuthError::Provider(error) => {
                crate::auth_verify::provider_failure(error)
            }
            error => classify(PackageReadError::Authentication(error)),
        })?;
    let data = parse(&response, &id, params.limit)?;
    credentials
        .verify_current(&readonly)
        .await
        .map_err(|error| classify(PackageReadError::Credentials(error)))?;
    let profile = credentials
        .publication_witness()
        .ok_or(VerificationFailure::CredentialUnavailable)?;
    Ok(RecentLibraryRead { data, profile })
}

fn text(value: &Value, maximum: usize) -> Option<&str> {
    value.as_str().filter(|text| {
        !text.trim().is_empty()
            && text.chars().count() <= maximum
            && !text.chars().any(char::is_control)
    })
}

fn platform(devices: &[String]) -> HistoryPlatform {
    let mut pc = false;
    let mut console = false;
    for device in devices {
        match device.as_str() {
            "PC" | "Win32" => pc = true,
            "XboxOne" | "XboxSeries" => console = true,
            _ => return HistoryPlatform::Unknown,
        }
    }
    match (pc, console) {
        (true, true) => HistoryPlatform::Mixed,
        (true, false) => HistoryPlatform::Pc,
        (false, true) => HistoryPlatform::Console,
        (false, false) => HistoryPlatform::Unknown,
    }
}

#[derive(Default)]
struct RejectedImageShapes {
    count: u32,
    scheme: BTreeMap<&'static str, u32>,
    host: BTreeMap<&'static str, u32>,
    path: BTreeMap<&'static str, u32>,
    query_keys: BTreeMap<&'static str, u32>,
    unknown_key_count: u32,
}

impl RejectedImageShapes {
    fn record(&mut self, value: Option<&Value>) {
        const KEYS: [&str; 12] = [
            "w",
            "h",
            "q",
            "f",
            "m",
            "mode",
            "background",
            "format",
            "url",
            "fit",
            "crop",
            "pad",
        ];
        let value = value
            .and_then(Value::as_str)
            .filter(|value| value.len() <= 2048);
        let scheme = match value {
            Some(value) if value.starts_with("http://") => "http",
            Some(value) if value.starts_with("https://") => "https",
            Some(value) if value.starts_with("//") => "relative",
            _ => "other",
        };
        let parsed = value.and_then(|value| {
            let url = if scheme == "relative" {
                format!("https:{value}")
            } else {
                value.to_owned()
            };
            reqwest::Url::parse(&url).ok()
        });
        let host = match parsed.as_ref().and_then(reqwest::Url::host_str) {
            Some("store-images.s-microsoft.com") => "store",
            Some("images-eds.xboxlive.com") => "eds",
            Some("images-eds-ssl.xboxlive.com") => "edsSSL",
            _ => "other",
        };
        let path = match parsed.as_ref().map(reqwest::Url::path) {
            Some(path) if path.starts_with("/image/") => {
                let asset = path.strip_prefix("/image/").unwrap_or_default();
                if !asset.is_empty() && !asset.contains('/') {
                    "storeSingleAsset"
                } else {
                    "nested"
                }
            }
            Some(path)
                if path
                    .strip_prefix('/')
                    .is_some_and(|path| !path.is_empty() && !path.contains('/')) =>
            {
                "opaque"
            }
            _ => "other",
        };
        self.count += 1;
        *self.scheme.entry(scheme).or_default() += 1;
        *self.host.entry(host).or_default() += 1;
        *self.path.entry(path).or_default() += 1;
        if let Some(parsed) = parsed {
            for (key, _) in parsed.query_pairs().take(64) {
                if let Some(known) = KEYS.into_iter().find(|known| *known == key.as_ref()) {
                    *self.query_keys.entry(known).or_default() += 1;
                } else {
                    self.unknown_key_count += 1;
                }
            }
        }
    }

    fn line(&self) -> Option<String> {
        (self.count != 0).then(|| {
            serde_json::json!({
                "category": "recentArtworkRejectedShapes",
                "count": self.count,
                "scheme": self.scheme,
                "host": self.host,
                "path": self.path,
                "queryKeys": self.query_keys,
                "unknownKeyCount": self.unknown_key_count,
            })
            .to_string()
        })
    }
}

fn parse(
    response: &Value,
    private_id: &str,
    limit: u32,
) -> Result<RecentLibraryData, VerificationFailure> {
    let invalid = || VerificationFailure::ResponseInvalid;
    if let Some(id) = response.get("xuid")
        && !id.is_null()
        && id.as_str() != Some(private_id)
    {
        return Err(invalid());
    }
    let entries = response
        .get("titles")
        .and_then(Value::as_array)
        .filter(|titles| (1..=100).contains(&limit) && titles.len() <= limit as usize)
        .ok_or_else(invalid)?;
    let mut seen = BTreeSet::new();
    let mut titles = Vec::new();
    let mut rejected_images = RejectedImageShapes::default();
    for entry in entries {
        let kind = entry
            .get("type")
            .and_then(|value| text(value, 64))
            .ok_or_else(invalid)?;
        if kind != "Game" {
            eprintln!("Recent library metadata excluded: title type is not Game.");
            continue;
        }
        if let Some(visible) = entry
            .get("titleHistory")
            .and_then(|value| value.get("visible"))
        {
            match visible.as_bool() {
                Some(false) => continue,
                Some(true) => {}
                None => return Err(invalid()),
            }
        }
        let id = entry
            .get("titleId")
            .and_then(Value::as_str)
            .ok_or_else(invalid)?;
        let number = id.parse::<u32>().map_err(|_| invalid())?;
        if number == 0 || number.to_string() != id || !seen.insert(id.to_owned()) {
            return Err(invalid());
        }
        let name = entry
            .get("name")
            .and_then(|value| text(value, 1024))
            .ok_or_else(invalid)?;
        let reported = entry
            .get("devices")
            .and_then(Value::as_array)
            .filter(|devices| devices.len() <= 32)
            .ok_or_else(invalid)?;
        let devices: Vec<_> = reported
            .iter()
            .map(|value| text(value, 64).map(str::to_owned).ok_or_else(invalid))
            .collect::<Result<_, _>>()?;
        let last_played_at = match entry
            .get("titleHistory")
            .and_then(|value| value.get("lastTimePlayed"))
        {
            None | Some(Value::Null) => None,
            Some(value) => {
                let date = value
                    .as_str()
                    .and_then(|date| chrono::DateTime::parse_from_rfc3339(date).ok());
                if date.is_none() {
                    eprintln!(
                        "Recent library metadata rejected: invalid optional last-played date."
                    );
                }
                date.map(|date| date.to_rfc3339())
            }
        };
        let (artwork, artwork_status) =
            xodus_management::artwork::history_image(entry.get("displayImage"));
        if artwork_status == xodus_management::wire::ArtworkStatus::Rejected {
            rejected_images.record(entry.get("displayImage"));
        }
        titles.push(RecentTitle {
            title_id: id.to_owned(),
            name: name.to_owned(),
            last_played_at,
            platform: platform(&devices),
            devices,
            artwork,
            artwork_status,
            product_id: None,
        });
    }
    if let Some(line) = rejected_images.line() {
        eprintln!("{line}");
    }
    Ok(RecentLibraryData {
        scope: "recentlyPlayed".to_owned(),
        source: "XboxTitleHub:v2".to_owned(),
        checked_at: xodus_management::state::now(),
        freshness: Freshness::Live,
        completeness: Completeness::Partial,
        next_cursor: None,
        titles,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    fn game(devices: Value) -> Value {
        json!({"titleId":"1","name":"Fixture Harbor","type":"Game","mediaItemType":"Application",
            "devices":devices,"displayImage":"//store-images.s-microsoft.com/image/fixture",
            "titleHistory":{"lastTimePlayed":"2026-10-05T12:00:00Z","visible":true},
            "productId":"DO_NOT_MAP","pfn":"DO_NOT_MAP","unknown":"ignored"})
    }

    #[test]
    fn recent_library_parses_actual_game_type_platforms_and_private_identity_without_mapping() {
        for (devices, expected) in [
            (json!(["PC", "Win32"]), HistoryPlatform::Pc),
            (json!(["XboxOne", "XboxSeries"]), HistoryPlatform::Console),
            (json!(["PC", "XboxOne"]), HistoryPlatform::Mixed),
            (json!(["PC", "FutureDevice"]), HistoryPlatform::Unknown),
            (json!([]), HistoryPlatform::Unknown),
        ] {
            let parsed = parse(
                &json!({"xuid":"PRIVATE_SENTINEL","titles":[game(devices)]}),
                "PRIVATE_SENTINEL",
                100,
            )
            .unwrap();
            assert_eq!(parsed.titles[0].platform, expected);
            assert!(parsed.titles[0].product_id.is_none());
            assert!(parsed.next_cursor.is_none());
            let wire = serde_json::to_string(&parsed).unwrap();
            for private in ["PRIVATE_SENTINEL", "productId", "pfn", "DO_NOT_MAP", "xuid"] {
                assert!(!wire.contains(private));
            }
        }
        assert_eq!(
            parse(&json!({"titles":[]}), "PRIVATE_SENTINEL", 100)
                .unwrap()
                .titles
                .len(),
            0
        );
    }

    #[test]
    fn recent_library_rejects_needed_bad_data_but_keeps_optional_art_and_future_devices_honest() {
        for value in [
            json!({}),
            json!({"titles":null}),
            json!({"titles":[{}]}),
            json!({"titles":[game(json!(["PC"])),game(json!(["PC"]))]}),
            json!({"xuid":"wrong","titles":[]}),
        ] {
            assert!(matches!(
                parse(&value, "PRIVATE_SENTINEL", 100),
                Err(VerificationFailure::ResponseInvalid)
            ));
        }
        for (field, value) in [
            ("titleId", json!("4294967296")),
            ("titleId", json!("01")),
            ("name", json!("")),
            ("devices", json!(["PRIVATE\nSENTINEL"])),
        ] {
            let mut entry = game(json!(["PC"]));
            entry[field] = value;
            assert!(parse(&json!({"titles":[entry]}), "PRIVATE_SENTINEL", 100).is_err());
        }
        let mut entry = game(json!(["PC"]));
        entry["displayImage"] = json!("http://attacker.invalid/PRIVATE_SENTINEL");
        entry["titleHistory"]["lastTimePlayed"] = json!("PRIVATE_SENTINEL");
        let parsed = parse(&json!({"titles":[entry]}), "PRIVATE_SENTINEL", 100).unwrap();
        assert!(parsed.titles[0].artwork.is_empty());
        assert_eq!(
            parsed.titles[0].artwork_status,
            xodus_management::wire::ArtworkStatus::Rejected
        );
        assert!(parsed.titles[0].last_played_at.is_none());
        assert!(
            parse(
                &json!({"titles":[game(json!(["PC"]))]}),
                "PRIVATE_SENTINEL",
                0
            )
            .is_err()
        );
    }

    #[test]
    fn recent_library_hidden_and_non_game_titles_are_not_fabricated_as_games() {
        let mut hidden = game(json!(["PC"]));
        hidden["titleHistory"]["visible"] = json!(false);
        let mut app = game(json!(["PC"]));
        app["type"] = json!("Application");
        assert!(
            parse(&json!({"titles":[hidden,app]}), "PRIVATE_SENTINEL", 100)
                .unwrap()
                .titles
                .is_empty()
        );
    }

    #[test]
    fn rejected_image_shapes_only_emit_closed_aggregate_categories_and_query_key_counts() {
        let mut shapes = RejectedImageShapes::default();
        assert!(shapes.line().is_none());
        for url in [
            "http://store-images.s-microsoft.com/image/PRIVATE_SENTINEL?w=PRIVATE_SENTINEL&h=100&PRIVATE_SENTINEL=PRIVATE_SENTINEL",
            "https://images-eds-ssl.xboxlive.com/image/PRIVATE_SENTINEL?url=PRIVATE_SENTINEL",
            "//images-eds.xboxlive.com/PRIVATE_SENTINEL?format=PRIVATE_SENTINEL",
            "https://PRIVATE_SENTINEL.invalid/private/PRIVATE_SENTINEL?PRIVATE_SENTINEL=PRIVATE_SENTINEL",
        ] {
            shapes.record(Some(&json!(url)));
        }
        shapes.record(Some(&json!({"PRIVATE_SENTINEL": "PRIVATE_SENTINEL"})));
        shapes.record(Some(&json!("PRIVATE_SENTINEL".repeat(2048))));
        let line = shapes.line().unwrap();
        assert!(!line.contains("PRIVATE_SENTINEL"));
        assert!(!line.contains("http://"));
        assert!(!line.contains("https://"));
        let summary: Value = serde_json::from_str(&line).unwrap();
        assert_eq!(summary["count"], 6);
        assert_eq!(
            summary["host"],
            json!({"store":1,"eds":1,"edsSSL":1,"other":3})
        );
        assert_eq!(
            summary["queryKeys"],
            json!({"w":1,"h":1,"url":1,"format":1})
        );
        assert_eq!(summary["unknownKeyCount"], 2);
        assert_eq!(summary.as_object().unwrap().len(), 7);
        assert!(line.len() + 1 <= 512);
    }

    #[test]
    fn rejected_image_shape_summary_is_bounded_for_a_full_history_window() {
        let mut shapes = RejectedImageShapes::default();
        let query = [
            "w",
            "h",
            "q",
            "f",
            "m",
            "mode",
            "background",
            "format",
            "url",
            "fit",
            "crop",
            "pad",
        ]
        .map(|key| format!("{key}=PRIVATE_SENTINEL"))
        .join("&");
        for index in 0..100 {
            let scheme = ["http://", "https://", "//", "file://"][index % 4];
            let host = [
                "store-images.s-microsoft.com",
                "images-eds.xboxlive.com",
                "images-eds-ssl.xboxlive.com",
                "PRIVATE_SENTINEL.invalid",
            ][index % 4];
            let path = [
                "image/PRIVATE_SENTINEL",
                "image/nested/PRIVATE_SENTINEL",
                "PRIVATE_SENTINEL",
                "",
            ][index % 4];
            shapes.record(Some(&json!(format!(
                "{scheme}{host}/{path}?{query}&PRIVATE_SENTINEL=value"
            ))));
        }
        let line = shapes.line().unwrap();
        assert!(!line.contains("PRIVATE_SENTINEL"));
        assert!(line.len() + 1 <= 512);
        assert_eq!(serde_json::from_str::<Value>(&line).unwrap()["count"], 100);
    }

    #[test]
    fn actual_store_http_shape_produces_same_https_tile_without_guessed_metadata() {
        let mut entry = game(json!(["PC"]));
        entry["displayImage"] = json!("http://store-images.s-microsoft.com/image/apps.fixture");
        let result = parse(&json!({"titles":[entry]}), "PRIVATE_SENTINEL", 1).unwrap();
        assert_eq!(
            result.titles[0].artwork_status,
            xodus_management::wire::ArtworkStatus::Available
        );
        assert_eq!(
            result.titles[0].artwork[0].url,
            "https://store-images.s-microsoft.com/image/apps.fixture"
        );
        assert!(result.titles[0].artwork[0].width.is_none());
        assert!(result.titles[0].artwork[0].height.is_none());
        assert!(result.titles[0].product_id.is_none());
    }
}
