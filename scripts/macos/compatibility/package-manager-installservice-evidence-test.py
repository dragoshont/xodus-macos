"""Validate genuine native-client provenance and the reproduced activation block.

An evidence-test pass means the native failure was reproduced, not installation
success. Requires pefile from the existing scoped CLI metadata tools.
"""
import argparse
import hashlib
import json
from pathlib import Path

import pefile


PINS = {
    "x64": {
        "InstallService": "5172d5d3ac77c56b60899ae468b154c80b77972302c1fafff99470483223c8c2",
        "UMPDC": "b3407a812c7f34ac6b5beefb2a78e8f2d7c4376f7c7012f880aab4fe07235e4e",
        "rmclient": "667884a4471c9da403368adc1f8acb38c7a22fd5e75ce35deb13a4a8263a49f4",
    },
    "x86": {
        "InstallService": "e2e479ea1e033b1d23dbc015adf88df48695cc5e1c17d52713ba5e678f3a1169",
        "UMPDC": "52e5f333029039f3a7e5f28c673fbc25d5b2f2151dea6358802e668759d736a5",
        "rmclient": "c8f143f406ebf36cab8ee79934aa74b01f1dd252415a757fa3e18fdd346be420",
    },
}


def validate(root: Path, state_bridge: bool = False) -> None:
    for arch, pins in PINS.items():
        for component, expected in pins.items():
            name = (
                f"package-manager-InstallService-{arch}.dll"
                if component == "InstallService"
                else f"package-manager-{arch}-{component}.dll"
            )
            actual = hashlib.sha256((root / name).read_bytes()).hexdigest()
            if actual != expected:
                raise AssertionError(f"Changed genuine artifact: {name}")
        alias = pefile.PE(str(root / f"package-manager-ham-forward-{arch}.dll"))
        assert alias.FILE_HEADER.Machine == (0x8664 if arch == "x64" else 0x14C)
        assert alias.OPTIONAL_HEADER.AddressOfEntryPoint == 0
        assert not hasattr(alias, "DIRECTORY_ENTRY_IMPORT")
        exports = alias.DIRECTORY_ENTRY_EXPORT.symbols
        assert len(exports) == 1
        assert exports[0].name == b"HamQueryPackageUsageInfo"
        assert exports[0].forwarder == b"rmclient.HamQueryPackageUsageInfo"
        initial = (root / f"package-manager-installservice-native-{arch}.log").read_text(
            encoding="utf-8", errors="replace"
        )
        restored = (root / f"package-manager-installservice-restored-{arch}.log").read_text(
            encoding="utf-8", errors="replace"
        )
        assert "STORE_NATIVE_LOAD_ERROR=126" in initial
        assert "STORE_NATIVE_FACTORY_HRESULT=0x00000000" in restored
        assert "unimplemented function api-ms-win-stateseparation-helpers-l1-1-0.dll.GetPersistedRegistryLocationW" in restored
        assert "STORE_NATIVE_ACTIVATION_HRESULT=0x00000000" not in restored
        assert "STORE_NATIVE_READ_QUEUE_HRESULT=" not in restored
        assert "STORE_NATIVE_CLIENT_CONTROL_PASS" not in restored
        assert "import_dll Library UMPDC.dll" not in restored
        assert "import_dll Library api-ms-win-ham-apphistory-l1-1-0.dll" not in restored
        print(f"GENUINE_FACTORY_REACHED={arch} ACTUAL_ACTIVATION_BLOCK=GetPersistedRegistryLocationW")
    if state_bridge:
        expected_host = "becad014fb8efa8cb5e314931cca92778ad42c649b12a6909632cacd68af4f40"
        host = root / "package-manager-state-kernelbase-x64.dll"
        assert hashlib.sha256(host.read_bytes()).hexdigest() == expected_host
        receipt = json.loads((root / "package-manager-state-bridge-receipt.json").read_bytes())
        assert receipt["original_sha256"] == PINS["x64"]["InstallService"]
        assert receipt["all_other_iat_slot_addresses_preserved"]
        assert receipt["pointer_bytes"] == 8
        assert receipt["original_iat_slot_rva"] == receipt["replacement_iat_slot_rva"]
        assert receipt["modified_copy_is_not_vendor_signed"]
        derived = root / "package-manager-InstallService-state-bridge-x64.dll"
        assert hashlib.sha256(derived.read_bytes()).hexdigest() == receipt["modified_sha256"]
        image = pefile.PE(str(derived))
        assert image.OPTIONAL_HEADER.DATA_DIRECTORY[4].VirtualAddress == 0
        assert image.OPTIONAL_HEADER.DATA_DIRECTORY[4].Size == 0
        bridge = pefile.PE(str(root / "package-manager-state-helper-x64.dll"))
        assert bridge.OPTIONAL_HEADER.AddressOfEntryPoint == 0
        assert not hasattr(bridge, "DIRECTORY_ENTRY_IMPORT")
        assert len(bridge.DIRECTORY_ENTRY_EXPORT.symbols) == 1
        assert bridge.DIRECTORY_ENTRY_EXPORT.symbols[0].name == b"GetPersistedRegistryLocationW"
        assert bridge.DIRECTORY_ENTRY_EXPORT.symbols[0].forwarder == (
            b"package-manager-state-kernelbase-x64.GetPersistedRegistryLocationW"
        )
        log = (root / "package-manager-installservice-state-direct-x64.log").read_text(
            encoding="utf-8", errors="replace"
        )
        assert "unimplemented function ntdll.dll.RtlCreateTagHeap" in log
        assert "Initialization of" in log and "package-manager-state-kernelbase-x64.dll" in log
        assert "No implementation for ntdll.dll.RtlGetPersistedStateLocation" in log
        assert "STORE_NATIVE_LOAD_ERROR=317" in log
        assert "STORE_NATIVE_ACTIVATION_HRESULT=0x00000000" not in log
        print("GENUINE_STATE_HOST_REACHED=x64 ACTUAL_INITIALIZATION_BLOCK=RtlCreateTagHeap")
    print("PACKAGE_MANAGER_INSTALLSERVICE_EVIDENCE_TEST_PASS (blocked activation; no installation)")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("evidence_directory", type=Path)
    parser.add_argument("--state-bridge", action="store_true")
    args = parser.parse_args()
    validate(args.evidence_directory, args.state_bridge)
