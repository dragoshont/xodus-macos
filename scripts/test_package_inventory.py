import json
from contextlib import closing
from pathlib import Path
import sqlite3
import tempfile
import unittest

from package_inventory import build_database, project


def product(**changes):
    row = dict(id="9NK9K07FDPJV", title="Synthetic game", fmt="EAppxBundle",
               arch="x64", fullTrust=True, customInstall=False,
               fw="Microsoft.VCLibs.140.00.UWPDesktop", drivers=0, attrs="")
    row.update(changes)
    return row


class InventoryTests(unittest.TestCase):
    def test_encryption_is_not_runtime_type(self):
        self.assertEqual(project([product()])[0][-1], "declares-full-trust")
        self.assertEqual(project([product(fullTrust=False, fw="")])[0][-1], "runtime-unknown")
        self.assertEqual(project([product(fullTrust=False, fw="Microsoft.NET.Native.Runtime.2.2")])[0][-1],
                         "declares-dotnet-native")

    def test_no_package_is_not_a_third_party_launcher_claim(self):
        self.assertEqual(project([product(fmt="(none)")])[0][-1], "no-desktop-package-in-snapshot")

    def test_invalid_snapshot_fails(self):
        for rows in ([product(), product()], [product(fullTrust="false")],
                     [product(drivers=-1)], [product(id="../bad")]):
            with self.assertRaises(ValueError):
                project(rows)

    def test_database_preserves_evidence_and_drops_private_fields(self):
        with tempfile.TemporaryDirectory() as root:
            output = Path(root) / "inventory.sqlite"
            evidence = [dict(product_id="9NK9K07FDPJV", kind="catalog-declaration",
                             runtime_type="contains-desktop-process", source="synthetic catalog",
                             detail="fullTrustProcess extension", observed_at="2026-10-10")]
            self.assertEqual(build_database(
                [product(KeyId="DO_NOT_KEEP", PackageUri="DO_NOT_KEEP")],
                output, "GB", "2026-10-10", evidence), 1)
            with closing(sqlite3.connect(output)) as db:
                self.assertEqual(db.execute("SELECT count(*) FROM encrypted_desktop_candidates").fetchone()[0], 1)
                self.assertEqual(db.execute("SELECT runtime_type FROM runtime_evidence").fetchone()[0],
                                 "contains-desktop-process")
                self.assertNotIn("DO_NOT_KEEP", "\n".join(db.iterdump()))
                self.assertEqual(json.loads(db.execute("SELECT frameworks_json FROM products").fetchone()[0]),
                                 ["Microsoft.VCLibs.140.00.UWPDesktop"])
            with self.assertRaises(FileExistsError):
                build_database([product()], output, "GB", "2026-10-10")

    def test_invalid_evidence_does_not_create_output(self):
        with tempfile.TemporaryDirectory() as root:
            output = Path(root) / "inventory.sqlite"
            with self.assertRaises(ValueError):
                build_database([product()], output, "GB", "2026-10-10", [{}])
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
