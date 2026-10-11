import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest

from owned_capture_control import ControlFirstAbort, IncrementalJournal, OwnershipError


def root_lines(native, pid=0x10, tid=0x14):
    return (f"{tid:04x}: init_first_thread( unix_pid={native}, unix_tid=1 )\n"
            f"{tid:04x}: init_first_thread() = 0 {{ pid={pid:04x}, tid={tid:04x} }}\n")


def child_lines(native, pid=0x20, tid=0x24):
    return ("0014: new_process( parent_process=0000 )\n"
            f"0014: new_process() = 0 {{ info=0001, pid={pid:04x}, handle=0040 }}\n"
            f"{tid:04x}: init_first_thread( unix_pid={native}, unix_tid=2 )\n"
            f"{tid:04x}: init_first_thread() = 0 {{ pid={pid:04x}, tid={tid:04x} }}\n")


def reap(process):
    if process.poll() is None:
        process.kill()
    process.wait(timeout=5)


class FakeClock:
    now = 0

    def __call__(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


class OwnedControlTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "server.log"
        self.path.write_text(root_lines(100) + child_lines(200))
        self.clock = FakeClock()
        self.identities = {100: "root-start-A", 200: "child-start-B"}
        self.signals = []
        self.reader = IncrementalJournal(self.path, clock=self.clock)

        def send(pid, stamp, sig, deadline):
            if self.identities.get(pid) != stamp:
                raise OwnershipError("Changed immediately before signal")
            self.signals.append((pid, sig))
            self.identities.pop(pid)

        self.control = ControlFirstAbort(self.reader, 100, "root-start-A",
                                        lambda pid, deadline: self.identities.get(pid),
                                        send, self.clock, self.clock.sleep, kill_signal=9)

    def test_descendants_stop_before_restoration_without_census(self):
        self.control.abort()
        self.control.restore(lambda: self.signals.append("restore"))
        self.assertEqual(self.signals, [(200, signal.SIGTERM), (100, signal.SIGTERM), "restore"])
        self.assertTrue(self.control.quiet)
        self.assertLess(self.clock.now, 4)

    def test_blocked_census_is_never_invoked(self):
        blocked = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])
        self.addCleanup(reap, blocked)
        self.control.census = lambda: blocked.wait(timeout=60)
        self.control.abort()
        self.assertTrue(self.control.quiet)
        self.assertIsNone(blocked.poll())

    def test_identity_mismatch_preflight_signals_nothing(self):
        self.control.observe(4)
        self.identities[200] = "reused-child-C"
        with self.assertRaisesRegex(OwnershipError, "identity changed"):
            self.control.abort()
        self.assertEqual(self.signals, [])
        with self.assertRaisesRegex(OwnershipError, "forbidden"):
            self.control.restore(lambda: self.fail("restored uncertain state"))

    def test_same_native_pid_new_wine_generation_signals_nothing(self):
        self.control.observe(4)
        with self.path.open("a") as out:
            out.write(child_lines(200))
        with self.assertRaisesRegex(OwnershipError, "generation/identity changed"):
            self.control.abort()
        self.assertEqual(self.signals, [])

    def test_launcher_identity_mismatch(self):
        self.identities[100] = "new-launcher"
        with self.assertRaisesRegex(OwnershipError, "Launcher identity"):
            self.control.abort()
        self.assertEqual(self.signals, [])

    def test_backlog_bound_refuses_control_and_preserves_bytes(self):
        self.path.write_bytes(b"x" * 2_000_001)
        before = self.path.read_bytes()
        with self.assertRaisesRegex(OwnershipError, "backlog"):
            self.control.abort()
        self.assertEqual(self.signals, [])
        self.assertEqual(self.path.read_bytes(), before)

    def test_partial_pending_event_refuses_signals(self):
        with self.path.open("a") as out:
            out.write("0014: new_process( parent_process=0000 )\n")
        with self.assertRaisesRegex(OwnershipError, "Incomplete"):
            self.control.abort()
        self.assertEqual(self.signals, [])

    def test_boundary_completed_by_live_writer_is_resettled(self):
        with self.path.open("a") as out:
            out.write("0014: new_process( parent_process=0000 )\n")
        sleep = self.clock.sleep

        def complete(seconds):
            sleep(seconds)
            with self.path.open("a") as out:
                out.write("0014: new_process() = 0 { info=0002, pid=0030, handle=0044 }\n")
            self.control.sleep = sleep

        self.control.sleep = complete
        self.control.abort()
        self.assertTrue(self.control.quiet)
        self.assertEqual(self.signals, [(200, signal.SIGTERM), (100, signal.SIGTERM)])

    def test_identity_callback_deadline_failure_blocks_restoration(self):
        def blocked(pid, deadline):
            self.clock.now = deadline
            raise OwnershipError("Bounded identity probe deadline")
        self.control.identity = blocked
        with self.assertRaisesRegex(OwnershipError, "deadline"):
            self.control.abort()
        self.assertEqual(self.signals, [])
        self.assertFalse(self.control.quiet)

    def test_unresponsive_owned_process_escalates_but_never_restores(self):
        self.control.send_signal = lambda pid, stamp, sig, deadline: self.signals.append((pid, sig))
        with self.assertRaisesRegex(OwnershipError, "did not quiesce"):
            self.control.abort(seconds=1, grace=.1)
        self.assertIn((200, 9), self.signals)
        self.assertLessEqual(self.clock.now, 1.001)
        self.assertFalse(self.control.quiet)

    def test_new_child_during_grace_is_proven_and_stopped(self):
        def send(pid, stamp, sig, deadline):
            self.signals.append((pid, sig))
            self.identities.pop(pid)
            if pid == 200:
                self.identities[300] = "child-C"
                with self.path.open("a") as out:
                    out.write(child_lines(300, pid=0x30, tid=0x34))
        self.control.send_signal = send
        self.control.abort(grace=.1)
        self.assertIn((300, 9), self.signals)
        self.assertTrue(self.control.quiet)

    def test_trace_truncation_blocks_restoration(self):
        self.control.observe(4)
        self.path.write_text("")
        with self.assertRaisesRegex(OwnershipError, "truncated"):
            self.control.abort()
        self.assertEqual(self.signals, [])

    def test_identity_changes_at_signal_boundary_are_not_signalled(self):
        def send(pid, expected, sig, deadline):
            self.identities[pid] = "reused-immediately-before-signal"
            if self.identities[pid] != expected:
                raise OwnershipError("Changed immediately before signal")
            self.signals.append((pid, sig))
        self.control.send_signal = send
        with self.assertRaisesRegex(OwnershipError, "immediately"):
            self.control.abort()
        self.assertEqual(self.signals, [])
        self.assertFalse(self.control.quiet)

    def test_partial_line_across_bounded_reads(self):
        self.path.write_text(root_lines(100)[:-1])
        self.reader.drain(4)
        self.assertFalse(self.reader.settled())
        with self.path.open("a") as out:
            out.write("\n")
        self.reader.drain(4)
        self.assertTrue(self.reader.settled())

    def test_unrelated_generation_is_not_signalled(self):
        with self.path.open("a") as out:
            out.write(root_lines(300, 0x30, 0x34))
        self.identities[300] = "unrelated"
        self.control.abort()
        self.assertIn(300, self.identities)
        self.assertNotIn(300, [pid for pid, sig in self.signals])

    def test_live_writer_stops_before_restore_original_overrun_preserved(self):
        output = Path(self.temp.name) / "overrun.log"
        code = ("import time\n"
                f"f=open({str(output)!r},'ab',buffering=0)\n"
                "while True:\n f.write(b'x'*4096)\n time.sleep(.005)\n")
        process = subprocess.Popen([sys.executable, "-c", code])
        self.addCleanup(reap, process)
        deadline = time.monotonic() + 5
        while (not output.exists() or output.stat().st_size < 8192) and time.monotonic() < deadline:
            time.sleep(.01)
        self.assertGreaterEqual(output.stat().st_size, 8192)
        original = output.read_bytes()
        self.path.write_text(root_lines(process.pid))
        reader = IncrementalJournal(self.path)
        stamp = f"synthetic-launch-{process.pid}"

        def identity(pid, deadline):
            return stamp if pid == process.pid and process.poll() is None else None

        def send(pid, expected, sig, deadline):
            if identity(pid, deadline) != expected:
                raise OwnershipError("Live fixture identity changed")
            os.kill(pid, sig)

        control = ControlFirstAbort(reader, process.pid, stamp, identity, send, kill_signal=9)
        control.abort()
        size = output.stat().st_size
        control.restore(lambda: self.assertIsNotNone(process.poll()))
        time.sleep(.05)
        self.assertEqual(size, output.stat().st_size)
        self.assertEqual(output.read_bytes()[:len(original)], original)
        self.assertTrue(control.quiet)


if __name__ == "__main__":
    unittest.main()
