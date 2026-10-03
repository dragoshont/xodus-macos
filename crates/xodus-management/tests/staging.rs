use sha2::{Digest, Sha256};
use std::fs;
use xodus_management::staging::*;
use xodus_management::wire::ErrorCode;

fn directory() -> (tempfile::TempDir, std::path::PathBuf) {
    let temporary = tempfile::tempdir().unwrap();
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        fs::set_permissions(temporary.path(), fs::Permissions::from_mode(0o700)).unwrap();
    }
    let path = temporary.path().canonicalize().unwrap();
    (temporary, path)
}

fn manifest(data: &[u8]) -> Manifest {
    Manifest {
        product_id: "fixture-product".to_owned(),
        edition_id: "fixture-edition".to_owned(),
        package_id: "fixture-package".to_owned(),
        package_version: "fixture-version".to_owned(),
        files: vec![ManifestFile {
            path: "content/data.bin".to_owned(),
            bytes: data.len() as u64,
            sha256: Sha256::digest(data)
                .iter()
                .map(|byte| format!("{byte:02x}"))
                .collect(),
        }],
    }
}

fn commit(
    store: &mut StagingStore,
    data: &[u8],
    id: Option<String>,
    revision: u64,
) -> LocalInstallation {
    let transaction = store.prepare(manifest(data), id, revision, 0).unwrap();
    store
        .write_file(
            &transaction.transaction_id,
            "content/data.bin",
            data,
            || false,
        )
        .unwrap();
    store.verify(&transaction.transaction_id).unwrap();
    store.commit(&transaction.transaction_id).unwrap()
}

#[test]
fn registry_count_capacity_is_enforced_before_preparation_and_authoritative_promotion() {
    let (_temporary, path) = directory();
    let mut store = StagingStore::open(&path).unwrap();
    let pending = store.prepare(manifest(b"pending"), None, 0, 0).unwrap();
    store
        .write_file(
            &pending.transaction_id,
            "content/data.bin",
            b"pending".as_slice(),
            || false,
        )
        .unwrap();
    store.verify(&pending.transaction_id).unwrap();
    let first = commit(&mut store, b"x", None, 0);
    for _ in 1..MAX_REGISTRY_INSTALLATIONS {
        commit(&mut store, b"x", None, 0);
    }
    let saves = path.join("saves").join(&first.installation_id);
    fs::create_dir(&saves).unwrap();
    fs::write(saves.join("progress.save"), b"user progress").unwrap();
    let before = fs::read(path.join("registry.json")).unwrap();
    assert_eq!(store.snapshot().len(), MAX_REGISTRY_INSTALLATIONS);
    assert_eq!(
        store
            .prepare(manifest(b"extra"), None, 0, 0)
            .unwrap_err()
            .code,
        ErrorCode::LimitExceeded
    );
    assert_eq!(
        store.commit(&pending.transaction_id).unwrap_err().code,
        ErrorCode::LimitExceeded
    );
    assert_eq!(fs::read(path.join("registry.json")).unwrap(), before);
    assert_eq!(store.recoveries().unwrap()[0].phase, Phase::Verified);
    assert!(
        path.join("staging")
            .join(&pending.transaction_id)
            .join("content/data.bin")
            .is_file()
    );
    assert!(!path.join("versions").join(&pending.transaction_id).exists());
    assert_eq!(
        fs::read(
            path.join("versions")
                .join(&first.active.transaction_id)
                .join("content/data.bin")
        )
        .unwrap(),
        b"x"
    );
    assert_eq!(
        fs::read(saves.join("progress.save")).unwrap(),
        b"user progress"
    );
    drop(store);
    assert_eq!(
        StagingStore::open(&path).unwrap().snapshot().len(),
        MAX_REGISTRY_INSTALLATIONS
    );
}

