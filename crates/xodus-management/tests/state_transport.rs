use std::fs;
use tokio::io::{AsyncWriteExt, BufReader};
use xodus_management::state::{Store, catalog_key};
use xodus_management::transport::{self, Line};
use xodus_management::wire::*;

fn params() -> ProductParams {
    ProductParams {
        product_id: "FIXTURE00001".to_owned(),
        market: "US".to_owned(),
        language: "en-US".to_owned(),
        refresh: Refresh::Network,
    }
}

fn directory() -> (tempfile::TempDir, std::path::PathBuf) {
    let temp = tempfile::tempdir().unwrap();
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        fs::set_permissions(temp.path(), fs::Permissions::from_mode(0o700)).unwrap();
    }
    let path = temp.path().canonicalize().unwrap();
    (temp, path)
}

#[test]
fn query_validation_preserves_exact_text_and_rejects_unsafe_parameters() {
    let base = serde_json::json!({"kind":"request","protocol":{"major":1,"minor":0},
        "requestID":"query-validation","command":"catalog.query","params":{
        "query":" Halo ","market":"US","language":"en-US","limit":8,"cursor":null}});
    let request = transport::parse(&serde_json::to_vec(&base).unwrap()).unwrap();
    let Operation::CatalogQuery(params) = request.operation else {
        panic!("query expected")
    };
    assert_eq!(params.query, " Halo ");
    for (key, value) in [
        ("query", serde_json::json!(" \u{2003} ")),
        ("query", serde_json::json!("Halo\n")),
        ("query", serde_json::json!("x".repeat(257))),
        ("limit", serde_json::json!(17)),
        ("cursor", serde_json::json!("q1-")),
        ("cursor", serde_json::json!("q1-abc")),
        ("cursor", serde_json::json!("https://attacker.invalid/")),
    ] {
        let mut frame = base.clone();
        frame["params"][key] = value;
        assert_eq!(
            transport::parse(&serde_json::to_vec(&frame).unwrap())
                .unwrap_err()
                .code,
            ErrorCode::InvalidRequest
        );
    }
    for command in ["catalog.search", "catalog.discover", "catalog.query"] {
        let mut frame = base.clone();
        frame["command"] = serde_json::json!(command);
        if command == "catalog.search" {
            frame["params"]["platform"] = serde_json::json!("pc");
        } else if command == "catalog.discover" {
            frame["params"].as_object_mut().unwrap().remove("query");
        }
        transport::parse(&serde_json::to_vec(&frame).unwrap()).unwrap();
        frame["params"].as_object_mut().unwrap().remove("cursor");
        assert_eq!(
            transport::parse(&serde_json::to_vec(&frame).unwrap())
                .unwrap_err()
                .code,
            ErrorCode::InvalidRequest
        );
    }
}

#[test]
fn durable_idempotency_revision_and_interruption() {
    let (_temporary, path) = directory();
    let mut store = Store::open(&path).unwrap();
    let session = store.state.session_id.clone();
    let (job, created) = store.enqueue("request-a", "key-a", params()).unwrap();
    assert!(created);
    let (same, created) = store.enqueue("request-b", "key-a", params()).unwrap();
    assert!(!created);
    assert_eq!(same.job_id, job.job_id);
    assert_eq!(store.state.watermark, 1);
    let mut other = params();
    other.product_id = "FIXTURE00002".to_owned();
    assert_eq!(
        store.enqueue("request-c", "key-a", other).unwrap_err().code,
        ErrorCode::IdempotencyConflict
    );
    assert_eq!(
        store
            .checked_job(&JobMutation {
                job_id: job.job_id.clone(),
                expected_revision: 0
            })
            .unwrap_err()
            .code,
        ErrorCode::RevisionConflict
    );
    drop(store);
    let mut store = Store::open(&path).unwrap();
    assert_eq!(store.state.session_id, session);
    assert_eq!(store.state.watermark, 2);
    let recovered = store.state.jobs[&job.job_id].clone();
    assert_eq!(recovered.state, JobState::Failed);
    assert!(recovered.error.unwrap().retryable);
    let retried = store
        .retry(&JobMutation {
            job_id: job.job_id,
            expected_revision: 2,
        })
        .unwrap();
    assert_eq!(retried.attempt, 2);
    assert_eq!(retried.revision, 3);
}

