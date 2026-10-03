"""Deterministic contract artifact generation; never contacts an account or API."""
import copy
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "docs" / "contracts" / "foundation-v1.schema.json"
schema = json.loads(BASE.read_text(encoding="utf-8"))
defs = schema["$defs"]


def ref(name):
    return {"$ref": f"#/$defs/{name}"}


def enum(*values):
    return {"enum": list(values)}


def obj(fields, optional=()):
    return {
        "type": "object",
        "required": [key for key in fields if key not in optional],
        "properties": fields,
        "additionalProperties": False,
    }


def array(item, maximum=4096):
    return {"type": "array", "items": item, "maxItems": maximum}


def nullable(item):
    return {"anyOf": [item, {"type": "null"}]}


text = {"type": "string", "minLength": 1, "maxLength": 1024}
identifier = ref("identifier")
uint = {"type": "integer", "minimum": 0, "maximum": 18446744073709551615}
boolean = {"type": "boolean"}
date = {"type": "string", "format": "date-time"}
empty = obj({})
product_params = obj({
    "productID": {"type": "string", "pattern": "^[A-Z0-9]{12}$"},
    "market": {"type": "string", "pattern": "^[A-Z]{2}$"},
    "language": {"type": "string", "pattern": "^[a-z]{2,3}(-[A-Z]{2})?$"},
    "refresh": enum("cache", "network"),
})
mutation = obj({"jobID": identifier, "expectedRevision": uint})
installation_mutation = obj({"installationID": identifier, "expectedRevision": uint})
commands = {
    "hello": obj({"client": text, "clientVersion": text,
                  "requiredCapabilities": array(identifier, 32)}, ("requiredCapabilities",)),
    "auth.begin": obj({"accountScope": {"const": "default"}}),
    "auth.cancel": obj({"flowID": identifier}),
    "auth.status": empty,
    "auth.logout": empty,
    "inventory.snapshot": obj({"accountScope": {"const": "default"},
                               "market": product_params["properties"]["market"],
                               "refresh": enum("cache", "network")}),
    "catalog.search": obj({
        "query": {"type": "string", "maxLength": 256},
        "market": product_params["properties"]["market"],
        "language": product_params["properties"]["language"],
        "platform": {"const": "pc"},
        "limit": {"type": "integer", "minimum": 1, "maximum": 100},
        "cursor": nullable({"type": "string", "maxLength": 128}),
    }),
    "catalog.discover": obj({
        "market": product_params["properties"]["market"],
        "language": product_params["properties"]["language"],
        "limit": {"type": "integer", "minimum": 1, "maximum": 16},
        "cursor": nullable({"type": "string", "maxLength": 128}),
    }),
    "catalog.query": obj({
        "query": {"type": "string", "minLength": 1, "maxLength": 256},
        "market": product_params["properties"]["market"],
        "language": product_params["properties"]["language"],
        "limit": {"type": "integer", "minimum": 1, "maximum": 16},
        "cursor": nullable({"type": "string", "maxLength": 16384, "pattern": "^q1-([0-9a-f]{2})+$"}),
    }),
    "product.detail": product_params,
    "install.plan": obj({
        "productID": product_params["properties"]["productID"], "editionID": identifier,
        "architecture": enum("arm64", "x86_64"),
        "language": product_params["properties"]["language"],
        "market": product_params["properties"]["market"],
        "destination": text, "experimentalConsent": boolean,
    }),
    "jobs.enqueue": {"oneOf": [
        obj({"kind": {"const": "catalogRefresh"}, "idempotencyKey": identifier,
             "product": {**product_params, "properties": {
                 **product_params["properties"], "refresh": {"const": "network"}}}}),
        obj({"kind": {"const": "install"}, "idempotencyKey": identifier,
             "planID": identifier, "planDigest": text}),
    ]},
    "jobs.pause": mutation, "jobs.resume": mutation,
    "jobs.cancel": mutation, "jobs.retry": mutation, "jobs.snapshot": empty,
    "events.replay": obj({"sessionID": identifier, "afterSequence": uint,
                           "limit": {"type": "integer", "minimum": 1, "maximum": 1000}}),
    "installed.snapshot": empty,
    "game.launch": installation_mutation,
    "game.rollback": installation_mutation,
    "game.update": obj({**installation_mutation["properties"], "planID": identifier,
                         "planDigest": text}),
    "game.remove": obj({**installation_mutation["properties"],
                         "preserveSaves": boolean, "confirmed": boolean}),
    "diagnostics.export": empty,
}
schema["title"] = "Xodus management protocol 1.0 scoped backend"
schema["description"] = (
    "Frozen strict envelope/evidence plus operation schemas. Unsupported operations "
    "only return typed errors. No credentials or arbitrary URLs in requests."
)
defs["request"]["properties"]["command"]["enum"] = list(commands)
defs["request"]["allOf"] = [
    {"if": {"properties": {"command": {"const": command}}},
     "then": {"properties": {"params": params}}}
    for command, params in commands.items()
]
defs["error"]["properties"]["code"]["enum"] += [
    "INVALID_REQUEST", "UNKNOWN_COMMAND", "HELLO_REQUIRED", "DUPLICATE_REQUEST",
    "NOT_FOUND", "INVALID_TRANSITION", "IDEMPOTENCY_CONFLICT", "STATE_LOCKED", "LIMIT_EXCEEDED",
]
defs["capability"] = obj({
    "command": identifier, "supported": boolean,
    "audience": nullable(text), "reason": nullable(text),
})
defs["helloData"] = obj({
    "protocol": ref("protocol"), "backendVersion": text,
    "runtimeFingerprint": nullable(text), "capabilities": array(ref("capability"), 32),
    "sessionID": identifier, "schema": {"const": schema["$id"]}, "catalogCorpus": text,
})
defs["authData"] = obj({
    "state": enum("signedOut", "credentialPresent", "expired", "invalid"),
    "credentialStore": {"const": "macOSKeychain"}, "audience": nullable(text),
    "expiresAt": nullable(date), "entitlementAuthorized": {"const": False},
})
defs["authFlow"] = obj({
    "flowID": identifier, "state": enum("pending", "completed", "cancelled", "failed"),
    "error": nullable(ref("error")),
})
defs["authData"]["properties"]["flow"] = ref("authFlow")
defs["productRecord"] = obj({
    "productID": identifier, "title": text, "market": text, "language": text,
    "source": text, "checkedAt": date, "freshness": enum("live", "cached"),
    "editions": array(ref("productEvidence"), 256), "pcCatalogCandidate": boolean,
})
defs["productRecord"]["properties"]["resolvedLanguage"] = text
defs["productData"] = obj({"product": ref("productRecord")})
defs["searchData"] = obj({
    "products": array(ref("productRecord"), 100),
    "corpus": {"const": "observedPublicProducts"},
    "completeness": {"const": "partial"}, "nextCursor": nullable(text), "cacheRevision": uint,
})
defs["discoveryFailure"] = obj({"productID": product_params["properties"]["productID"], "error": ref("error")})
defs["discoveryData"] = obj({
    "corpus": {"const": "pcGamePassDiscovery"}, "completeness": {"const": "partial"},
    "source": {"const": "MicrosoftGamePassSigls:v3"}, "checkedAt": date,
    "freshness": {"const": "live"},
    "corpusRevision": {"type": "string", "pattern": "^[a-f0-9]{64}$"},
    "products": {**array(ref("productRecord"), 16), "minItems": 1},
    "failures": array(ref("discoveryFailure"), 16), "nextCursor": nullable(text),
})
defs["failedDiscoveryData"] = copy.deepcopy(defs["discoveryData"])
defs["failedDiscoveryData"]["properties"]["products"] = array(ref("productRecord"), 0)
defs["failedDiscoveryData"]["properties"]["failures"]["minItems"] = 1
defs["queryData"] = obj({
    "corpus": {"const": "publicMicrosoftStoreSearch"}, "completeness": {"const": "partial"},
    "source": {"const": "MicrosoftStoreEdge:v9.0/searchResults"}, "checkedAt": date,
    "freshness": {"const": "live"}, "query": commands["catalog.query"]["properties"]["query"],
    "products": array({"allOf": [ref("productRecord"),
        {"properties": {"pcCatalogCandidate": {"const": True}, "freshness": {"const": "live"}}}]}, 16),
    "failures": array(ref("discoveryFailure"), 16),
    "nextCursor": commands["catalog.query"]["properties"]["cursor"],
})
defs["failedQueryData"] = copy.deepcopy(defs["queryData"])
defs["failedQueryData"]["properties"]["products"]["maxItems"] = 0
defs["failedQueryData"]["properties"]["failures"]["minItems"] = 1
defs["queryData"]["allOf"] = [{
    "if": {"properties": {"products": {"maxItems": 0}}},
    "then": {"properties": {"failures": {"maxItems": 0}}},
}]
defs["job"] = obj({
    "jobID": identifier, "revision": uint, "kind": {"const": "catalogRefresh"},
    "state": enum("queued", "running", "completed", "failed", "cancelled"),
    "requestID": identifier, "createdAt": date, "updatedAt": date,
    "product": product_params, "attempt": {"type": "integer", "minimum": 1, "maximum": 3},
    "error": nullable(ref("error")),
})
defs["jobData"] = obj({"job": ref("job"), "watermark": uint})
defs["jobsData"] = obj({
    "sessionID": identifier, "watermark": uint, "jobs": array(ref("job"), 256),
})
defs["event"]["properties"]["event"] = {"const": "job.changed"}
defs["event"]["properties"]["data"] = ref("job")
defs["event"]["required"] += ["requestID", "jobID", "revision"]
defs["replayData"] = obj({
    "sessionID": identifier, "watermark": uint,
    "events": array(ref("event"), 1000), "hasMore": boolean,
})
defs["installationRecord"] = obj({
    "installationID": identifier, "revision": uint, "productID": identifier,
    "editionID": identifier, "packageID": identifier, "packageVersion": text,
    "packageDigest": text, "runtimeFingerprint": text, "managedRoot": text,
    "savePolicy": {"const": "preserve"}, "health": enum("verified", "broken", "recoveryRequired"),
})
defs["installedData"] = obj({
    "registryVersion": ref("protocol"), "scope": {"const": "managementRegistryOnly"},
    "completeness": {"const": "complete"},
    "installations": array(ref("installationRecord")), "watermark": uint,
})
defs["diagnosticsData"] = obj({
    "reportVersion": {"const": 1}, "backendVersion": text, "protocol": ref("protocol"),
    "redacted": {"const": True}, "jobCount": uint, "cachedProductCount": uint,
    "runtimeCertified": {"const": False}, "inventoryAuthorized": {"const": False},
})
defs["success"]["properties"]["data"] = {"oneOf": [
    ref(name) for name in ("helloData", "authData", "productData", "searchData", "discoveryData", "queryData",
                          "jobData", "jobsData", "replayData", "installedData", "diagnosticsData")
]}


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes((json.dumps(value, indent=2, ensure_ascii=True) + "\n").encode("utf-8"))


