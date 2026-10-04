# SPDX-License-Identifier: GPL-3.0-only
"""Apply the bounded platform fix to an explicitly selected public checkout."""

import argparse
from pathlib import Path
import subprocess
import sys


WINE_REVISION = "eab69739f15180b96797645ccade44c1c4414980"
SOURCE_BLOBS = {
    "server/fsync.c": "7d3ec646f9e5caf526dfb189b7ae0992e2f7e925",
    "dlls/ntdll/unix/fsync.c": "25d41cac79358b8425f4b4c8ba5692e21158582f",
    "server/inproc_sync.c": "a7020ef8fcddc7974d38f0704e8e5a999ec4c16b",
    "dlls/ntdll/unix/sync.c": "67ab945cef86a20f8a7126f1347a6092e38a6544",
    "dlls/ntdll/unix/loader.c": "2498c2aae4479e1542ca5fb0697f114175ba0a83",
    "dlls/ntdll/unix/signal_x86_64.c": "110404c274cf3480ae3b0ae470bf7e4947c8d7a3",
    "dlls/ntdll/unix/system.c": "1821c6eb3cae85131b0b54cfe0447a294c14397c",
    "dlls/win32u/opengl.c": "03346f5b5f6a211be2944707edd29768ddebe817",
    "dlls/winegstreamer/media-converter/media-converter.h": "3dd8864d54f8acb80bb911315b2dd0b14a8c70fd",
}


def git(checkout: Path, *arguments: str) -> str:
    return subprocess.check_output(
        ["git", "-C", str(checkout), *arguments], text=True
    ).strip()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("checkout", type=Path)
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    checkout = args.checkout.absolute()
    if any(part.is_symlink() for part in (checkout, *checkout.parents)) or not checkout.is_dir():
        parser.error("Select an existing, non-symlink disposable public checkout.")
    checkout = checkout.resolve(strict=True)
    if git(checkout, "rev-parse", "HEAD") != WINE_REVISION:
        parser.error("The selected checkout is not the pinned public Wine revision.")
    for relative, expected in SOURCE_BLOBS.items():
        source = checkout / relative
        if any(part.is_symlink() for part in (source, *source.parents)):
            parser.error(f"Refusing a symlink in the selected source path: {relative}")
        if not source.is_file() or git(checkout, "hash-object", str(source)) != expected:
            parser.error(f"Refusing changed or absent public source: {relative}")
    patch = Path(__file__).with_name("fsync-platform.patch").resolve(strict=True)
    patch_text = patch.read_text(encoding="utf-8")
    subprocess.run(
        ["git", "-C", str(checkout), "apply", "--check", "-"],
        input=patch_text, text=True, check=True,
    )
    if args.apply:
        subprocess.run(
            ["git", "-C", str(checkout), "apply", "-"],
            input=patch_text, text=True, check=True,
        )
        print("Applied the public platform guards; no runtime was executed.")
    else:
        print("Pinned public source and patch verified; no source was changed.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except subprocess.CalledProcessError as error:
        print(f"Public source verification/application failed (exit {error.returncode}).", file=sys.stderr)
        sys.exit(1)
