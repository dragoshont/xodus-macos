#!/usr/bin/env python3
"""Redirect one PE import without moving any existing import-address-table slot."""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def align(value, alignment):
    if not alignment or alignment & (alignment - 1):
        raise ValueError("Invalid PE alignment")
    return (value + alignment - 1) & ~(alignment - 1)


def redirect(original, module, symbol, replacement):
    ordinal = None
    if symbol.startswith("#"):
        if not symbol[1:].isdigit() or not 1 <= int(symbol[1:]) <= 65535:
            raise ValueError("Invalid imported ordinal")
        ordinal = int(symbol[1:])
    data = bytearray(original)
    if data[:2] != b"MZ":
        raise ValueError("Expected a PE image")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("Missing PE signature")
    section_count = struct.unpack_from("<H", data, pe + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    optional = pe + 24
    magic = struct.unpack_from("<H", data, optional)[0]
    if magic not in (0x10B, 0x20B):
        raise ValueError("Unsupported PE optional header")
    width = 8 if magic == 0x20B else 4
    thunk_format = "<Q" if width == 8 else "<I"
    directories = optional + (112 if width == 8 else 96)
    section_alignment, file_alignment = struct.unpack_from("<II", data, optional + 32)
    sections_start = optional + optional_size
    sections = []
    for index in range(section_count):
        offset = sections_start + index * 40
        virtual_size, va, raw_size, raw = struct.unpack_from("<IIII", data, offset + 8)
        sections.append((va, virtual_size, raw, raw_size))
    if not sections or not all(raw + size <= len(data) for _, _, raw, size in sections):
        raise ValueError("Invalid PE section boundaries")
    first_raw = min(raw for _, _, raw, size in sections if size)
    if sections_start + (section_count + 1) * 40 > first_raw:
        raise ValueError("No header space for a new import section")

    def at(rva, size=1):
        for va, virtual_size, raw, raw_size in sections:
            if va <= rva and rva + size <= va + raw_size:
                return raw + rva - va
        raise ValueError("RVA points outside file-backed sections")

    def string(rva):
        offset = at(rva)
        limit = min(len(data), offset + 4096)
        end = data.find(0, offset, limit)
        if end < 0:
            raise ValueError("Unterminated PE import name")
        return bytes(data[offset:end]).decode("ascii")

    import_rva, import_size = struct.unpack_from("<II", data, directories + 8)
    if not import_rva or import_size < 20:
        raise ValueError("Image has no usable import directory")
    descriptors = []
    offset = at(import_rva, 20)
    for index in range(4096):
        descriptor = list(struct.unpack_from("<IIIII", data, offset + index * 20))
        if not any(descriptor):
            break
        descriptors.append(descriptor)
    else:
        raise ValueError("Unterminated import directory")
    matches = []
    for index, descriptor in enumerate(descriptors):
        if string(descriptor[3]).lower() != module.lower():
            continue
        if not descriptor[0]:
            raise ValueError("Bound-only import descriptors are not supported")
        thunks = []
        for position in range(65536):
            value = struct.unpack_from(thunk_format, data, at(descriptor[0] + position * width, width))[0]
            if not value:
                break
            thunks.append(value)
            is_ordinal = bool(value & (1 << (width * 8 - 1)))
            if ((is_ordinal and ordinal is not None and value & 65535 == ordinal) or
                    (not is_ordinal and ordinal is None and string(value + 2) == symbol)):
                matches.append((index, position, thunks))
        else:
            raise ValueError("Unterminated import-name table")
        for match in matches:
            if match[0] == index:
                match[2][:] = thunks
    if len(matches) != 1:
        raise ValueError("Expected exactly one matching import, found " + str(len(matches)))
    descriptor_index, slot, thunks = matches[0]
    old = descriptors[descriptor_index]
    if not thunks or not all(isinstance(value, int) for value in thunks):
        raise ValueError("Invalid import thunk table")
    at(old[4] + slot * width, width)
    new_va = align(max(va + max(size, raw_size) for va, size, _, raw_size in sections), section_alignment)
    payload = bytearray()

    def append(content, alignment=1):
        padding = align(len(payload), alignment) - len(payload)
        payload.extend(b"\0" * padding)
        rva = new_va + len(payload)
        payload.extend(content)
        return rva

    def table(values):
        return append(b"".join(struct.pack(thunk_format, value) for value in [*values, 0]), width)

    replacement_name = append(replacement.encode("ascii") + b"\0")
    new_descriptors = []
    if slot:
        new_descriptors.append([table(thunks[:slot]), 0, 0, old[3], old[4]])
    new_descriptors.append([table([thunks[slot]]), 0, 0, replacement_name, old[4] + slot * width])
    if slot + 1 < len(thunks):
        new_descriptors.append([
            table(thunks[slot + 1:]), 0, 0, old[3], old[4] + (slot + 1) * width
        ])
    replaced = descriptors[:descriptor_index] + new_descriptors + descriptors[descriptor_index + 1:]
    new_import_rva = append(
        b"".join(struct.pack("<IIIII", *entry) for entry in [*replaced, [0] * 5]), 4
    )
    raw = align(len(data), file_alignment)
    raw_size = align(len(payload), file_alignment)
    data.extend(b"\0" * (raw - len(data)))
    data.extend(payload)
    data.extend(b"\0" * (raw_size - len(payload)))
    header = sections_start + section_count * 40
    struct.pack_into("<8sIIIIIIHHI", data, header, b".xboximp",
        len(payload), new_va, raw_size, raw, 0, 0, 0, 0, 0x40000040)
    struct.pack_into("<H", data, pe + 6, section_count + 1)
    struct.pack_into("<I", data, optional + 56, align(new_va + len(payload), section_alignment))
    struct.pack_into("<I", data, optional + 64, 0)
    struct.pack_into("<II", data, directories + 8, new_import_rva, (len(replaced) + 1) * 20)
    # The modified lab copy must not advertise the original Authenticode or bound-import data.
    struct.pack_into("<II", data, directories + 4 * 8, 0, 0)
    struct.pack_into("<II", data, directories + 11 * 8, 0, 0)
    return bytes(data), {
        "module": module, "symbol": symbol, "replacement_module": replacement,
        "original_iat_slot_rva": old[4] + slot * width,
        "replacement_iat_slot_rva": old[4] + slot * width,
        "original_thunk_count": len(thunks), "pointer_bytes": width,
        "all_other_iat_slot_addresses_preserved": True,
        "modified_copy_is_not_vendor_signed": True,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--module", required=True)
    parser.add_argument("--symbol", required=True)
    parser.add_argument("--replacement", required=True)
    args = parser.parse_args()
    for value in (args.module, args.symbol, args.replacement):
        if not value or any(char in value for char in ("/", "\\", "\0")):
            raise ValueError("Expected a DLL basename or import symbol")
        value.encode("ascii")
    if args.destination.exists() or args.source.resolve() == args.destination.resolve():
        raise ValueError("Refusing to replace the original or an existing modified copy")
    original = args.source.read_bytes()
    output, receipt = redirect(original, args.module, args.symbol, args.replacement)
    with args.destination.open("xb") as stream:
        stream.write(output)
    receipt.update(original_sha256=hashlib.sha256(original).hexdigest(),
                   modified_sha256=hashlib.sha256(output).hexdigest())
    print(json.dumps(receipt, indent=2))


if __name__ == "__main__":
    main()
