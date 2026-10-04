# SPDX-License-Identifier: GPL-3.0-only
"""Exercise only an owned Windows async DLL, without a game, account or network provider."""

import os
from pathlib import Path
import stat
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "public-rps"))
import check_windows as runner


def verify_component(root, executable, name):
    library = executable.parent / name
    if root not in library.parents:
        raise RuntimeError("The explicit check component is outside the owned root.")
    metadata = library.lstat()
    if (not stat.S_ISREG(metadata.st_mode) or metadata.st_uid != os.getuid()
            or metadata.st_mode & 0o022):
        raise RuntimeError("The explicit check component is not an owned non-writable regular file.")
    for path in (library.parent, *library.parent.parents):
        metadata = path.lstat()
        if (not stat.S_ISDIR(metadata.st_mode) or metadata.st_uid != os.getuid()
                or metadata.st_mode & 0o022):
            raise RuntimeError("The explicit check component has an unsafe ancestor directory.")
        if path == root:
            return
    raise RuntimeError("The explicit check component is outside the owned root.")


def check_async(directory, environment, executables, controller, controller_socket):
    verify_component(directory.parent, executables[2], "xodus_async.dll")
    for mode in ("manual", "threadpool", "delayed", "cancel"):
        runner.wait_server_ready(controller, controller_socket)
        output = runner.run_owned(
            [str(executables[0]), str(executables[2]), mode], environment, 20,
        )
        if f"Isolated Windows async {mode} outcome passed." not in output:
            raise RuntimeError(f"The selected Windows async {mode} check did not complete.")
        runner.wait_server_ready(controller, controller_socket)
    return ("Four actual Windows async/taskqueue checks passed; no game, account, "
            "HTTP provider or shim account exchange was used.")


if __name__ == "__main__":
    runner.main(checks=check_async)
