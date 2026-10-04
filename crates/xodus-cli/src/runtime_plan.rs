use std::process::ExitCode;
use tokio::io::{AsyncRead, AsyncReadExt, AsyncWrite, AsyncWriteExt};
use xodus_management::runtime_provider::{MAX_CONFIGURATION_BYTES, parse, plan};
use xodus_management::wire::{ErrorCode, WireError};

fn unavailable() -> WireError {
    WireError::new(
        ErrorCode::InternalError,
        "Runtime configuration planning input or output is unavailable. No provider or prefix was modified.",
        false,
    )
}

async fn serve<R: AsyncRead + Unpin, W: AsyncWrite + Unpin>(
    reader: R,
    mut writer: W,
) -> Result<(), WireError> {
    let mut bytes = Vec::new();
    reader
        .take((MAX_CONFIGURATION_BYTES + 1) as u64)
        .read_to_end(&mut bytes)
        .await
        .map_err(|_| unavailable())?;
    let configuration = parse(&bytes)?;
    let result = plan(configuration)?;
    let mut bytes = serde_json::to_vec(&result).map_err(|_| unavailable())?;
    bytes.push(b'\n');
    writer.write_all(&bytes).await.map_err(|_| unavailable())?;
    writer.flush().await.map_err(|_| unavailable())
}

pub async fn run() -> ExitCode {
    match serve(tokio::io::stdin(), tokio::io::stdout()).await {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("{}", error.message);
            ExitCode::FAILURE
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use xodus_management::runtime_provider::{Configuration, Provider};

    #[tokio::test]
    async fn all_four_source_selections_produce_real_unknown_evidence_plans() {
        for provider in Provider::ALL {
            let input = serde_json::to_vec(&Configuration::preset(provider)).unwrap();
            let mut output = Vec::new();
            serve(input.as_slice(), &mut output).await.unwrap();
            assert_eq!(output.last(), Some(&b'\n'));
            let result: serde_json::Value = serde_json::from_slice(&output).unwrap();
            assert_eq!(
                result["configuration"]["provider"],
                serde_json::to_value(provider).unwrap()
            );
            assert_eq!(result["launchable"], false);
            assert_eq!(result["installation"], "notInspected");
            assert_eq!(result["devicePreflight"], "notPerformed");
            assert_eq!(result["gameVerification"], "notVerified");
        }
    }

    #[tokio::test]
    async fn invalid_and_oversized_configuration_never_produces_a_success_plan() {
        for input in [b"{}".to_vec(), vec![b'x'; MAX_CONFIGURATION_BYTES + 1]] {
            let mut output = Vec::new();
            assert!(serve(input.as_slice(), &mut output).await.is_err());
            assert!(output.is_empty());
        }
    }

    #[tokio::test]
    async fn explicit_eof_is_required_but_oversized_open_input_is_already_rejected() {
        let input = serde_json::to_vec(&Configuration::preset(Provider::Gptk4)).unwrap();
        let (mut sender, receiver) = tokio::io::duplex(MAX_CONFIGURATION_BYTES * 2);
        sender.write_all(&input).await.unwrap();
        let mut output = Vec::new();
        let mut serving = Box::pin(serve(receiver, &mut output));
        assert!(
            tokio::time::timeout(std::time::Duration::from_millis(25), &mut serving)
                .await
                .is_err()
        );
        sender.shutdown().await.unwrap();
        serving.await.unwrap();
        assert!(!output.is_empty());

        let (mut sender, receiver) = tokio::io::duplex(MAX_CONFIGURATION_BYTES * 2);
        sender
            .write_all(&vec![b'x'; MAX_CONFIGURATION_BYTES + 1])
            .await
            .unwrap();
        let mut output = Vec::new();
        assert!(
            tokio::time::timeout(
                std::time::Duration::from_secs(1),
                serve(receiver, &mut output),
            )
            .await
            .unwrap()
            .is_err()
        );
        assert!(output.is_empty());
        drop(sender);
    }

    #[tokio::test]
    async fn output_failure_is_not_acknowledged_as_success() {
        let input = serde_json::to_vec(&Configuration::preset(Provider::Gptk3)).unwrap();
        let (writer, reader) = tokio::io::duplex(64);
        drop(reader);
        assert!(serve(input.as_slice(), writer).await.is_err());
    }
}
