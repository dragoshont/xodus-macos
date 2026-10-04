# SPDX-License-Identifier: GPL-3.0-only
"""Component-selection refusals without starting Windows or Wine."""

from pathlib import Path
import sys
import tempfile
import unittest

import check_windows_async as checks


@unittest.skipUnless(sys.platform in ("darwin", "linux"), "Requires Unix ownership metadata.")
class ComponentChecks(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="async-component-", dir=Path.cwd())
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.parent = self.root / "owned"
        self.parent.mkdir(mode=0o700)
        self.executable = self.parent / "helper.exe"
        self.library = self.parent / "xodus_async.dll"
        self.library.touch(mode=0o600)

    def test_owned_regular_sibling_is_accepted(self):
        checks.verify_component(self.root, self.executable, self.library.name)

    def test_missing_sibling_refuses(self):
        with self.assertRaises(FileNotFoundError):
            checks.verify_component(self.root, self.executable, "missing.dll")

    def test_writable_sibling_refuses(self):
        self.library.chmod(0o666)
        with self.assertRaisesRegex(RuntimeError, "non-writable"):
            checks.verify_component(self.root, self.executable, self.library.name)

    def test_aliased_sibling_refuses(self):
        alias = self.parent / "alias.dll"
        alias.symlink_to(self.library)
        with self.assertRaisesRegex(RuntimeError, "regular file"):
            checks.verify_component(self.root, self.executable, alias.name)

    def test_writable_ancestor_refuses(self):
        self.parent.chmod(0o777)
        with self.assertRaisesRegex(RuntimeError, "unsafe ancestor"):
            checks.verify_component(self.root, self.executable, self.library.name)

    def test_aliased_ancestor_refuses(self):
        alias = self.root / "alias"
        alias.symlink_to(self.parent, target_is_directory=True)
        with self.assertRaisesRegex(RuntimeError, "unsafe ancestor"):
            checks.verify_component(self.root, alias / "helper.exe", self.library.name)

    def test_wrong_root_refuses(self):
        with self.assertRaisesRegex(RuntimeError, "outside"):
            checks.verify_component(self.parent / "different", self.executable, self.library.name)


if __name__ == "__main__":
    unittest.main()
