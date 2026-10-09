"""One-shot, exclusively owned installer-default publication in an Xbox prefix."""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import subprocess


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--stage", type=Path, required=True)
    args = parser.parse_args()
    stage = args.stage.resolve(strict=True)
    expected = Path.home() / "xodus-runs" / "xbox-transient-query-20261007-001"
    if stage != expected:
        raise ValueError("This operation is bound to the identified owned experimental stage")
    prefix = stage / "prefix"
    build = stage / "build"
    asset = stage / "coremessaging-registrar-install-default.bin"
    if digest(asset) != "9bd08f9f4b736143bffd91017c87dffecade6010dcfb2b06527169bf659a3ceb":
        raise ValueError("Wrong installation asset")
    if digest(prefix / "drive_c" / "windows" / "system32" / "coremessaging.dll") != (
        "88c7b4d6ee26f7695de0885f39db2949ad77bffc20902b0d19fde7f7ad9d31bc"
    ):
        raise ValueError("Installation asset does not match the staged genuine component")
    receipt = stage / "registrar-default-install-receipt.json"
    if receipt.exists():
        raise FileExistsError("Reconcile the existing receipt before retrying this operation")
    env = os.environ.copy()
    env.update({
        "WINEPREFIX": str(prefix),
        "WINESERVER": str(build / "server" / "wineserver"),
        "WINEDLLPATH": str(build / "dlls") +
            ":/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/lib/wine/x86_64-windows",
        "DYLD_FALLBACK_LIBRARY_PATH": "/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/lib64",
        "WINEDLLOVERRIDES": "winemac.drv=;combase=b;ole32=b;rpcrt4=b;wintypes=n;"
            "coremessaging=n;rmclient=n;twinapi.appcore=n;windows.ui.xaml=n;umpdc=n;"
            "api-ms-win-ham-apphistory-l1-1-0=n",
        "WINEDEBUG": "-all",
    })
    with (stage / "registrar-default-install.lock").open("a+b") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        if receipt.exists():
            raise FileExistsError("Another publication completed; reconcile its receipt before retrying")
        # No other installer/helper owns this prefix; hold its lock through read-back.
        stopped = subprocess.run([env["WINESERVER"], "-k"], env=env, check=False)
        if stopped.returncode not in (0, 1):
            raise RuntimeError(f"Owned prefix shutdown failed: {stopped.returncode}")
        subprocess.run([env["WINESERVER"], "-w"], env=env, check=True, timeout=30)
        intent = {
            "operation": "install",
            "scope": "mac-experimental-xbox-app",
            "prefix": str(prefix),
            "value": "WM_RegistrarServer/SecurityDescriptor",
            "asset_sha256": digest(asset),
            "state": "publication-not-yet-reconciled",
        }
        receipt.write_text(json.dumps(intent, indent=2), encoding="utf-8")
        result = subprocess.run(
            [str(build / "wine"), str(stage / "stage-coremessaging-install-default.exe"), str(asset)],
            env=env, text=True, capture_output=True, timeout=60,
        )
        (stage / "registrar-default-install.log").write_text(
            result.stdout + result.stderr, encoding="utf-8"
        )
        observed = {
            "operation": "install",
            "scope": "mac-experimental-xbox-app",
            "prefix": str(prefix),
            "value": "WM_RegistrarServer/SecurityDescriptor",
            "asset_sha256": digest(asset),
            "component_sha256": digest(prefix / "drive_c" / "windows" / "system32" / "coremessaging.dll"),
            "exclusive_quiescent_prefix_before_publication": True,
            "exit_code": result.returncode,
            "publication_and_readback_confirmed": result.returncode == 0 and
                "STAGED_MATCHING_COMPONENT_DEFAULT" in result.stdout,
            "result": result.stdout + result.stderr,
            "rollback": "Remove only this newly published value after checking its asset digest; never overwrite another value.",
        }
        receipt.write_text(json.dumps(observed, indent=2), encoding="utf-8")
        print(result.stdout, end="")
        if result.returncode or not observed["publication_and_readback_confirmed"]:
            raise RuntimeError("Publication was not confirmed; inspect the receipt before any retry")


if __name__ == "__main__":
    main()
