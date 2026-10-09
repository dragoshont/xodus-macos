#!/usr/bin/env python3
"""Verify CODE ONLY and register an explicit local-developer activation catalog."""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import stat
import struct
import subprocess
import xml.etree.ElementTree as ET

ALPHABET = "0123456789abcdefghjkmnpqrstvwxyz"
MAX_CATALOG = 4 * 1024 * 1024


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for data in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(data)
    return h.digest()


def publisher_id(publisher):
    bits = "".join(f"{b:08b}" for b in hashlib.sha256(publisher.encode("utf-16le")).digest()[:8])
    return "".join(ALPHABET[int(bits[i:i + 5].ljust(5, "0"), 2)] for i in range(0, 64, 5))


def safe_file(root, relative):
    relative = relative.replace("\\", "/")
    path = Path(relative)
    if path.is_absolute() or ".." in path.parts or not path.parts:
        raise ValueError("unsafe package-relative path")
    result = root / path
    if result.is_symlink() or not result.is_file() or result.resolve().is_relative_to(root) is False:
        raise ValueError("missing/symlinked/out-of-root code file: " + relative)
    return result


def text(value):
    data = value.encode("utf-8")
    if not data or len(data) > 4096 or b"\0" in data:
        raise ValueError("invalid catalog string")
    return struct.pack("<I", len(data)) + data


def version(value):
    parts = tuple(int(v) for v in value.split("."))
    if len(parts) != 4 or any(v < 0 or v > 65535 for v in parts):
        raise ValueError("invalid package version")
    return parts


def verify_package(root, receipt, audit_dir, openssl):
    manifest = safe_file(root, "AppxManifest.xml")
    blockmap = safe_file(root, "AppxBlockMap.xml")
    signature = safe_file(root, "AppxSignature.p7x")
    xml = ET.parse(manifest).getroot()
    identity = xml.find("{*}Identity").attrib
    if identity["Name"] != receipt["name"] or identity["Version"] != receipt["version"]:
        raise ValueError("receipt/manifest identity mismatch")
    if identity.get("ProcessorArchitecture") != "x64":
        raise ValueError("only actual x64 packages are admitted")
    files = []
    receipt_names = set()
    for record in receipt["files"]:
        path = safe_file(root, record["path"])
        actual = digest(path)
        if path.stat().st_size != record["bytes"] or actual.hex() != record["sha256"]:
            raise ValueError("code receipt mismatch: " + str(path))
        receipt_names.add(record["path"].replace("\\", "/"))
        files.append((str(path), actual))
    bm = ET.parse(blockmap).getroot()
    if bm.get("HashMethod") != "http://www.w3.org/2001/04/xmlenc#sha256":
        raise ValueError("unsupported BlockMap hash algorithm")
    for item in bm.findall("{*}File"):
        relative = item.get("Name").replace("\\", "/")
        path = safe_file(root, relative)
        if relative not in receipt_names or path.stat().st_size != int(item.get("Size")):
            raise ValueError("BlockMap/receipt file mismatch: " + relative)
        with path.open("rb") as stream:
            for block in item.findall("{*}Block"):
                if hashlib.sha256(stream.read(65536)).digest() != base64.b64decode(block.get("Hash"), validate=True):
                    raise ValueError("BlockMap block mismatch: " + relative)
            if stream.read(1):
                raise ValueError("BlockMap omitted bytes: " + relative)
    raw = signature.read_bytes()
    if raw[:4] != b"PKCX":
        raise ValueError("not an AppX PKCS7 signature")
    der = audit_dir / (identity["Name"] + ".signature.der")
    signed = audit_dir / (identity["Name"] + ".signed-content")
    der.write_bytes(raw[4:])
    result = subprocess.run([openssl, "smime", "-verify", "-noverify", "-inform", "DER",
                             "-in", str(der), "-out", str(signed)],
                            capture_output=True, text=True)
    if result.returncode:
        raise ValueError("PKCS7 cryptographic verification failed: " + result.stderr[-1500:])
    content = signed.read_bytes()
    marker = content.find(b"AXBM")
    if marker < 0 or content[marker + 4:marker + 36] != digest(blockmap):
        raise ValueError("signed AppX digest does not bind this BlockMap")
    pub = publisher_id(identity["Publisher"])
    family = identity["Name"] + "_" + pub
    full = "_".join((identity["Name"], identity["Version"], "x64",
                     identity.get("ResourceId", ""), pub))
    return {"identity": identity, "family": family, "full": full, "xml": xml,
            "root": root, "files": files, "manifest_sha256": digest(manifest).hex(),
            "blockmap_sha256": digest(blockmap).hex(),
            "signature_sha256": digest(signature).hex(),
            "pkcs7_signature_math_verified": True,
            "signed_blockmap_and_all_block_files_verified": True,
            "signer_chain_trust_verified": False,
            "original_zip_AXPC_AXCD_AXCT_verified": False}


