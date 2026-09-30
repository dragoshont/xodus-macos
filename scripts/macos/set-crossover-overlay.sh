#!/usr/bin/env bash

set -euo pipefail

mode="${1:-on}"
bottles_root="$HOME/Library/Application Support/CrossOver/Bottles"

if [[ "$mode" != "on" && "$mode" != "off" ]]; then
    echo "Usage: $0 [on|off]" >&2
    exit 2
fi

if [[ ! -d "$bottles_root" ]]; then
    echo "CrossOver bottles directory does not exist: $bottles_root" >&2
    exit 1
fi

python3 - "$mode" "$bottles_root" <<'PY'
from pathlib import Path
import shutil
import sys

mode = sys.argv[1]
bottles_root = Path(sys.argv[2])
settings = {
    "MTL_HUD_ENABLED": "1",
    "DXVK_HUD": "fps,frametimes,gpuload,memory",
}

for config in sorted(bottles_root.glob("*/cxbottle.conf")):
    original = config.read_text(encoding="utf-8")
    backup = config.with_suffix(config.suffix + ".before-xodus-overlay")
    if not backup.exists():
        shutil.copy2(config, backup)

    lines = original.splitlines()
    section_index = next(
        (index for index, line in enumerate(lines) if line.strip() == "[EnvironmentVariables]"),
        None,
    )
    if section_index is None:
        lines.extend(["", "[EnvironmentVariables]"])
        section_index = len(lines) - 1

    section_end = len(lines)
    for index in range(section_index + 1, len(lines)):
        if lines[index].startswith("[") and lines[index].endswith("]"):
            section_end = index
            break

    keys = tuple(f'"{key}"' for key in settings)
    lines = [
        line
        for index, line in enumerate(lines)
        if not (section_index < index < section_end and line.lstrip().startswith(keys))
    ]

    section_index = lines.index("[EnvironmentVariables]")
    if mode == "on":
        additions = [f'"{key}" = "{value}"' for key, value in settings.items()]
        lines[section_index + 1:section_index + 1] = additions

    config.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"{mode}: {config.parent.name}")
PY

if [[ "$mode" == "on" ]]; then
    launchctl setenv MTL_HUD_ENABLED 1
    launchctl setenv DXVK_HUD "fps,frametimes,gpuload,memory"
else
    launchctl unsetenv MTL_HUD_ENABLED
    launchctl unsetenv DXVK_HUD
fi

echo "Restart CrossOver and any running games for the overlay change to apply."