fn sized_registry_fixture(registry: &mut serde_json::Value, target: usize) {
    let template = registry["installations"]
        .as_object()
        .unwrap()
        .values()
        .next()
        .unwrap()
        .clone();
    let mut ids = Vec::new();
    for index in 0..16 {
        let id = uuid::Uuid::from_u128(10000 + index).to_string();
        let mut entry = template.clone();
        entry["installationID"] = serde_json::json!(id);
        entry["rollback"] = serde_json::Value::Null;
        entry["active"]["transactionID"] =
            serde_json::json!(uuid::Uuid::from_u128(20000 + index).to_string());
        entry["active"]["manifest"]["files"] = serde_json::json!(
            (0..256)
                .map(|file| ManifestFile {
                    path: format!("f{file:03}"),
                    bytes: 0,
                    sha256: Sha256::digest([])
                        .iter()
                        .map(|byte| format!("{byte:02x}"))
                        .collect()
                })
                .collect::<Vec<_>>()
        );
        registry["installations"][&id] = entry;
        ids.push(id);
    }
    let mut remaining = target
        .checked_sub(serde_json::to_vec(registry).unwrap().len())
        .unwrap();
    for id in ids {
        for file in registry["installations"][&id]["active"]["manifest"]["files"]
            .as_array_mut()
            .unwrap()
        {
            let original = file["path"].as_str().unwrap().to_owned();
            let added = (1024 - original.len()).min(remaining);
            file["path"] = serde_json::json!(format!("{original}{}", "x".repeat(added)));
            remaining -= added;
        }
    }
    assert_eq!(remaining, 0);
    assert_eq!(serde_json::to_vec(registry).unwrap().len(), target);
}

#[test]
fn exact_registry_byte_boundary_reopens_and_one_byte_excess_never_replaces_active_or_saves() {
    for excess in [0, 1] {
        let (_temporary, path) = directory();
        let mut store = StagingStore::open(&path).unwrap();
        let first = commit(&mut store, b"first", None, 0);
        let saves = path.join("saves").join(&first.installation_id);
        fs::create_dir(&saves).unwrap();
        fs::write(saves.join("progress.save"), b"user progress").unwrap();
        let pending = store
            .prepare(
                manifest(b"second"),
                Some(first.installation_id.clone()),
                1,
                0,
            )
            .unwrap();
        store
            .write_file(
                &pending.transaction_id,
                "content/data.bin",
                b"second".as_slice(),
                || false,
            )
            .unwrap();
        store.verify(&pending.transaction_id).unwrap();
        let mut registry: serde_json::Value =
            serde_json::from_slice(&fs::read(path.join("registry.json")).unwrap()).unwrap();
        let mut candidate = registry.clone();
        let entry = &mut candidate["installations"][&first.installation_id];
        entry["revision"] = serde_json::json!(2);
        entry["rollback"] = serde_json::to_value(&first.active).unwrap();
        entry["active"] =
            serde_json::json!({"transactionID":pending.transaction_id,"manifest":pending.manifest});
        let delta = serde_json::to_vec(&candidate).unwrap().len()
            - serde_json::to_vec(&registry).unwrap().len();
        sized_registry_fixture(&mut registry, MAX_STORAGE_JSON_BYTES - delta + excess);
        drop(store);
        fs::write(
            path.join("registry.json"),
            serde_json::to_vec(&registry).unwrap(),
        )
        .unwrap();
        let mut store = StagingStore::open(&path).unwrap();
        let before = fs::read(path.join("registry.json")).unwrap();
        let mut exact_candidate: serde_json::Value = serde_json::from_slice(&before).unwrap();
        exact_candidate["installations"][&first.installation_id] =
            candidate["installations"][&first.installation_id].clone();
        assert_eq!(
            serde_json::to_vec(&exact_candidate).unwrap().len(),
            MAX_STORAGE_JSON_BYTES + excess
        );
        if excess == 0 {
            let updated = store.commit(&pending.transaction_id).unwrap();
            assert_eq!(updated.active.transaction_id, pending.transaction_id);
            assert_eq!(
                fs::metadata(path.join("registry.json")).unwrap().len(),
                MAX_STORAGE_JSON_BYTES as u64
            );
            let mut too_large = manifest(b"third");
            too_large.package_version.push('x');
            assert_eq!(
                store
                    .prepare(too_large, Some(first.installation_id.clone()), 2, 0)
                    .unwrap_err()
                    .code,
                ErrorCode::LimitExceeded
            );
        } else {
            assert_eq!(
                store
                    .prepare(
                        manifest(b"second"),
                        Some(first.installation_id.clone()),
                        1,
                        0
                    )
                    .unwrap_err()
                    .code,
                ErrorCode::LimitExceeded
            );
            assert_eq!(
                store.commit(&pending.transaction_id).unwrap_err().code,
                ErrorCode::LimitExceeded
            );
            assert_eq!(fs::read(path.join("registry.json")).unwrap(), before);
            assert_eq!(store.recoveries().unwrap()[0].phase, Phase::Verified);
            assert!(path.join("staging").join(&pending.transaction_id).is_dir());
            assert!(!path.join("versions").join(&pending.transaction_id).exists());
            assert_eq!(
                store
                    .snapshot()
                    .iter()
                    .find(|entry| entry.installation_id == first.installation_id)
                    .unwrap()
                    .active
                    .transaction_id,
                first.active.transaction_id
            );
        }
        assert_eq!(
            fs::read(
                path.join("versions")
                    .join(&first.active.transaction_id)
                    .join("content/data.bin")
            )
            .unwrap(),
            b"first"
        );
        assert_eq!(
            fs::read(saves.join("progress.save")).unwrap(),
            b"user progress"
        );
        drop(store);
        let reopened = StagingStore::open(&path).unwrap();
        assert_eq!(reopened.snapshot().len(), 17);
        assert_eq!(
            reopened
                .snapshot()
                .iter()
                .find(|entry| entry.installation_id == first.installation_id)
                .unwrap()
                .revision,
            if excess == 0 { 2 } else { 1 }
        );
    }
}

