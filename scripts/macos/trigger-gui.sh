#!/usr/bin/env bash

set -euo pipefail

action="${1:-}"
argument="${2:-}"
state_root="$HOME/Library/Application Support/XodusRemote"
request_dir="$state_root/requests"
processed_dir="$state_root/processed"

case "$action" in
    crossover | overlay-on | overlay-off | quit-native-steam | quit-windows-steam | steam-control | install-hogwarts | launch-hogwarts | xgameruntime-smoke | xodus-login | start-xodus-service | stop-xodus-service | xodus-service-smoke)
        if [[ -n "$argument" ]]; then
            echo "Action $action does not accept an argument." >&2
            exit 2
        fi
        ;;
    edge-cdp)
        argument="${argument:-9222}"
        if ! [[ "$argument" =~ ^[0-9]+$ ]] || ((argument < 1024 || argument > 65535)); then
            echo "Edge CDP port must be from 1024 through 65535." >&2
            exit 2
        fi
        ;;
    *)
        echo "Usage: $0 {crossover|overlay-on|overlay-off|quit-native-steam|quit-windows-steam|steam-control|install-hogwarts|launch-hogwarts|xgameruntime-smoke|xodus-login|start-xodus-service|stop-xodus-service|xodus-service-smoke|edge-cdp} [edge-cdp-port]" >&2
        exit 2
        ;;
esac

mkdir -p "$request_dir" "$processed_dir"
chmod 700 "$state_root" "$request_dir" "$processed_dir"

run_id="$(date -u +%Y%m%dT%H%M%SZ)-$(uuidgen | tr '[:upper:]' '[:lower:]')"
temporary="$state_root/$run_id.tmp"
request="$request_dir/$run_id.request"

printf '%s\n%s\n' "$action" "$argument" >"$temporary"
chmod 600 "$temporary"
mv "$temporary" "$request"

echo "RUN_ID=$run_id"
echo "STATUS_FILE=$processed_dir/$run_id.status"
echo "LOG_DIR=$HOME/Library/Logs/XodusRemote"
