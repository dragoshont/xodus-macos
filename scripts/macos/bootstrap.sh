#!/usr/bin/env bash

set -euo pipefail

install_crossover=false

if [[ "${1:-}" == "--with-crossover" ]]; then
    install_crossover=true
elif [[ $# -gt 0 ]]; then
    echo "Usage: $0 [--with-crossover]" >&2
    exit 2
fi

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "This bootstrap script must run on macOS." >&2
    exit 1
fi

if [[ "$(uname -m)" != "arm64" ]]; then
    echo "This setup currently targets Apple Silicon Macs." >&2
    exit 1
fi

if ! xcode-select -p >/dev/null 2>&1; then
    cat >&2 <<'EOF'
Apple Command Line Tools are required.

Install them with:
  xcode-select --install

Or install the listed package with softwareupdate, then rerun this script.
EOF
    exit 2
fi

if ! command -v brew >/dev/null 2>&1; then
    echo "Installing Homebrew from the official installer..."
    NONINTERACTIVE=1 /bin/bash -c \
        "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
fi

eval "$(/opt/homebrew/bin/brew shellenv)"

brew_init="eval \"\$(/opt/homebrew/bin/brew shellenv)\""
touch "$HOME/.zprofile"
if ! grep -Fqx "$brew_init" "$HOME/.zprofile"; then
    printf '\n%s\n' "$brew_init" >>"$HOME/.zprofile"
fi

brew update
brew install git protobuf cmake ninja pkg-config jq node tmux shellcheck gh

if ! command -v rustup >/dev/null 2>&1; then
    echo "Installing rustup from the official installer..."
    curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs |
        sh -s -- -y --profile minimal --default-toolchain 1.98.0
fi

export PATH="$HOME/.cargo/bin:$PATH"
rustup toolchain install 1.98.0 --profile minimal
rustup default 1.98.0
rustup component add clippy rustfmt

brew install --cask microsoft-edge
PLAYWRIGHT_SKIP_BROWSER_DOWNLOAD=1 npm install --global playwright

if [[ "$install_crossover" == true ]]; then
    brew install --cask crossover
fi

echo
echo "Bootstrap complete. Open a new shell or run:"
echo "  $brew_init"
echo "  export PATH=\"\$HOME/.cargo/bin:\$PATH\""
