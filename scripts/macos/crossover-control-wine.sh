#!/usr/bin/env bash

set -euo pipefail

bottles_root="$HOME/Library/Application Support/CrossOver/Bottles"
bottle="HogwartsControl"
if [[ -d "$bottles_root/GroundedControl" && ! -d "$bottles_root/$bottle" ]]; then
    bottle="GroundedControl"
fi

exec "/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin/wine" \
    --bottle "$bottle" \
    "$@"
