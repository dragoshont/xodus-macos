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


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


parser = argparse.ArgumentParser()
parser.add_argument("--fixture-binary", type=Path, required=True)
parser.add_argument("--root", type=Path, required=True)
args = parser.parse_args()
binary = args.fixture_binary.resolve()
require(binary.is_file() and args.root.is_dir(), "Fixture binary and owned root must exist")
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
            require(part, "Empty-profile fixture closed during ping")
            actual.extend(part)
        require(actual == expected, "Fixture ping response did not match the request")


with tempfile.TemporaryDirectory(prefix="empty-rps-", dir=args.root) as temporary:
    root = Path(temporary).resolve()
    require(root.stat().st_mode & 0o777 == 0o700, "Owned fixture directory is not private")
    endpoint = root / "peer.sock"
    require(len(os.fsencode(endpoint)) <= 103, "Owned fixture socket path exceeds its limit")
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
        require(
            rejected.returncode == 1 and rejected.stdout == b"",
            "Fixture refusal had an unexpected exit status or stdout",
        )
        require(
            rejected.stderr == (
                b"Expected --empty-memory-fixture --management-socket "
                b"with one absolute private Unix path\n"
            ),
            "Executable did not return the distinctive empty-memory fixture refusal",
        )
        require(
            not endpoint.exists() and list(root.iterdir()) == [],
            "Rejected fixture invocation created persistent state",
        )
    process = subprocess.Popen(
        [str(binary), "--empty-memory-fixture", "--management-socket", str(endpoint)], cwd=root,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
    try:
        deadline = time.monotonic() + 5
        while not endpoint.exists() and process.poll() is None and time.monotonic() < deadline:
            time.sleep(0.02)
        require(
            endpoint.exists() and process.poll() is None, "Empty-profile fixture was not ready"
        )
        require(endpoint.stat().st_mode & 0o777 == 0o600, "Owned fixture socket is not private")
        ping(endpoint, b"before-empty-account-refusal")
        request = (
            b"<MSATokenRequest><ClientId>0011223344556677</ClientId>"
            b"<AllowUi>false</AllowUi><MsaFullTrust>true</MsaFullTrust></MSATokenRequest>"
        )
        with socket.socket(socket.AF_UNIX) as peer:
            peer.settimeout(3)
            peer.connect(str(endpoint))
            peer.sendall(frame(3, request))
            require(peer.recv(1) == b"", "Empty account received a fabricated response frame")
        require(process.poll() is None, "Refusal terminated the fixture")
        ping(endpoint, b"after-empty-account-refusal")
        with socket.socket(socket.AF_UNIX) as blocked:
            blocked.settimeout(5)
            blocked.connect(str(endpoint))
            blocked.sendall(frame(1, b"pending")[:2])
            os.kill(process.pid, signal.SIGINT)
            require(process.wait(timeout=5) == 0, "Fixture did not shut down successfully")
            require(blocked.recv(1) == b"", "SIGINT did not cancel the pending header")
        require(not endpoint.exists(), "Owned fixture socket was not cleaned up")
        require(list(root.iterdir()) == [], "Fixture wrote persistent state")
        require(process.stdout.read() == b"", "Fixture emitted public stdout")
        require(
            process.stderr.read().splitlines() == [
                banner, b"Private runtime peer request failed"
            ],
            "Fixture emitted unexpected stderr",
        )
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
