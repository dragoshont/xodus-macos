"""Bounded native process/public-catalog smoke check; never signs in or launches games."""
import argparse
import hashlib
import json
import os
import selectors
import subprocess
import tempfile
import time
from pathlib import Path

import jsonschema

parser = argparse.ArgumentParser()
parser.add_argument("--binary", type=Path, required=True)
parser.add_argument("--root", type=Path, required=True)
parser.add_argument("--product", default="9NBLGGH2JHXJ")
args = parser.parse_args()
schema = json.loads((Path(__file__).resolve().parents[1] /
                     "docs/contracts/management-v1.schema.json").read_text())
validator = jsonschema.Draft202012Validator(schema, format_checker=jsonschema.FormatChecker())


class Client:
    def __init__(self, state):
        environment = dict(os.environ, XODUS_LOG="trace", RUST_LOG="trace")
        self.process = subprocess.Popen(
            [str(args.binary), "manage", "--protocol", "1", "--state-dir", str(state)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=environment)
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.process.stdout, selectors.EVENT_READ)
        self.buffer = bytearray()
        self.events = []
        self.results = set()

    def send(self, identifier, command, params):
        request = {"kind": "request", "protocol": {"major": 1, "minor": 0},
                   "requestID": identifier, "command": command, "params": params}
        self.process.stdin.write(json.dumps(request, separators=(",", ":")).encode() + b"\n")
        self.process.stdin.flush()

    def frame(self, timeout=40):
        deadline = time.monotonic() + timeout
        while b"\n" not in self.buffer:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not self.selector.select(remaining):
                raise AssertionError("Timed out waiting for a management frame")
            data = os.read(self.process.stdout.fileno(), 65536)
            if not data:
                raise AssertionError("Backend closed before a terminal frame")
            self.buffer.extend(data)
            assert len(self.buffer.split(b"\n", 1)[0]) <= 1024 * 1024
        line, _, remainder = self.buffer.partition(b"\n")
        self.buffer = bytearray(remainder)
        frame = json.loads(line)
        validator.validate(frame)
        if frame["kind"] == "result":
            assert frame["requestID"] not in self.results, "Duplicate terminal result"
            self.results.add(frame["requestID"])
        else:
            self.events.append(frame)
        return frame

    def result(self, identifier):
        while True:
            frame = self.frame()
            if frame["kind"] == "result":
                assert frame["requestID"] == identifier, "Uncorrelated result"
                return frame

    def close(self):
        self.process.stdin.close()
        try:
            assert self.process.wait(timeout=10) == 0
            assert not self.process.stdout.read(), "Unexpected trailing stdout"
            assert not self.process.stderr.read(), "Unexpected diagnostic output"
        finally:
            if self.process.poll() is None:
                self.process.kill()
                self.process.wait(timeout=10)
            self.selector.close()

    def abort(self):
        if self.process.poll() is None:
            self.process.kill()
            self.process.wait(timeout=10)
        self.selector.close()


assert args.binary.is_file() and args.root.is_dir()
with tempfile.TemporaryDirectory(prefix="management-public-smoke-", dir=args.root) as temporary:
    state = Path(temporary).resolve()
    client = Client(state)
    try:
        client.send("hello", "hello", {"client": "public-smoke", "clientVersion": "1"})
        hello = client.result("hello")
        assert hello["ok"] and hello["data"]["runtimeFingerprint"] is None
        session = hello["data"]["sessionID"]
        capabilities = {entry["command"]: entry for entry in hello["data"]["capabilities"]}
        for command in ("inventory.snapshot", "install.plan", "game.launch", "game.update",
                        "game.rollback", "game.remove", "jobs.pause", "jobs.resume"):
            assert not capabilities[command]["supported"]
        product_params = {"productID": args.product, "market": "US",
                          "language": "en-US", "refresh": "network"}
        client.send("detail", "product.detail", product_params)
        detail = client.result("detail")
        assert detail["ok"], f"Public catalog detail blocked: {detail['error']['code']}"
        record = detail["data"]["product"]
        assert record["productID"] == args.product and record["freshness"] == "live"
        assert record["editions"]
        for edition in record["editions"]:
            assert edition["entitlement"]["kind"] == "unknown"
            assert edition["installability"]["kind"] in ("unknown", "blocked")
            assert edition["compatibility"]["kind"] == "unknown"
        client.send("enqueue", "jobs.enqueue", {
            "kind": "catalogRefresh", "idempotencyKey": "public-smoke-key", "product": product_params})
        enqueued = client.result("enqueue")
        assert enqueued["ok"]
        job_id = enqueued["data"]["job"]["jobID"]
        while True:
            event = client.frame()
            assert event["kind"] == "event"
            if event["jobID"] == job_id and event["data"]["state"] in ("completed", "failed"):
                assert event["data"]["state"] == "completed", "Live refresh failed"
                break
        client.send("snapshot", "jobs.snapshot", {})
        snapshot = client.result("snapshot")
        assert snapshot["data"]["jobs"][0]["state"] == "completed"
        client.send("again", "jobs.enqueue", {
            "kind": "catalogRefresh", "idempotencyKey": "public-smoke-key", "product": product_params})
        assert client.result("again")["data"]["job"]["jobID"] == job_id
        client.send("replay", "events.replay", {"sessionID": session, "afterSequence": 0, "limit": 1000})
        replay = client.result("replay")
        assert [event["sequence"] for event in replay["data"]["events"]] == [1, 2, 3]
        client.send("search", "catalog.search", {"query": "", "market": "US", "language": "en-US",
                                               "platform": "pc", "limit": 100, "cursor": None})
        search = client.result("search")
        assert search["data"]["completeness"] == "partial"
        assert search["data"]["corpus"] == "observedPublicProducts"
        client.send("inventory", "inventory.snapshot", {
            "accountScope": "default", "market": "US", "refresh": "network"})
        assert client.result("inventory")["error"]["code"] == "ACCESS_UNKNOWN"
        client.send("launch", "game.launch", {"installationID": "not-installed", "expectedRevision": 0})
        assert client.result("launch")["error"]["code"] == "RUNTIME_MISMATCH"
        client.send("installed", "installed.snapshot", {})
        installed = client.result("installed")
        assert installed["data"]["scope"] == "managementRegistryOnly"
        assert installed["data"]["installations"] == []
        client.send("diagnostics", "diagnostics.export", {})
        report = client.result("diagnostics")["data"]
        assert report["redacted"] and not report["runtimeCertified"] and not report["inventoryAuthorized"]
        client.close()
    except BaseException:
        client.abort()
        raise
    reconnected = Client(state)
    try:
        reconnected.send("reconnect", "hello", {"client": "public-smoke", "clientVersion": "1"})
        assert reconnected.result("reconnect")["data"]["sessionID"] == session
        reconnected.send("durable", "jobs.snapshot", {})
        assert reconnected.result("durable")["data"]["jobs"][0]["jobID"] == job_id
        reconnected.close()
    except BaseException:
        reconnected.abort()
        raise
print(json.dumps({"nativeProcess": "passed", "publicCatalogDetail": "passed",
                  "publicRefreshJobReplayReconnect": "passed", "ownedInventory": "notQueried",
                  "accountConsent": "notExecuted", "gameLaunch": "gatedNotExecuted",
                  "schemaFrames": "validated", "binarySHA256": hashlib.sha256(args.binary.read_bytes()).hexdigest()}))
