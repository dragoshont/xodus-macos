# SPDX-License-Identifier: GPL-3.0-only
"""Default drawable admission/outcome guards; these tests do not display a window."""

from pathlib import Path
import unittest
from unittest.mock import call, patch

import check_default_drawable as drawable


class DrawableChecks(unittest.TestCase):
    def test_graphical_session_precedes_runner(self):
        events = []
        with patch.object(drawable.graphics, "require_graphics_session",
                          side_effect=lambda: events.append("admit")), \
                patch.object(drawable.runner, "main",
                             side_effect=lambda **kwargs: events.append(kwargs["checks"])):
            drawable.main()
        self.assertEqual(events, ["admit", drawable.check_drawable])

    def test_unavailable_session_cannot_start_runner(self):
        with patch.object(drawable.graphics, "require_graphics_session",
                          side_effect=RuntimeError("no graphics session")), \
                patch.object(drawable.runner, "main") as run:
            with self.assertRaisesRegex(RuntimeError, "no graphics session"):
                drawable.main()
            run.assert_not_called()

    def test_exact_modes_and_pid_bound_server_checks(self):
        with patch.object(drawable.runner, "wait_server_ready") as ready, \
                patch.object(drawable.runner, "run_owned", side_effect=[
                    "Isolated Windows legacy default-drawable outcome passed.",
                    "Isolated Windows core default-drawable outcome passed.",
                ]) as run:
            result = drawable.check_drawable(None, {}, ["wine", "server", "check"], "pid", "socket")
        self.assertEqual(ready.call_args_list, [call("pid", "socket")] * 4)
        self.assertEqual(run.call_args_list, [
            call(["wine", "check", "--present"], {}, 25),
            call(["wine", "check", "--present-core"], {}, 25),
        ])
        self.assertIn("not compositor capture", result)

    def test_missing_wrong_or_offscreen_outcomes_refuse(self):
        for results in (
            [""],
            ["Isolated Windows core default-drawable outcome passed."],
            ["Isolated Windows legacy graphics outcome passed."],
            ["Isolated Windows legacy default-drawable outcome passed.", ""],
            ["Isolated Windows legacy default-drawable outcome passed."] * 2,
        ):
            with self.subTest(results=results), \
                    patch.object(drawable.runner, "wait_server_ready"), \
                    patch.object(drawable.runner, "run_owned", side_effect=results):
                with self.assertRaisesRegex(RuntimeError, "default drawable did not complete"):
                    drawable.check_drawable(None, {}, ["wine", "server", "check"], None, None)

    def test_process_failure_propagates(self):
        with patch.object(drawable.runner, "wait_server_ready"), \
                patch.object(drawable.runner, "run_owned",
                             side_effect=RuntimeError("owned failure")) as run:
            with self.assertRaisesRegex(RuntimeError, "owned failure"):
                drawable.check_drawable(None, {}, ["wine", "server", "check"], None, None)
            run.assert_called_once()

    def test_source_uses_nonactivation_and_only_owned_default_buffer(self):
        source = (Path(__file__).parent / "windows_graphics_smoke.c").read_text()
        self.assertIn("WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE", source)
        self.assertIn("SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW", source)
        self.assertIn("GetForegroundWindow() != original_foreground", source)
        self.assertIn("GetActiveWindow() == window", source)
        self.assertIn("SwapBuffers(device)", source)
        self.assertNotIn("GetDC(NULL)", source)
        self.assertNotIn("SetForegroundWindow(", source)
        self.assertNotIn("SetFocus(", source)


if __name__ == "__main__":
    unittest.main()
