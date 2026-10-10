use std::io::Read;
use std::process::ExitCode;

use xodus::appx::{MAX_MANIFEST_BYTES, Manifest, inspect};

pub fn run(path: String, architecture: Option<String>) -> ExitCode {
    match inspect_file(&path, architecture.as_deref()) {
        Ok(output) => {
            println!("{output}");
            ExitCode::SUCCESS
        }
        Err(error) => {
            eprintln!("Failed to inspect local manifest: {error}");
            ExitCode::FAILURE
        }
    }
}

fn inspect_file(
    path: &str,
    architecture: Option<&str>,
) -> Result<String, Box<dyn std::error::Error>> {
    let file = std::fs::File::open(path)?;
    let mut bytes = Vec::new();
    file.take(MAX_MANIFEST_BYTES as u64 + 1)
        .read_to_end(&mut bytes)?;
    let manifest = inspect(&bytes)?;
    let selection = match (&manifest, architecture) {
        (Manifest::Bundle(bundle), Some(architecture)) => Some(bundle.select(architecture)?),
        (Manifest::Package(_), Some(_)) => {
            return Err("Architecture selection requires a bundle manifest".into());
        }
        _ => None,
    };
    Ok(serde_json::to_string_pretty(&serde_json::json!({
        "Inspection": &manifest,
        "Selection": selection
    }))?)
}
