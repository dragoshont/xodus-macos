# SPDX-License-Identifier: GPL-3.0-only
"""Metadata-only snapshot guards; no application access or graphical execution."""

from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import Mock, patch

import check_graphics_foreground as guard


class ForegroundChecks(unittest.TestCase):
    def test_exact_pid_only_and_bounded_read(self):
        with patch.object(guard.subprocess, "run",
                          return_value=subprocess.CompletedProcess([], 0, "123\n")) as run:
            self.assertEqual(guard.foreground(Path("helper")), 123)
            run.assert_called_once_with(["helper"], capture_output=True, text=True, check=True, timeout=10)

    def test_invalid_or_absent_metadata_is_not_a_default_pid(self):
        for value in ("", "0", "-1", str(2 ** 31), "12\n13", "\u0661", "not a PID"):
            with self.subTest(value=value), \
                    patch.object(guard.subprocess, "run",
                                 return_value=subprocess.CompletedProcess([], 0, value)):
                with self.assertRaisesRegex(RuntimeError, "invalid PID"):
                    guard.foreground(Path("helper"))
        with patch.object(guard.subprocess, "run",
                          side_effect=subprocess.CalledProcessError(1, ["helper"])):
            with self.assertRaises(subprocess.CalledProcessError):
                guard.foreground(Path("helper"))

    def test_unchanged_snapshots_preserve_exact_process_arguments_and_output(self):
        original = Mock(return_value="owned outcome")
        with patch.object(guard, "foreground", side_effect=[42, 42]), patch("builtins.print"):
            self.assertEqual(guard.guarded_run(original, "helper", ["owned"], {}, 25), "owned outcome")
        original.assert_called_once_with(["owned"], {}, 25)

    def test_changed_or_failed_snapshot_rejects_success(self):
        for result in ([42, 43], [42, RuntimeError("metadata unavailable")]):
            with self.subTest(result=result), patch.object(guard, "foreground", side_effect=result):
                with self.assertRaises(RuntimeError):
                    guard.guarded_run(Mock(return_value="success"), "helper", [], {}, 25)

    def test_initial_snapshot_failure_prevents_process(self):
        original = Mock()
        with patch.object(guard, "foreground", side_effect=RuntimeError("metadata unavailable")):
            with self.assertRaises(RuntimeError):
                guard.guarded_run(original, "helper", [], {}, 25)
        original.assert_not_called()

    def test_original_failure_and_simultaneous_guard_failure_remain_visible(self):
        error = RuntimeError("owned process failed")
        for snapshots in ([42, 42], [42, 43]):
            with self.subTest(snapshots=snapshots), patch.object(guard, "foreground", side_effect=snapshots):
                with self.assertRaises(RuntimeError) as raised:
                    guard.guarded_run(Mock(side_effect=error), "helper", [], {}, 25)
                if snapshots[0] == snapshots[1]:
                    self.assertIs(raised.exception, error)
                else:
                    self.assertIs(raised.exception.__context__, error)

    @unittest.skipUnless(hasattr(guard.os, "getuid"), "POSIX file ownership")
    def test_owned_helper_boundary(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve(strict=True)
            helper = root / "helper"
            helper.write_text("owned")
            helper.chmod(0o500)
            self.assertEqual(guard.select_helper(helper, root), helper)
            helper.chmod(0o666)
            with self.assertRaisesRegex(RuntimeError, "writable"):
                guard.select_helper(helper, root)
            helper.chmod(0o500)
            alias = root / "alias"
            alias.symlink_to(helper)
            with self.assertRaisesRegex(RuntimeError, "non-aliased"):
                guard.select_helper(alias, root)
            (root / "child").mkdir(mode=0o700)
            with self.assertRaisesRegex(RuntimeError, "inside"):
                guard.select_helper(helper, root / "child")

    def test_all_four_callbacks_and_failures_remain_distinct(self):
        with patch.object(guard.drawable.graphics, "check_graphics", return_value="offscreen") as offscreen, \
                patch.object(guard.drawable, "check_drawable", return_value="default") as default:
            result = guard.check_all(None, {}, [], None, None)
        offscreen.assert_called_once_with(None, {}, [], None, None)
        default.assert_called_once_with(None, {}, [], None, None)
        self.assertIn("offscreen\ndefault", result)
        with patch.object(guard.drawable.graphics, "check_graphics",
                          side_effect=RuntimeError("offscreen failed")), \
                patch.object(guard.drawable, "check_drawable") as default:
            with self.assertRaisesRegex(RuntimeError, "offscreen failed"):
                guard.check_all(None, {}, [], None, None)
            default.assert_not_called()

    @unittest.skipUnless(hasattr(guard.os, "getuid"), "POSIX file ownership")
    def test_sibling_traversal_refuses_before_helper_execution(self):
        with tempfile.TemporaryDirectory() as directory:
            parent = Path(directory).resolve(strict=True)
            root, sibling = parent / "owned", parent / "sibling"
            root.mkdir(mode=0o700)
            sibling.mkdir(mode=0o700)
            helper = sibling / "collector"
            helper.write_text("must not execute")
            helper.chmod(0o500)
            for supplied in (helper, root / ".." / "sibling" / "collector"):
                with self.subTest(supplied=supplied), \
                        patch.object(guard.sys, "argv", [
                            "guard", "--foreground-helper", str(supplied),
                            str(root), "loader", "server", "check",
                        ]), \
                        patch.object(guard.drawable.graphics, "require_graphics_session"), \
                        patch.object(guard.drawable.runner, "main") as run, \
                        patch.object(guard.subprocess, "run") as execute:
                    with self.assertRaisesRegex(RuntimeError, "inside"):
                        guard.main()
                    run.assert_not_called()
                    execute.assert_not_called()

    def test_main_restores_global_runner_and_arguments_after_failure(self):
        arguments = ["guard", "--foreground-helper", "helper", "root", "loader", "server", "check"]
        original = guard.drawable.runner.run_owned
        with patch.object(guard.sys, "argv", arguments), \
                patch.object(guard.drawable.graphics, "require_graphics_session"), \
                patch.object(guard, "select_helper", return_value=Path("helper")), \
                patch.object(guard.drawable.runner, "main", side_effect=RuntimeError("owned failure")):
            with self.assertRaisesRegex(RuntimeError, "owned failure"):
                guard.main()
            self.assertIs(guard.sys.argv, arguments)
            self.assertIs(guard.drawable.runner.run_owned, original)


if __name__ == "__main__":
    unittest.main()
