"""Compile and pin diagnostic candidates on the existing Mac toolchain; never run them."""

import hashlib
import json
import os
import subprocess
from pathlib import Path

import pefile

root = Path.home() / "xodus-app-tooling"
source = root / "celeste/wine-source"
build = root / "celeste/wine-build"
candidate = Path(__file__).resolve().parent
compiler = "/opt/homebrew/bin/x86_64-w64-mingw32-gcc"
environment = dict(os.environ)
environment["PATH"] = "/opt/homebrew/bin:" + environment.get("PATH", "")
flags = [
    compiler, "-D__WINESRC__", "-D_UCRT", "-D__WINE_PE_BUILD",
    "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
    "-I" + str(build / "include"), "-I" + str(source / "include"),
    "-I" + str(source / "include/msvcrt"),
]
manifest = {"files": {}, "commands": []}
for name in ("debug-event-fixture", "debug-event-controller", "debug-event-package-controller"):
    command = flags + [str(candidate / (name + ".c")), "-o",
                       str(candidate / (name + ".exe")), "-lbcrypt"]
    result = subprocess.run(command, capture_output=True, text=True, env=environment, timeout=120)
    (candidate / (name + "-compile.log")).write_text(result.stdout + result.stderr)
    manifest["commands"].append({"argv": command, "exit": result.returncode})
    result.check_returncode()
    image = pefile.PE(str(candidate / (name + ".exe")))
    if image.FILE_HEADER.Machine != 0x8664 or image.OPTIONAL_HEADER.Magic != 0x20B:
        raise RuntimeError("Candidate is not AMD64 PE32+")
    if name == "debug-event-fixture":
        exports = {entry.name.decode(): entry.address for entry in image.DIRECTORY_ENTRY_EXPORT.symbols
                   if entry.name}
        manifest["fixture_rvas"] = [exports["debug_site" + str(index)] for index in range(5)]
for path in candidate.iterdir():
    if path.suffix in (".c", ".h", ".exe", ".py") or path.name.endswith("-compile.log"):
        manifest["files"][path.name] = hashlib.sha256(path.read_bytes()).hexdigest()
(candidate / "source-compile-manifest.json").write_text(json.dumps(manifest, indent=2))
print(json.dumps(manifest, indent=2))
