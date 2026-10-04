"""Run the actual smoke tool with mocked processes; never execute a broker."""
import argparse
import ast
import json
import os
import runpy
import subprocess
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

parser = argparse.ArgumentParser()
parser.add_argument("--tool", type=Path, default=Path(__file__).with_name("smoke_empty_runtime.py"))
parser.add_argument("--probe", choices=["production-refusal", "wrong-status", "unexpected-stdout"])
args = parser.parse_args()
tool = args.tool.resolve()


def probe():
    calls = []
    started = []
    fixture_usage = (
        b"Expected --empty-memory-fixture --management-socket "
        b"with one absolute private Unix path\n"
    )

    def refused(command, **_):
        calls.append(command[1:])
        return subprocess.CompletedProcess(
            command,
            0 if args.probe == "wrong-status" else 1,
            b"unexpected" if args.probe == "unexpected-stdout" else b"",
            b"Expected --management-socket with one absolute private Unix path\n"
            if args.probe == "production-refusal" else fixture_usage,
        )

    def blocked_start(command, **_):
        started.append(command[1:])
        raise RuntimeError("Mock prevented fixture startup after unsafe probes")

    temporary = mock.MagicMock()
    temporary.__enter__.return_value = "/mock/owned-private-root"
    error = None
    with (
        mock.patch.object(sys, "argv", [
            str(tool), "--fixture-binary", "/mock/production-service", "--root", "/mock/root"
        ]),
        mock.patch("pathlib.Path.is_file", return_value=True),
        mock.patch("pathlib.Path.is_dir", return_value=True),
        mock.patch("pathlib.Path.stat", return_value=SimpleNamespace(st_mode=0o700)),
        mock.patch("pathlib.Path.exists", return_value=False),
        mock.patch("pathlib.Path.iterdir", side_effect=lambda: iter(())),
        mock.patch("tempfile.TemporaryDirectory", return_value=temporary),
        mock.patch("subprocess.run", side_effect=refused),
        mock.patch("subprocess.Popen", side_effect=blocked_start),
    ):
        try:
            runpy.run_path(str(tool), run_name="__main__")
        except (AssertionError, RuntimeError) as failure:
            error = type(failure).__name__
    print(json.dumps({"calls": calls, "started": started, "error": error}))


class SmokeRecognitionTests(unittest.TestCase):
    def test_refusal_is_terminal_in_normal_and_optimized_interpreters(self):
        for mode, flags, optimize in [
            ("normal", [], None),
            ("dash-O", ["-O"], None),
            ("environment", [], "1"),
        ]:
            for response in ["production-refusal", "wrong-status", "unexpected-stdout"]:
                with self.subTest(mode=mode, response=response):
                    env = dict(os.environ)
                    env.pop("PYTHONOPTIMIZE", None)
                    if optimize is not None:
                        env["PYTHONOPTIMIZE"] = optimize
                    completed = subprocess.run(
                        [sys.executable, *flags, str(Path(__file__).resolve()),
                         "--tool", str(tool), "--probe", response],
                        env=env, capture_output=True, text=True, timeout=10,
                    )
                    self.assertEqual(completed.returncode, 0, completed.stderr)
                    result = json.loads(completed.stdout)
                    self.assertEqual(
                        result["calls"], [["--empty-memory-fixture", "--management-socket"]],
                        "An unrecognized executable received another probe, possibly no arguments",
                    )
                    self.assertEqual(result["started"], [], "Unrecognized executable was started")
                    self.assertIsNotNone(result["error"], "Invalid fixture response was accepted")

    def test_all_pass_fail_checks_survive_optimization(self):
        syntax = ast.parse(tool.read_text(), filename=str(tool))
        self.assertFalse(
            any(isinstance(node, ast.Assert) for node in ast.walk(syntax)),
            "Smoke pass/fail checks must not use optimization-removable assert statements",
        )


if __name__ == "__main__":
    if args.probe:
        probe()
    else:
        unittest.main(argv=[sys.argv[0]])
