# SPDX-License-Identifier: GPL-3.0-only
"""Check an owned WGL offscreen framebuffer; no window presentation or outside capture."""

import ctypes
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "public-rps"))
import check_windows as runner


def require_graphics_session():
    if sys.platform != "darwin":
        raise RuntimeError("The Windows graphics check requires macOS.")
    security = ctypes.CDLL("/System/Library/Frameworks/Security.framework/Security")
    get_info = security.SessionGetInfo
    get_info.argtypes = [ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32),
                         ctypes.POINTER(ctypes.c_uint32)]
    get_info.restype = ctypes.c_int32
    attributes = ctypes.c_uint32()
    status = get_info(0xffffffff, None, ctypes.byref(attributes))
    if status:
        raise RuntimeError(f"Cannot read this process's graphics-session access: OSStatus {status}.")
    if not attributes.value & 0x0010:
        raise RuntimeError("This process has no macOS graphics-session access. "
                           "Run the check from the graphical login session, not an SSH audit session.")


def check_graphics(directory, environment, executables, controller, controller_socket):
    for mode, arguments in [("legacy", []), ("core", ["--core"])]:
        runner.wait_server_ready(controller, controller_socket)
        output = runner.run_owned([str(executables[0]), str(executables[2]), *arguments],
                                  environment, 25)
        if f"Isolated Windows {mode} graphics outcome passed." not in output:
            raise RuntimeError(f"The selected Windows {mode} graphics check did not complete.")
        runner.wait_server_ready(controller, controller_socket)
    return ("Actual Windows legacy/core WGL offscreen RGBA8 clear/readback passed; no window "
            "presentation, account, game or another application's pixels were used.")


def main():
    require_graphics_session()
    runner.main(checks=check_graphics)


if __name__ == "__main__":
    main()
