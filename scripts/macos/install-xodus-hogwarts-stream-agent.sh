#!/usr/bin/env bash

set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "This installer must run on macOS." >&2
    exit 1
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cli="$repo_root/target/release/xodus-cli"
destination="$HOME/Games/Xodus/HogwartsLegacy-Xbox"
log_dir="$HOME/Library/Logs/XodusRemote"
launch_agents="$HOME/Library/LaunchAgents"
label="com.xodus.hogwarts-stream"
plist="$launch_agents/$label.plist"
uid="$(id -u)"

if [[ ! -x "$cli" ]]; then
    echo "Release xodus-cli is missing. Run cargo build --release --workspace first." >&2
    exit 1
fi

mkdir -p "$destination" "$log_dir" "$launch_agents"

cat >"$plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>$label</string>
    <key>ProgramArguments</key>
    <array>
        <string>/usr/bin/caffeinate</string>
        <string>-dimsu</string>
        <string>$cli</string>
        <string>streaming</string>
        <string>9MT5NJ5W7B8Z</string>
        <string>$destination</string>
        <string>--parallel</string>
        <string>8</string>
        <string>--market</string>
        <string>GB</string>
    </array>
    <key>WorkingDirectory</key>
    <string>$repo_root</string>
    <key>EnvironmentVariables</key>
    <dict>
        <key>HOME</key>
        <string>$HOME</string>
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
    <string>$log_dir/hogwarts-xbox-stream.stdout.log</string>
    <key>StandardErrorPath</key>
    <string>$log_dir/hogwarts-xbox-stream.stderr.log</string>
</dict>
</plist>
EOF

plutil -lint "$plist"
launchctl bootout "gui/$uid/$label" 2>/dev/null || true
sudo launchctl bootstrap "gui/$uid" "$plist"
sudo launchctl enable "gui/$uid/$label"

echo "Installed $label."
