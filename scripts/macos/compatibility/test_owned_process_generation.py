import os
from pathlib import Path
import subprocess
import sys
import time
import unittest


@unittest.skipUnless(sys.platform == "darwin" and "XODUS_GENERATION_HELPER" in os.environ,
                     "Requires compiled Darwin generation helper")
class GenerationTests(unittest.TestCase):
    helper = Path(os.environ.get("XODUS_GENERATION_HELPER", "owned-process-generation"))

    def stamp(self, pid):
        result = subprocess.run([str(self.helper), str(pid)], capture_output=True,
                                text=True, timeout=1)
        self.assertFalse(result.stderr)
        self.assertIn(result.returncode, (0, 1))
        return result.stdout.strip() if result.returncode == 0 else None

    def test_owned_exec_and_state_changes_keep_birth(self):
        child = subprocess.Popen(["/usr/bin/perl", "-e",
                                  "kill 'STOP', $$; exec '/bin/sleep', '2'; die $!;"])
        try:
            stopped_pid, status = os.waitpid(child.pid, os.WUNTRACED)
            self.assertEqual(stopped_pid, child.pid)
            self.assertTrue(os.WIFSTOPPED(status))
            before = self.stamp(child.pid)
            self.assertRegex(before, rf"^{child.pid}:\d+:\d+$")
            time.sleep(.05)
            self.assertEqual(before, self.stamp(child.pid))
            os.kill(child.pid, __import__("signal").SIGCONT)
            time.sleep(.1)
            self.assertEqual(before, self.stamp(child.pid))
            child.wait(timeout=3)
            self.assertIsNone(self.stamp(child.pid))
        finally:
            if child.poll() is None:
                child.kill()
            child.wait()

    def test_distinct_children_and_exited_child(self):
        first = subprocess.Popen(["/bin/sleep", "2"])
        second = subprocess.Popen(["/bin/sleep", "2"])
        try:
            self.assertNotEqual(self.stamp(first.pid), self.stamp(second.pid))
            first.kill()
            first.wait()
            self.assertIsNone(self.stamp(first.pid))
            self.assertIsNotNone(self.stamp(second.pid))
        finally:
            for child in (first, second):
                if child.poll() is None:
                    child.kill()
                child.wait()


if __name__ == "__main__":
    unittest.main()
