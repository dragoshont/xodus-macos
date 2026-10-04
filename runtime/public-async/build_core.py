# SPDX-License-Identifier: GPL-3.0-only
"""Build only the pinned public Win32 async/taskqueue core, not an HTTP provider."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys


SOURCES = (
    "Source/Task/AsyncLib.cpp",
    "Source/Task/TaskQueue.cpp",
    "Source/Task/ThreadPool_win32.cpp",
    "Source/Task/WaitTimer_win32.cpp",
)


def checked_source(source, dependency):
    supplied = source.absolute()
    if any(path.is_symlink() for path in (supplied, *supplied.parents)):
        raise ValueError("Select a non-aliased public source checkout.")
    source = supplied.resolve(strict=True)
    head = subprocess.run(
        ["git", "-C", str(source), "rev-parse", "HEAD"],
        check=True, capture_output=True, text=True,
    ).stdout.strip()
    if head != dependency["commit"]:
        raise ValueError("The public async source is not at the pinned commit.")
    changed = subprocess.run(
        ["git", "-C", str(source), "status", "--porcelain", "--untracked-files=all"],
        check=True, capture_output=True, text=True,
    ).stdout
    if changed:
        raise ValueError("Refusing modified public async source.")
    ignored = subprocess.run(
        ["git", "-C", str(source), "ls-files", "--others", "--ignored",
         "--exclude-standard", "--", "Include", "Source"],
        check=True, capture_output=True, text=True,
    ).stdout
    if ignored:
        raise ValueError("Refusing ignored files that could shadow public async includes.")
    license_path = source / dependency["licenseFile"]
    data = license_path.read_bytes()
    blob = hashlib.sha1(b"blob " + str(len(data)).encode("ascii") + b"\0" + data).hexdigest()
    if license_path.is_symlink() or blob != dependency["licenseGitBlob"]:
        raise ValueError("The public MIT license does not match the pinned blob.")
    return source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path, help="New, explicit caller-owned object directory")
    parser.add_argument("--compiler", default="x86_64-w64-mingw32-g++")
    args = parser.parse_args()
    package = Path(__file__).resolve().parent
    dependency = json.loads((package / "dependency.json").read_text())
    source = checked_source(args.source, dependency)
    output = args.output.absolute()
    if any(path.is_symlink() for path in (output, *output.parents)):
        raise ValueError("Select a non-aliased output directory.")
    output.mkdir(mode=0o700)
    flags = [
        "-std=c++17", "-DHC_PLATFORM_MSBUILD_GUESS=HC_PLATFORM_WIN32",
        "-D_WIN32_WINNT=0x0601", "-DHC_NO_DEFAULT_HTTP_PROVIDER",
        "-DHC_NO_DEFAULT_WEBSOCKET_PROVIDER", "-Werror", "-Wno-unknown-pragmas",
        "-include", str(package / "windows_compat.h"),
    ]
    flags += ["-I" + str(source / name) for name in
              ("Include", "Source", "Source/Common", "Source/Task")]
    objects = []
    for path in [source / name for name in SOURCES] + [package / "core_support.cpp"]:
        target = output / (path.stem + ".o")
        subprocess.run(
            [args.compiler, *flags, "-c", str(path), "-o", str(target)],
            check=True,
        )
        objects.append(target)
        print(f"Compiled public Win32 core object: {target.name}", flush=True)
    subprocess.run(
        [args.compiler, "-shared", "-static-libgcc", "-static-libstdc++",
         *map(str, objects), "-o", str(output / "xodus_async.dll"),
         "-Wl,--export-all-symbols", "-Wl,-Bstatic", "-lwinpthread",
         "-Wl,-Bdynamic", "-lws2_32", "-lole32"],
        check=True,
    )
    subprocess.run(
        [args.compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic",
         "-DHC_PLATFORM_MSBUILD_GUESS=HC_PLATFORM_WIN32", "-D_WIN32_WINNT=0x0601",
         "-isystem", str(source / "Include"), "-I", str(package),
         str(package / "windows_async_smoke.cpp"), "-static",
         "-o", str(output / "xodus-async-smoke.exe")],
        check=True,
    )
    (output / dependency["licenseFile"]).write_bytes(
        (source / dependency["licenseFile"]).read_bytes()
    )
    print("Windows async core DLL and check linked; execution and shim pairing are not proved.")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f"Public async build refused or failed: {error}", file=sys.stderr)
        sys.exit(1)
