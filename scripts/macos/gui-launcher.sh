#!/usr/bin/env bash

set -u

state_root="$HOME/Library/Application Support/XodusRemote"
request_dir="$state_root/requests"
processed_dir="$state_root/processed"
log_dir="$HOME/Library/Logs/XodusRemote"

mkdir -p "$request_dir" "$processed_dir" "$log_dir"
chmod 700 "$state_root" "$request_dir" "$processed_dir"

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
        quit-windows-steam)
            cxwine="/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin/wine"
            steam="$HOME/Library/Application Support/CrossOver/Bottles/GroundedControl/drive_c/Program Files (x86)/Steam/Steam.exe"
            if [[ ! -x "$cxwine" || ! -f "$steam" ]]; then
                echo "CrossOver or the GroundedControl Steam executable is missing." >"$stderr_log"
                status=1
            else
                "$cxwine" \
                    --bottle GroundedControl \
                    --no-wait \
                    "$steam" -shutdown >"$stdout_log" 2>"$stderr_log"
                status=$?
            fi
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
        launch-hogwarts)
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
                    "$steam" -applaunch 990080 >"$stdout_log" 2>"$stderr_log"
                status=$?
            fi
            ;;
        xgameruntime-smoke)
            cxwine="/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin/wine"
            test_exe="$HOME/src/xgameruntime-pr18/build/windows-x64-diagnostic/bin/test_xgameruntime.exe"
            if [[ ! -x "$cxwine" || ! -f "$test_exe" ]]; then
                echo "CrossOver or the diagnostic xgameruntime test executable is missing." >"$stderr_log"
                status=1
            else
                "$cxwine" \
                    --bottle XGameRuntimeTest \
                    --no-gui \
                    "$test_exe" \
                    --gtest_filter=XThreadingTests.VerifySimpleAsyncCall \
                    >"$stdout_log" 2>"$stderr_log"
                status=$?
            fi
            ;;
        xodus-service-smoke)
            service="$HOME/src/xodus-macos/target/release/xodus-service"
            socket="/tmp/xodus.sock"
            if [[ ! -x "$service" ]]; then
                echo "Release xodus-service is missing." >"$stderr_log"
                status=1
            elif pgrep -f '/target/release/[x]odus-service' >/dev/null 2>&1; then
                echo "Another xodus-service process is already running." >"$stderr_log"
                status=1
            elif [[ -e "$socket" ]]; then
                echo "A stale Xodus socket already exists at $socket." >"$stderr_log"
                status=1
            else
                XODUS_LOG=warn "$service" >"$stdout_log" 2>"$stderr_log" &
                service_pid=$!
                ready=false
                for _ in {1..45}; do
                    if [[ -S "$socket" ]]; then
                        ready=true
                        break
                    fi
                    if ! kill -0 "$service_pid" 2>/dev/null; then
                        break
                    fi
                    sleep 1
                done

                if [[ "$ready" == true ]]; then
                    stat -f 'mode=%Sp owner=%Su group=%Sg path=%N' "$socket" >>"$stdout_log"
                    kill -INT "$service_pid"
                    wait "$service_pid"
                    status=$?
                    if [[ -e "$socket" ]]; then
                        echo "The Xodus socket was not removed during shutdown." >>"$stderr_log"
                        status=1
                    fi
                else
                    wait "$service_pid"
                    status=$?
                fi
            fi
            ;;
        xodus-login)
            cli="$HOME/src/xodus-macos/target/release/xodus-cli"
            if [[ ! -x "$cli" ]]; then
                echo "Release xodus-cli is missing." >"$stderr_log"
                status=1
            else
                XODUS_LOG=warn "$cli" login >"$stdout_log" 2>"$stderr_log"
                status=$?
            fi
            ;;
        start-xodus-service)
            launchctl kickstart -k "gui/$(id -u)/com.xodus.service" \
                >"$stdout_log" 2>"$stderr_log"
            status=$?
            ;;
        stop-xodus-service)
            launchctl kill SIGINT "gui/$(id -u)/com.xodus.service" \
                >"$stdout_log" 2>"$stderr_log"
            status=$?
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
