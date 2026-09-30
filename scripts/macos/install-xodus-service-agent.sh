#!/usr/bin/env bash

set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "This installer must run on macOS." >&2
    exit 1
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
service="$repo_root/target/release/xodus-service"
log_dir="$HOME/Library/Logs/XodusRemote"
launch_agents="$HOME/Library/LaunchAgents"
label="com.xodus.service"
plist="$launch_agents/$label.plist"
uid="$(id -u)"

if [[ ! -x "$service" ]]; then
    echo "Release xodus-service is missing. Run cargo build --release --workspace first." >&2
    exit 1
fi

mkdir -p "$log_dir" "$launch_agents"

cat >"$plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>$label</string>
    <key>ProgramArguments</key>
    <array>
        <string>$service</string>
    </array>
    <key>WorkingDirectory</key>
    <string>$repo_root</string>
    <key>EnvironmentVariables</key>
    <dict>
        <key>PATH</key>
        <string>$HOME/.cargo/bin:/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin</string>
        <key>XODUS_LOG</key>
        <string>warn</string>
    </dict>
    <key>RunAtLoad</key>
    <false/>
    <key>KeepAlive</key>
    <false/>
    <key>LimitLoadToSessionType</key>
    <string>Aqua</string>
    <key>ProcessType</key>
    <string>Background</string>
    <key>StandardOutPath</key>
    <string>$log_dir/xodus-service.stdout.log</string>
    <key>StandardErrorPath</key>
    <string>$log_dir/xodus-service.stderr.log</string>
</dict>
</plist>
EOF

plutil -lint "$plist"
launchctl bootout "gui/$uid/$label" 2>/dev/null || true
sudo launchctl bootstrap "gui/$uid" "$plist"
sudo launchctl enable "gui/$uid/$label"

echo "Installed $label."
