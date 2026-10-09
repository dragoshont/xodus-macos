#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import struct
import unittest


ROOT = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("redirect_import", ROOT / "redirect-pe-import.py")
implementation = importlib.util.module_from_spec(spec)
spec.loader.exec_module(implementation)


def fixture(pointer_bytes, target_index=1):
    data = bytearray(0x800)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3c, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    optional_size = 240 if pointer_bytes == 8 else 224
    struct.pack_into("<HH", data, 0x84, 0x8664 if pointer_bytes == 8 else 0x14c, 1)
    struct.pack_into("<H", data, 0x94, optional_size)
    optional = 0x98
    struct.pack_into("<H", data, optional, 0x20b if pointer_bytes == 8 else 0x10b)
    struct.pack_into("<II", data, optional + 32, 0x1000, 0x200)
    struct.pack_into("<II", data, optional + 56, 0x2000, 0x200)
    directory = optional + (112 if pointer_bytes == 8 else 96)
    struct.pack_into("<II", data, directory + 8, 0x1000, 40)
    struct.pack_into("<I", data, directory - 4, 16)
    section = optional + optional_size
    struct.pack_into("<8sIIIIIIHHI", data, section, b".rdata", 0x600, 0x1000,
                     0x600, 0x200, 0, 0, 0, 0, 0x40000040)
    struct.pack_into("<IIIII", data, 0x200, 0x1060, 0, 0, 0x1050, 0x1090)
    data[0x250:0x25a] = b"ntdll.dll\0"
    names = ["Before", "Middle", "After"]
    names[target_index] = "RtlQueryPackageClaims"
    for index, name in enumerate(names):
        address = 0x1100 + index * 0x40
        position = 0x300 + index * 0x40
        encoded = name.encode("ascii") + b"\0"
        data[position:position + 2] = b"\0\0"
        data[position + 2:position + 2 + len(encoded)] = encoded
        fmt = "<Q" if pointer_bytes == 8 else "<I"
        struct.pack_into(fmt, data, 0x260 + index * pointer_bytes, address)
        struct.pack_into(fmt, data, 0x290 + index * pointer_bytes, address)
    return bytes(data)


def import_slots(data):
    pe = struct.unpack_from("<I", data, 0x3c)[0]
    count = struct.unpack_from("<H", data, pe + 6)[0]
    size = struct.unpack_from("<H", data, pe + 20)[0]
    optional = pe + 24
    width = 8 if struct.unpack_from("<H", data, optional)[0] == 0x20b else 4
    directory = optional + (112 if width == 8 else 96)
    sections = []
    for index in range(count):
        position = optional + size + index * 40
        va, raw_size, raw = struct.unpack_from("<III", data, position + 12)
        sections.append((va, raw, raw_size))

    def at(rva):
        for va, raw, raw_size in sections:
            if va <= rva < va + raw_size:
                return raw + rva - va
        raise ValueError("Unmapped import")

    def string(rva):
        position = at(rva)
        return data[position:data.index(b"\0", position)].decode("ascii")

    offset = at(struct.unpack_from("<I", data, directory + 8)[0])
    slots = {}
    while any(struct.unpack_from("<IIIII", data, offset)):
        original, _, _, name, iat = struct.unpack_from("<IIIII", data, offset)
        module = string(name)
        index = 0
        while True:
            entry = struct.unpack_from("<Q" if width == 8 else "<I",
                                       data, at(original + index * width))[0]
            if not entry:
                break
            name = ("#" + str(entry & 0xffff)) if entry & (1 << (width * 8 - 1)) else string(entry + 2)
            slots[iat + index * width] = (module, name)
            index += 1
        offset += 20
    return slots


class RedirectTests(unittest.TestCase):
    def test_redirects_ordinal_without_moving_iat_or_other_imports(self):
        for width in (4, 8):
            for position in (0, 1, 2):
                with self.subTest(width=width, position=position):
                    original = bytearray(fixture(width, position))
                    value = (1 << (width * 8 - 1)) | 122
                    fmt = "<Q" if width == 8 else "<I"
                    struct.pack_into(fmt, original, 0x260 + position * width, value)
                    struct.pack_into(fmt, original, 0x290 + position * width, value)
                    patched, receipt = implementation.redirect(
                        original, "ntdll.dll", "#122", "ordinal-provider.dll")
                    before, after = import_slots(original), import_slots(patched)
                    self.assertEqual(set(before), set(after))
                    for address, entry in before.items():
                        expected = ("ordinal-provider.dll", "#122") if entry[1] == "#122" else entry
                        self.assertEqual(after[address], expected)
                    self.assertEqual(receipt["original_iat_slot_rva"], receipt["replacement_iat_slot_rva"])
                    self.assertEqual(original[0x200:0x800], patched[0x200:0x800])

    def test_rejects_invalid_or_missing_ordinal(self):
        for symbol in ("#", "#0", "#65536", "#abc", "#122"):
            with self.subTest(symbol=symbol), self.assertRaises(ValueError):
                implementation.redirect(fixture(8), "ntdll.dll", symbol, "ordinal-provider.dll")

    def test_preserves_all_iat_addresses_in_both_architectures_and_positions(self):
        for width in (4, 8):
            for position in (0, 1, 2):
                with self.subTest(width=width, position=position):
                    original = fixture(width, position)
                    patched, receipt = implementation.redirect(
                        original, "ntdll.dll", "RtlQueryPackageClaims", "package-claims.dll"
                    )
                    before, after = import_slots(original), import_slots(patched)
                    self.assertEqual(set(before), set(after))
                    for address, entry in before.items():
                        expected = ("package-claims.dll", entry[1]) if entry[1] == "RtlQueryPackageClaims" else entry
                        self.assertEqual(after[address], expected)
                    self.assertEqual(receipt["original_iat_slot_rva"], receipt["replacement_iat_slot_rva"])
                    self.assertEqual(original[0x200:0x800], patched[0x200:0x800])

    def test_rejects_missing_import_and_non_pe_data(self):
        with self.assertRaises(ValueError):
            implementation.redirect(fixture(8), "ntdll.dll", "Missing", "package-claims.dll")
        with self.assertRaises(ValueError):
            implementation.redirect(b"not a Windows executable", "ntdll.dll",
                                    "RtlQueryPackageClaims", "package-claims.dll")


if __name__ == "__main__":
    unittest.main()
