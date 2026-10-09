import ctypes
import importlib.util
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET

spec = importlib.util.spec_from_file_location(
    "installation_default",
    Path(__file__).with_name("extract-coremessaging-install-default.py"),
)
extractor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(extractor)


class InstallationDefaultTests(unittest.TestCase):
    def setUp(self):
        api = ctypes.WinDLL("advapi32.dll", use_last_error=True)
        api.ConvertStringSecurityDescriptorToSecurityDescriptorW.argtypes = [
            ctypes.c_wchar_p, ctypes.c_ulong, ctypes.POINTER(ctypes.c_void_p),
            ctypes.POINTER(ctypes.c_ulong),
        ]
        out = ctypes.c_void_p()
        length = ctypes.c_ulong()
        if not api.ConvertStringSecurityDescriptorToSecurityDescriptorW(
            "D:(A;;0x1;;;WD)", 1, ctypes.byref(out), ctypes.byref(length)
        ):
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            self.descriptor = ctypes.string_at(out.value, length.value)
        finally:
            free = ctypes.WinDLL("kernel32.dll").LocalFree
            free.argtypes = [ctypes.c_void_p]
            free(out)

    def manifest(self):
        root = ET.Element("assembly")
        ET.SubElement(root, "assemblyIdentity", name="Microsoft-OneCore-CoreMessaging",
                      version="10.0.26100.9444")
        key = ET.SubElement(root, "registryKey", keyName=(
            r"HKEY_LOCAL_MACHINE\Software\Microsoft\SecurityManager\TransientObjects"
            r"\%5C%5C.%5CAlpcPort%5CWM_RegistrarServer"
        ))
        ET.SubElement(key, "registryValue", name="SecurityDescriptor",
                      valueType="REG_BINARY", value=self.descriptor.hex())
        return root, key

    def component(self):
        return SimpleNamespace(VS_FIXEDFILEINFO=[
            SimpleNamespace(FileVersionMS=10 << 16, FileVersionLS=26100 << 16 | 9444)
        ])

    def test_exact_matching_default(self):
        root, _ = self.manifest()
        with patch.object(extractor.pefile, "PE", return_value=self.component()) as pe:
            _, _, data = extractor.extract_default(root, b"immutable-component")
        pe.assert_called_once_with(data=b"immutable-component")
        self.assertEqual(data, self.descriptor)

    def test_duplicate_entry_rejected(self):
        root, key = self.manifest()
        ET.SubElement(key, "registryValue", name="SecurityDescriptor",
                      valueType="REG_BINARY", value=self.descriptor.hex())
        with patch.object(extractor.pefile, "PE", return_value=self.component()):
            with self.assertRaisesRegex(ValueError, "exactly one"):
                extractor.extract_default(root, b"component")

    def test_version_mismatch_rejected(self):
        root, _ = self.manifest()
        root[0].set("version", "10.0.26100.1")
        with patch.object(extractor.pefile, "PE", return_value=self.component()):
            with self.assertRaisesRegex(ValueError, "does not match"):
                extractor.extract_default(root, b"component")

    def test_truncated_ace_rejected(self):
        data = bytearray(self.descriptor)
        acl = int.from_bytes(data[16:20], "little")
        data[acl + 8 + 2:acl + 8 + 4] = (65535).to_bytes(2, "little")
        with self.assertRaisesRegex(ValueError, "ACE extent"):
            extractor.validate_portable_descriptor(bytes(data))

    def test_truncated_sid_rejected(self):
        data = bytearray(self.descriptor)
        acl = int.from_bytes(data[16:20], "little")
        data[acl + 8 + 8 + 1] = 15
        with self.assertRaisesRegex(ValueError, "SID extent"):
            extractor.validate_portable_descriptor(bytes(data))

    def test_machine_account_sid_rejected(self):
        data = bytearray(32)
        data[0] = 1
        data[2:4] = (0x8000).to_bytes(2, "little")
        data[4:8] = (20).to_bytes(4, "little")
        data[20:28] = bytes([1, 1, 0, 0, 0, 0, 0, 5])
        data[28:32] = (21).to_bytes(4, "little")
        with self.assertRaisesRegex(ValueError, "not portable"):
            extractor.validate_portable_descriptor(bytes(data))


if __name__ == "__main__":
    unittest.main()