#[test]
fn cancelled_job_cannot_be_revived_and_replay_is_ordered() {
    let (_temporary, path) = directory();
    let mut store = Store::open(&path).unwrap();
    let (job, _) = store.enqueue("request", "key", params()).unwrap();
    store
        .transition(&job.job_id, JobState::Running, None, None)
        .unwrap();
    store
        .transition(
            &job.job_id,
            JobState::Cancelled,
            Some(WireError::new(ErrorCode::Cancelled, "Cancelled.", false)),
            None,
        )
        .unwrap();
    assert_eq!(
        store
            .transition(&job.job_id, JobState::Running, None, None)
            .unwrap_err()
            .code,
        ErrorCode::InvalidTransition
    );
    let replay = store
        .replay(&ReplayParams {
            session_id: store.state.session_id.clone(),
            after_sequence: 0,
            limit: 2,
        })
        .unwrap();
    assert_eq!(
        replay
            .events
            .iter()
            .map(|event| event.sequence)
            .collect::<Vec<_>>(),
        vec![1, 2]
    );
    assert!(replay.has_more);
    let rest = store
        .replay(&ReplayParams {
            session_id: store.state.session_id.clone(),
            after_sequence: 2,
            limit: 2,
        })
        .unwrap();
    assert_eq!(rest.events[0].data.state, JobState::Cancelled);
    assert!(!rest.has_more);
}

#[test]
fn lock_and_corrupt_state_fail_closed() {
    let (_temporary, path) = directory();
    let store = Store::open(&path).unwrap();
    assert!(matches!(
        Store::open(&path),
        Err(WireError {
            code: ErrorCode::StateLocked,
            ..
        })
    ));
    drop(store);
    fs::write(path.join("management.json"), b"{broken").unwrap();
    assert!(matches!(
        Store::open(&path),
        Err(WireError {
            code: ErrorCode::RegistryRecoveryRequired,
            ..
        })
    ));
    assert_eq!(fs::read(path.join("management.json")).unwrap(), b"{broken");
}

#[test]
fn failed_persistence_does_not_mutate_memory() {
    let (_temporary, path) = directory();
    let mut store = Store::open(&path).unwrap();
    fs::remove_file(path.join("management.json")).unwrap();
    fs::create_dir(path.join("management.json")).unwrap();
    assert_eq!(
        store.enqueue("request", "key", params()).unwrap_err().code,
        ErrorCode::RegistryRecoveryRequired
    );
    assert_eq!(store.state.watermark, 0);
    assert!(store.state.jobs.is_empty());
}

#[test]
fn replay_wrong_session_future_and_expired_are_explicit() {
    let (_temporary, path) = directory();
    let mut store = Store::open(&path).unwrap();
    let (job, _) = store.enqueue("request", "key", params()).unwrap();
    assert_eq!(
        store
            .replay(&ReplayParams {
                session_id: "different".to_owned(),
                after_sequence: 0,
                limit: 1
            })
            .unwrap_err()
            .code,
        ErrorCode::EventsExpired
    );
    assert_eq!(
        store
            .replay(&ReplayParams {
                session_id: store.state.session_id.clone(),
                after_sequence: 10,
                limit: 1
            })
            .unwrap_err()
            .code,
        ErrorCode::EventsExpired
    );
    store
        .transition(&job.job_id, JobState::Running, None, None)
        .unwrap();
    store.state.events.remove(0);
    assert_eq!(
        store
            .replay(&ReplayParams {
                session_id: store.state.session_id.clone(),
                after_sequence: 0,
                limit: 1
            })
            .unwrap_err()
            .code,
        ErrorCode::EventsExpired
    );
}

