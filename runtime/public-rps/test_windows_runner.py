# SPDX-License-Identifier: GPL-3.0-only
"""Darwin-only ownership regressions; helpers never execute Wine."""

from pathlib import Path
import select
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

import check_windows as runner


SERVER = """
import os, signal, socket, sys, time
signal.signal(signal.SIGINT, lambda *arguments: sys.exit(0))
time.sleep(float(sys.argv[2]))
with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as listener:
    listener.bind(sys.argv[1])
    os.chmod(sys.argv[1], 0o600)
    listener.listen(8)
    while True:
        with listener.accept()[0]:
            pass
"""


@unittest.skipUnless(sys.platform == "darwin", "Requires actual Darwin LOCAL_PEERPID.")
class OwnershipChecks(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="o-", dir=Path.cwd())
        self.root = Path(self.directory.name)
        self.socket_directory = self.root / "s"
        self.socket_directory.mkdir(mode=0o700)
        self.endpoint = self.socket_directory / "socket"
        self.children = []

    def tearDown(self):
        try:
            for child in self.children:
                runner.stop_owned_process(child)
        finally:
            self.directory.cleanup()

    def child(self, source, *arguments):
        process = subprocess.Popen([sys.executable, "-c", source, *map(str, arguments)],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.children.append(process)
        return process

    def server(self, delay=0):
        return self.child(SERVER, self.endpoint, delay)

    def test_delayed_server_is_ready_and_pid_bound_before_continuing(self):
        process = self.server(0.3)
        started = time.monotonic()
        runner.wait_server_ready(process, self.endpoint, timeout=2)
        self.assertGreaterEqual(time.monotonic() - started, 0.3)
        connection, pid = runner.connected_server(self.endpoint, 1)
        with connection:
            self.assertEqual(pid, process.pid)

    def test_main_does_not_launch_client_before_delayed_server_is_ready(self):
        binaries = [self.root / name for name in ("loader", "server", "helper.exe")]
        for binary in binaries:
            binary.touch()
        original_popen = subprocess.Popen
        endpoint = self.endpoint
        launched = []

        class CheckedClient(Exception):
            pass

        def delayed_server(arguments, **options):
            process = original_popen(
                [sys.executable, "-c", SERVER, str(endpoint), "0.3"],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
            )
            self.children.append(process)
            launched.append(time.monotonic())
            return process

        def checked_client(*arguments):
            self.assertGreaterEqual(time.monotonic() - launched[0], 0.3)
            self.assertTrue(endpoint.is_socket())
            raise CheckedClient

        try:
            with mock.patch.object(sys, "argv", ["check", str(self.root), *map(str, binaries)]):
                with mock.patch.object(subprocess, "Popen", side_effect=delayed_server):
                    with mock.patch.object(runner, "run_owned", side_effect=checked_client):
                        with mock.patch.object(runner, "server_endpoint",
                                               return_value=endpoint, create=True):
                            with self.assertRaises(CheckedClient):
                                runner.main()
            self.assertFalse(list(self.root.glob("w-*")))
        finally:
            for process in self.children:
                runner.stop_owned_process(process)
            if endpoint.exists():
                endpoint.unlink()

    def test_wait_is_bounded_without_launching_any_client(self):
        process = self.server(2)
        started = time.monotonic()
        with self.assertRaisesRegex(RuntimeError, "did not become ready"):
            runner.wait_server_ready(process, self.endpoint, timeout=0.1)
        self.assertLess(time.monotonic() - started, 1)

    def test_exited_recorded_server_fails_before_any_client(self):
        process = self.child("raise SystemExit(2)")
        process.wait(timeout=2)
        with self.assertRaisesRegex(RuntimeError, "exited before socket readiness"):
            runner.wait_server_ready(process, self.endpoint, timeout=1)

    def test_another_listening_pid_is_not_readiness(self):
        actual = self.server()
        runner.wait_server_ready(actual, self.endpoint, timeout=2)
        recorded = self.child("import time; time.sleep(3)")
        with self.assertRaisesRegex(RuntimeError, "not owned by the recorded"):
            runner.wait_server_ready(recorded, self.endpoint, timeout=1)

    def test_reconcile_verified_replacement_before_removing_prefix(self):
        replacement = self.server()
        runner.wait_server_ready(replacement, self.endpoint, timeout=2)
        recorded = self.child("raise SystemExit(2)")
        recorded.wait(timeout=2)
        runner.reconcile_server(recorded, self.endpoint,
                                runner.process_executable(replacement.pid),
                                clients_started=False, timeout=2)
        replacement.wait(timeout=2)
        self.assertIsNotNone(replacement.returncode)
        connection, pid = runner.connected_server(self.endpoint, 0.2)
        self.assertIsNone(connection)
        self.assertIsNone(pid)

    def test_unknown_replacement_is_not_killed_and_prefix_is_retained(self):
        replacement = self.server()
        runner.wait_server_ready(replacement, self.endpoint, timeout=2)
        recorded = self.child("raise SystemExit(2)")
        recorded.wait(timeout=2)
        with self.assertRaisesRegex(RuntimeError, "Unrecognized socket owner"):
            runner.reconcile_server(recorded, self.endpoint, self.root / "not-server",
                                    clients_started=False, timeout=2)
        self.assertIsNone(replacement.poll())
        self.assertTrue(self.root.is_dir())

    def test_main_retains_private_directory_on_reconciliation_failure(self):
        binaries = [self.root / name for name in ("loader", "server", "helper.exe")]
        for binary in binaries:
            binary.touch()
        original_popen = subprocess.Popen

        def exited_server(*arguments, **options):
            process = original_popen([sys.executable, "-c", "raise SystemExit(2)"])
            self.children.append(process)
            return process

        with mock.patch.object(sys, "argv", ["check", str(self.root), *map(str, binaries)]):
            with mock.patch.object(subprocess, "Popen", side_effect=exited_server):
                with mock.patch.object(runner, "reconcile_server",
                                       side_effect=RuntimeError("Unrecognized socket owner")):
                    with self.assertRaisesRegex(RuntimeError, "private directory retained"):
                        runner.main()
        self.assertEqual(len(list(self.root.glob("w-*"))), 1)

    def test_unexpected_server_death_without_listener_retains_live_prefix(self):
        binaries = [self.root / name for name in ("loader", "server", "helper.exe")]
        for binary in binaries:
            binary.touch()
        original_popen = subprocess.Popen
        started = []
        clients = []

        def start_server(*arguments, **options):
            process = original_popen([sys.executable, "-c", SERVER, str(self.endpoint), "0"],
                                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            self.children.append(process)
            started.append(process)
            return process

        def lose_server(*arguments):
            marker = Path(arguments[1]["WINEPREFIX"]) / "synthetic-client.txt"
            source = (
                "import sys,time; handle=open(sys.argv[1],'w'); "
                "handle.write('synthetic'); handle.flush(); "
                "print('ready',flush=True); time.sleep(5)"
            )
            client = original_popen([sys.executable, "-c", source, str(marker)],
                                    stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
            self.children.append(client)
            clients.append(client)
            try:
                self.assertTrue(select.select([client.stdout], [], [], 2)[0])
                self.assertEqual(client.stdout.readline(), b"ready\n")
            finally:
                client.stdout.close()
            started[0].terminate()
            started[0].wait(timeout=2)
            return "Isolated Windows check started; no credentials requested."

        with mock.patch.object(sys, "argv", ["check", str(self.root), *map(str, binaries)]):
            with mock.patch.object(subprocess, "Popen", side_effect=start_server):
                with mock.patch.object(runner, "run_owned", side_effect=lose_server):
                    with mock.patch.object(runner, "server_endpoint", return_value=self.endpoint):
                        with self.assertRaises(RuntimeError):
                            runner.main()
        self.assertEqual(len(list(self.root.glob("w-*"))), 1)
        self.assertIsNone(clients[0].poll())
        self.assertEqual(next(self.root.glob("w-*/prefix/synthetic-client.txt")).read_text(),
                         "synthetic")
        connection, pid = runner.connected_server(self.endpoint, 0.2)
        self.assertIsNone(connection)
        self.assertIsNone(pid)

    def test_abnormal_shutdown_status_does_not_confirm_client_cleanup(self):
        source = SERVER.replace("sys.exit(0)", "sys.exit(77)")
        process = self.child(source, self.endpoint, 0)
        runner.wait_server_ready(process, self.endpoint, timeout=2)
        with self.assertRaisesRegex(RuntimeError, "did not confirm graceful"):
            runner.reconcile_server(process, self.endpoint,
                                    runner.process_executable(process.pid),
                                    clients_started=True, timeout=2)
        self.assertEqual(process.returncode, 77)
        self.assertTrue(self.root.is_dir())

    def test_abnormal_replacement_exit_is_not_successful_reconciliation(self):
        source = SERVER.replace("sys.exit(0)", "sys.exit(77)")
        replacement = self.child(source, self.endpoint, 0)
        runner.wait_server_ready(replacement, self.endpoint, timeout=2)
        recorded = self.child("raise SystemExit(2)")
        recorded.wait(timeout=2)
        with self.assertRaisesRegex(RuntimeError, "Replacement shutdown was not graceful"):
            runner.reconcile_server(recorded, self.endpoint,
                                    runner.process_executable(replacement.pid),
                                    clients_started=False, timeout=2)
        replacement.wait(timeout=2)
        self.assertEqual(replacement.returncode, 77)
        self.assertTrue(self.root.is_dir())


if __name__ == "__main__":
    unittest.main()
