"""Account for preserved split captures before classifying an owned controller."""

from dataclasses import dataclass
from pathlib import Path
from typing import Optional


@dataclass(frozen=True)
class CaptureState:
    status: str
    returncode: Optional[int]
    stdout: str
    sizes: dict
    total_bytes: int


def poll_capture(process, stdout_path, capture_paths, deadline, now, byte_limit, marker):
    if byte_limit < 0:
        raise ValueError("Capture byte limit must be non-negative")
    stdout_path = Path(stdout_path)
    paths = [stdout_path, *(Path(path) for path in capture_paths)]
    paths = list(dict.fromkeys(paths))
    returncode = process.poll()
    with stdout_path.open("rb") as stream:
        stdout = stream.read(byte_limit + 1).decode("utf-8", errors="replace")
    sizes = {str(path): path.stat().st_size for path in paths}
    total = sum(sizes.values())
    if total > byte_limit:
        status = "over_limit"
    elif returncode is not None or (marker and marker in stdout):
        status = "ready"
    elif now > deadline:
        status = "timeout"
    else:
        status = "waiting"
    return CaptureState(status, returncode, stdout, sizes, total)
