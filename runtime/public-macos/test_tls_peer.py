# SPDX-License-Identifier: GPL-3.0-only
"""Real loopback checks for bounded fixture shutdown; no Wine or host trust writes."""

from pathlib import Path
import socket
import ssl
import tempfile
import threading
import unittest
from unittest import mock

from check_windows_tls import Handler, OwnedServer, prepare_fixture


class PeerShutdownChecks(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="xodus-owned-tls-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve(strict=True)
        self.der, self.certificate, self.context = prepare_fixture(self.root, self.root)

    def start(self):
        peer = OwnedServer(self.context)
        self.addCleanup(peer.server_close)
        worker = threading.Thread(target=peer.serve)
        worker.start()
        self.addCleanup(peer.stop, worker)
        return peer, worker

    def test_stalled_handshake_is_interrupted_before_joining_worker(self):
        peer, worker = self.start()
        with socket.create_connection(("127.0.0.1", peer.server_port), timeout=3):
            with peer.condition:
                self.assertTrue(peer.condition.wait_for(lambda: peer.active is not None, timeout=2))
            peer.stop(worker)
            self.assertFalse(worker.is_alive())
            self.assertIsNone(peer.active)

    def test_stalled_http_headers_are_interrupted_before_joining_worker(self):
        parsing = threading.Event()
        parse_request = Handler.parse_request

        def observed_parse(handler):
            parsing.set()
            return parse_request(handler)

        with mock.patch.object(Handler, "parse_request", observed_parse):
            peer, worker = self.start()
            context = ssl.create_default_context(cafile=str(self.certificate))
            with socket.create_connection(("127.0.0.1", peer.server_port), timeout=3) as connection:
                with context.wrap_socket(connection, server_hostname="localhost") as secure:
                    secure.sendall(b"GET /component-check HTTP/1.1\r\nHost: localhost\r\n")
                    self.assertTrue(parsing.wait(timeout=2))
                    peer.stop(worker)
                    self.assertFalse(worker.is_alive())
                    self.assertIsNone(peer.active)
                    self.assertFalse(peer.failures)


if __name__ == "__main__":
    unittest.main(verbosity=2)
