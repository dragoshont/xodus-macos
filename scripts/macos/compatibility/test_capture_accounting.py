import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from capture_accounting import poll_capture


class Controller:
    def __init__(self, returncode):
        self.returncode = returncode
        self.polled = False

    def poll(self):
        self.polled = True
        return self.returncode


class CaptureAccountingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.stdout = self.root / "stdout.log"
        self.stderr = self.root / "stderr.log"
        self.server = self.root / "server.log"
        for path in (self.stdout, self.stderr, self.server):
            path.write_bytes(b"")

    def state(self, process=None, limit=100, now=11):
        return poll_capture(process or Controller(None), self.stdout,
                            [self.stderr, self.server], 10, now, limit, "FIRST_WAIT")

    def test_rapid_real_exit_reads_completed_stdout_before_timeout(self):
        with self.stdout.open("wb") as output, self.stderr.open("wb") as debug:
            process = subprocess.Popen(
                [sys.executable, "-c",
                 "import sys; print('FIRST_WAIT=0\\nEXIT=4103'); "
                 "print('original stderr', file=sys.stderr)"],
                stdout=output, stderr=debug)
            self.addCleanup(lambda: process.kill() if process.poll() is None else None)
            process.wait(timeout=10)
        state = self.state(process)
        self.assertEqual(state.status, "ready")
        self.assertEqual(state.returncode, 0)
        self.assertIn("EXIT=4103", state.stdout)
        self.assertEqual(self.stderr.read_text().strip(), "original stderr")

    def test_completed_exit_without_first_wait_is_not_timeout(self):
        self.stdout.write_bytes(b"ACTIVATION=1\nEXIT=4103\n")
        self.assertEqual(self.state(Controller(1)).status, "ready")

    def test_aggregate_limit_counts_all_three_files(self):
        for path in (self.stdout, self.stderr, self.server):
            path.write_bytes(b"x" * 40)
        state = self.state(limit=100, now=0)
        self.assertEqual(state.status, "over_limit")
        self.assertEqual(state.total_bytes, 120)
        self.assertEqual(list(state.sizes.values()), [40, 40, 40])

    def test_completed_marker_does_not_hide_over_limit(self):
        self.stdout.write_bytes(b"FIRST_WAIT=0\nEXIT=4103\n")
        self.stderr.write_bytes(b"e" * 50)
        self.server.write_bytes(b"s" * 50)
        originals = [p.read_bytes() for p in (self.stdout, self.stderr, self.server)]
        state = self.state(Controller(0), limit=100)
        self.assertEqual(state.status, "over_limit")
        self.assertIn("EXIT=4103", state.stdout)
        self.assertEqual(originals, [p.read_bytes() for p in (self.stdout, self.stderr, self.server)])

    def test_marker_read_while_controller_running_precedes_timeout(self):
        self.stdout.write_bytes(b"FIRST_WAIT=0\n")
        state = self.state()
        self.assertEqual(state.status, "ready")
        self.assertIsNone(state.returncode)

    def test_running_without_marker_times_out(self):
        self.assertEqual(self.state().status, "timeout")
        self.assertEqual(self.state(now=9).status, "waiting")

    def test_exact_threshold_is_admitted(self):
        self.stderr.write_bytes(b"x" * 100)
        self.assertEqual(self.state(Controller(0)).status, "ready")

    def test_duplicate_stdout_path_not_counted_twice(self):
        self.stdout.write_bytes(b"x" * 50)
        state = poll_capture(Controller(0), self.stdout, [self.stdout, self.stderr],
                             0, 1, 50, "")
        self.assertEqual(state.total_bytes, 50)
        self.assertEqual(state.status, "ready")

    def test_poll_precedes_stdout_read(self):
        stdout = self.stdout

        class FinishingController:
            def poll(self):
                stdout.write_bytes(b"EXIT=4103\n")
                return 0

        self.assertIn("EXIT=4103", self.state(FinishingController()).stdout)

    def test_missing_capture_is_explicit_error(self):
        self.server.unlink()
        with self.assertRaises(FileNotFoundError):
            self.state()

    def test_negative_limit_rejected(self):
        with self.assertRaises(ValueError):
            self.state(limit=-1)

    def test_live_dump_growth_is_part_of_aggregate_bound(self):
        dump = self.root / "exception-0.dmp"
        dump.write_bytes(b"d" * 80)
        self.stderr.write_bytes(b"e" * 21)
        state = poll_capture(Controller(None), self.stdout, [self.stderr, dump],
                             10, 0, 100, "")
        self.assertEqual(state.status, "over_limit")
        self.assertEqual(state.total_bytes, 101)
        self.assertEqual(dump.stat().st_size, 80)

    def test_dump_completed_at_process_exit_cannot_hide_over_limit(self):
        dump = self.root / "exception-0.dmp"
        dump.write_bytes(b"")

        class FinishingController:
            def poll(self):
                dump.write_bytes(b"d" * 101)
                return 0

        state = poll_capture(FinishingController(), self.stdout, [self.stderr, dump],
                             10, 11, 100, "")
        self.assertEqual(state.status, "over_limit")
        self.assertEqual(state.returncode, 0)
        self.assertEqual(dump.stat().st_size, 101)


if __name__ == "__main__":
    unittest.main()