#[test]
fn atomic_update_rollback_preserves_separate_saves() {
    let (_temporary, path) = directory();
    let mut store = StagingStore::open(&path).unwrap();
    let first = commit(&mut store, b"first", None, 0);
    let saves = path.join("saves").join(&first.installation_id);
    fs::create_dir(&saves).unwrap();
    fs::write(saves.join("progress.save"), b"user progress").unwrap();
    let second = commit(
        &mut store,
        b"second",
        Some(first.installation_id.clone()),
        1,
    );
    assert_eq!(second.revision, 2);
    assert_eq!(
        second.rollback.as_ref().unwrap().transaction_id,
        first.active.transaction_id
    );
    let restored = store.rollback(&first.installation_id, 2).unwrap();
    assert_eq!(restored.active.transaction_id, first.active.transaction_id);
    assert_eq!(
        fs::read(saves.join("progress.save")).unwrap(),
        b"user progress"
    );
    drop(store);
    let store = StagingStore::open(&path).unwrap();
    assert_eq!(store.snapshot()[0].revision, 3);
}

#[test]
fn wrong_hash_cancel_and_corrupt_update_never_replace_known_good() {
    let (_temporary, path) = directory();
    let mut store = StagingStore::open(&path).unwrap();
    let first = commit(&mut store, b"first", None, 0);
    let transaction = store
        .prepare(
            manifest(b"second"),
            Some(first.installation_id.clone()),
            1,
            0,
        )
        .unwrap();
    assert_eq!(
        store
            .write_file(
                &transaction.transaction_id,
                "content/data.bin",
                b"bad".as_slice(),
                || false
            )
            .unwrap_err()
            .code,
        ErrorCode::IntegrityFailed
    );
    assert_eq!(
        store
            .write_file(
                &transaction.transaction_id,
                "content/data.bin",
                b"second".as_slice(),
                || true
            )
            .unwrap_err()
            .code,
        ErrorCode::Cancelled
    );
    store
        .write_file(
            &transaction.transaction_id,
            "content/data.bin",
            b"second".as_slice(),
            || false,
        )
        .unwrap();
    store.verify(&transaction.transaction_id).unwrap();
    fs::write(
        path.join("staging")
            .join(&transaction.transaction_id)
            .join("content/data.bin"),
        b"tamper",
    )
    .unwrap();
    assert_eq!(
        store.commit(&transaction.transaction_id).unwrap_err().code,
        ErrorCode::IntegrityFailed
    );
    assert_eq!(
        store.snapshot()[0].active.transaction_id,
        first.active.transaction_id
    );
}

