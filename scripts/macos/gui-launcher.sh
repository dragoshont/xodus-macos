#!/usr/bin/env bash

set -u

state_root="$HOME/Library/Application Support/XodusRemote"
request_dir="$state_root/requests"
processed_dir="$state_root/processed"
log_dir="$HOME/Library/Logs/XodusRemote"
lock_dir="$state_root/launcher.lock"

mkdir -p "$request_dir" "$processed_dir" "$log_dir"
chmod 700 "$state_root" "$request_dir" "$processed_dir"

if ! mkdir "$lock_dir" 2>/dev/null; then
    exit 0
fi
trap 'rmdir "$lock_dir"' EXIT

shopt -s nullglob
for request in "$request_dir"/*.request; do
    run_id="$(basename "$request" .request)"
    action="$(sed -n '1p' "$request" | tr -d '\r\n')"
    argument="$(sed -n '2p' "$request" | tr -d '\r\n')"
    stdout_log="$log_dir/$run_id.stdout.log"
    stderr_log="$log_dir/$run_id.stderr.log"
    status_file="$processed_dir/$run_id.status"

    case "$action" in
        crossover)
            /usr/bin/open -a "/Applications/CrossOver.app" \
                >"$stdout_log" 2>"$stderr_log"
            status=$?
            ;;
        quit-native-steam)
            /usr/bin/osascript \
                -e 'tell application id "com.valvesoftware.steam" to quit' \
                >"$stdout_log" 2>"$stderr_log"
            status=$?
            ;;
        steam-grounded)
            cxwine="/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin/wine"
            steam="$HOME/Library/Application Support/CrossOver/Bottles/GroundedControl/drive_c/Program Files (x86)/Steam/Steam.exe"
            if [[ ! -x "$cxwine" || ! -f "$steam" ]]; then
                echo "CrossOver or the GroundedControl Steam executable is missing." >"$stderr_log"
                status=1
            else
                "$cxwine" \
                    --bottle GroundedControl \
                    --no-wait \
                    --cx-log "$log_dir/$run_id.crossover.log" \
                    "$steam" >"$stdout_log" 2>"$stderr_log"
                status=$?
            fi
            ;;
        install-grounded)
            cxwine="/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin/wine"
            steam="$HOME/Library/Application Support/CrossOver/Bottles/GroundedControl/drive_c/Program Files (x86)/Steam/Steam.exe"
            if [[ ! -x "$cxwine" || ! -f "$steam" ]]; then
                echo "CrossOver or the GroundedControl Steam executable is missing." >"$stderr_log"
                status=1
            else
                "$cxwine" \
                    --bottle GroundedControl \
                    --no-wait \
                    --cx-log "$log_dir/$run_id.crossover.log" \
                    "$steam" "steam://install/962130" >"$stdout_log" 2>"$stderr_log"
                status=$?
            fi
            ;;
        install-hogwarts)
            cxwine="/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin/wine"
            steam="$HOME/Library/Application Support/CrossOver/Bottles/GroundedControl/drive_c/Program Files (x86)/Steam/Steam.exe"
            if [[ ! -x "$cxwine" || ! -f "$steam" ]]; then
                echo "CrossOver or the GroundedControl Steam executable is missing." >"$stderr_log"
                status=1
            else
                "$cxwine" \
                    --bottle GroundedControl \
                    --no-wait \
                    --cx-log "$log_dir/$run_id.crossover.log" \
                    "$steam" "steam://install/990080" >"$stdout_log" 2>"$stderr_log"
                status=$?
            fi
            ;;
        edge-cdp)
            port="${argument:-9222}"
            if ! [[ "$port" =~ ^[0-9]+$ ]] || ((port < 1024 || port > 65535)); then
                echo "Invalid CDP port: $port" >"$stderr_log"
                status=2
            else
                profile_dir="$HOME/Library/Application Support/XodusEdgeCDP"
                mkdir -p "$profile_dir"
                /usr/bin/open -na "/Applications/Microsoft Edge.app" --args \
                    --remote-debugging-address=127.0.0.1 \
                    --remote-debugging-port="$port" \
                    --user-data-dir="$profile_dir" \
                    >"$stdout_log" 2>"$stderr_log"
                status=$?
            fi
            ;;
        *)
            echo "Unsupported action: $action" >"$stderr_log"
            status=2
            ;;
    esac

    printf '%s\n' "$status" >"$status_file"
    rm -f "$request"
done
