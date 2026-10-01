#!/usr/bin/env bash

set -euo pipefail

runs_root="$HOME/xodus-runs"
latest_file="$runs_root/latest-hogwarts-xbox-stream"

if [[ ! -f "$latest_file" ]]; then
    echo "No Hogwarts Xbox streaming run has been started." >&2
    exit 1
fi

run_id="$(cat "$latest_file")"
run_dir="$runs_root/$run_id"
destination="$(cat "$run_dir/destination")"
label="gui/$(id -u)/com.xodus.hogwarts-stream"

printf 'run_id=%s\n' "$run_id"
printf 'destination=%s\n' "$destination"

if launchctl print "$label" 2>/dev/null | grep -q 'state = running'; then
    echo "status=running"
else
    echo "status=exited"
fi

for file in .xodus-streaming-tmp.msixvc .xodus-streaming.msixvc; do
    path="$destination/$file"
    if [[ -e "$path" ]]; then
        printf '%s_apparent_bytes=%s\n' \
            "${file//[^[:alnum:]]/_}" \
            "$(stat -f '%z' "$path")"
        printf '%s_disk_kib=%s\n' \
            "${file//[^[:alnum:]]/_}" \
            "$(du -sk "$path" | awk '{print $1}')"
    fi
done

printf 'destination_disk_kib=%s\n' "$(du -sk "$destination" | awk '{print $1}')"
printf 'disk_free_bytes=%.0f\n' "$(df -k / | awk 'NR == 2 {print $4 * 1024}')"

echo "recent_log:"
python3 - \
    "$HOME/Library/Logs/XodusRemote/hogwarts-xbox-stream.stdout.log" \
    "$HOME/Library/Logs/XodusRemote/hogwarts-xbox-stream.stderr.log" <<'PY'
from pathlib import Path
import re
import sys

ansi = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
for value in sys.argv[1:]:
    path = Path(value)
    if not path.exists():
        continue
    text = path.read_text(encoding="utf-8", errors="replace")
    text = ansi.sub("", text).replace("\r", "\n")
    lines = [line.strip() for line in text.splitlines() if line.strip()]
    for line in lines[-20:]:
        if "http://" in line or "https://" in line:
            print("[URL redacted]")
        else:
            print(line[:500])
PY
