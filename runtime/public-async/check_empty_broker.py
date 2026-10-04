# SPDX-License-Identifier: GPL-3.0-only
"""Failure-only real Rust fixture/public gaming-COM integration; no native credential backend."""

import argparse
import functools
import hashlib
from pathlib import Path
import signal
import struct
import subprocess
import sys
import tempfile
import time

import check_windows_async as components

runner = components.runner
FIXTURE_SHA256 = "9e854df1e44042c09067fc2d63ff6e4beeddeb73350c0329059cda28f2d13fb1"
BANNER = b"Developer fixture: empty in-memory account; no native credential access"


def verify_fixture(root, supplied):
    path = supplied.absolute()
    if any(item.is_symlink() for item in (path, *path.parents)):
        raise RuntimeError("Select a non-aliased empty-memory fixture.")
    path = path.resolve(strict=True)
    components.verify_component(root, path, path.name)
    if hashlib.sha256(path.read_bytes()).hexdigest() != FIXTURE_SHA256:
        raise RuntimeError("The selected binary is not the pinned developer-only empty-memory fixture.")
    return path


def ping(process, endpoint, payload):
    deadline = time.monotonic() + 5
    while True:
        if process.poll() is not None:
            raise RuntimeError("The recorded empty-memory fixture exited before ping.")
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise RuntimeError("The recorded empty-memory fixture did not become ready.")
        peer, pid = runner.connected_server(endpoint, min(remaining, 0.25))
        if peer is not None:
            break
        time.sleep(min(remaining, 0.05))
    with peer:
        if pid != process.pid or process.poll() is not None:
            raise RuntimeError("The private fixture socket belongs to another process.")
        peer.settimeout(3)
        peer.sendall(struct.pack("<IHH", 0x58445358, 1, len(payload)) + payload)
        header = runner.receive_exact(peer, 8)
        if header != struct.pack("<IHH", 0x58445358, 2, len(payload)):
            raise RuntimeError("The empty-memory fixture did not return the exact ping header.")
        if runner.receive_exact(peer, len(payload)) != payload:
            raise RuntimeError("The empty-memory fixture did not echo the exact ping payload.")


def stop_fixture(process):
    if process.poll() is None:
        process.send_signal(signal.SIGINT)
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)
            raise RuntimeError("The owned fixture required forced termination, not graceful reconciliation.")
    if process.returncode != 0:
        raise RuntimeError("The owned fixture did not exit successfully.")


def check_broker(directory, environment, executables, controller, controller_socket, *, fixture):
    for name in ("xgameruntime.dll", "xodus_async.dll", "MicrosoftGame.config"):
        components.verify_component(directory.parent, executables[2], name)
    verify_fixture(directory.parent, fixture)
    # Independent of the Wine prefix: uncertain native cleanup must retain its own private directory.
    private = Path(tempfile.mkdtemp(prefix="b-", dir=directory.parent))
    sockets = private / "s"
    sockets.mkdir(mode=0o700)
    endpoint = sockets / "peer.sock"
    stdout, stderr = private / "stdout", private / "stderr"
    process = None
    try:
        with stdout.open("wb") as out, stderr.open("wb") as err:
            process = subprocess.Popen(
                [str(fixture), "--empty-memory-fixture", "--management-socket", str(endpoint)], cwd=private,
                env=environment, stdout=out, stderr=err,
            )
            try:
                ping(process, endpoint, b"before-public-consumer")
                runner.wait_server_ready(controller, controller_socket)
                child = dict(environment, XODUS_RUNTIME_SOCKET=str(endpoint))
                output = runner.run_owned(
                    [str(executables[0]), str(executables[2]), "rps-peer-closed"], child, 20,
                )
                if "Isolated Windows async rps-peer-closed outcome passed." not in output:
                    raise RuntimeError("The real public gaming peer-closure outcome did not complete.")
                runner.wait_server_ready(controller, controller_socket)
                ping(process, endpoint, b"after-public-consumer")
            finally:
                stop_fixture(process)
        if (list(sockets.iterdir()) or
                {item.name for item in private.iterdir()} != {"s", "stdout", "stderr"}):
            raise RuntimeError("The fixture did not remove only its owned socket without persistent state.")
        if stdout.stat().st_size or stderr.stat().st_size > 65536:
            raise RuntimeError("The fixture produced unexpected or unbounded output.")
        lines = stderr.read_bytes().splitlines()
        if lines != [BANNER, b"Private runtime peer request failed"]:
            raise RuntimeError("The fixture did not identify its empty-memory failure path.")
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        raise RuntimeError(f"Empty-memory fixture check failed; private evidence retained: {private}") from error
    stdout.unlink()
    stderr.unlink()
    sockets.rmdir()
    private.rmdir()
    print(output, end="" if output.endswith("\n") else "\n")
    for line in lines:
        print(line.decode("ascii"))
    return ("Real Rust empty-memory fixture and public gaming COM refused the user add with E_FAIL, "
            "one completion and no handle; ping before/after and graceful fixture cleanup passed. "
            "Peer closure has no wire authentication category. No native credentials, successful RPS, "
            "account authorization, production runtime pair or licensed gameplay.")


def main():
    parser = argparse.ArgumentParser(description=__doc__, add_help=False)
    parser.add_argument("--broker-fixture", type=Path, required=True)
    args, remaining = parser.parse_known_args()
    if not remaining:
        parser.error("Supply the owned root and the shared runner's selected binaries.")
    root = Path(remaining[0]).absolute()
    if any(path.is_symlink() for path in (root, *root.parents)):
        parser.error("Select a non-aliased owned root.")
    fixture = verify_fixture(root.resolve(strict=True), args.broker_fixture)
    arguments = sys.argv
    try:
        sys.argv = [arguments[0], *remaining]
        runner.main(checks=functools.partial(check_broker, fixture=fixture))
    finally:
        sys.argv = arguments


if __name__ == "__main__":
    main()
