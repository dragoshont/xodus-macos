# SPDX-License-Identifier: GPL-3.0-only
"""Exercise real public gaming COM async and controlled RPS failure paths only."""

import os
from pathlib import Path
import socket
import shutil
import sys
import threading

import check_windows_async as async_check

runner = async_check.runner


def check_shim(directory, environment, executables, controller, controller_socket):
    for name in ("xgameruntime.dll", "MicrosoftGame.config"):
        async_check.verify_component(directory.parent, executables[2], name)
    async_check.check_async(directory, environment, executables, controller, controller_socket)
    for mode, fixture in (
        ("core-missing", None),
        ("core-bad-abi", "abi-refusal-fixture.dll"),
        ("core-missing-export", "export-refusal-fixture.dll"),
    ):
        runner.wait_server_ready(controller, controller_socket)
        selected = directory / mode
        selected.mkdir(mode=0o700)
        for name in ("xgameruntime.dll", executables[2].name):
            async_check.verify_component(directory.parent, executables[2], name)
            shutil.copyfile(executables[2].parent / name, selected / name)
        if fixture is not None:
            async_check.verify_component(directory.parent, executables[2], fixture)
            shutil.copyfile(executables[2].parent / fixture, selected / "xodus_async.dll")
        output = runner.run_owned(
            [str(executables[0]), str(selected / executables[2].name), mode], environment, 20,
        )
        if f"Isolated Windows async {mode} outcome passed." not in output:
            raise RuntimeError("The actual public gaming core refusal was not exact.")
        runner.wait_server_ready(controller, controller_socket)
    endpoint = directory / "r.sock"
    for mode in ("malformed", "expired", "missing"):
        runner.wait_server_ready(controller, controller_socket)
        child = environment.copy()
        child.pop("XODUS_RUNTIME_SOCKET", None)
        if mode == "missing":
            output = runner.run_owned(
                [str(executables[0]), str(executables[2]), "rps-missing"], child, 20,
            )
        else:
            child["XODUS_RUNTIME_SOCKET"] = str(endpoint)
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as listener:
                listener.settimeout(15)
                listener.bind(str(endpoint))
                os.chmod(endpoint, 0o600)
                listener.listen(1)
                stop, failures = threading.Event(), []
                worker = threading.Thread(target=runner.peer, args=(listener, mode, stop, failures))
                worker.start()
                try:
                    output = runner.run_owned(
                        [str(executables[0]), str(executables[2]), "rps-" + mode], child, 20,
                    )
                finally:
                    stop.set()
                    worker.join(timeout=20)
                if worker.is_alive():
                    raise RuntimeError("The owned synthetic COM RPS peer did not stop.")
                if failures:
                    raise RuntimeError("The owned synthetic COM RPS peer failed.") from failures[0]
            endpoint.unlink()
        if f"Isolated Windows async rps-{mode} outcome passed." not in output:
            raise RuntimeError("The actual public gaming user failure did not reconcile.")
        runner.wait_server_ready(controller, controller_socket)
    return ("Four public gaming COM async checks, three core loader refusals and "
            "three real XUserAdd failure paths passed; "
            "only owned malformed/expired/missing synthetic RPS was used, not an account or game.")


if __name__ == "__main__":
    runner.main(checks=check_shim)
