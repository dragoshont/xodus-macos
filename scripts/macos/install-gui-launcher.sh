#!/usr/bin/env bash

set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "This installer must run on macOS." >&2
    exit 1
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
state_root="$HOME/Library/Application Support/XodusRemote"
bin_dir="$state_root/bin"
request_dir="$state_root/requests"
processed_dir="$state_root/processed"
log_dir="$HOME/Library/Logs/XodusRemote"
launch_agents="$HOME/Library/LaunchAgents"
label="com.xodus.remote-launch"
plist="$launch_agents/$label.plist"
uid="$(id -u)"

mkdir -p "$bin_dir" "$request_dir" "$processed_dir" "$log_dir" "$launch_agents"
chmod 700 "$state_root" "$bin_dir" "$request_dir" "$processed_dir"
install -m 700 "$repo_root/scripts/macos/gui-launcher.sh" "$bin_dir/gui-launcher.sh"

cat >"$plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>$label</string>
    <key>ProgramArguments</key>
    <array>
        <string>/bin/bash</string>
        <string>$bin_dir/gui-launcher.sh</string>
    </array>
    <key>RunAtLoad</key>
    <true/>
    <key>WatchPaths</key>
    <array>
        <string>$request_dir</string>
    </array>
    <key>LimitLoadToSessionType</key>
    <string>Aqua</string>
    <key>ProcessType</key>
    <string>Interactive</string>
    <key>StandardOutPath</key>
    <string>$log_dir/launch-agent.stdout.log</string>
    <key>StandardErrorPath</key>
    <string>$log_dir/launch-agent.stderr.log</string>
</dict>
</plist>
EOF

plutil -lint "$plist"
launchctl bootout "gui/$uid/$label" 2>/dev/null || true
sudo launchctl bootstrap "gui/$uid" "$plist"
sudo launchctl enable "gui/$uid/$label"
sudo launchctl kickstart -k "gui/$uid/$label"

echo "Installed and started $label."
