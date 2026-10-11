"""Compile only; this script never starts a Windows executable."""

import hashlib
import json
import os
import subprocess
from pathlib import Path

import pefile

root = Path.home() / "xodus-app-tooling/celeste"
candidate = Path(__file__).resolve().parent
environment = dict(os.environ)
environment["PATH"] = "/opt/homebrew/bin:" + environment.get("PATH", "")
flags = [
    "/opt/homebrew/bin/x86_64-w64-mingw32-gcc",
    "-D__WINESRC__", "-D_UCRT", "-D__WINE_PE_BUILD",
    "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wl,--no-insert-timestamp",
    "-I" + str(root / "wine-build/include"),
    "-I" + str(root / "wine-source/include"),
    "-I" + str(root / "wine-source/include/msvcrt"),
]
manifest = {"files": {}, "commands": []}
for name in ("exception-event-fixture", "exception-event-controller", "exception-event-package-controller"):
    command = flags + [str(candidate / (name + ".c")), "-o",
                       str(candidate / (name + ".exe")), "-lbcrypt", "-ldbghelp"]
    if name == "exception-event-package-controller":
        command += ["-ladvapi32", "-luser32"]
    result = subprocess.run(command, capture_output=True, text=True, env=environment, timeout=120)
    (candidate / (name + "-compile.log")).write_text(result.stdout + result.stderr)
    manifest["commands"].append({"argv": command, "exit": result.returncode})
    result.check_returncode()
    image = pefile.PE(str(candidate / (name + ".exe")))
    if image.FILE_HEADER.Machine != 0x8664 or image.OPTIONAL_HEADER.Magic != 0x20B:
        raise RuntimeError("Not an AMD64 PE32+ candidate")
    manifest.setdefault("imports", {})[name] = {
        entry.dll.decode(): [item.name.decode() if item.name else "#" + str(item.ordinal)
                             for item in entry.imports]
        for entry in image.DIRECTORY_ENTRY_IMPORT
    }
    prohibited = {"SetThreadContext", "WriteProcessMemory", "DebugBreakProcess", "StackWalk64"}
    if any(prohibited.intersection(names) for names in manifest["imports"][name].values()):
        raise RuntimeError("Exception-only candidate imports mutation or unverified unwinding")
for path in candidate.iterdir():
    if path.suffix in (".c", ".h", ".exe", ".py") or path.name.endswith("-compile.log"):
        manifest["files"][path.name] = hashlib.sha256(path.read_bytes()).hexdigest()
(candidate / "source-compile-manifest.json").write_text(json.dumps(manifest, indent=2))
print(json.dumps(manifest["files"], indent=2))