#[test]
fn interrupted_work_is_recoverable_not_registered_or_deleted() {
    let (_temporary, path) = directory();
    let mut store = StagingStore::open(&path).unwrap();
    let transaction = store.prepare(manifest(b"new"), None, 0, 0).unwrap();
    store
        .write_file(
            &transaction.transaction_id,
            "content/data.bin",
            b"new".as_slice(),
            || false,
        )
        .unwrap();
    drop(store);
    let mut store = StagingStore::open(&path).unwrap();
    assert!(store.snapshot().is_empty());
    assert_eq!(store.recoveries().unwrap().len(), 1);
    store
        .discard_uncommitted(&transaction.transaction_id)
        .unwrap();
    assert!(store.recoveries().unwrap().is_empty());
    assert!(path.join("saves").exists());
}

#[test]
fn traversal_duplicate_overflow_and_unknown_files_fail_closed() {
    let (_temporary, path) = directory();
    let mut store = StagingStore::open(&path).unwrap();
    for bad in [
        "../outside",
        "/absolute",
        "C:/drive",
        "folder\\file",
        ".owner.json",
        "folder/../file",
    ] {
        let mut invalid = manifest(b"new");
        invalid.files[0].path = bad.to_owned();
        assert_eq!(
            store.prepare(invalid, None, 0, 0).unwrap_err().code,
            ErrorCode::InvalidRequest
        );
    }
    let mut duplicate = manifest(b"new");
    duplicate.files.push(duplicate.files[0].clone());
    assert_eq!(
        store.prepare(duplicate, None, 0, 0).unwrap_err().code,
        ErrorCode::InvalidRequest
    );
    let mut overflow = manifest(b"new");
    overflow.files[0].bytes = u64::MAX;
    assert_eq!(
        store.prepare(overflow, None, 0, 1).unwrap_err().code,
        ErrorCode::InvalidRequest
    );
    let transaction = store.prepare(manifest(b"new"), None, 0, 0).unwrap();
    fs::write(
        path.join("staging")
            .join(&transaction.transaction_id)
            .join("unexpected"),
        b"must survive",
    )
    .unwrap();
    assert!(
        store
            .discard_uncommitted(&transaction.transaction_id)
            .is_err()
    );
    assert!(
        path.join("staging")
            .join(&transaction.transaction_id)
            .join("unexpected")
            .exists()
    );
}

#[cfg(unix)]
#[test]
fn symlink_escape_and_stale_revision_cannot_mutate_external_content() {
    let (_temporary, path) = directory();
    let mut store = StagingStore::open(&path).unwrap();
    let transaction = store.prepare(manifest(b"new"), None, 0, 0).unwrap();
    let outside = path.join("unrelated");
    fs::create_dir(&outside).unwrap();
    fs::write(outside.join("data.bin"), b"untouched").unwrap();
    std::os::unix::fs::symlink(
        &outside,
        path.join("staging")
            .join(&transaction.transaction_id)
            .join("content"),
    )
    .unwrap();
    assert!(
        store
            .write_file(
                &transaction.transaction_id,
                "content/data.bin",
                b"new".as_slice(),
                || false
            )
            .is_err()
    );
    assert_eq!(fs::read(outside.join("data.bin")).unwrap(), b"untouched");
    let installation = commit(&mut store, b"first", None, 0);
    assert_eq!(
        store
            .prepare(
                manifest(b"update"),
                Some(installation.installation_id),
                0,
                0
            )
            .unwrap_err()
            .code,
        ErrorCode::RevisionConflict
    );
}

