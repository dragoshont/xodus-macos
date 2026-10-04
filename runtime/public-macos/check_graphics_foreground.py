# SPDX-License-Identifier: GPL-3.0-only
"""Owned offscreen/default drawable checks with foreground-PID snapshots, not continuous capture."""

import argparse
import os
from pathlib import Path
import subprocess
import sys

import check_default_drawable as drawable


def select_helper(supplied, root):
    helper = supplied.absolute()
    if any(path.is_symlink() for path in (helper, *helper.parents)) or not helper.is_file():
        raise RuntimeError("Select a non-aliased regular foreground helper.")
    root = root.absolute()
    if any(path.is_symlink() for path in (root, *root.parents)) or not root.is_dir():
        raise RuntimeError("Select a non-aliased owned output root.")
    if root not in helper.parents:
        raise RuntimeError("The foreground helper must be inside the owned output root.")
    for path in (helper, *helper.parents):
        info = path.stat()
        if info.st_uid != os.getuid() or info.st_mode & 0o022:
            raise RuntimeError("The foreground helper and its owned ancestors must not be writable by others.")
        if path == root:
            break
    return helper


def foreground(helper):
    result = subprocess.run([str(helper)], capture_output=True, text=True, check=True, timeout=10)
    value = result.stdout.strip()
    if not value.isascii() or not value.isdecimal() or not 0 < int(value) < 2 ** 31:
        raise RuntimeError("The metadata-only foreground check returned an invalid PID.")
    return int(value)


def guarded_run(original, helper, arguments, environment, timeout):
    before = foreground(helper)
    try:
        output = original(arguments, environment, timeout)
    finally:
        if foreground(helper) != before:
            raise RuntimeError("The foreground PID changed across the owned check; "
                               "no activation preservation may be claimed.")
    print(output, end="" if output.endswith("\n") else "\n")
    return output


def check_all(directory, environment, executables, controller, controller_socket):
    offscreen = drawable.graphics.check_graphics(
        directory, environment, executables, controller, controller_socket)
    default = drawable.check_drawable(directory, environment, executables, controller, controller_socket)
    return f"{offscreen}\n{default}\nNative foreground PID snapshots were unchanged across each owned process."


def main():
    drawable.graphics.require_graphics_session()
    parser = argparse.ArgumentParser(description=__doc__, add_help=False)
    parser.add_argument("--foreground-helper", type=Path, required=True)
    args, remaining = parser.parse_known_args()
    if not remaining:
        parser.error("Supply the owned root and the shared runner's selected binaries.")
    helper = select_helper(args.foreground_helper, Path(remaining[0]))
    original, arguments = drawable.runner.run_owned, sys.argv
    try:
        drawable.runner.run_owned = lambda *values: guarded_run(original, helper, *values)
        sys.argv = [arguments[0], *remaining]
        drawable.runner.main(checks=check_all)
    finally:
        drawable.runner.run_owned, sys.argv = original, arguments


if __name__ == "__main__":
    main()
