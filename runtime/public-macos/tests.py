# SPDX-License-Identifier: GPL-3.0-only
"""Actual patch/application checks using only a supplied public Git source."""

import argparse
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from apply_platform_fix import SOURCE_BLOBS, WINE_REVISION


PUBLIC_SOURCE = None
APPLICATOR = Path(__file__).with_name("apply_platform_fix.py").resolve()


class PlatformApplicationChecks(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="xodus-public-platform-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve(strict=True)
        self.checkout = self.root / "checkout"
        shallow = self.git("-C", str(PUBLIC_SOURCE),
                           "rev-parse", "--is-shallow-repository").stdout.strip()
        clone_options = (
            ["--no-local", "--depth=1", "--filter=blob:none"]
            if shallow == "true" else ["--shared"]
        )
        self.git("clone", "--quiet", "--no-checkout", *clone_options,
                 str(PUBLIC_SOURCE), str(self.checkout))
        self.git("-C", str(self.checkout), "sparse-checkout", "init", "--no-cone")
        self.git("-C", str(self.checkout), "sparse-checkout", "set", "--no-cone",
                 "--stdin", input="".join(f"/{name}\n" for name in SOURCE_BLOBS))
        self.git("-C", str(self.checkout), "checkout", "--quiet", "--detach", WINE_REVISION)

    def git(self, *arguments, input=None):
        result = subprocess.run(
            ["git", *arguments], input=input, text=True,
            capture_output=True,
        )
        if result.returncode:
            self.fail(f"Public fixture Git command failed: {result.stderr.strip()}")
        return result

    def apply(self, checkout=None, write=False):
        arguments = [sys.executable, str(APPLICATOR), str(checkout or self.checkout)]
        if write:
            arguments.append("--apply")
        return subprocess.run(arguments, text=True, capture_output=True)

    def snapshot(self):
        return {name: (self.checkout / name).read_bytes() for name in SOURCE_BLOBS}

    def test_dry_run_preserves_every_checked_source(self):
        before = self.snapshot()
        result = self.apply()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(before, self.snapshot())

    def test_applies_all_files_and_refuses_repeat_without_mutation(self):
        before = self.snapshot()
        result = self.apply(write=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        after = self.snapshot()
        self.assertTrue(all(before[name] != after[name] for name in SOURCE_BLOBS))
        self.git("-C", str(self.checkout), "diff", "--check")
        result = self.apply(write=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(after, self.snapshot())

    def test_changed_source_refused_before_any_write(self):
        original = self.snapshot()
        for name in SOURCE_BLOBS:
            with self.subTest(source=name):
                selected = self.checkout / name
                with selected.open("a", encoding="utf-8") as source:
                    source.write("\n")
                before = self.snapshot()
                result = self.apply(write=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(before, self.snapshot())
                selected.write_bytes(original[name])

    def test_wrong_revision_refused_before_any_write(self):
        self.git("-C", str(self.checkout), "-c", "user.name=Public fixture",
                 "-c", "user.email=fixture@example.invalid", "commit", "--quiet",
                 "--allow-empty", "-m", "Isolated wrong-revision fixture")
        before = self.snapshot()
        result = self.apply(write=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(before, self.snapshot())

    def test_selected_checkout_and_ancestor_aliases_refused(self):
        before = self.snapshot()
        alias = self.root / "checkout-alias"
        alias.symlink_to(self.checkout, target_is_directory=True)
        self.assertNotEqual(self.apply(alias, write=True).returncode, 0)
        parent_alias = self.root / "parent-alias"
        parent_alias.symlink_to(self.root, target_is_directory=True)
        self.assertNotEqual(
            self.apply(parent_alias / "checkout", write=True).returncode, 0
        )
        self.assertEqual(before, self.snapshot())

    def test_source_alias_refused_before_any_write(self):
        first = self.checkout / next(iter(SOURCE_BLOBS))
        target = self.root / "original-source"
        first.rename(target)
        first.symlink_to(target)
        before = self.snapshot()
        result = self.apply(write=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(first.is_symlink())
        self.assertEqual(before, self.snapshot())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("public_source", type=Path)
    args = parser.parse_args()
    PUBLIC_SOURCE = args.public_source.resolve(strict=True)
    unittest.main(argv=[sys.argv[0]], verbosity=2)