#[cfg(unix)]
#[test]
fn symlink_state_is_never_read_or_overwritten() {
    let (_temporary, path) = directory();
    let target = path.join("unrelated");
    fs::write(&target, b"untouched").unwrap();
    std::os::unix::fs::symlink(&target, path.join("management.json")).unwrap();
    assert!(Store::open(&path).is_err());
    assert_eq!(fs::read(target).unwrap(), b"untouched");
}

#[test]
fn key_is_identity_and_locale_not_title() {
    assert_eq!(catalog_key(&params()), "FIXTURE00001:US:en-US");
}

#[tokio::test]
async fn framing_exact_limit_and_oversize_drain() {
    let data = [vec![b' '; MAX_LINE_BYTES], b"\n{}\n".to_vec()].concat();
    let mut reader = BufReader::new(std::io::Cursor::new(data));
    assert!(matches!(transport::read_line(&mut reader).await.unwrap(),
        Line::Frame(bytes) if bytes.len() == MAX_LINE_BYTES));
    assert!(
        matches!(transport::read_line(&mut reader).await.unwrap(), Line::Frame(bytes) if bytes == b"{}")
    );
    let data = [vec![b' '; MAX_LINE_BYTES + 1], b"\n{}\n".to_vec()].concat();
    let mut reader = BufReader::new(std::io::Cursor::new(data));
    assert!(matches!(
        transport::read_line(&mut reader).await.unwrap(),
        Line::Oversized
    ));
    assert!(
        matches!(transport::read_line(&mut reader).await.unwrap(), Line::Frame(bytes) if bytes == b"{}")
    );
}

#[tokio::test]
async fn fragmented_and_truncated_lines() {
    let (mut input, output) = tokio::io::duplex(8);
    let writer = tokio::spawn(async move {
        input.write_all(b"{").await.unwrap();
        tokio::task::yield_now().await;
        input.write_all(b"}\nunfinished").await.unwrap();
    });
    let mut reader = BufReader::new(output);
    assert!(
        matches!(transport::read_line(&mut reader).await.unwrap(), Line::Frame(bytes) if bytes == b"{}")
    );
    assert!(matches!(
        transport::read_line(&mut reader).await.unwrap(),
        Line::Truncated
    ));
    writer.await.unwrap();
}

#[test]
fn wire_parser_rejects_injection_and_incompatible_versions() {
    let mut value = serde_json::json!({"kind":"request","protocol":{"major":1,"minor":0},
        "requestID":"request","command":"product.detail",
        "params":{"productID":"FIXTURE00001","market":"US","language":"en-US","refresh":"network"}});
    assert!(transport::parse(&serde_json::to_vec(&value).unwrap()).is_ok());
    value["params"]["productID"] = serde_json::json!("../credentials");
    assert_eq!(
        transport::parse(&serde_json::to_vec(&value).unwrap())
            .unwrap_err()
            .code,
        ErrorCode::InvalidRequest
    );
    value["protocol"]["minor"] = serde_json::json!(1);
    assert_eq!(
        transport::parse(&serde_json::to_vec(&value).unwrap())
            .unwrap_err()
            .code,
        ErrorCode::ProtocolMismatch
    );
    value["protocol"]["minor"] = serde_json::json!(0);
    value["command"] = serde_json::json!("private.execute");
    assert_eq!(
        transport::parse(&serde_json::to_vec(&value).unwrap())
            .unwrap_err()
            .code,
        ErrorCode::UnknownCommand
    );
}

#[test]
fn duplicate_fields_are_rejected_at_every_depth() {
    for input in [
        r#"{"kind":"request","protocol":{"major":1,"minor":0},"requestID":"request","command":"auth.status","command":"auth.logout","params":{}}"#,
        r#"{"kind":"request","protocol":{"major":1,"minor":0,"minor":0},"requestID":"request","command":"auth.status","params":{}}"#,
        r#"{"kind":"request","protocol":{"major":1,"minor":0},"requestID":"request","command":"hello","params":{"client":"fixture","client":"fixture","clientVersion":"1"}}"#,
    ] {
        assert_eq!(
            transport::parse(input.as_bytes()).unwrap_err().code,
            ErrorCode::InvalidRequest
        );
    }
}
