use xodus_management::wire::{InstallationRecord, Operation, Request};

#[test]
fn all_frozen_requests_decode() {
    let frames: Vec<serde_json::Value> = serde_json::from_str(include_str!(
        "../../../docs/contracts/fixtures/management-v1/positive.json"
    ))
    .unwrap();
    let requests: Vec<_> = frames
        .iter()
        .filter(|frame| frame["kind"] == "request")
        .collect();
    assert_eq!(requests.len(), xodus_management::transport::COMMANDS.len());
    for frame in requests {
        let parsed: Request = serde_json::from_value(frame.clone()).unwrap();
        assert_eq!(serde_json::to_value(parsed).unwrap(), *frame);
    }
}

#[test]
fn unknown_envelope_and_params_are_rejected() {
    let mut value = serde_json::json!({
        "kind": "request", "protocol": {"major": 1, "minor": 0},
        "requestID": "fixture", "command": "auth.status", "params": {}
    });
    let parsed: Request = serde_json::from_value(value.clone()).unwrap();
    assert!(matches!(parsed.operation, Operation::AuthStatus(_)));
    value["token"] = serde_json::json!("fixture");
    assert!(serde_json::from_value::<Request>(value.clone()).is_err());
    value.as_object_mut().unwrap().remove("token");
    value["params"]["unexpected"] = serde_json::json!(true);
    assert!(serde_json::from_value::<Request>(value).is_err());
}

#[test]
fn installation_runtime_is_required_nullable_and_old_wire_records_still_decode() {
    let frames: Vec<serde_json::Value> = serde_json::from_str(include_str!(
        "../../../docs/contracts/fixtures/management-v1/positive.json"
    ))
    .unwrap();
    let records: Vec<_> = frames
        .iter()
        .filter_map(|frame| frame["data"]["installations"].as_array())
        .flatten()
        .collect();
    assert_eq!(records.len(), 2);
    for value in records {
        let parsed: InstallationRecord = serde_json::from_value(value.clone()).unwrap();
        assert_eq!(
            parsed.runtime_fingerprint.is_none(),
            value["runtimeFingerprint"].is_null()
        );
        assert_eq!(serde_json::to_value(parsed).unwrap(), *value);
        let mut missing = value.clone();
        missing
            .as_object_mut()
            .unwrap()
            .remove("runtimeFingerprint");
        assert!(serde_json::from_value::<InstallationRecord>(missing).is_err());
        let mut invalid = value.clone();
        invalid["runtimeFingerprint"] = serde_json::json!(true);
        assert!(serde_json::from_value::<InstallationRecord>(invalid).is_err());
    }
}

#[test]
fn install_plan_failure_details_are_closed_exact_tuples_and_reserved_ready_dto_is_not_live_data() {
    use xodus_management::wire::{Data, InstallPlanData, InstallPlanFailureDetails};
    let frames: Vec<serde_json::Value> = serde_json::from_str(include_str!(
        "../../../docs/contracts/fixtures/management-v1/positive.json"
    ))
    .unwrap();
    let details: Vec<_> = frames
        .iter()
        .filter_map(|frame| {
            let details = &frame["error"]["details"];
            (details["category"] == "installPlanFailure").then_some(details)
        })
        .collect();
    assert_eq!(details.len(), 17);
    for value in details {
        let parsed: InstallPlanFailureDetails = serde_json::from_value(value.clone()).unwrap();
        assert_eq!(serde_json::to_value(parsed).unwrap(), *value);
        for field in ["category", "stage", "reason"] {
            let mut invalid = value.clone();
            invalid.as_object_mut().unwrap().remove(field);
            assert!(serde_json::from_value::<InstallPlanFailureDetails>(invalid).is_err());
        }
        let mut invalid = value.clone();
        invalid["reason"] = serde_json::json!("PRIVATE_SENTINEL");
        assert!(serde_json::from_value::<InstallPlanFailureDetails>(invalid).is_err());
    }
    assert!(
        serde_json::from_value::<InstallPlanFailureDetails>(serde_json::json!({
            "category":"installPlanFailure", "stage":"integrity", "reason":"ambiguous"
        }))
        .is_err()
    );
    let reserved: serde_json::Value = serde_json::from_str(include_str!(
        "../../../docs/contracts/fixtures/management-v1/install-plan-reserved.json"
    ))
    .unwrap();
    let parsed: InstallPlanData = serde_json::from_value(reserved["data"].clone()).unwrap();
    assert_eq!(serde_json::to_value(parsed).unwrap(), reserved["data"]);
    for field in reserved["data"].as_object().unwrap().keys() {
        let mut missing = reserved["data"].clone();
        missing.as_object_mut().unwrap().remove(field);
        assert!(
            serde_json::from_value::<InstallPlanData>(missing).is_err(),
            "{field}"
        );
    }
    assert!(serde_json::from_value::<Data>(reserved["data"].clone()).is_err());
}
