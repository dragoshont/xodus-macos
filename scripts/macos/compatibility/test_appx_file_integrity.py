import base64
import hashlib
from pathlib import Path
import tempfile
import unittest
import xml.etree.ElementTree as ET

from appx_file_integrity import BLOCK, FILE_HASH, verify_plaintext


class PlaintextIntegrityTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.path = Path(self.directory.name) / "image"
        self.content = b"authentic plaintext" * 6000
        self.path.write_bytes(self.content)

    def item(self, encrypted=False):
        item = ET.Element("File", Name="image", Size=str(len(self.content)),
                          Encrypted="true" if encrypted else "false")
        for offset in range(0, len(self.content), 65536):
            chunk = self.content[offset:offset + 65536]
            if encrypted:
                chunk = b"ciphertext fixture"
            ET.SubElement(item, BLOCK, Hash=self.digest(chunk))
        ET.SubElement(item, FILE_HASH, Hash=self.digest(self.content))
        return item

    @staticmethod
    def digest(value):
        return base64.b64encode(hashlib.sha256(value).digest()).decode("ascii")

    def test_encrypted_plaintext_uses_signed_file_hash(self):
        verify_plaintext(self.path, self.item(True))

    def test_unencrypted_blocks_and_file_hash(self):
        verify_plaintext(self.path, self.item())

    def test_encrypted_tampering_is_rejected(self):
        item = self.item(True)
        self.path.write_bytes(b"x" * len(self.content))
        with self.assertRaisesRegex(ValueError, "FileHash mismatch"):
            verify_plaintext(self.path, item)

    def test_encrypted_missing_plaintext_hash_is_rejected(self):
        item = self.item(True)
        item.remove(item.find(FILE_HASH))
        with self.assertRaisesRegex(ValueError, "lacks signed"):
            verify_plaintext(self.path, item)

    def test_zero_byte_encrypted_file_requires_no_plaintext_hash(self):
        self.path.write_bytes(b"")
        item = ET.Element("File", Name="empty", Size="0", Encrypted="true")
        verify_plaintext(self.path, item)

    def test_unencrypted_bad_blocks_are_rejected(self):
        item = self.item()
        item.find(BLOCK).set("Hash", self.digest(b"incorrect"))
        with self.assertRaisesRegex(ValueError, "block mismatch"):
            verify_plaintext(self.path, item)

    def test_invalid_hash_and_flag_are_rejected(self):
        item = self.item(True)
        item.find(FILE_HASH).set("Hash", "eA==")
        with self.assertRaisesRegex(ValueError, "32 bytes"):
            verify_plaintext(self.path, item)
        item.set("Encrypted", "unknown")
        with self.assertRaisesRegex(ValueError, "encryption flag"):
            verify_plaintext(self.path, item)

    def test_omitted_blocks_and_wrong_size_are_rejected(self):
        item = self.item()
        item.remove(item.findall(BLOCK)[-1])
        with self.assertRaisesRegex(ValueError, "omitted"):
            verify_plaintext(self.path, item)
        item.set("Size", "1")
        with self.assertRaisesRegex(ValueError, "size mismatch"):
            verify_plaintext(self.path, item)


if __name__ == "__main__":
    unittest.main()