write(ROOT / "docs" / "contracts" / "management-v1.schema.json", schema)
fixture_root = ROOT / "docs" / "contracts" / "fixtures" / "management-v1"
protocol = {"major": 1, "minor": 0}
positive = []
examples = {
    "hello": {"client": "fixture-client", "clientVersion": "1.0"},
    "auth.begin": {"accountScope": "default"}, "auth.cancel": {"flowID": "fixture-flow"},
    "inventory.snapshot": {"accountScope": "default", "market": "US", "refresh": "cache"},
    "catalog.search": {"query": "", "market": "US", "language": "en-US",
                       "platform": "pc", "limit": 100, "cursor": None},
    "catalog.discover": {"market": "US", "language": "en-US", "limit": 8, "cursor": None},
    "catalog.query": {"query": "Fixture Harbor", "market": "US", "language": "en-US", "limit": 8, "cursor": None},
    "product.detail": {"productID": "FIXTURE00001", "market": "US",
                       "language": "en-US", "refresh": "network"},
    "install.plan": {"productID": "FIXTURE00001", "editionID": "fixture-edition",
                     "architecture": "arm64", "language": "en-US", "market": "US",
                     "destination": "/fixture/managed", "experimentalConsent": False},
    "jobs.enqueue": {"kind": "catalogRefresh", "idempotencyKey": "fixture-key",
                     "product": {"productID": "FIXTURE00001", "market": "US",
                                 "language": "en-US", "refresh": "network"}},
    "events.replay": {"sessionID": "fixture-session", "afterSequence": 0, "limit": 1000},
    "game.update": {"installationID": "fixture-install", "expectedRevision": 1,
                    "planID": "fixture-plan", "planDigest": "fixture-digest"},
    "game.remove": {"installationID": "fixture-install", "expectedRevision": 1,
                    "preserveSaves": True, "confirmed": True},
}
for command in commands:
    params = examples.get(command, {})
    if command in ("jobs.pause", "jobs.resume", "jobs.cancel", "jobs.retry"):
        params = {"jobID": "fixture-job", "expectedRevision": 1}
    if command in ("game.launch", "game.rollback"):
        params = {"installationID": "fixture-install", "expectedRevision": 1}
    positive.append({"kind": "request", "protocol": protocol,
                     "requestID": f"fixture-{len(positive)+1}", "command": command, "params": params})
