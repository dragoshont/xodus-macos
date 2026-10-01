#!/usr/bin/env bash

set -u

state_root="$HOME/Library/Application Support/XodusRemote"
request_dir="$state_root/requests"
processed_dir="$state_root/processed"
log_dir="$HOME/Library/Logs/XodusRemote"
overlay_file="$state_root/performance-overlay"
bottles_root="$HOME/Library/Application Support/CrossOver/Bottles"
bottle="HogwartsControl"
if [[ -d "$bottles_root/GroundedControl" && ! -d "$bottles_root/$bottle" ]]; then
    bottle="GroundedControl"
fi
cxwine="/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin/wine"
steam="$bottles_root/$bottle/drive_c/Program Files (x86)/Steam/Steam.exe"

mkdir -p "$request_dir" "$processed_dir" "$log_dir"
chmod 700 "$state_root" "$request_dir" "$processed_dir"

if [[ ! -f "$overlay_file" ]]; then
    printf 'on\n' >"$overlay_file"
fi

if [[ "$(cat "$overlay_file")" == "on" ]]; then
    export MTL_HUD_ENABLED=1
    export DXVK_HUD="fps,frametimes,gpuload,memory"
    launchctl setenv MTL_HUD_ENABLED "$MTL_HUD_ENABLED"
    launchctl setenv DXVK_HUD "$DXVK_HUD"
else
    unset MTL_HUD_ENABLED DXVK_HUD
    launchctl unsetenv MTL_HUD_ENABLED
    launchctl unsetenv DXVK_HUD
fi

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
        overlay-on)
            printf 'on\n' >"$overlay_file"
            launchctl setenv MTL_HUD_ENABLED 1
            launchctl setenv DXVK_HUD "fps,frametimes,gpuload,memory"
            echo "Performance overlay enabled for future game launches." >"$stdout_log"
            status=0
            ;;
        overlay-off)
            printf 'off\n' >"$overlay_file"
            launchctl unsetenv MTL_HUD_ENABLED
            launchctl unsetenv DXVK_HUD
            echo "Performance overlay disabled for future game launches." >"$stdout_log"
            status=0
            ;;
        quit-native-steam)
            /usr/bin/osascript \
                -e 'tell application id "com.valvesoftware.steam" to quit' \
                >"$stdout_log" 2>"$stderr_log"
            status=$?
            ;;
        quit-windows-steam)
            if [[ ! -x "$cxwine" || ! -f "$steam" ]]; then
                echo "CrossOver or the control-bottle Steam executable is missing." >"$stderr_log"
                status=1
            else
                "$cxwine" \
                    --bottle "$bottle" \
                    --no-wait \
                    "$steam" -shutdown >"$stdout_log" 2>"$stderr_log"
                status=$?
            fi
            ;;
        steam-control)
            if [[ ! -x "$cxwine" || ! -f "$steam" ]]; then
                echo "CrossOver or the control-bottle Steam executable is missing." >"$stderr_log"
                status=1
            else
                "$cxwine" \
                    --bottle "$bottle" \
                    --no-wait \
                    --cx-log "$log_dir/$run_id.crossover.log" \
                    "$steam" >"$stdout_log" 2>"$stderr_log"
                status=$?
            fi
            ;;
        install-hogwarts)
            if [[ ! -x "$cxwine" || ! -f "$steam" ]]; then
                echo "CrossOver or the control-bottle Steam executable is missing." >"$stderr_log"
                status=1
            else
                "$cxwine" \
                    --bottle "$bottle" \
                    --no-wait \
                    --cx-log "$log_dir/$run_id.crossover.log" \
                    "$steam" "steam://install/990080" >"$stdout_log" 2>"$stderr_log"
                status=$?
            fi
            ;;
        launch-hogwarts)
            if [[ ! -x "$cxwine" || ! -f "$steam" ]]; then
                echo "CrossOver or the control-bottle Steam executable is missing." >"$stderr_log"
                status=1
            else
                "$cxwine" \
                    --bottle "$bottle" \
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
        xodus-hogwarts-probe)
            cli="$HOME/src/xodus-macos/target/release/xodus-cli"
            if [[ ! -x "$cli" ]]; then
                echo "Release xodus-cli is missing." >"$stderr_log"
                status=1
            else
                summary="$state_root/hogwarts-package-probe.txt"
                expect_script="$state_root/hogwarts-package-probe.expect"
                cat >"$expect_script" <<EOF
