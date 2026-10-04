# SPDX-License-Identifier: GPL-3.0-only
"""Empty-memory pairing guards; no native credential backend, broker or Wine execution."""

from pathlib import Path
import signal
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import Mock, patch

import check_empty_broker as checks


class EmptyBrokerChecks(unittest.TestCase):
    def test_ping_uses_recorded_peer_pid_and_exact_framing(self):
        process, peer = Mock(pid=123), Mock()
        process.poll.return_value = None
        peer.__enter__ = Mock(return_value=peer)
        peer.__exit__ = Mock(return_value=False)
        with patch.object(checks.runner, "connected_server", return_value=(peer, 123)), \
                patch.object(checks.runner, "receive_exact",
                             side_effect=[struct.pack("<IHH", 0x58445358, 2, 5), b"owned"]):
            checks.ping(process, "socket", b"owned")
        peer.sendall.assert_called_once_with(struct.pack("<IHH", 0x58445358, 1, 5) + b"owned")

    def test_wrong_peer_is_refused_before_sending(self):
        process, peer = Mock(pid=123), Mock()
        process.poll.return_value = None
        peer.__enter__ = Mock(return_value=peer)
        peer.__exit__ = Mock(return_value=False)
        with patch.object(checks.runner, "connected_server", return_value=(peer, 456)):
            with self.assertRaisesRegex(RuntimeError, "another process"):
                checks.ping(process, "socket", b"owned")
        peer.sendall.assert_not_called()

    def test_dead_process_or_wrong_header_is_not_ping_success(self):
        process = Mock(pid=123)
        process.poll.return_value = 1
        with self.assertRaisesRegex(RuntimeError, "exited"):
            checks.ping(process, "socket", b"owned")
        process.poll.return_value = None
        peer = Mock()
        peer.__enter__ = Mock(return_value=peer)
        peer.__exit__ = Mock(return_value=False)
        with patch.object(checks.runner, "connected_server", return_value=(peer, 123)), \
                patch.object(checks.runner, "receive_exact", return_value=b"wrong"):
            with self.assertRaisesRegex(RuntimeError, "header"):
                checks.ping(process, "socket", b"owned")

    def test_fixture_shutdown_requires_graceful_zero_exit(self):
        process = Mock(returncode=0)
        process.poll.return_value = None
        checks.stop_fixture(process)
        process.send_signal.assert_called_once_with(signal.SIGINT)
        process.wait.assert_called_once_with(timeout=5)
        process.kill.assert_not_called()
        process.returncode = 1
        with self.assertRaisesRegex(RuntimeError, "successfully"):
            checks.stop_fixture(process)

    def test_forced_shutdown_is_not_success(self):
        process = Mock(returncode=-9)
        process.poll.return_value = None
        process.wait.side_effect = [subprocess.TimeoutExpired("owned fixture", 5), -9]
        with self.assertRaisesRegex(RuntimeError, "forced termination"):
            checks.stop_fixture(process)
        process.kill.assert_called_once()

    def test_mismatched_bytes_refuse_before_process_launch(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            binary = root / "not-the-fixture"
            binary.write_bytes(b"not the approved developer-only fixture")
            with patch.object(checks.components, "verify_component"), \
                    patch.object(checks.subprocess, "Popen") as run:
                with self.assertRaisesRegex(RuntimeError, "pinned"):
                    checks.verify_fixture(root, binary)
                run.assert_not_called()

    def test_real_consumer_selection_and_no_fake_authentication_category(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            directory = root / "wine-prefix"
            directory.mkdir()
            fixture = root / "fixture"
            process = Mock(returncode=0)
            process.poll.return_value = None

            def launch(arguments, **kwargs):
                self.assertEqual(arguments[:3], [str(fixture), "--empty-memory-fixture", "--management-socket"])
                kwargs["stderr"].write(checks.BANNER + b"\nPrivate runtime peer request failed\n")
                return process

            with patch.object(checks.components, "verify_component"), \
                    patch.object(checks, "verify_fixture"), \
                    patch("builtins.print"), \
                    patch.object(checks.subprocess, "Popen", side_effect=launch), \
                    patch.object(checks, "ping") as ping, \
                    patch.object(checks.runner, "wait_server_ready"), \
                    patch.object(checks.runner, "run_owned",
                                 return_value="Isolated Windows async rps-peer-closed outcome passed.") as run:
                outcome = checks.check_broker(directory, {}, ["wine", "server", root / "check.exe"],
                                             None, None, fixture=fixture)
            self.assertEqual(ping.call_count, 2)
            arguments, environment, timeout = run.call_args.args
            self.assertEqual(arguments, ["wine", str(root / "check.exe"), "rps-peer-closed"])
            self.assertEqual(timeout, 20)
            self.assertTrue(environment["XODUS_RUNTIME_SOCKET"].endswith("peer.sock"))
            self.assertIn("no wire authentication category", outcome)
            self.assertEqual(list(root.iterdir()), [directory])

    def test_uncertain_native_cleanup_retains_separate_private_directory(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            directory = root / "wine-prefix"
            directory.mkdir()
            process = Mock(returncode=1)
            process.poll.return_value = None
            with patch.object(checks.components, "verify_component"), \
                    patch.object(checks, "verify_fixture"), \
                    patch.object(checks.subprocess, "Popen", return_value=process), \
                    patch.object(checks, "ping"), \
                    patch.object(checks.runner, "wait_server_ready"), \
                    patch.object(checks.runner, "run_owned", side_effect=RuntimeError("consumer failed")):
                with self.assertRaisesRegex(RuntimeError, "private evidence retained"):
                    checks.check_broker(directory, {}, ["wine", "server", root / "check.exe"],
                                        None, None, fixture=root / "fixture")
            retained = [path for path in root.iterdir() if path != directory]
            self.assertEqual(len(retained), 1)
            self.assertTrue((retained[0] / "s").is_dir())


if __name__ == "__main__":
    unittest.main()
