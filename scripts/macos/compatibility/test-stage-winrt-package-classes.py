import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import xml.etree.ElementTree as ET


spec = importlib.util.spec_from_file_location(
    "stage_classes", Path(__file__).with_name("stage-winrt-package-classes.py"))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def package(root, name, classes, payload=b"unit-test-only provider bytes", with_provider=True):
    directory = root / name
    directory.mkdir()
    document = ET.Element("Package")
    server = ET.SubElement(document, "InProcessServer")
    ET.SubElement(server, "Path").text = "Provider.dll"
    for value in classes:
        ET.SubElement(server, "ActivatableClass", ActivatableClassId=value)
    manifest = ET.tostring(document)
    (directory / "AppxManifest.xml").write_bytes(manifest)
    files = [{"path": "AppxManifest.xml", "sha256": hashlib.sha256(manifest).hexdigest()}]
    if with_provider:
        (directory / "Provider.dll").write_bytes(payload)
        files.append({"path": "Provider.dll", "sha256": hashlib.sha256(payload).hexdigest()})
    return {"name": name, "files": files}


class StageTests(unittest.TestCase):
    def test_hash_qualified_provider_and_no_deployment_key(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            records = [package(root, "App", ["Microsoft.Sample.Class", "Windows.Management.Deployment.PackageManager"])]
            (root / "package-code-receipts.json").write_text(json.dumps(records))
            text, classes, _, _ = module.stage(root)
            self.assertIn("Microsoft.Sample.Class", classes)
            self.assertNotIn("Windows.Management.Deployment.PackageManager", text)
            (root / "App/Provider.dll").write_bytes(b"modified unit-test bytes")
            with self.assertRaises(ValueError):
                module.stage(root)

    def test_conflicts_are_reported_not_globally_selected(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            records = [package(root, "App", ["Microsoft.Sample.Shared"]),
                       package(root, "Framework", ["Microsoft.Sample.Shared"], b"different unit-test version")]
            (root / "package-code-receipts.json").write_text(json.dumps(records))
            text, classes, conflicts, _ = module.stage(root)
            self.assertNotIn("Microsoft.Sample.Shared", classes)
            self.assertNotIn("Microsoft.Sample.Shared", text)
            self.assertEqual(len(conflicts["Microsoft.Sample.Shared"]), 2)

    def test_actual_dependency_provider_and_missing_provider_errors(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            records = [package(root, "App", ["Microsoft.Sample.App"]),
                       package(root, "Framework", ["Microsoft.Sample.Framework"], with_provider=False)]
            (root / "package-code-receipts.json").write_text(json.dumps(records))
            _, classes, _, supplied = module.stage(root)
            self.assertEqual(classes["Microsoft.Sample.App"], classes["Microsoft.Sample.Framework"])
            self.assertEqual(len(supplied), 1)
            (root / "App/Provider.dll").unlink()
            with self.assertRaises(ValueError):
                module.stage(root)


if __name__ == "__main__":
    unittest.main()
