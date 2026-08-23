#!/bin/sh
# Installs the exact Zig build pinned by build.zig.zon
# (.minimum_zig_version) into ~/.local/zig-<version> and symlinks it as
# ~/.local/bin/zig, so `zig build` works without hunting down a community
# mirror by hand. Override the destination with ZIG_DIR=/some/dir.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
VERSION=$(awk -F'"' '/\.minimum_zig_version/ {print $2; exit}' "$ROOT/build.zig.zon")

if [ -z "$VERSION" ]; then
    echo "install-zig: cannot determine pinned version from build.zig.zon" >&2
    exit 1
fi

case "$(uname -s)/$(uname -m)" in
    Linux/x86_64)  TRIPLE=x86_64-linux ;;
    Linux/aarch64) TRIPLE=aarch64-linux ;;
    Darwin/arm64)  TRIPLE=aarch64-macos ;;
    Darwin/x86_64) TRIPLE=x86_64-macos ;;
    *) echo "install-zig: unsupported platform $(uname -s)/$(uname -m)" >&2; exit 1 ;;
esac

DEST="${ZIG_DIR:-$HOME/.local/zig-$VERSION}"

if [ ! -x "$DEST/zig" ]; then
    URL="https://pkg.hexops.org/zig/$TRIPLE-$VERSION.tar.xz"
    echo "install-zig: fetching $URL"
    mkdir -p "$DEST"
    curl --fail --location --silent --show-error "$URL" \
        | tar -xJ --strip-components=1 -C "$DEST"
fi

mkdir -p "$HOME/.local/bin"
ln -sf "$DEST/zig" "$HOME/.local/bin/zig"

echo "install-zig: zig $VERSION -> $HOME/.local/bin/zig"
