#!/usr/bin/env bash

set -euo pipefail

cx_root="/Applications/CrossOver.app/Contents/SharedSupport/CrossOver"
build="$HOME/src/build/crossover-wine-xodus-full-26.3.0-x86_64"
bottles_root="$HOME/Library/Application Support/CrossOver/Bottles"
bottle="HogwartsControl"
if [[ -d "$bottles_root/GroundedControl" && ! -d "$bottles_root/$bottle" ]]; then
    bottle="GroundedControl"
fi

if [[ ! -x "$build/wine" || ! -x "$build/server/wineserver" ]]; then
    echo "Private Xodus CrossOver Wine build is missing." >&2
    exit 1
fi

export CX_ROOT="$cx_root"
export CX_HOME="$HOME/Library/Application Support/CrossOver"
export CX_BOTTLE="$bottle"
export CX_BOTTLE_PATH="$bottles_root"
export CX_MANAGED_BOTTLE_PATH="/Library/Application Support/CrossOver/Bottles"
export CX_GRAPHICS_BACKEND="d3dmetal"
export CX_APPLEGPTK_LIBD3DSHARED_PATH="$cx_root/lib64/apple_gptk/external/libd3dshared.dylib"
export WINEPREFIX="$bottles_root/$bottle"
export WINESERVER="$build/server/wineserver"
export WINELOADER="$build/wine"
export WINEWRAPPER="$build/dlls/winewrapper/x86_64-windows/winewrapper.exe"
export WINEDLLPATH="$build/dlls/bcrypt:$build/dlls/secur32:$build/dlls/ws2_32:$cx_root/lib64/apple_gptk/wine/x86_64-windows"
export DYLD_FALLBACK_LIBRARY_PATH="$cx_root/lib64${DYLD_FALLBACK_LIBRARY_PATH:+:$DYLD_FALLBACK_LIBRARY_PATH}"
export GST_PLUGIN_SYSTEM_PATH="$cx_root/lib64/gstreamer-1.0"
export GST_REGISTRY="$CX_HOME/gstreamer-1.0-registry.x86_64.bin"
export PATH="$cx_root/bin:$HOME/.cargo/bin:/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin"
export MTL_HUD_ENABLED="${MTL_HUD_ENABLED:-1}"
export DXVK_HUD="${DXVK_HUD:-fps,frametimes,gpuload,memory}"
export WINEDEBUG="${XODUS_WINE_DEBUG:--all}"
export WINEDLLOVERRIDES="ws2_32=b;bcrypt=b;secur32=b;windows.web=b;xgameruntime=n,b${WINEDLLOVERRIDES:+;$WINEDLLOVERRIDES}"

exec "$build/wine" "$@"
