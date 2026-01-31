#!/bin/sh
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
FONT_URL="https://github.com/ryanoasis/nerd-fonts/releases/download/v3.1.1/JetBrainsMono.zip"
FONT_DEST="$HOME/.local/share/fonts"

log() {
    printf "[install] %s\n" "$*"
}

warn() {
    printf "[install] WARNING: %s\n" "$*" >&2
}

ensure_packages() {
    log "Detecting package manager..."

    if command -v apt-get >/dev/null 2>&1; then
        log "Using apt-get to install dependencies"
        sudo apt-get update
        sudo apt-get install -y build-essential libncurses-dev libmagic-dev xclip curl unzip
    elif command -v pacman >/dev/null 2>&1; then
        log "Using pacman to install dependencies"
        sudo pacman -Syu --noconfirm
        sudo pacman -S --noconfirm base-devel ncurses file xclip curl unzip
    elif command -v dnf >/dev/null 2>&1; then
        log "Using dnf to install dependencies"
        sudo dnf install -y gcc make ncurses-devel libmagic-devel file xclip curl unzip
    else
        warn "Package manager not detected. Please install gcc, make, ncurses-dev, libmagic-dev, xclip, curl, and unzip manually."
    fi
}

install_fonts() {
    mkdir -p "$FONT_DEST"
    tmpdir="$(mktemp -d)"
    trap 'rm -rf "$tmpdir"' EXIT

    log "Downloading JetBrainsMono Nerd Font..."
    curl -fLo "$tmpdir/JetBrainsMono.zip" "$FONT_URL"

    log "Installing Nerd Font to $FONT_DEST"
    unzip -qo "$tmpdir/JetBrainsMono.zip" -d "$FONT_DEST"

    if command -v fc-cache >/dev/null 2>&1; then
        fc-cache -fv "$FONT_DEST"
    else
        warn "fc-cache not available; please run 'fc-cache -fv $FONT_DEST' after installation"
    fi
}

build_project() {
    log "Building CupidFM"
    (cd "$ROOT_DIR" && make clean && make)
}

install_binary() {
    DEST="/usr/bin/cupidfm"
    log "Copying built binary to $DEST"
    sudo install -m 755 "$ROOT_DIR/cupidfm" "$DEST"
}

log "Starting CupidFM installer"
ensure_packages
install_fonts
build_project
install_binary
log "CupidFM installed successfully"
