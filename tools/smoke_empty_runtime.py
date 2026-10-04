"""Developer-only empty-memory fixture: production route, never production broker/account."""
import argparse
import hashlib
import os
import signal
import socket
import struct
import subprocess
import tempfile
import time
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--fixture-binary", type=Path, required=True)
parser.add_argument("--root", type=Path, required=True)
args = parser.parse_args()
binary = args.fixture_binary.resolve()
assert binary.is_file() and args.root.is_dir()
banner = b"Developer fixture: empty in-memory account; no native credential access"


def frame(kind, payload):
    return struct.pack("<IHH", 0x58445358, kind, len(payload)) + payload


def ping(endpoint, payload):
    with socket.socket(socket.AF_UNIX) as peer:
        peer.settimeout(3)
        peer.connect(str(endpoint))
        peer.sendall(frame(1, payload))
        expected = frame(2, payload)
        actual = bytearray()
        while len(actual) < len(expected):
            part = peer.recv(len(expected) - len(actual))
            assert part, "Empty-profile fixture closed during ping"
            actual.extend(part)
        assert actual == expected


with tempfile.TemporaryDirectory(prefix="empty-rps-", dir=args.root) as temporary:
    root = Path(temporary).resolve()
    assert root.stat().st_mode & 0o777 == 0o700
    endpoint = root / "peer.sock"
    assert len(os.fsencode(endpoint)) <= 103
    env = dict(os.environ, XODUS_LOG="trace", RUST_LOG="trace")
    # First probe contains the lab gate, so the production executable would refuse
    # before initialization. Never try no-argument fallback on an unidentified binary.
    for arguments in [
        ["--empty-memory-fixture", "--management-socket"],
        [],
        ["--management-socket", str(endpoint)],
        ["--fixture-socket", str(endpoint)],
    ]:
        rejected = subprocess.run(
            [str(binary), *arguments], cwd=root, env=env,
            capture_output=True, timeout=5)
        assert rejected.returncode == 1 and rejected.stdout == b""
        assert rejected.stderr == (
            b"Expected --empty-memory-fixture --management-socket "
            b"with one absolute private Unix path\n"
        )
        assert not endpoint.exists() and list(root.iterdir()) == []
    process = subprocess.Popen(
        [str(binary), "--empty-memory-fixture", "--management-socket", str(endpoint)], cwd=root,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
    try:
        deadline = time.monotonic() + 5
        while not endpoint.exists() and process.poll() is None and time.monotonic() < deadline:
            time.sleep(0.02)
        assert endpoint.exists() and process.poll() is None, "Empty-profile fixture was not ready"
        assert endpoint.stat().st_mode & 0o777 == 0o600
        ping(endpoint, b"before-empty-account-refusal")
        request = (
            b"<MSATokenRequest><ClientId>0011223344556677</ClientId>"
            b"<AllowUi>false</AllowUi><MsaFullTrust>true</MsaFullTrust></MSATokenRequest>"
        )
        with socket.socket(socket.AF_UNIX) as peer:
            peer.settimeout(3)
            peer.connect(str(endpoint))
            peer.sendall(frame(3, request))
            assert peer.recv(1) == b"", "Empty account received a fabricated response frame"
        assert process.poll() is None, "Refusal terminated the fixture"
        ping(endpoint, b"after-empty-account-refusal")
        with socket.socket(socket.AF_UNIX) as blocked:
            blocked.settimeout(5)
            blocked.connect(str(endpoint))
            blocked.sendall(frame(1, b"pending")[:2])
            os.kill(process.pid, signal.SIGINT)
            assert process.wait(timeout=5) == 0
            assert blocked.recv(1) == b"", "SIGINT did not cancel the pending header"
        assert not endpoint.exists(), "Owned fixture socket was not cleaned up"
        assert list(root.iterdir()) == [], "Fixture wrote persistent state"
        assert process.stdout.read() == b"", "Fixture emitted public stdout"
        assert process.stderr.read().splitlines() == [
            banner, b"Private runtime peer request failed"
        ]
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=5)

print({
    "emptyMemoryFixture": "passed",
    "productionFramingAndManagementRoute": "used",
    "validMsaRequest": "closedWithoutReply",
    "wireAuthenticationCategory": "notExposed",
    "pingBeforeAndAfter": "passed",
    "blockedHeaderSigintAndOwnedCleanup": "passed",
    "inMemoryAbsenceLookup": "performed",
    "nativeKeychainOrUserCredentials": "notAccessedByConstruction",
    "promptMutationDeviceOrAccountHttp": "notPerformedByConstruction",
    "ticketsOrRuntimeGame": "notIssuedOrExecuted",
    "binarySHA256": hashlib.sha256(binary.read_bytes()).hexdigest(),
})
