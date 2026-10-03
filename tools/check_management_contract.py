"""Schema shape checks for deterministic sanitized fixtures (not live API tests)."""
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
print(f"Contract fixtures: {len(positive)} positive, {len(negative)} negative, "
      f"{len(evidence)} evidence-edge; preserved 9 foundation definitions.")