def register(args):
    source = Path(args.code_root).resolve()
    target = Path(args.catalog_dir).resolve()
    target.mkdir(parents=True, exist_ok=True)
    os.chmod(target, 0o700)
    audit = target / "provenance"
    audit.mkdir(exist_ok=True)
    receipts = json.loads(Path(args.receipts).read_text())
    packages = {}
    for receipt in receipts:
        root = (source / receipt["name"]).resolve()
        packages[receipt["name"]] = verify_package(root, receipt, audit, args.openssl)
    selected = packages[args.package]
    for dep in selected["xml"].findall("{*}Dependencies/{*}PackageDependency"):
        actual = packages.get(dep.get("Name"))
        if actual is None or actual["identity"]["Publisher"] != dep.get("Publisher") or \
                version(actual["identity"]["Version"]) < version(dep.get("MinVersion")):
            raise ValueError("registered dependency closure is missing/mismatched")
    app = next((v for v in selected["xml"].findall("{*}Applications/{*}Application")
                if v.get("Id") == args.app), None)
    if app is None or app.get("EntryPoint") == "Windows.FullTrustApplication":
        raise ValueError("selected app absent or outside bounded UWP-identity activation scope")
    executable = safe_file(selected["root"], app.get("Executable"))
    data = executable.read_bytes()[:4096]
    pe = struct.unpack_from("<I", data, 60)[0]
    if data[:2] != b"MZ" or data[pe:pe + 4] != b"PE\0\0" or \
            struct.unpack_from("<H", data, pe + 4)[0] != 0x8664:
        raise ValueError("selected declared executable is not actual AMD64 PE")
    unix_path = str(executable)
    # Prefix drive y: must map to the immutable parent of the root supplied here.
    mapped = Path(args.dos_mapping_root).resolve()
    if not executable.is_relative_to(mapped):
        raise ValueError("selected executable is outside declared DOS mapping")
    nt_image = "\\??\\" + args.drive.upper() + ":\\" + str(executable.relative_to(mapped)).replace("/", "\\")
    files = [v for p in packages.values() for v in p["files"]]
    body = b"".join(text(v) for v in (selected["full"], args.app, selected["family"], nt_image))
    body += struct.pack("<I", len(files))
    for path, sha in files:
        body += text(path) + sha
    blob = b"XPA1" + struct.pack("<III", 1, len(body), 0) + body
    if len(blob) > MAX_CATALOG or len(files) > 8192:
        raise ValueError("catalog resource bound exceeded")
    temporary = target / "catalog.new"
    temporary.write_bytes(blob)
    os.chmod(temporary, 0o600)
    os.replace(temporary, target / "catalog.bin")
    report = {"kind": "explicit-local-developer-registration", "origin": "DeveloperUnsigned",
              "origin_value": 4, "store_installation": False, "authenticated_user": False,
              "capability_or_privilege_grants": [],
              "package_full_name": selected["full"], "app_id": args.app,
              "package_family_name": selected["family"], "nt_image": nt_image,
              "catalog_sha256": hashlib.sha256(blob).hexdigest(), "receipt_files": len(files),
              "packages": [{k: v for k, v in p.items() if k not in ("xml", "root", "files")}
                           for p in packages.values()]}
    (target / "registration.json").write_text(json.dumps(report, indent=2))
    print(json.dumps({k: v for k, v in report.items() if k != "packages"}, indent=2))


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--code-root", required=True)
    p.add_argument("--receipts", required=True)
    p.add_argument("--catalog-dir", required=True)
    p.add_argument("--package", required=True)
    p.add_argument("--app", required=True)
    p.add_argument("--dos-mapping-root", required=True)
    p.add_argument("--drive", default="Y")
    p.add_argument("--openssl", default="/opt/homebrew/opt/openssl@3/bin/openssl")
    args = p.parse_args()
    if len(args.drive) != 1 or not args.drive.isalpha():
        p.error("one drive letter required")
    register(args)


if __name__ == "__main__":
    main()
