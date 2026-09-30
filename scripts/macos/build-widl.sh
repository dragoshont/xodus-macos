#!/usr/bin/env bash

set -euo pipefail

version="26.3.0"
archive_sha256="ac99c8ca4b3848f3e81784135f023df266b61c2345726ea55a50b3e030dd6872"
source_root="$HOME/src"
archive="$source_root/crossover-sources-$version.tar.gz"
source_dir="$source_root/crossover-sources-$version"
wine_source="$source_dir/wine"
build_dir="$source_root/build/crossover-wine-tools-$version"
install_dir="$HOME/.local/xodus-wine-tools/bin"
source_url="https://media.codeweavers.com/pub/crossover/source/crossover-sources-$version.tar.gz"

if [[ "$(uname -s)" != "Darwin" || "$(uname -m)" != "arm64" ]]; then
    echo "This script currently supports Apple Silicon macOS only." >&2
    exit 1
fi

for command_name in curl make shasum x86_64-w64-mingw32-gcc; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing required command: $command_name" >&2
        exit 1
    fi
done

if [[ ! -x /opt/homebrew/opt/bison/bin/bison ]]; then
    echo "Homebrew Bison is required. Run bootstrap.sh --with-runtime-toolchain." >&2
    exit 1
fi

mkdir -p "$source_root" "$build_dir" "$install_dir"

if [[ ! -f "$archive" ]]; then
    curl \
        --proto '=https' \
        --tlsv1.2 \
        --fail \
        --location \
        --retry 3 \
        --output "$archive" \
        "$source_url"
fi

printf '%s  %s\n' "$archive_sha256" "$archive" | shasum -a 256 -c -

if [[ ! -x "$wine_source/configure" ]]; then
    mkdir -p "$source_dir"
    tar -xzf "$archive" -C "$source_dir" --strip-components=1
fi

export PATH="/opt/homebrew/opt/bison/bin:/opt/homebrew/bin:$PATH"
export PKG_CONFIG_PATH="/opt/homebrew/opt/freetype/lib/pkgconfig:${PKG_CONFIG_PATH:-}"

if [[ ! -f "$build_dir/Makefile" ]]; then
    (
        cd "$build_dir"
        "$wine_source/configure" \
            --enable-archs=x86_64 \
            --disable-tests \
            --without-x \
            --without-wayland \
            --without-vulkan
    )
fi

make -C "$build_dir" -j"$(sysctl -n hw.ncpu)" tools/widl/widl

real_widl="$build_dir/tools/widl/widl"
cat >"$install_dir/widl" <<EOF
#!/bin/bash
exec "$real_widl" -I "$wine_source/include" "\$@"
EOF
chmod 700 "$install_dir/widl"

"$install_dir/widl" -V
printf 'Installed xgameruntime-compatible widl wrapper: %s\n' "$install_dir/widl"