timestamp = "2026-10-03T12:00:00Z"
job = {"jobID": "fixture-job", "revision": 2, "kind": "catalogRefresh", "state": "cancelled",
       "requestID": "fixture-enqueue", "createdAt": timestamp, "updatedAt": timestamp,
       "product": examples["product.detail"], "attempt": 1,
       "error": {"code": "CANCELLED", "message": "Refresh cancelled.", "retryable": False}}
event = {"kind": "event", "protocol": protocol, "sessionID": "fixture-session", "sequence": 2,
         "requestID": "fixture-enqueue", "jobID": "fixture-job", "revision": 2,
         "event": "job.changed", "data": job}
evidence = {
    "productID": "FIXTURE00001", "editionID": "fixture-edition",
    "entitlement": {"kind": "unknown", "source": "notQueried", "checkedAt": None},
    "installability": {"kind": "unknown", "reason": "Package authorization not established."},
    "compatibility": {"kind": "unknown", "source": "notCertified", "checkedAt": None},
    "installation": {"kind": "notInstalled"},
    "inventory": {"completeness": "unknown", "checkedAt": None, "lastCompleteAt": None,
                  "source": "notQueried", "reason": "No complete entitlement source."},
}
record = {"productID": "FIXTURE00001", "title": "Fixture Harbor", "market": "US",
          "language": "en-US", "source": "fixture", "checkedAt": timestamp,
          "freshness": "cached", "editions": [evidence], "pcCatalogCandidate": True}
