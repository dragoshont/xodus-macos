"""Extract one portable default from matching component installation metadata.

This does not read Windows registry policy or provision a Wine prefix.
"""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import xml.etree.ElementTree as ET

import pefile


class DeltaInput(ctypes.Structure):
    _fields_ = [
        ("start", ctypes.c_void_p),
        ("size", ctypes.c_size_t),
        ("editable", ctypes.c_int),
    ]


class DeltaOutput(ctypes.Structure):
    _fields_ = [("start", ctypes.c_void_p), ("size", ctypes.c_size_t)]


def decode_manifest(data, dictionary_data):
    pe = pefile.PE(data=dictionary_data)
    resources = [
        language.data.struct
        for kind in pe.DIRECTORY_ENTRY_RESOURCE.entries if kind.id == 614
        for name in kind.directory.entries if name.id == 1
        for language in name.directory.entries
    ]
    if len(resources) != 1:
        raise ValueError("Expected exactly one servicing manifest dictionary")
    resource = resources[0]
    dictionary_bytes = pe.get_data(resource.OffsetToData, resource.Size)
    if data[:4] != b"DCM\x01" or data[4:8] != b"PA30":
        raise ValueError("Expected the identified DCM/PA30 component format")
    dictionary = ctypes.create_string_buffer(dictionary_bytes)
    delta = ctypes.create_string_buffer(data[4:])
    api = ctypes.WinDLL("msdelta.dll", use_last_error=True)
    api.ApplyDeltaB.argtypes = [
        ctypes.c_longlong, DeltaInput, DeltaInput, ctypes.POINTER(DeltaOutput)
    ]
    api.ApplyDeltaB.restype = ctypes.c_int
    api.DeltaFree.argtypes = [ctypes.c_void_p]
    output = DeltaOutput()
    if not api.ApplyDeltaB(
        0, DeltaInput(ctypes.addressof(dictionary), len(dictionary_bytes), 0),
        DeltaInput(ctypes.addressof(delta), len(data) - 4, 0),
        ctypes.byref(output),
    ):
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        return ET.fromstring(ctypes.string_at(output.start, output.size))
    finally:
        api.DeltaFree(output.start)


def validate_portable_descriptor(data):
    api = ctypes.WinDLL("advapi32.dll", use_last_error=True)
    api.IsValidSecurityDescriptor.argtypes = [ctypes.c_void_p]
    api.IsValidSecurityDescriptor.restype = ctypes.c_int
    if len(data) < 20 or not int.from_bytes(data[2:4], "little") & 0x8000:
        raise ValueError("Expected a self-relative component default")
    # Reject machine/domain-account identities, without interpreting their policy.
    for offset in [int.from_bytes(data[i:i + 4], "little") for i in (4, 8)]:
        if offset:
            validate_sid(data, offset, len(data))
    for field in (12, 16):
        offset = int.from_bytes(data[field:field + 4], "little")
        if not offset:
            continue
        if offset + 8 > len(data):
            raise ValueError("Truncated default ACL")
        end = offset + int.from_bytes(data[offset + 2:offset + 4], "little")
        count = int.from_bytes(data[offset + 4:offset + 6], "little")
        if end > len(data):
            raise ValueError("Default ACL exceeds its descriptor")
        cursor = offset + 8
        for _ in range(count):
            if cursor + 8 > end:
                raise ValueError("Truncated default ACE")
            size = int.from_bytes(data[cursor + 2:cursor + 4], "little")
            if size < 16 or cursor + size > end:
                raise ValueError("Invalid default ACE extent")
            if data[cursor] not in (0, 1, 2, 17):
                raise ValueError("Unqualified default ACE type")
            validate_sid(data, cursor + 8, cursor + size)
            cursor += size
    buffer = ctypes.create_string_buffer(data)
    if not api.IsValidSecurityDescriptor(buffer):
        raise ValueError("Component default is not a valid security descriptor")


def validate_sid(data, offset, end):
    if offset + 8 > end:
        raise ValueError("Truncated default SID")
    count = data[offset + 1]
    if data[offset] != 1 or offset + 8 + 4 * count > end:
        raise ValueError("Invalid default SID extent")
    authority = int.from_bytes(data[offset + 2:offset + 8], "big")
    if authority == 5 and count and int.from_bytes(data[offset + 8:offset + 12], "little") == 21:
        raise ValueError("Machine/domain account identity is not portable installation data")


def extract_default(root, component_data):
    identities = [e for e in root if e.tag.split("}")[-1] == "assemblyIdentity"]
    if len(identities) != 1:
        raise ValueError("Missing component identity")
    identity = identities[0].attrib
    component = pefile.PE(data=component_data)
    fixed = component.VS_FIXEDFILEINFO[0]
    version = ".".join(str(n) for n in (
        fixed.FileVersionMS >> 16, fixed.FileVersionMS & 0xffff,
        fixed.FileVersionLS >> 16, fixed.FileVersionLS & 0xffff,
    ))
    if identity.get("name", "").lower() != "microsoft-onecore-coremessaging" or identity.get("version") != version:
        raise ValueError("Installation manifest does not match the actual component")
    key = (
        r"HKEY_LOCAL_MACHINE\Software\Microsoft\SecurityManager\TransientObjects"
        r"\%5C%5C.%5CAlpcPort%5CWM_RegistrarServer"
    )
    values = [
        value for element in root.iter() if element.attrib.get("keyName") == key
        for value in element if value.attrib.get("name") == "SecurityDescriptor"
    ]
    if len(values) != 1 or values[0].attrib.get("valueType") != "REG_BINARY":
        raise ValueError("Expected exactly one installer-owned binary registrar default")
    data = bytes.fromhex(values[0].attrib["value"].replace(",", ""))
    validate_portable_descriptor(data)
    return identity, key, data


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--dictionary-dll", type=Path, required=True)
    parser.add_argument("--component-dll", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise FileExistsError("Refusing to overwrite an existing installation artifact")
    manifest_data = args.manifest.read_bytes()
    dictionary_data = args.dictionary_dll.read_bytes()
    component_data = args.component_dll.read_bytes()
    root = decode_manifest(manifest_data, dictionary_data)
    identity, key, data = extract_default(root, component_data)
    artifact = {
        "source": "component-installation-manifest-not-live-registry",
        "component_identity": identity,
        "manifest_sha256": hashlib.sha256(manifest_data).hexdigest(),
        "component_sha256": hashlib.sha256(component_data).hexdigest(),
        "dictionary_sha256": hashlib.sha256(dictionary_data).hexdigest(),
        "key": key,
        "value_name": "SecurityDescriptor",
        "value_type": "REG_BINARY",
        "value_sha256": hashlib.sha256(data).hexdigest(),
        "value_hex": data.hex(),
        "machine_account_identities": False,
    }
    args.output.write_text(json.dumps(artifact, indent=2), encoding="utf-8")
    print(f"Extracted one matching portable installation default: {len(data)} bytes")


if __name__ == "__main__":
    main()
