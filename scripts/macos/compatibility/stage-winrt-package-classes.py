#!/usr/bin/env python3
"""Stage WinRT class mappings from verified original package code, not package identity."""
import argparse
import hashlib
import json
from pathlib import Path, PureWindowsPath
import xml.etree.ElementTree as ET


def local_name(tag):
    return tag.rsplit("}", 1)[-1]


def registry_text(value):
    if "\0" in value or "\n" in value or "\r" in value:
        raise ValueError("Invalid registry string")
    return value.replace("\\", "\\\\").replace('"', '\\"')


def stage(root):
    root = root.resolve(strict=True)
    receipts = json.loads((root / "package-code-receipts.json").read_text())
    classes = {}
    conflicts = {}
    supplied_by_dependency = {}
    for package in receipts:
        if (Path(package["name"]).name != package["name"]
            or package["name"] in (".", "..")
            or any(char in package["name"] for char in ("\\", ":", "\0"))):
            raise ValueError("Invalid package-code directory name")
        package_root = root / package["name"]
        if package_root.is_symlink() or not package_root.resolve(strict=True).is_relative_to(root):
            raise ValueError("Package code escapes the verified staging directory")
        expected = {file["path"].casefold(): file["sha256"] for file in package["files"]}
        manifest = package_root / "AppxManifest.xml"
        if hashlib.sha256(manifest.read_bytes()).hexdigest() != expected.get("appxmanifest.xml"):
            raise ValueError("Original package manifest no longer matches its captured code receipt")
        for server in ET.parse(manifest).getroot().iter():
            if local_name(server.tag) != "InProcessServer":
                continue
            relevant = [
                node for node in server.iter()
                if local_name(node.tag) == "ActivatableClass"
                and node.attrib.get("ActivatableClassId", "").startswith(
                    ("Microsoft.", "XboxPcApp", "ReactNative", "Lottie"))
            ]
            if not relevant:
                continue
            path_node = next((node for node in server if local_name(node.tag) == "Path"), None)
            if path_node is None or not path_node.text:
                continue
            declared = PureWindowsPath(path_node.text)
            if declared.is_absolute() or declared.drive or ".." in declared.parts:
                raise ValueError("Package declared an unsafe activation-server path")
            relative = "/".join(declared.parts)
            path = package_root.joinpath(*declared.parts)
            if not path.resolve().is_relative_to(root):
                raise ValueError("Activation provider escapes the verified package graph")
            if path.suffix.lower() != ".dll":
                continue
            if not path.is_file():
                candidates = []
                for dependency in receipts:
                    for file in dependency["files"]:
                        if file["path"].casefold() != relative.casefold():
                            continue
                        candidate = root / dependency["name"] / file["path"]
                        if not candidate.resolve().is_relative_to(root):
                            raise ValueError("Dependency provider escapes the verified package graph")
                        if candidate.is_symlink() or not candidate.is_file():
                            raise ValueError("Verified dependency code is unavailable")
                        if hashlib.sha256(candidate.read_bytes()).hexdigest() != file["sha256"]:
                            raise ValueError("Verified dependency code changed")
                        candidates.append((candidate, file["sha256"]))
                if not candidates:
                    raise ValueError("Original declared activation DLL is unavailable: " + package["name"] + "/" + relative)
                if len({digest for _, digest in candidates}) != 1:
                    raise ValueError("Package graph has conflicting versions of " + relative)
                path = candidates[0][0]
                supplied_by_dependency[package["name"] + "/" + relative] = str(path)
            elif path.is_symlink() or hashlib.sha256(path.read_bytes()).hexdigest() != expected.get(relative.casefold()):
                raise ValueError("Original declared activation DLL changed: " + package["name"] + "/" + relative)
            for node in server.iter():
                if local_name(node.tag) != "ActivatableClass":
                    continue
                name = node.attrib.get("ActivatableClassId")
                if not name:
                    raise ValueError("Activation class has no declared identity")
                if name.startswith("Windows.Management.Deployment."):
                    continue
                if not name.startswith(("Microsoft.", "XboxPcApp", "ReactNative", "Lottie")):
                    continue
                value = "Z:" + str(path.resolve()).replace("/", "\\")
                if name in conflicts:
                    if value not in conflicts[name]:
                        conflicts[name].append(value)
                    continue
                if name in classes and classes[name] != value:
                    conflicts[name] = [classes.pop(name), value]
                    continue
                classes[name] = value
    lines = ["Windows Registry Editor Version 5.00", ""]
    for name, path in sorted(classes.items()):
        if any(character in name for character in ("\0", "\r", "\n", "[", "]", "\\", '"')):
            raise ValueError("Invalid class name")
        lines.extend([
            "[HKEY_LOCAL_MACHINE\\Software\\Microsoft\\WindowsRuntime\\ActivatableClassId\\" + name + "]",
            '"DllPath"="' + registry_text(path) + '"',
            '"ActivationType"=dword:00000000',
            '"Threading"=dword:00000000',
            "",
        ])
    return "\n".join(lines), classes, conflicts, supplied_by_dependency


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package_code", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    if args.output.exists():
        raise ValueError("Refusing to overwrite an existing activation mapping")
    text, classes, conflicts, dependencies = stage(args.package_code)
    with args.output.open("xb") as stream:
        stream.write(b"\xff\xfe" + text.encode("utf-16-le"))
    print(json.dumps({
        "original_declared_inprocess_classes": len(classes),
        "registry_file": str(args.output),
        "package_registration_or_token_identity_created": False,
        "game_or_user_entitlement_created": False,
        "deployment_manager_scope_untouched": True,
        "ambiguous_classes_not_registered": conflicts,
        "context_aware_package_graph_required_for_ambiguous_classes": bool(conflicts),
        "real_provider_files_supplied_by_verified_dependency": dependencies,
    }, indent=2))


if __name__ == "__main__":
    main()
