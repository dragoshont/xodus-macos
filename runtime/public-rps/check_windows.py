# SPDX-License-Identifier: GPL-3.0-only
"""Exercise a selected public Wine candidate against synthetic Unix peers only."""

import argparse
from pathlib import Path
import os
import socket
import struct
import subprocess
import tempfile
import threading
import time
import xml.etree.ElementTree as ET


MAGIC = 0x58445358


def stop_owned_process(process):
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def run_owned(arguments, environment, timeout):
    with tempfile.TemporaryFile(dir=environment["TMPDIR"]) as log:
        process = subprocess.Popen(arguments, env=environment, start_new_session=True,
                                   stdout=log, stderr=subprocess.STDOUT)
        try:
            process.wait(timeout=timeout)
        except subprocess.TimeoutExpired as error:
            log.seek(0)
            raise RuntimeError("Owned Windows check timed out: "
                               + log.read(4000).decode("utf-8", errors="replace")) from error
        finally:
            stop_owned_process(process)
        log.seek(0)
        output = log.read(4000).decode("utf-8", errors="replace")
    if process.returncode:
        raise RuntimeError(f"Owned Windows check failed (exit {process.returncode}): " + output)
    return output


def receive_exact(connection, size):
    result = bytearray()
    while len(result) < size:
        part = connection.recv(size - len(result))
        if not part:
            raise RuntimeError("The synthetic peer received a truncated request.")
        result.extend(part)
    return bytes(result)


def peer(listener, mode, stop, failures):
    try:
        with listener.accept()[0] as connection:
            connection.settimeout(5)
            magic, request_type, size = struct.unpack("<IHH", receive_exact(connection, 8))
            if magic != MAGIC or request_type != 3 or size > 4096:
                raise RuntimeError("Unexpected synthetic request framing.")
            request = ET.fromstring(receive_exact(connection, size))
            fields = {child.tag: child.text for child in request}
            if (request.tag != "MSATokenRequest" or len(request) != 3
                    or fields != {"ClientId": "0011223344556677",
                                  "AllowUi": "false", "MsaFullTrust": "true"}):
                raise RuntimeError("Unexpected synthetic request shape.")
            if mode == "timeout":
                stop.wait(3)
                return
            expiry = int(time.time()) + (3600 if mode != "expired" else -3600)
            response = (
                "<MSATokenResponse><Token>syntheticUser</Token>"
                f"<Expiry>{expiry}</Expiry><DeviceRps>syntheticDevice</DeviceRps>"
                f"<DeviceExpiry>{expiry}</DeviceExpiry></MSATokenResponse>"
            ).encode("ascii")
            if mode == "malformed":
                response = b"<MSATokenResponse><Token>syntheticUser</Token></MSATokenResponse>"
            frame = struct.pack("<IHH", MAGIC, 4, len(response)) + response
            for offset in range(0, len(frame), 7):
                connection.sendall(frame[offset:offset + 7])
    except (OSError, ValueError, ET.ParseError, RuntimeError) as error:
        failures.append(error)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("owned_root", type=Path)
    parser.add_argument("loader", type=Path)
    parser.add_argument("server", type=Path)
    parser.add_argument("executable", type=Path)
    args = parser.parse_args()
    if os.name != "posix":
        parser.error("Run on the selected macOS host.")
    root = args.owned_root.absolute()
    if any(path.is_symlink() for path in (root, *root.parents)) or not root.is_dir():
        parser.error("Select an existing non-aliased owned output directory.")
    root = root.resolve(strict=True)
    executables = []
    for supplied in (args.loader, args.server, args.executable):
        path = supplied.absolute()
        if any(part.is_symlink() for part in (path, *path.parents)) or not path.is_file():
            parser.error("Each selected executable must be a non-aliased regular file.")
        path = path.resolve(strict=True)
        if root not in path.parents:
            parser.error("All selected binaries must be inside the owned output root.")
        executables.append(path)
    if root.stat().st_uid != os.getuid() or root.stat().st_mode & 0o022:
        parser.error("The selected root must be owned and not group/world writable.")
    with tempfile.TemporaryDirectory(prefix="w-", dir=root) as temporary:
        directory = Path(temporary)
        home, prefix = directory / "home", directory / "prefix"
        home.mkdir(mode=0o700)
        prefix.mkdir(mode=0o700)
        endpoint = directory / "r.sock"
        if len(os.fsencode(endpoint)) > 103 or not str(endpoint).isascii():
            parser.error("Use a short ASCII owned output root for the Wine socket check.")
        environment = {
            "HOME": str(home),
            "PATH": "/usr/bin:/bin:/usr/sbin:/sbin",
            "TMPDIR": str(directory),
            "LANG": "en_US.UTF-8",
            "WINEPREFIX": str(prefix),
            "WINEARCH": "win64",
            "WINELOADER": str(executables[0]),
            "WINESERVER": str(executables[1]),
            "WINEDEBUG": "-all",
            "WINEFSYNC": "0",
            "WINEDLLOVERRIDES": "mscoree,mshtml,winemenubuilder.exe=",
        }
        with (directory / "server.log").open("wb") as log:
            server = subprocess.Popen([str(executables[1]), "-f", "-p"], env=environment,
                                      start_new_session=True, stdout=log, stderr=log)
            try:
                output = run_owned([str(executables[0]), str(executables[2]), "--bootstrap"],
                                   environment, 90)
                if "Isolated Windows check started; no credentials requested." not in output:
                    raise RuntimeError("The selected Windows harness did not reach its entry point.")
                if server.poll() is not None:
                    raise RuntimeError("The explicitly owned Wine server stopped unexpectedly.")
                for mode in ("success", "malformed", "expired", "timeout"):
                    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as listener:
                        listener.settimeout(20)
                        listener.bind(str(endpoint))
                        os.chmod(endpoint, 0o600)
                        listener.listen(1)
                        stop, failures = threading.Event(), []
                        worker = threading.Thread(target=peer,
                                                  args=(listener, mode, stop, failures))
                        worker.start()
                        try:
                            expected = "protocol" if mode in ("malformed", "expired") else mode
                            output = run_owned([str(executables[0]), str(executables[2]),
                                                str(endpoint), expected], environment, 25)
                            if "Isolated Windows RPS outcome passed." not in output:
                                raise RuntimeError("The selected Windows check did not complete.")
                        finally:
                            stop.set()
                            worker.join(timeout=25)
                        if worker.is_alive():
                            raise RuntimeError("The owned synthetic peer did not stop.")
                        if failures:
                            raise RuntimeError("The owned synthetic peer failed.") from failures[0]
                    endpoint.unlink()
                    print(f"Windows/Wine synthetic {mode}: passed")
            finally:
                stop_owned_process(server)
    print("Four actual Windows-branch checks passed; no real credentials or game were used.")


if __name__ == "__main__":
    main()
