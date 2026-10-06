"""Schema shape checks for deterministic sanitized fixtures (not live API tests)."""
import copy
import json
from pathlib import Path

import jsonschema

root = Path(__file__).resolve().parents[1]
schema = json.loads((root / "docs/contracts/management-v1.schema.json").read_text())
baseline = json.loads((root / "docs/contracts/foundation-v1.schema.json").read_text())
jsonschema.Draft202012Validator.check_schema(schema)
validator = jsonschema.Draft202012Validator(schema, format_checker=jsonschema.FormatChecker())
fixtures = root / "docs/contracts/fixtures/management-v1"
positive = json.loads((fixtures / "positive.json").read_text())
negative = json.loads((fixtures / "negative.json").read_text())
for frame in positive:
    validator.validate(frame)
failed_schema = {**schema, "oneOf": [{"$ref": "#/$defs/failedDiscoveryData"}]}
failed_validator = jsonschema.Draft202012Validator(
    failed_schema, format_checker=jsonschema.FormatChecker())
failed_frame = next(frame for frame in positive
                    if frame.get("requestID") == "fixture-discovery-all-failed")
failed_data = failed_frame["error"]["details"]
failed_validator.validate(failed_data)
failed_cases = [
    {**failed_data, "products": [next(frame["data"]["product"] for frame in positive
                                  if "product" in frame.get("data", {}))]},
    {**failed_data, "failures": []},
    {**failed_data, "failures": failed_data["failures"] * 17},
    {**failed_data, "source": "unknown"},
    {**failed_data, "token": "fixture-not-a-token"},
]
missing_provenance = copy.deepcopy(failed_data)
del missing_provenance["checkedAt"]
failed_cases.append(missing_provenance)
for case in failed_cases:
    assert list(failed_validator.iter_errors(case)), "unsafe failed discovery details"
failed_query_schema = {**schema, "oneOf": [{"$ref": "#/$defs/failedQueryData"}]}
failed_query_validator = jsonschema.Draft202012Validator(
    failed_query_schema, format_checker=jsonschema.FormatChecker())
failed_query_frame = next(frame for frame in positive if frame.get("requestID") == "fixture-query-all-failed")
failed_query = failed_query_frame["error"]["details"]
failed_query_validator.validate(failed_query)
failed_query_cases = [
    {**failed_query, "failures": []},
    {**failed_query, "failures": failed_query["failures"] * 17},
    {**failed_query, "query": ""},
    {**failed_query, "nextCursor": "https://attacker.invalid/"},
    {**failed_query, "token": "fixture-not-a-token"},
    {key: value for key, value in failed_query.items() if key != "checkedAt"},
]
for case in failed_query_cases:
    assert list(failed_query_validator.iter_errors(case)), "unsafe failed query details"
for case in negative:
    assert list(validator.iter_errors(case["frame"])), case["name"]
for name in ("identifier", "protocol", "fingerprint", "entitlement", "installability",
             "compatibility", "installation", "inventoryMetadata", "productEvidence"):
    assert schema["$defs"][name] == baseline["$defs"][name], name
evidence = json.loads((fixtures / "evidence-edge.json").read_text())
evidence_schema = {**schema, "oneOf": [{"$ref": "#/$defs/productEvidence"}]}
for item in evidence:
    jsonschema.Draft202012Validator(
        evidence_schema, format_checker=jsonschema.FormatChecker()).validate(item)
assert positive == [json.loads(line) for line in (fixtures / "positive.jsonl").read_text().splitlines()]
reserved = json.loads((fixtures / "install-plan-reserved.json").read_text())
plan_validator = jsonschema.Draft202012Validator(
    {**schema, "oneOf": [{"$ref": "#/$defs/installPlanData"}]},
    format_checker=jsonschema.FormatChecker())
plan_validator.validate(reserved["data"])
for case in reserved["invalid"]:
    assert list(plan_validator.iter_errors(case["data"])), case["name"]
print(f"Contract fixtures: {len(positive)} positive, {len(negative)} negative, "
      f"{len(failed_cases)} failed-page negative checks, "
      f"{len(failed_query_cases)} failed-query negative checks, "
      f"{len(evidence)} evidence-edge; reserved descriptor {len(reserved['invalid'])} negatives; "
      "preserved 9 foundation definitions.")
