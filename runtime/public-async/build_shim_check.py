# SPDX-License-Identifier: GPL-3.0-only
"""Build an original check of the selected public gaming COM interface."""

import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys

import apply_to_public_shim as overlay


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("build", type=Path)
    parser.add_argument("core", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--compiler", default="x86_64-w64-mingw32-g++")
    args = parser.parse_args()
    for supplied in (args.source, args.build, args.core, args.output):
        if any(path.is_symlink() for path in (supplied.absolute(), *supplied.absolute().parents)):
            raise ValueError("Select non-aliased public input and output paths.")
    source = args.source.resolve(strict=True)
    head = subprocess.run(["git", "-C", str(source), "rev-parse", "HEAD"],
                          check=True, capture_output=True, text=True).stdout.strip()
    if head != overlay.COMMIT:
        raise ValueError("Unexpected public gaming revision.")
    original = subprocess.run(["git", "-C", str(source), "show", "HEAD:xthreading.c"],
                              check=True, capture_output=True).stdout
    blob = hashlib.sha1(b"blob " + str(len(original)).encode() + b"\0" + original).hexdigest()
    if blob != overlay.BLOBS["xthreading.c"]:
        raise ValueError("Unexpected public COM declarations.")
    package = Path(__file__).resolve().parent
    output = args.output.absolute()
    output.mkdir(mode=0o700)
    (output / "shim_check_api.h").write_text(overlay.check_header(original.decode("utf-8")))
    (output / "MicrosoftGame.config").write_text(
        "<Game><MSAAppId>0011223344556677</MSAAppId>"
        "<MSAFullTrust>true</MSAFullTrust></Game>\n",
        encoding="ascii",
    )
    subprocess.run(
        [args.compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic",
         "-DXODUS_SHIM_CHECK", "-D_WIN32_WINNT=0x0601",
         "-isystem", str(args.build / "dlls" / "xgameruntime"),
         "-isystem", str(source),
         "-I", str(output), "-I", str(package),
         str(package / "windows_async_smoke.cpp"), "-static",
         "-o", str(output / "xodus-async-smoke.exe")],
        check=True,
    )
    for name, flags in (
        ("abi-refusal-fixture.dll", ["-DXODUS_REJECT_ABI"]),
        ("export-refusal-fixture.dll", []),
    ):
        subprocess.run(
            [args.compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic",
             *flags, "-shared", "-static", "-I", str(package),
             str(package / "abi_refusal_fixture.cpp"), "-o", str(output / name)],
            check=True,
        )
    for path, name in (
        (args.core / "xodus_async.dll", "xodus_async.dll"),
        (args.build / "dlls" / "xgameruntime" / "x86_64-windows" / "xgameruntime.dll",
         "xgameruntime.dll"),
    ):
        if path.is_symlink() or not path.is_file():
            raise ValueError("The explicit public check component is missing or aliased.")
        shutil.copyfile(path, output / name)
    print("Built a real public gaming COM check; execution is not yet proved.")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f"Public COM check build refused or failed: {error}", file=sys.stderr)
        sys.exit(1)
