#!/usr/bin/env bash

set -u

failures=0

section() {
    printf '\n== %s ==\n' "$1"
}

required_command() {
    local name="$1"
    if command -v "$name" >/dev/null 2>&1; then
        printf '%-14s %s\n' "$name" "$(command -v "$name")"
    else
        printf '%-14s MISSING\n' "$name"
        failures=$((failures + 1))
    fi
}

optional_command() {
    local name="$1"
    if command -v "$name" >/dev/null 2>&1; then
        printf '%-14s %s\n' "$name" "$(command -v "$name")"
    else
        printf '%-14s not installed\n' "$name"
    fi
}

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "This doctor script must run on macOS." >&2
    exit 1
fi

if [[ -x /opt/homebrew/bin/brew ]]; then
    eval "$(/opt/homebrew/bin/brew shellenv)"
fi
export PATH="$HOME/.cargo/bin:$PATH"

section "System"
sw_vers
printf 'Architecture: %s\n' "$(uname -m)"
printf 'Hardware: %s\n' "$(sysctl -n hw.model 2>/dev/null || echo unknown)"
printf 'GUI user: %s\n' "$(stat -f '%Su' /dev/console 2>/dev/null || echo unknown)"
df -h /

section "Apple developer tools"
if developer_dir="$(xcode-select -p 2>/dev/null)"; then
    printf 'Developer directory: %s\n' "$developer_dir"
    clang --version | head -n 1
else
    echo "Command Line Tools: MISSING"
    failures=$((failures + 1))
fi

section "Required commands"
for command_name in git brew rustup rustc cargo protoc python3 node npm playwright; do
    required_command "$command_name"
done

section "Supporting commands"
for command_name in cmake ninja pkg-config jq tmux shellcheck gh x86_64-w64-mingw32-gcc; do
    optional_command "$command_name"
done
if [[ -x /opt/homebrew/opt/bison/bin/bison ]]; then
    printf '%-14s %s\n' "brew-bison" "/opt/homebrew/opt/bison/bin/bison"
else
    printf '%-14s not installed\n' "brew-bison"
fi
if [[ -x "$HOME/.local/xodus-wine-tools/bin/widl" ]]; then
    printf '%-14s %s\n' "wine-widl" "$HOME/.local/xodus-wine-tools/bin/widl"
else
    printf '%-14s not built\n' "wine-widl"
fi

section "Versions"
git --version 2>/dev/null || true
brew --version 2>/dev/null | head -n 1 || true
rustup --version 2>/dev/null | head -n 1 || true
rustc --version 2>/dev/null || true
cargo --version 2>/dev/null || true
protoc --version 2>/dev/null || true
cmake --version 2>/dev/null | head -n 1 || true
ninja --version 2>/dev/null || true
pkg-config --version 2>/dev/null || true
jq --version 2>/dev/null || true
node --version 2>/dev/null || true
npm --version 2>/dev/null || true
playwright --version 2>/dev/null || true
tmux -V 2>/dev/null || true
shellcheck --version 2>/dev/null | head -n 2 || true
gh --version 2>/dev/null | head -n 1 || true
x86_64-w64-mingw32-gcc --version 2>/dev/null | head -n 1 || true
/opt/homebrew/opt/bison/bin/bison --version 2>/dev/null | head -n 1 || true

section "Rosetta"
if pgrep oahd >/dev/null 2>&1 || arch -x86_64 /usr/bin/true >/dev/null 2>&1; then
    echo "Rosetta 2: installed"
else
    echo "Rosetta 2: not installed"
fi

section "CrossOver"
crossover_path=""
for candidate in "/Applications/CrossOver.app" "$HOME/Applications/CrossOver.app"; do
    if [[ -d "$candidate" ]]; then
        crossover_path="$candidate"
        break
    fi
done

if [[ -n "$crossover_path" ]]; then
    crossover_version="$(defaults read "$crossover_path/Contents/Info" CFBundleShortVersionString 2>/dev/null || echo unknown)"
    printf 'Path: %s\nVersion: %s\n' "$crossover_path" "$crossover_version"
else
    echo "CrossOver: not installed"
fi

section "Microsoft Edge"
edge_path="/Applications/Microsoft Edge.app"
if [[ -d "$edge_path" ]]; then
    edge_version="$(defaults read "$edge_path/Contents/Info" CFBundleShortVersionString 2>/dev/null || echo unknown)"
    printf 'Path: %s\nVersion: %s\n' "$edge_path" "$edge_version"
else
    echo "Microsoft Edge: not installed"
    failures=$((failures + 1))
fi

section "GUI launch agent"
for label in com.xodus.remote-launch com.xodus.service; do
    launch_agent="gui/$(id -u)/$label"
    if launchctl print "$launch_agent" >/dev/null 2>&1; then
        printf 'LaunchAgent: loaded (%s)\n' "$launch_agent"
    else
        printf 'LaunchAgent: not loaded (%s)\n' "$launch_agent"
        failures=$((failures + 1))
    fi
done

section "CrossOver performance overlay"
bottles_root="$HOME/Library/Application Support/CrossOver/Bottles"
if [[ -d "$bottles_root" ]]; then
    shopt -s nullglob
    bottle_configs=("$bottles_root"/*/cxbottle.conf)
    if ((${#bottle_configs[@]} == 0)); then
        echo "No CrossOver bottles found"
    fi
    for config in "${bottle_configs[@]}"; do
        bottle_name="$(basename "$(dirname "$config")")"
        if grep -Fq '"MTL_HUD_ENABLED" = "1"' "$config" &&
            grep -Fq '"DXVK_HUD" = "fps,frametimes,gpuload,memory"' "$config"; then
            printf '%-20s enabled\n' "$bottle_name"
        else
            printf '%-20s disabled\n' "$bottle_name"
            failures=$((failures + 1))
        fi
    done
else
    echo "No CrossOver bottles directory found"
fi

section "Repository"
if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    printf 'Root: %s\n' "$(git rev-parse --show-toplevel)"
    printf 'Branch: %s\n' "$(git branch --show-current)"
    printf 'Commit: %s\n' "$(git rev-parse HEAD)"
else
    echo "Not running inside a Git worktree"
fi

if ((failures > 0)); then
    printf '\nDoctor found %d missing required component(s).\n' "$failures" >&2
    exit 1
fi

echo
echo "Mac build prerequisites are present."
