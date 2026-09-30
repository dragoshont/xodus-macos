#!/usr/bin/env bash

set -euo pipefail

app_id="${1:-}"
if ! [[ "$app_id" =~ ^[0-9]+$ ]]; then
    echo "Usage: $0 STEAM_APP_ID" >&2
    exit 2
fi

steam_root="$HOME/Library/Application Support/CrossOver/Bottles/GroundedControl/drive_c/Program Files (x86)/Steam"
manifest="$steam_root/steamapps/appmanifest_$app_id.acf"
content_log="$steam_root/logs/content_log.txt"

field() {
    local key="$1"
    awk -F '"' -v key="$key" '$2 == key { print $4; exit }' "$manifest"
}

printf 'timestamp=%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
printf 'app_id=%s\n' "$app_id"

if [[ ! -f "$manifest" ]]; then
    echo "manifest=absent"
    exit 1
fi

state_flags="$(field StateFlags)"
install_dir="$(field installdir)"
bytes_downloaded="$(field BytesDownloaded)"
bytes_to_download="$(field BytesToDownload)"
size_on_disk="$(field SizeOnDisk)"
staging_dir="$steam_root/steamapps/downloading/$app_id"

printf 'manifest=present\n'
printf 'state_flags=%s\n' "${state_flags:-unknown}"
printf 'install_dir=%s\n' "${install_dir:-unknown}"
printf 'bytes_downloaded=%s\n' "${bytes_downloaded:-0}"
printf 'bytes_to_download=%s\n' "${bytes_to_download:-0}"
printf 'size_on_disk=%s\n' "${size_on_disk:-0}"
if [[ -d "$staging_dir" ]]; then
    staging_kib="$(du -sk "$staging_dir" | awk '{ print $1 }')"
    printf 'staging_bytes=%s\n' "$((staging_kib * 1024))"
else
    echo "staging_bytes=0"
fi

if [[ "${bytes_to_download:-0}" =~ ^[0-9]+$ ]] &&
    [[ "${bytes_downloaded:-0}" =~ ^[0-9]+$ ]] &&
    ((bytes_to_download > 0)); then
    awk -v downloaded="$bytes_downloaded" -v total="$bytes_to_download" \
        'BEGIN { printf "download_percent=%.2f\n", downloaded * 100 / total }'
fi

free_bytes="$(df -k / | awk 'NR == 2 { print $4 * 1024 }')"
printf 'disk_free_bytes=%.0f\n' "$free_bytes"

if pgrep -f 'C:\\Program Files \(x86\)\\Steam\\[s]team.exe' >/dev/null; then
    echo "windows_steam=running"
else
    echo "windows_steam=stopped"
fi

if pgrep -f '/Library/Application Support/Steam/Steam.AppBundle/Steam/Contents/MacOS/[s]team_osx' >/dev/null; then
    echo "native_steam=running"
else
    echo "native_steam=stopped"
fi

if [[ -f "$content_log" ]]; then
    echo "recent_content_events:"
    tail -n 200 "$content_log" |
        grep -Ei 'error|fail|download|update|manifest|prealloc|disk' |
        tail -n 20 || true
fi
