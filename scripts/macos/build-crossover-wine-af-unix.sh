#!/usr/bin/env bash

set -euo pipefail

crossover_version="26.3.0"
crossover_sha256="ac99c8ca4b3848f3e81784135f023df266b61c2345726ea55a50b3e030dd6872"
xodus_wine_commit="6b7313c1bd"
source_root="$HOME/src"
archive="$source_root/crossover-sources-$crossover_version.tar.gz"
base_source="$source_root/crossover-sources-$crossover_version/wine"
patched_source="$source_root/crossover-wine-xodus-afunix-$crossover_version"
build_dir="$source_root/build/crossover-wine-xodus-afunix-$crossover_version"
install_prefix="$HOME/.local/xodus-crossover-wine"
patch_file="$source_root/xodus-af-unix-$xodus_wine_commit.patch"
source_url="https://media.codeweavers.com/pub/crossover/source/crossover-sources-$crossover_version.tar.gz"

if [[ "$(uname -s)" != "Darwin" || "$(uname -m)" != "arm64" ]]; then
    echo "This script currently supports Apple Silicon macOS only." >&2
    exit 1
fi

for command_name in brew git make shasum x86_64-w64-mingw32-gcc; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Missing required command: $command_name" >&2
        exit 1
    fi
done

if [[ ! -x /opt/homebrew/opt/bison/bin/bison ]]; then
    echo "Homebrew Bison is required. Run bootstrap.sh --with-runtime-toolchain." >&2
    exit 1
fi

mkdir -p "$source_root"

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

printf '%s  %s\n' "$crossover_sha256" "$archive" | shasum -a 256 -c -

if [[ ! -x "$base_source/configure" ]]; then
    mkdir -p "$source_root/crossover-sources-$crossover_version"
    tar -xzf "$archive" \
        -C "$source_root/crossover-sources-$crossover_version" \
        --strip-components=1
fi

xodus_wine="$source_root/xodus-wine"
if [[ ! -d "$xodus_wine/.git" ]]; then
    git clone \
        --filter=blob:none \
        --no-checkout \
        --single-branch \
        --branch bleeding-edge \
        https://github.com/xodus-gaming/wine.git \
        "$xodus_wine"
fi
git -C "$xodus_wine" fetch origin bleeding-edge

if [[ "$(git -C "$xodus_wine" rev-parse "$xodus_wine_commit")" != "$xodus_wine_commit"* ]]; then
    echo "Could not resolve Xodus Wine commit $xodus_wine_commit." >&2
    exit 1
fi

git -C "$xodus_wine" show --format=email --binary "$xodus_wine_commit" >"$patch_file"

if [[ ! -f "$patched_source/.xodus-afunix-applied" ]]; then
    rm -rf "$patched_source"
    if cp -cR "$base_source" "$patched_source" 2>/dev/null; then
        :
    else
        cp -R "$base_source" "$patched_source"
    fi
    git -C "$patched_source" apply --check "$patch_file"
    git -C "$patched_source" apply "$patch_file"
    printf '%s\n' "$xodus_wine_commit" >"$patched_source/.xodus-afunix-applied"
fi

export PATH="/opt/homebrew/opt/bison/bin:/opt/homebrew/bin:$PATH"
export PKG_CONFIG_PATH="/opt/homebrew/opt/freetype/lib/pkgconfig:${PKG_CONFIG_PATH:-}"

if [[ ! -f "$build_dir/Makefile" ]]; then
    mkdir -p "$build_dir"
    (
        cd "$build_dir"
        "$patched_source/configure" \
            --prefix="$install_prefix" \
            --enable-archs=x86_64 \
            --disable-tests \
            --without-x \
            --without-wayland \
            --without-vulkan
    )
fi

make -C "$build_dir" -j"$(sysctl -n hw.ncpu)"

echo "Patched Wine build completed at $build_dir."
echo "No files were copied into the installed CrossOver application."
