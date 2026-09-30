#!/usr/bin/env bash

set -euo pipefail

port="${1:-9222}"
profile_dir="${XODUS_EDGE_PROFILE:-$HOME/Library/Application Support/XodusEdgeCDP}"

if ! [[ "$port" =~ ^[0-9]+$ ]] || ((port < 1024 || port > 65535)); then
    echo "Usage: $0 [port from 1024 through 65535]" >&2
    exit 2
fi

edge="/Applications/Microsoft Edge.app"
if [[ ! -d "$edge" ]]; then
    echo "Microsoft Edge is not installed at $edge." >&2
    exit 1
fi

mkdir -p "$profile_dir"

open -na "$edge" --args \
    --remote-debugging-address=127.0.0.1 \
    --remote-debugging-port="$port" \
    --user-data-dir="$profile_dir"

endpoint="http://127.0.0.1:$port/json/version"
for _ in {1..30}; do
    if curl --fail --silent "$endpoint"; then
        echo
        echo "Edge CDP is listening on localhost port $port."
        exit 0
    fi
    sleep 1
done

echo "Edge launched, but its CDP endpoint did not become ready at $endpoint." >&2
exit 1
