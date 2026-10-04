# SPDX-License-Identifier: GPL-3.0-only
"""Graphics access must be established before the owned runner starts."""

import unittest
from pathlib import Path
from unittest.mock import call, patch

import check_windows_graphics as graphics


class GraphicsSessionChecks(unittest.TestCase):
    def invoke(self, status, attributes, error=None):
        def get_info(session, identifier, output):
            self.assertEqual(session, 0xffffffff)
            self.assertIsNone(identifier)
            output._obj.value = attributes
            return status

        with patch.object(graphics.sys, "platform", "darwin"), \
                patch.object(graphics.ctypes, "CDLL") as library, \
                patch.object(graphics.runner, "main") as run:
            library.return_value.SessionGetInfo.side_effect = get_info
            if error:
                with self.assertRaisesRegex(RuntimeError, error):
                    graphics.main()
                run.assert_not_called()
            else:
                graphics.main()
                run.assert_called_once_with(checks=graphics.check_graphics)
            library.assert_called_once_with("/System/Library/Frameworks/Security.framework/Security")

    def test_graphical_session_is_admitted(self):
        self.invoke(0, 0x6030)

    def test_ssh_session_is_refused_before_runner(self):
        self.invoke(0, 0x5020, "no macOS graphics-session access")

    def test_session_api_error_is_not_success(self):
        self.invoke(-50, 0x6030, "OSStatus -50")

    def test_other_hosts_do_not_load_framework_or_start_runner(self):
        with patch.object(graphics.sys, "platform", "win32"), \
                patch.object(graphics.ctypes, "CDLL") as library, \
                patch.object(graphics.runner, "main") as run:
            with self.assertRaisesRegex(RuntimeError, "requires macOS"):
                graphics.main()
            library.assert_not_called()
            run.assert_not_called()


class GraphicsOutcomeChecks(unittest.TestCase):
    def test_both_contexts_require_exact_outcomes_and_server_checks(self):
        executables = [Path("/owned/wine"), Path("/owned/wineserver"), Path("/owned/check.exe")]
        environment = {"fixture": "not credentials"}
        with patch.object(graphics.runner, "wait_server_ready") as ready, \
                patch.object(graphics.runner, "run_owned", side_effect=[
                    "Isolated Windows legacy graphics outcome passed.",
                    "Isolated Windows core graphics outcome passed.",
                ]) as run:
            outcome = graphics.check_graphics(None, environment, executables, "controller", "socket")
            self.assertIn("no window presentation", outcome)
            self.assertEqual(ready.call_args_list, [call("controller", "socket")] * 4)
            self.assertEqual(run.call_args_list, [
                call([str(executables[0]), str(executables[2])], environment, 25),
                call([str(executables[0]), str(executables[2]), "--core"], environment, 25),
            ])

    def test_missing_or_wrong_mode_outcomes_are_not_success(self):
        for outputs, mode in [
            ([""], "legacy"),
            (["Isolated Windows core graphics outcome passed."], "legacy"),
            (["Isolated Windows legacy graphics outcome passed.", ""], "core"),
            (["Isolated Windows legacy graphics outcome passed."] * 2, "core"),
        ]:
            with self.subTest(mode=mode, outputs=outputs), \
                    patch.object(graphics.runner, "wait_server_ready"), \
                    patch.object(graphics.runner, "run_owned", side_effect=outputs) as run:
                with self.assertRaisesRegex(RuntimeError, f"Windows {mode} graphics check"):
                    graphics.check_graphics(None, {}, ["wine", "server", "check.exe"], None, None)
                self.assertEqual(run.call_count, len(outputs))

    def test_process_failure_is_propagated_without_starting_next_mode(self):
        with patch.object(graphics.runner, "wait_server_ready"), \
                patch.object(graphics.runner, "run_owned",
                             side_effect=RuntimeError("owned process failed")) as run:
            with self.assertRaisesRegex(RuntimeError, "owned process failed"):
                graphics.check_graphics(None, {}, ["wine", "server", "check.exe"], None, None)
            run.assert_called_once()


if __name__ == "__main__":
    unittest.main()
