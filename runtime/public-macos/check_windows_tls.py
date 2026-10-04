# SPDX-License-Identifier: GPL-3.0-only
"""Check public Wine HTTPS validation against an owned loopback fixture only."""

import errno
import http.server
from pathlib import Path
import socket
import ssl
import subprocess
import sys
import threading

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "public-rps"))
import check_windows as runner


BODY = b"xodus-tls-ok"
CONFIG = """[req]
distinguished_name=subject
x509_extensions=server
prompt=no
[subject]
CN=localhost
[server]
basicConstraints=critical,CA:TRUE
keyUsage=critical,digitalSignature,keyEncipherment,keyCertSign
extendedKeyUsage=serverAuth
subjectAltName=DNS:localhost
"""


def openssl(arguments, environment):
    result = subprocess.run(["/usr/bin/openssl", *map(str, arguments)], env=environment,
                            capture_output=True, timeout=30)
    if result.returncode:
        raise RuntimeError("Owned certificate generation failed: "
                           + result.stderr.decode("utf-8", errors="replace")[:2000])


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path != "/component-check":
            self.server.failures.append("Unexpected owned-peer request path.")
            self.send_error(400)
            return
        self.send_response(200)
        self.send_header("Content-Length", str(len(BODY)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(BODY)

    def log_message(self, *arguments):
        pass


class OwnedServer(http.server.HTTPServer):
    def __init__(self, context):
        self.context = context
        self.attempts = 0
        self.failures = []
        self.condition = threading.Condition()
        self.stopping = threading.Event()
        self.active = None
        super().__init__(("127.0.0.1", 0), Handler)
        self.timeout = 0.1

    def get_request(self):
        connection, address = super().get_request()
        self.attempts += 1
        connection.settimeout(3)
        with self.condition:
            if self.stopping.is_set():
                connection.close()
                raise OSError("The owned HTTPS peer is stopping.")
            try:
                secure = self.context.wrap_socket(connection, server_side=True,
                                                  do_handshake_on_connect=False)
            except (OSError, ssl.SSLError):
                connection.close()
                raise
            self.active = secure
            self.condition.notify_all()
        try:
            secure.do_handshake()
            return secure, address
        except (OSError, ssl.SSLError):
            self.shutdown_request(secure)
            raise

    def handle_error(self, request, address):
        if not self.stopping.is_set():
            self.failures.append("Owned TLS request handler failed.")

    def shutdown_request(self, request):
        with self.condition:
            try:
                super().shutdown_request(request)
            finally:
                if self.active is request:
                    self.active = None
                self.condition.notify_all()

    def serve(self):
        while not self.stopping.is_set():
            self.handle_request()

    def stop(self, worker):
        failure = None
        with self.condition:
            self.stopping.set()
            if self.active is not None:
                try:
                    self.active.shutdown(socket.SHUT_RDWR)
                except OSError as error:
                    if error.errno != errno.ENOTCONN:
                        failure = error
        worker.join(timeout=5)
        if worker.is_alive():
            raise RuntimeError("The owned HTTPS service did not stop.")
        if failure is not None:
            raise RuntimeError("Cannot interrupt the owned HTTPS connection.") from failure


def prepare_fixture(directory, home):
    fixture = directory / "tls"
    fixture.mkdir(mode=0o700)
    key, certificate, der, config = [fixture / name for name in
                                     ("key.pem", "certificate.pem", "certificate.der", "request.cnf")]
    key.touch(mode=0o600, exist_ok=False)
    config.write_text(CONFIG, encoding="ascii")
    tool_environment = {"HOME": str(home), "TMPDIR": str(fixture),
                        "PATH": "/usr/bin:/bin", "LANG": "en_US.UTF-8"}
    openssl(["req", "-x509", "-newkey", "rsa:2048", "-sha256", "-nodes",
             "-keyout", key, "-out", certificate, "-days", "1", "-config", config],
            tool_environment)
    if key.stat().st_mode & 0o077:
        raise RuntimeError("The owned fixture private key is not owner-only.")
    openssl(["x509", "-in", certificate, "-outform", "DER", "-out", der], tool_environment)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(str(certificate), str(key))
    return der, certificate, context


def check_tls(directory, environment, executables, controller, controller_socket):
    der, certificate, context = prepare_fixture(directory, environment["HOME"])
    with OwnedServer(context) as peer:
        worker = threading.Thread(target=peer.serve)
        worker.start()
        try:
            port = peer.server_port
            client_context = ssl.create_default_context(cafile=str(certificate))
            with socket.create_connection(("127.0.0.1", port), timeout=3) as connection:
                with client_context.wrap_socket(connection, server_hostname="localhost") as secure:
                    secure.sendall(b"GET /component-check HTTP/1.1\r\nHost: localhost\r\n"
                                   b"Connection: close\r\n\r\n")
                    response = bytearray()
                    while len(response) < 8192:
                        part = secure.recv(min(4096, 8192 - len(response)))
                        if not part:
                            break
                        response.extend(part)
                    if len(response) == 8192:
                        raise RuntimeError("The owned HTTPS readiness response exceeded its bound.")
                    if not response.startswith(b"HTTP/1.0 200 ") or response.split(b"\r\n\r\n", 1)[-1] != BODY:
                        raise RuntimeError("The owned HTTPS fixture did not become responsive.")
            dos_certificate = "Z:" + str(der).replace("/", "\\")
            for mode in ("untrusted", "trusted", "hostname"):
                runner.wait_server_ready(controller, controller_socket)
                previous_attempts = peer.attempts
                output = runner.run_owned([str(executables[0]), str(executables[2]),
                                           str(port), dos_certificate, mode], environment, 25)
                if "Isolated Windows TLS outcome passed." not in output:
                    raise RuntimeError("The selected Windows TLS check did not complete.")
                if peer.attempts <= previous_attempts or peer.failures:
                    raise RuntimeError("The owned TLS peer did not observe the expected connection.")
                runner.wait_server_ready(controller, controller_socket)
                print(f"Windows/Wine TLS {mode}: passed")
        finally:
            peer.stop(worker)
    return "Three actual Windows HTTPS checks passed; no account or host trust store was modified."


if __name__ == "__main__":
    runner.main(checks=check_tls)
