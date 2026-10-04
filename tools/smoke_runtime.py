"""Owned private broker/framing smoke; never requests credentials or starts a game."""
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
parser.add_argument("--binary", type=Path, required=True)
parser.add_argument("--root", type=Path, required=True)
args = parser.parse_args()
assert args.binary.is_file() and args.root.is_dir()


def read_exact(peer, count):
    data = bytearray()
    while len(data) < count:
        part = peer.recv(count - len(data))
        assert part, "Private broker closed before a full frame"
        data.extend(part)
    return bytes(data)


def frame(kind, payload):
    return struct.pack("<IHH", 0x58445358, kind, len(payload)) + payload


with tempfile.TemporaryDirectory(prefix="rps-", dir=args.root) as temporary:
    root = Path(temporary).resolve()
    os.chmod(root, 0o700)
    endpoint = root / "peer.sock"
    assert len(os.fsencode(endpoint)) <= 103
    process = subprocess.Popen(
        [str(args.binary), "--management-socket", str(endpoint)],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        env=dict(os.environ, XODUS_LOG="trace", RUST_LOG="trace"))
    try:
        deadline = time.monotonic() + 5
        while not endpoint.exists() and process.poll() is None and time.monotonic() < deadline:
            time.sleep(0.02)
        assert endpoint.exists() and process.poll() is None, "Private broker was not ready"
        assert endpoint.stat().st_mode & 0o777 == 0o600
        with socket.socket(socket.AF_UNIX) as peer:
            peer.settimeout(3)
            peer.connect(str(endpoint))
            payload = b"isolated-native-probe"
            peer.sendall(frame(1, payload))
            assert read_exact(peer, 8 + len(payload)) == frame(2, payload)
            peer.sendall(frame(1, b""))
            assert read_exact(peer, 8) == frame(2, b"")
        # Invalid request syntax is rejected before any credential getter can run.
        invalid_requests = [
            b"<MSATokenRequest><ClientId>invalid</ClientId></MSATokenRequest>",
            b"<MSATokenRequest><ClientId>000000004424da1f</ClientId><Scope>other</Scope></MSATokenRequest>",
            b"<Other><ClientId>000000004424da1f</ClientId></Other>",
            b"<MSATokenRequest><ClientId><Nested>000000004424da1f</Nested></ClientId></MSATokenRequest>",
        ]
        for payload in invalid_requests:
            with socket.socket(socket.AF_UNIX) as peer:
                peer.settimeout(3)
                peer.connect(str(endpoint))
                peer.sendall(frame(3, payload))
                assert peer.recv(1) == b"", "Invalid MSA request got a success-shaped reply"
        with socket.socket(socket.AF_UNIX) as peer:
            peer.settimeout(3)
            peer.connect(str(endpoint))
            payload = b"usable-after-refusal"
            peer.sendall(frame(1, payload))
            assert read_exact(peer, 8 + len(payload)) == frame(2, payload)
        os.kill(process.pid, signal.SIGINT)
        assert process.wait(timeout=5) == 0
        assert not endpoint.exists(), "Owned private socket was not cleaned up"
        assert process.stdout.read() == b"", "Private broker emitted public stdout"
        lines = process.stderr.read().splitlines()
        assert lines == [b"Private runtime peer request failed"] * len(invalid_requests), lines
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=5)
print({"privateNativeBroker": "passed", "pingAndFraming": "passed",
       "malformedBeforeCredentialRead": "passed", "accountTickets": "notRequested",
       "runtimeOrGameplay": "notExecuted",
       "binarySHA256": hashlib.sha256(args.binary.read_bytes()).hexdigest()})
