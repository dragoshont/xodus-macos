"""Read-only Store-call validation for the pinned original Xbox installer.

Requires dnfile and dncil. Never loads or invokes managed/native installer code.
"""
import argparse
import hashlib
import logging
from pathlib import Path

import dnfile
from dncil.cil.body.reader import read_method_body_from_bytes


INSTALLER_SHA256 = "ba30ea16cd8fbd9209d40ae193206ad00f042d100524cf310982c33369325ca2"
STORE_NAMESPACE = "Windows.ApplicationModel.Store.Preview.InstallControl"
REQUIRED_CALLS = {
    ("AppInstallManager", ".ctor"),
    ("AppInstallManager", "GetIsAppAllowedToInstallAsync"),
    ("AppInstallManager", "get_AppInstallItems"),
    ("AppInstallManager", "get_AppInstallItemsWithGroupSupport"),
    ("AppInstallManager", "StartProductInstallAsync"),
    ("AppInstallOptions", "put_TargetVolume"),
}


def inspect(path: Path) -> None:
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if digest != INSTALLER_SHA256:
        raise ValueError("Input is not the approved, unchanged Microsoft XboxInstaller 1.0.1.30")
    logging.getLogger("dnfile").setLevel(logging.ERROR)
    image = dnfile.dnPE(str(path), clr_lazy_load=True)
    if image.net is None:
        raise ValueError("Installer has no CLI metadata")
    tables = image.net.mdtables
    references = {}
    for index, reference in enumerate(tables.MemberRef.rows, 1):
        parent = reference.Class.row
        if hasattr(parent, "TypeNamespace") and str(parent.TypeNamespace) == STORE_NAMESPACE:
            references[0x0A000000 | index] = (
                str(parent.TypeName), str(reference.Name), bytes(reference.Signature.value).hex()
            )
    owners = {}
    for owner in tables.TypeDef.rows:
        name = ".".join(filter(None, (str(owner.TypeNamespace), str(owner.TypeName))))
        for method in owner.MethodList:
            owners[method.row_index] = name
    found = set()
    matches = 0
    for index, method in enumerate(tables.MethodDef.rows, 1):
        if not method.Rva:
            continue
        body = read_method_body_from_bytes(image.get_data(method.Rva))
        for instruction in body.instructions:
            token = getattr(instruction.operand, "value", None)
            if token not in references:
                continue
            owner, name, signature = references[token]
            found.add((owner, name))
            matches += 1
            print(f"INSTALLER_CALLER={owners[index]}.{method.Name} OFFSET=0x{instruction.offset:x}")
            print(f"STORE_CALLEE={STORE_NAMESPACE}.{owner}.{name} "
                  f"TOKEN=0x{token:08x} CLI_SIGNATURE={signature}")
    missing = REQUIRED_CALLS - found
    if missing:
        raise AssertionError(f"Expected real installer Store calls not observed: {sorted(missing)}")
    print(f"INSTALLER_SHA256={digest}")
    print(f"STORE_STATIC_CALLS={matches}")
    print("PACKAGE_MANAGER_STORE_STATIC_TEST_PASS")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("installer", type=Path)
    inspect(parser.parse_args().installer)
