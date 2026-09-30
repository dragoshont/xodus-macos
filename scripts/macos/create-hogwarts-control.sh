#!/usr/bin/env bash

set -euo pipefail

bottles_root="$HOME/Library/Application Support/CrossOver/Bottles"
default_bottle="HogwartsControl"
if [[ -d "$bottles_root/GroundedControl" && ! -d "$bottles_root/$default_bottle" ]]; then
    default_bottle="GroundedControl"
fi
bottle="${XODUS_STEAM_BOTTLE:-$default_bottle}"
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
        --description "Hogwarts Legacy Steam control with D3DMetal" \
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
