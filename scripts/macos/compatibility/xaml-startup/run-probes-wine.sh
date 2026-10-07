#!/bin/bash
# Runs every probe-manifest.txt line under the stage Wine build; pair with run-probes-native.ps1.
# Usage (wrap in the stage guard): ./guard.sh 1200 200 ./run-probes-wine.sh <probe dir> <manifest> <out dir>
set -u
S=~/xodus-runs/xbox-service-principal-20261007-001
BIN=$1; MANIFEST=$2; OUT=$3
export WINEPREFIX=$S/prefix WINESERVER=$S/build/server/wineserver
export WINEDLLPATH=$S/build/dlls:/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/lib/wine/x86_64-windows
export DYLD_FALLBACK_LIBRARY_PATH=/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/lib64
# same load order as the original-app runs (run-sp-model.sh)
export WINEDLLOVERRIDES='winemac.drv=;combase=b;ole32=b;rpcrt4=b;wintypes=n;coremessaging=n;rmclient=n;twinapi.appcore=n;windows.ui.xaml=n;umpdc=n;api-ms-win-ham-apphistory-l1-1-0=n;threadpoolwinrt=n'
W=$S/build/loader/wine
mkdir -p "$OUT" "$WINEPREFIX/drive_c/paired"
# same headless graphics setup as run-sp-model.sh (D3D11 device for audit-gpuprio)
WINEDEBUG=-all $W reg add 'HKCU\Software\Wine\Drivers' /v Graphics /d null /f > /dev/null 2>&1
grep -v '^#' "$MANIFEST" | while read -r exe args; do
    [ -n "$exe" ] || continue
    cp -f "$BIN/$exe" "$WINEPREFIX/drive_c/paired/$exe"
    name=${exe%.exe}
    for a in $args; do name="${name}_$a"; done
    # SIP strips DYLD_* when the protected /usr/bin/perl starts, so re-export it inside perl
    (cd "$WINEPREFIX/drive_c/paired" && WINEDEBUG=-all perl -e '$ENV{DYLD_FALLBACK_LIBRARY_PATH} = shift; alarm 30; exec @ARGV' \
        "$DYLD_FALLBACK_LIBRARY_PATH" "$W" "C:\\paired\\$exe" $args) \
        > "$OUT/$name.txt" 2>&1 < /dev/null
    rc=$?
    [ $rc -eq 142 ] && rc=timeout
    printf 'EXIT=%s\n' "$rc" >> "$OUT/$name.txt"
    tr -d '\r' < "$OUT/$name.txt" > "$OUT/$name.tmp" && mv "$OUT/$name.tmp" "$OUT/$name.txt"
    echo "$name.txt $rc"
done
WINEDEBUG=-all $W reg delete 'HKCU\Software\Wine\Drivers' /v Graphics /f > /dev/null 2>&1
perl -e 'alarm 20; exec @ARGV' "$WINESERVER" -k 2>/dev/null
exit 0