discovery = {"corpus": "pcGamePassDiscovery", "completeness": "partial",
    "source": "MicrosoftGamePassSigls:v3", "checkedAt": timestamp, "freshness": "live",
    "corpusRevision": "a" * 64, "products": [{**record, "freshness": "live", "resolvedLanguage":"en"}],
    "failures": [{"productID":"FIXTURE00002", "error":{"code":"NETWORK_UNAVAILABLE",
        "message":"Sanitized fixture lookup failure.", "retryable":True}}],
    "nextCursor":"d1-" + "a" * 64 + "-2"}
query_scope = hashlib.sha256(b"publicMicrosoftStoreSearch\0US\0en-US\0Fixture Harbor\0").hexdigest()
query_cursor = "q1-" + json.dumps({"version": 1, "scope": query_scope,
    "page": "https://storeedgefd.dsx.mp.microsoft.com/v9.0/pages/searchResults?market=US&locale=en-US&deviceFamily=windows.desktop&query=Fixture+Harbor&mediaType=games",
    "offset": 8, "revision": "a" * 64}, separators=(",", ":")).encode().hex()
query_page = {"corpus": "publicMicrosoftStoreSearch", "completeness": "partial",
    "source": "MicrosoftStoreEdge:v9.0/searchResults", "checkedAt": timestamp, "freshness": "live",
    "query": "Fixture Harbor", "products": discovery["products"], "failures": discovery["failures"],
    "nextCursor": query_cursor}
