# SPDX-License-Identifier: GPL-3.0-only
"""Exact public-source/refusal checks; no Wine, account or network operation."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import apply_to_public_shim as overlay
import build_core

import importlib.util
spec = importlib.util.spec_from_file_location(
    "rps_overlay", Path(__file__).resolve().parents[1] / "public-rps" / "apply_to_public_shim.py",
)
rps_overlay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rps_overlay)

SHIM_SOURCE = None
ASYNC_SOURCE = None


class Fixture(unittest.TestCase):
    def fixture(self, origin):
        temporary = tempfile.TemporaryDirectory(prefix="async-guard-", dir=Path.cwd())
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        source = root / "public"
        subprocess.run(
            ["git", "-c", "advice.detachedHead=false", "clone", "-q", "--shared",
             str(origin), str(source)],
            check=True,
        )
        return root, source


class ShimGuards(Fixture):
    def setUp(self):
        self.root, self.source = self.fixture(SHIM_SOURCE)
        changes = rps_overlay.prepare(self.source)
        for name, data in changes.items():
            (self.source / name).write_bytes(data)

    def unchanged(self):
        return {path.name: path.read_bytes() for path in self.source.iterdir() if path.is_file()}

    def test_exact_overlay_is_read_only_until_applied(self):
        before = self.unchanged()
        result = overlay.prepare(self.source)
        self.assertEqual(before, self.unchanged())
        self.assertEqual(set(result), {*overlay.BLOBS, "xodus_async_bridge.c", "xodus_async_bridge.h"})
        self.assertEqual(result["xthreading.c"].count(b"xodus_async_get_proc("), 23)
        self.assertIn(b"XThreadIsTimeSensitive", result["xthreading.c"])

    def test_user_failure_precedes_http_and_releases_before_completion(self):
        text = overlay.prepare(self.source)["xuser.c"].decode()
        call = text.index("get_rps_tickets( options")
        request = text.index('http_request( L"GET", L"https://title.mgt.xboxlive.com/titles/default')
        self.assertLess(call, request)
        self.assertIn("goto cleanup;", text[request:request + 220])
        start = text.index("        complete:")
        release = text.index("IUser_Release", start)
        complete = text.index("IXThreadingImpl_XAsyncComplete", start)
        self.assertLess(release, complete)
        self.assertIn("context->user = NULL;", text[release:complete])
        self.assertIn("free( impl );", text)

    def test_reapplication_refuses_without_writing(self):
        for name, data in overlay.prepare(self.source).items():
            (self.source / name).write_bytes(data)
        before = self.unchanged()
        with self.assertRaisesRegex(ValueError, "exact public/RPS"):
            overlay.prepare(self.source)
        self.assertEqual(before, self.unchanged())

    def test_changed_source_refuses_without_writing(self):
        path = self.source / "xuser.c"
        path.write_bytes(path.read_bytes() + b"\n")
        before = self.unchanged()
        with self.assertRaisesRegex(ValueError, "exact public/RPS"):
            overlay.prepare(self.source)
        self.assertEqual(before, self.unchanged())

    def test_existing_bridge_refuses_without_writing(self):
        (self.source / "xodus_async_bridge.c").write_text("owned prior content")
        before = self.unchanged()
        with self.assertRaisesRegex(ValueError, "existing bridge"):
            overlay.prepare(self.source)
        self.assertEqual(before, self.unchanged())

    def test_aliased_root_refuses(self):
        alias = self.root / "alias"
        alias.symlink_to(self.source, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, "non-aliased"):
            overlay.prepare(alias)

    def test_changed_revision_refuses(self):
        subprocess.run(
            ["git", "-C", str(self.source), "-c", "user.name=Owned fixture",
             "-c", "user.email=fixture@example.invalid", "commit", "-q", "--allow-empty",
             "-m", "Owned revision-refusal fixture"],
            check=True,
        )
        with self.assertRaisesRegex(ValueError, "different public"):
            overlay.prepare(self.source)

    def test_generated_com_wrappers_match_all_public_declarations(self):
        original = subprocess.run(
            ["git", "-C", str(self.source), "show", "HEAD:xthreading.c"],
            check=True, capture_output=True, text=True,
        ).stdout
        header = overlay.check_header(original)
        self.assertEqual(header.count("[[maybe_unused]]"), 23)
        self.assertEqual(header.count("IXThreadingImpl_X"), 23)
        with self.assertRaises(ValueError):
            overlay.check_header("")


class CoreGuards(Fixture):
    def setUp(self):
        self.root, self.source = self.fixture(ASYNC_SOURCE)
        self.dependency = json.loads((Path(__file__).parent / "dependency.json").read_text())

    def test_exact_dependency_and_license_are_accepted(self):
        self.assertEqual(build_core.checked_source(self.source, self.dependency), self.source)
        license_data = (self.source / self.dependency["licenseFile"]).read_bytes()
        blob = hashlib.sha1(b"blob " + str(len(license_data)).encode() + b"\0" + license_data).hexdigest()
        self.assertEqual(blob, self.dependency["licenseGitBlob"])

    def test_untracked_include_refuses(self):
        (self.source / "Include" / "shadow.h").write_text("untrusted")
        with self.assertRaisesRegex(ValueError, "modified"):
            build_core.checked_source(self.source, self.dependency)

    def test_ignored_include_refuses(self):
        (self.source / ".git" / "info" / "exclude").write_text("Include/shadow.h\n")
        (self.source / "Include" / "shadow.h").write_text("untrusted")
        with self.assertRaisesRegex(ValueError, "ignored"):
            build_core.checked_source(self.source, self.dependency)

    def test_changed_license_refuses(self):
        (self.source / self.dependency["licenseFile"]).write_text("modified")
        with self.assertRaises(ValueError):
            build_core.checked_source(self.source, self.dependency)

    def test_aliased_dependency_refuses(self):
        alias = self.root / "alias"
        alias.symlink_to(self.source, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, "non-aliased"):
            build_core.checked_source(alias, self.dependency)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--shim-source", required=True, type=Path)
    parser.add_argument("--async-source", required=True, type=Path)
    args, remaining = parser.parse_known_args()
    SHIM_SOURCE, ASYNC_SOURCE = args.shim_source.resolve(strict=True), args.async_source.resolve(strict=True)
    unittest.main(argv=[sys.argv[0], *remaining])
