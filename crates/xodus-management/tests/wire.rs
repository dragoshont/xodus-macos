use xodus_management::wire::{Operation, Request};

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