results = [
    {"protocol": protocol, "backendVersion": "fixture", "runtimeFingerprint": None,
     "capabilities": [{"command": "game.launch", "supported": False,
                       "audience": "Xbox package authorization and signed paired runtime",
                       "reason": "No certified runtime."}],
     "sessionID": "fixture-session", "schema": schema["$id"], "catalogCorpus": "observedPublicProducts"},
    {"state": "signedOut", "credentialStore": "macOSKeychain", "audience": None,
     "expiresAt": None, "entitlementAuthorized": False},
    {"product": record},
    {"products": [record], "corpus": "observedPublicProducts", "completeness": "partial",
     "nextCursor": None, "cacheRevision": 1},
    discovery,
    query_page,
    {**query_page, "products": [], "failures": [], "nextCursor": None},
    {"job": job, "watermark": 2},
    {"sessionID": "fixture-session", "watermark": 2, "jobs": [job]},
    {"sessionID": "fixture-session", "watermark": 2, "events": [event], "hasMore": False},
    {"registryVersion": protocol, "scope": "managementRegistryOnly",
     "completeness": "complete", "installations": [], "watermark": 2},
    {"reportVersion": 1, "backendVersion": "fixture", "protocol": protocol,
     "redacted": True, "jobCount": 1, "cachedProductCount": 1,
     "runtimeCertified": False, "inventoryAuthorized": False},
]
positive += [{"kind": "result", "protocol": protocol, "requestID": f"fixture-result-{i}",
              "ok": True, "data": data} for i, data in enumerate(results)]
positive += [event]
for state in ("pending", "completed", "cancelled", "failed"):
    positive.append({
        "kind": "result", "protocol": protocol, "requestID": f"fixture-auth-{state}", "ok": True,
        "data": {
            "state": "credentialPresent" if state == "completed" else "signedOut",
            "credentialStore": "macOSKeychain",
            "audience": "http://xboxlive.com" if state == "completed" else None,
            "expiresAt": "2099-01-01T00:00:00Z" if state == "completed" else None, "entitlementAuthorized": False,
            "flow": {"flowID": "fixture-flow", "state": state,
                     "error": {"code": "AUTH_CANCELLED" if state == "cancelled" else "AUTH_INVALID",
                               "message": "Sanitized fixture consent outcome.", "retryable": False}
                     if state in ("cancelled", "failed") else None},
        },
    })
for code in defs["error"]["properties"]["code"]["enum"]:
    positive.append({"kind": "result", "protocol": protocol, "requestID": "fixture-error",
                     "ok": False, "error": {"code": code, "message": "Sanitized fixture error.",
                                           "retryable": False}})
negative = []
for name, key, value in [
    ("wrongMajor", "protocol", {"major": 2, "minor": 0}),
    ("wrongMinor", "protocol", {"major": 1, "minor": 1}),
    ("emptyID", "requestID", ""),
    ("oversizedID", "requestID", "x" * 129),
    ("unknownCommand", "command", "unsafe.execute"),
    ("credentialField", "credential", "fixture-not-a-token"),
    ("unknownParams", "params", {"client": "fixture", "clientVersion": "1", "token": "fixture"}),
]:
    frame = copy.deepcopy(positive[0])
    frame[key] = value
    negative.append({"name": name, "frame": frame})