#[test]
fn interrupted_verified_and_promoted_versions_preserve_prior_registry() {
    let (_temporary, path) = directory();
    let mut store = StagingStore::open(&path).unwrap();
    let first = commit(&mut store, b"first", None, 0);
    let transaction = store
        .prepare(
            manifest(b"second"),
            Some(first.installation_id.clone()),
            1,
            0,
        )
        .unwrap();
    store
        .write_file(
            &transaction.transaction_id,
            "content/data.bin",
            b"second".as_slice(),
            || false,
        )
        .unwrap();
    store.verify(&transaction.transaction_id).unwrap();
    drop(store);
    let store = StagingStore::open(&path).unwrap();
    assert_eq!(
        store.snapshot()[0].active.transaction_id,
        first.active.transaction_id
    );
    assert_eq!(store.recoveries().unwrap()[0].phase, Phase::Verified);
    drop(store);
    let mut promoted = transaction.clone();
    promoted.phase = Phase::Promoting;
    let journal = path
        .join("journals")
        .join(format!("{}.json", transaction.transaction_id));
    fs::write(&journal, serde_json::to_vec(&promoted).unwrap()).unwrap();
    fs::rename(
        path.join("staging").join(&transaction.transaction_id),
        path.join("versions").join(&transaction.transaction_id),
    )
    .unwrap();
    let mut store = StagingStore::open(&path).unwrap();
    assert_eq!(
        store.snapshot()[0].active.transaction_id,
        first.active.transaction_id
    );
    assert_eq!(store.recoveries().unwrap()[0].phase, Phase::Promoting);
    store
        .discard_uncommitted(&transaction.transaction_id)
        .unwrap();
    assert!(
        path.join("versions")
            .join(&first.active.transaction_id)
            .exists()
    );
}

#[test]
fn commit_won_before_journal_cleanup_can_be_reconciled_explicitly() {
    let (_temporary, path) = directory();
    let mut store = StagingStore::open(&path).unwrap();
    let mut transaction = store.prepare(manifest(b"first"), None, 0, 0).unwrap();
    store
        .write_file(
            &transaction.transaction_id,
            "content/data.bin",
            b"first".as_slice(),
            || false,
        )
        .unwrap();
    store.verify(&transaction.transaction_id).unwrap();
    let installation = store.commit(&transaction.transaction_id).unwrap();
    transaction.phase = Phase::Promoting;
    fs::write(
        path.join("journals")
            .join(format!("{}.json", transaction.transaction_id)),
        serde_json::to_vec(&transaction).unwrap(),
    )
    .unwrap();
    drop(store);
    let mut store = StagingStore::open(&path).unwrap();
    assert_eq!(
        store.snapshot()[0].active.transaction_id,
        installation.active.transaction_id
    );
    assert!(
        store
            .discard_uncommitted(&transaction.transaction_id)
            .is_err()
    );
    store
        .reconcile_committed(&transaction.transaction_id)
        .unwrap();
    assert!(store.recoveries().unwrap().is_empty());
}

#[test]
fn rollback_reverifies_retained_content_and_manifest_identity_is_stable() {
    let (_temporary, path) = directory();
    let mut store = StagingStore::open(&path).unwrap();
    let first = commit(&mut store, b"first", None, 0);
    let mut wrong_identity = manifest(b"second");
    wrong_identity.edition_id = "different-edition".to_owned();
    assert_eq!(
        store
            .prepare(wrong_identity, Some(first.installation_id.clone()), 1, 0)
            .unwrap_err()
            .code,
        ErrorCode::InvalidRequest
    );
    let second = commit(
        &mut store,
        b"second",
        Some(first.installation_id.clone()),
        1,
    );
    fs::write(
        path.join("versions")
            .join(&first.active.transaction_id)
            .join("content/data.bin"),
        b"wrong",
    )
    .unwrap();
    assert_eq!(
        store.rollback(&first.installation_id, 2).unwrap_err().code,
        ErrorCode::IntegrityFailed
    );
    assert_eq!(
        store.snapshot()[0].active.transaction_id,
        second.active.transaction_id
    );
}

#[test]
fn space_and_corrupt_registry_do_not_turn_into_empty_success() {
    let (_temporary, path) = directory();
    let mut store = StagingStore::open(&path).unwrap();
    // Shared filesystem free space can change between a measurement and prepare.
    let reserve = u64::MAX / 4;
    assert!(fs2::available_space(&path).unwrap() < reserve);
    assert_eq!(
        store
            .prepare(manifest(b"data"), None, 0, reserve)
            .unwrap_err()
            .code,
        ErrorCode::InsufficientSpace
    );
    drop(store);
    fs::write(path.join("registry.json"), b"corrupt").unwrap();
    assert!(StagingStore::open(&path).is_err());
    assert_eq!(fs::read(path.join("registry.json")).unwrap(), b"corrupt");
}
