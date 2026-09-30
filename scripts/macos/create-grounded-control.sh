#!/usr/bin/env bash

set -euo pipefail

bottle="${XODUS_GROUNDED_BOTTLE:-GroundedControl}"
cxroot="/Applications/CrossOver.app/Contents/SharedSupport/CrossOver"
cxbottle="$cxroot/bin/cxbottle"
cxwine="$cxroot/bin/wine"
bottle_dir="$HOME/Library/Application Support/CrossOver/Bottles/$bottle"
cache_dir="$HOME/Library/Caches/Xodus"
installer="$cache_dir/SteamSetup.exe"
steam_url="https://cdn.akamai.steamstatic.com/client/installer/SteamSetup.exe"

if [[ ! -x "$cxbottle" || ! -x "$cxwine" ]]; then
    echo "CrossOver is not installed in /Applications." >&2
    exit 1
fi

if [[ ! -d "$bottle_dir" ]]; then
    "$cxbottle" \
        --bottle "$bottle" \
        --create \
        --template win10_64 \
        --description "Grounded Steam control with D3DMetal" \
        --param "EnvironmentVariables:CX_GRAPHICS_BACKEND=d3dmetal"
fi

if ! grep -Fq '"CX_GRAPHICS_BACKEND" = "d3dmetal"' "$bottle_dir/cxbottle.conf"; then
    echo "Bottle $bottle exists but is not configured for D3DMetal." >&2
    echo "Enable D3DMetal in CrossOver before continuing." >&2
    exit 1
fi

steam="$bottle_dir/drive_c/Program Files (x86)/Steam/Steam.exe"
if [[ ! -f "$steam" ]]; then
    mkdir -p "$cache_dir"
    curl \
        --proto '=https' \
        --tlsv1.2 \
        --fail \
        --location \
        --retry 3 \
        --output "$installer" \
        "$steam_url"
    shasum -a 256 "$installer"
    "$cxwine" --bottle "$bottle" --wait-children "$installer" /S
fi

printf 'Bottle: %s\n' "$bottle"
printf 'Graphics backend: d3dmetal\n'
printf 'Steam executable: %s\n' "$steam"
echo "Open CrossOver in the logged-in Mac desktop to sign into Steam."