bad_job = copy.deepcopy(event)
bad_job["sequence"] = -1
negative.append({"name": "negativeSequence", "frame": bad_job})
bad_result = copy.deepcopy(positive[len(commands)])
bad_result["error"] = {"code": "INTERNAL_ERROR", "message": "fixture", "retryable": False}
negative.append({"name": "bothSuccessAndFailure", "frame": bad_result})
bad_flow = copy.deepcopy(positive[-len(defs["error"]["properties"]["code"]["enum"]) - 1])
bad_flow["data"]["flow"]["token"] = "fixture-not-a-token"
negative.append({"name": "secretFieldInFlow", "frame": bad_flow})
bad_discovery = copy.deepcopy(next(frame for frame in positive if frame.get("command") == "catalog.discover"))
bad_discovery["params"]["limit"] = 17
negative.append({"name": "discoveryPageLimit", "frame": bad_discovery})
positive.append({"kind":"result","protocol":protocol,"requestID":"fixture-discovery-all-failed",
    "ok":False,"error":{"code":"PACKAGE_UNAVAILABLE","message":"No metadata resolved.",
    "retryable":True,"details":{**discovery,"products":[]}}})
positive.append({"kind":"result","protocol":protocol,"requestID":"fixture-query-all-failed",
    "ok":False,"error":{"code":"PACKAGE_UNAVAILABLE","message":"No PC metadata resolved.",
    "retryable":True,"details":{**query_page,"products":[]}}})
for name, key, value in [("queryPageLimit", "limit", 17), ("queryEmpty", "query", ""),
                         ("queryUntrustedCursor", "cursor", "https://attacker.invalid/")]:
    frame = copy.deepcopy(next(frame for frame in positive if frame.get("command") == "catalog.query"))
    frame["params"][key] = value
    negative.append({"name": name, "frame": frame})
console_result = copy.deepcopy(next(frame for frame in positive
    if frame.get("ok") and frame.get("data", {}).get("corpus") == "publicMicrosoftStoreSearch"
    and frame["data"]["products"]))
console_result["data"]["products"][0]["pcCatalogCandidate"] = False
negative.append({"name": "consoleOnlyQueryProduct", "frame": console_result})
for code, message in [("NETWORK_UNAVAILABLE", "Public query exceeded its bounded deadline."),
                      ("CANCELLED", "Transport closed before public query completed.")]:
    positive.append({"kind":"result","protocol":protocol,"requestID":"fixture-query-"+code.lower(),
        "ok":False,"error":{"code":code,"message":message,"retryable":True}})
write(fixture_root / "positive.json", positive)
write(fixture_root / "negative.json", negative)
write(fixture_root / "evidence-edge.json", [
    {**evidence, "entitlement": {"kind": "purchase", "source": "fixture", "checkedAt": timestamp},
     "installability": {"kind": "blocked", "reason": "Purchased but no available package."}},
    {**evidence, "entitlement": {"kind": "subscription", "source": "fixture", "checkedAt": timestamp,
                                "expiresAt": "2026-01-01T00:00:00Z"}},
    {**evidence, "installation": {"kind": "installed", "installationID": "fixture-revoked"},
     "entitlement": {"kind": "none", "source": "fixture", "checkedAt": timestamp}},
    {**evidence, "compatibility": {"kind": "experimental", "source": "fixture", "checkedAt": timestamp,
                                  "os": "fixture-os", "architecture": "arm64",
                                  "runtimeFingerprint": "fixture-runtime"}},
])
(fixture_root / "positive.jsonl").write_bytes(
    "".join(json.dumps(frame, separators=(",", ":")) + "\n" for frame in positive).encode("utf-8"))