#!/usr/bin/expect -f
set timeout 120
log_user 1
spawn -noecho env XODUS_LOG=warn "$cli" download 9MT5NJ5W7B8Z --market GB --dry-run
expect {
    -re "Select files to download" {
        after 500
        send -- "\\033\\[C"
        after 300
        send -- "\\r"
    }
    timeout {
        exit 124
    }
    eof {
        exit 1
    }
}
expect eof
catch wait result
exit [lindex \$result 3]
EOF
                chmod 700 "$expect_script"
                "$expect_script" >"$stdout_log" 2>"$stderr_log"
                status=$?
                python3 - "$stdout_log" "$summary" <<'PY'
from pathlib import Path
import re
import sys

raw_path = Path(sys.argv[1])
summary_path = Path(sys.argv[2])
text = raw_path.read_text(encoding="utf-8", errors="replace")
text = re.sub(r"\x1b\[[0-?]*[ -/]*[@-~]", "", text)
lines = []
url_seen = False
for line in text.replace("\r", "\n").splitlines():
    line = line.strip()
    if not line:
        continue
    if re.search(r"https?://", line):
        if not url_seen:
            lines.append("[download URLs redacted]")
            url_seen = True
        continue
    if (
        "Select files to download" in line
        or "ContentID:" in line
        or re.search(r"\.(?:msixvc|xvc|xsp)\b", line, re.IGNORECASE)
    ):
        if line not in lines:
            lines.append(line)

summary_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
summary_path.chmod(0o600)
raw_path.write_text(summary_path.read_text(encoding="utf-8"), encoding="utf-8")
raw_path.chmod(0o600)
PY
                rm -f "$expect_script"
            fi
            ;;
        start-xodus-hogwarts-stream)
            destination="$HOME/Games/Xodus/HogwartsLegacy-Xbox"
            runs_root="$HOME/xodus-runs"
            latest_file="$runs_root/latest-hogwarts-xbox-stream"
            stream_label="gui/$(id -u)/com.xodus.hogwarts-stream"
            if ! launchctl print "$stream_label" >/dev/null 2>&1; then
                echo "The Hogwarts streaming LaunchAgent is not installed." >"$stderr_log"
                status=1
            elif launchctl print "$stream_label" 2>/dev/null |
                grep -q 'state = running'; then
                echo "The Hogwarts Xbox stream is already running." >"$stderr_log"
                status=1
            else
                stream_run_id="hogwarts-xbox-stream-$(date -u +%Y%m%dT%H%M%SZ)"
                stream_run_dir="$runs_root/$stream_run_id"
                mkdir -p "$stream_run_dir" "$destination"
                printf '%s\n' "$destination" >"$stream_run_dir/destination"
                printf '%s\n' "$stream_run_id" >"$latest_file"
                : >"$HOME/Library/Logs/XodusRemote/hogwarts-xbox-stream.stdout.log"
                : >"$HOME/Library/Logs/XodusRemote/hogwarts-xbox-stream.stderr.log"
                launchctl kickstart -k "$stream_label" >"$stdout_log" 2>"$stderr_log"
                status=$?
                printf 'RUN_ID=%s\nDESTINATION=%s\n' \
                    "$stream_run_id" "$destination" >>"$stdout_log"
            fi
            ;;
        stop-xodus-hogwarts-stream)
            launchctl kill SIGINT "gui/$(id -u)/com.xodus.hogwarts-stream" \
                >"$stdout_log" 2>"$stderr_log"
            status=$?
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
