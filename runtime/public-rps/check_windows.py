# SPDX-License-Identifier: GPL-3.0-only
"""Exercise a selected public Wine candidate against synthetic Unix peers only."""

import argparse
from contextlib import closing
import ctypes
import errno
from pathlib import Path
import os
import select
import shutil
import signal
import socket
import stat
import struct
import subprocess
import sys
import tempfile
import threading
import time
import xml.etree.ElementTree as ET


MAGIC = 0x58445358
DARWIN_NOTE_EXITSTATUS = 0x04000000  # sys/event.h; older Python exposes only NOTE_EXIT.


def server_endpoint(prefix):
    info = prefix.stat()
    return Path(f"/tmp/.wine-{os.getuid()}/server-{info.st_dev:x}-{info.st_ino:x}/socket")


def connected_server(endpoint, timeout):
    try:
        for directory in (endpoint.parent.parent, endpoint.parent):
            info = directory.lstat()
            if (not stat.S_ISDIR(info.st_mode) or info.st_uid != os.getuid()
                    or info.st_mode & 0o077):
                raise RuntimeError("The prefix's Wine server directory is not private and owned.")
        info = endpoint.lstat()
        if (not stat.S_ISSOCK(info.st_mode) or info.st_uid != os.getuid()
                or info.st_mode & 0o077):
            raise RuntimeError("The prefix's Wine server socket is not private and owned.")
        connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            connection.settimeout(timeout)
            connection.connect(str(endpoint))
            # Darwin SOL_LOCAL / LOCAL_PEERPID identifies the listening process.
            pid = struct.unpack("=i", connection.getsockopt(0, 2, 4))[0]
            if pid <= 0:
                raise RuntimeError("The Wine server socket did not identify a peer PID.")
            return connection, pid
        except BaseException:
            connection.close()
            raise
    except OSError as error:
        if error.errno in (errno.ENOENT, errno.ECONNREFUSED):
            return None, None
        raise


def wait_server_ready(server, endpoint, timeout=10):
    deadline = time.monotonic() + timeout
    while True:
        if server.poll() is not None:
            raise RuntimeError("The recorded Wine server exited before socket readiness.")
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise RuntimeError("The recorded Wine server did not become ready in time.")
        connection, pid = connected_server(endpoint, min(remaining, 0.25))
        if connection is not None:
            connection.close()
            if pid != server.pid or server.poll() is not None:
                raise RuntimeError("The prefix's Wine socket is not owned by the recorded server.")
            return
        time.sleep(min(remaining, 0.05))


def process_executable(pid):
    library = ctypes.CDLL("/usr/lib/libSystem.B.dylib", use_errno=True)
    function = library.proc_pidpath
    function.argtypes = (ctypes.c_int, ctypes.c_void_p, ctypes.c_uint32)
    function.restype = ctypes.c_int
    buffer = ctypes.create_string_buffer(4096)
    if function(pid, buffer, len(buffer)) <= 0:
        raise OSError(ctypes.get_errno(), "Cannot identify the prefix's server executable.")
    return Path(os.fsdecode(buffer.value)).resolve(strict=True)


def reconcile_server(server, endpoint, executable, *, clients_started, timeout=5):
    if server.poll() is not None:
        if clients_started:
            raise RuntimeError("Server exited unexpectedly after client launch; private prefix retained.")
    else:
        os.kill(server.pid, signal.SIGINT)
        try:
            status = server.wait(timeout=timeout)
        except subprocess.TimeoutExpired as error:
            raise RuntimeError("Recorded server shutdown timed out; private prefix retained.") from error
        if clients_started and status != 0:
            raise RuntimeError("Wine did not confirm graceful client shutdown; private prefix retained.")
    deadline = time.monotonic() + timeout
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise RuntimeError("Owned server reconciliation timed out; private prefix retained.")
        connection, pid = connected_server(endpoint, min(remaining, 0.25))
        if connection is None:
            return
        try:
            with closing(select.kqueue()) as queue:
                event = select.kevent(pid, filter=select.KQ_FILTER_PROC,
                                      flags=select.KQ_EV_ADD | select.KQ_EV_ONESHOT,
                                      fflags=select.KQ_NOTE_EXIT | DARWIN_NOTE_EXITSTATUS)
                queue.control([event], 0, 0)
                if process_executable(pid) != executable:
                    raise RuntimeError("Unrecognized socket owner; private prefix retained.")
                # Wine SIGINT shuts down its clients; SIGTERM only exits the server.
                os.kill(pid, signal.SIGINT)
                events = queue.control(None, 1, max(0, deadline - time.monotonic()))
                if not events:
                    raise RuntimeError("Replacement server did not exit; private prefix retained.")
                if events[0].data != 0:
                    raise RuntimeError("Replacement shutdown was not graceful; private prefix retained.")
        finally:
            connection.close()


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
    if sys.platform != "darwin":
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
    temporary = tempfile.mkdtemp(prefix="w-", dir=root)
    server = None
    clients_started = False
    endpoint_to_reconcile = None
    try:
        directory = Path(temporary)
        home, prefix = directory / "home", directory / "prefix"
        home.mkdir(mode=0o700)
        prefix.mkdir(mode=0o700)
        endpoint = directory / "r.sock"
        if len(os.fsencode(endpoint)) > 103 or not str(endpoint).isascii():
            raise RuntimeError("Use a short ASCII owned output root for the Wine socket check.")
        endpoint_to_reconcile = server_endpoint(prefix)
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
            wait_server_ready(server, endpoint_to_reconcile)
            clients_started = True
            output = run_owned([str(executables[0]), str(executables[2]), "--bootstrap"],
                               environment, 90)
            if "Isolated Windows check started; no credentials requested." not in output:
                raise RuntimeError("The selected Windows harness did not reach its entry point.")
            wait_server_ready(server, endpoint_to_reconcile)
            for mode in ("success", "malformed", "expired", "timeout"):
                wait_server_ready(server, endpoint_to_reconcile)
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
                wait_server_ready(server, endpoint_to_reconcile)
                print(f"Windows/Wine synthetic {mode}: passed")
    finally:
        if server is not None:
            try:
                reconcile_server(server, endpoint_to_reconcile, executables[1],
                                 clients_started=clients_started)
            except (OSError, RuntimeError) as error:
                raise RuntimeError(
                    f"Server cleanup could not establish ownership; private directory retained: "
                    f"{temporary}"
                ) from error
        shutil.rmtree(temporary)
    print("Four actual Windows-branch checks passed; no real credentials or game were used.")


if __name__ == "__main__":
    main()
