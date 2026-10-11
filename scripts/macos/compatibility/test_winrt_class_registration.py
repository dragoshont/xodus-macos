from pathlib import Path
import tempfile
import unittest

from winrt_class_registration import ACTIVATABLE_CLASS_ROOT, RegistrationError, registration_plan

PACKAGE = "Y:\\xodus-app-tooling\\cuphead\\decoded-attempt1"
CLASSES = {
    "Windows.ApplicationModel.Resources.Core.ResourceContext": "MrmCoreR.dll",
    "Windows.ApplicationModel.Resources.Core.ResourceManager": "MrmCoreR.dll",
    "Windows.Graphics.Display.DisplayInformation": "Windows.Graphics.dll",
}


class RegistrationPlanTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.drive_c = Path(self.directory.name)
        self.system32 = self.drive_c / "windows" / "system32"
        self.system32.mkdir(parents=True)

    def test_resource_classes_use_single_system32_provider(self):
        (self.system32 / "MrmCoreR.dll").write_bytes(b"provider")
        plan = dict(registration_plan(self.drive_c, PACKAGE, CLASSES))
        for name in ("ResourceContext", "ResourceManager"):
            key = ACTIVATABLE_CLASS_ROOT + "Windows.ApplicationModel.Resources.Core." + name
            self.assertEqual(plan[key], "C:\\windows\\system32\\mrmcorer.dll")
        self.assertNotIn(PACKAGE + "\\MrmCoreR.dll", plan.values())

    def test_unlisted_class_stays_package_local(self):
        (self.system32 / "mrmcorer.dll").write_bytes(b"provider")
        plan = dict(registration_plan(self.drive_c, PACKAGE, CLASSES))
        self.assertEqual(plan[ACTIVATABLE_CLASS_ROOT + "Windows.Graphics.Display.DisplayInformation"],
                         PACKAGE + "\\Windows.Graphics.dll")

    def test_missing_system_provider_refuses_package_fallback(self):
        with self.assertRaisesRegex(RegistrationError, "absent"):
            registration_plan(self.drive_c, PACKAGE, CLASSES)

    def test_mismatched_library_refused(self):
        (self.system32 / "mrmcorer.dll").write_bytes(b"provider")
        with self.assertRaisesRegex(RegistrationError, "served by"):
            registration_plan(self.drive_c, PACKAGE,
                              {"Windows.ApplicationModel.Resources.Core.ResourceManager": "other.dll"})


if __name__ == "__main__":
    unittest.main()
