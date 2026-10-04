# SPDX-License-Identifier: GPL-3.0-only
"""Check only an owned nonactivating default drawable and buffer-swap API."""

import check_windows_graphics as graphics

runner = graphics.runner


def check_drawable(directory, environment, executables, controller, controller_socket):
    for mode, argument in (("legacy", "--present"), ("core", "--present-core")):
        runner.wait_server_ready(controller, controller_socket)
        output = runner.run_owned(
            [str(executables[0]), str(executables[2]), argument], environment, 25,
        )
        if f"Isolated Windows {mode} default-drawable outcome passed." not in output:
            raise RuntimeError(f"The owned Windows {mode} default drawable did not complete.")
        runner.wait_server_ready(controller, controller_socket)
    return ("Owned nonactivating legacy/core default RGBA8 drawables and buffer-swap API passed; "
            "not compositor capture, another application's pixels, an account or licensed gameplay.")


def main():
    graphics.require_graphics_session()
    runner.main(checks=check_drawable)


if __name__ == "__main__":
    main()
