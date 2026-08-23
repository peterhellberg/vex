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
    # Preferred mirror is pkg.hexops.org, which stores archives as
    # zig/<zig-triple>-<version>.tar.xz. If it does not have the build, fall back
    # to mirrors.nektro.net, which stores
    # zig/<version>/zig-<triple>-<version>.tar.xz with '+' percent-encoded
    # (e.g. 0.17.0-dev.387%2B31f157d80) but only over plain HTTP. Either way the
    # archive is verified against Zig's official minisign signature before
    # extraction.
    VERSION_ENC=$(printf '%s' "$VERSION" | sed 's/+/%2B/g')
    TMP=$(mktemp -d)
    trap 'rm -rf "$TMP"' EXIT

    for BASE in \
        "https://pkg.hexops.org/zig/zig-$TRIPLE-$VERSION" \
        "http://mirrors.nektro.net/zig/$VERSION_ENC/zig-$TRIPLE-$VERSION_ENC"; do
        echo "install-zig: fetching $BASE.tar.xz"
        if curl --fail --location --silent --show-error "$BASE.tar.xz" -o "$TMP/zig.tar.xz" &&
           curl --fail --location --silent --show-error "$BASE.tar.xz.minisig" -o "$TMP/zig.tar.xz.minisig"; then
            break
        fi
        rm -f "$TMP/zig.tar.xz" "$TMP/zig.tar.xz.minisig"
    done

    if [ ! -s "$TMP/zig.tar.xz" ]; then
        echo "install-zig: could not fetch zig $VERSION from any known mirror" >&2
        exit 1
    fi

    VERIFY=""
    for tool in minisign rsig; do
        if command -v "$tool" >/dev/null 2>&1; then VERIFY=$tool; break; fi
    done

    # Bootstrap minisign into $TMP when not installed. Fetched over HTTPS from
    # the official jedisct1/minisign GitHub release.
    if [ -z "$VERIFY" ] && [ "${ALLOW_UNSIGNED:-0}" != 1 ]; then
        case "$(uname -s)/$(uname -m)" in
            Linux/x86_64)
                curl --fail --location --silent --show-error \
                    "https://github.com/jedisct1/minisign/releases/download/0.12/minisign-0.12-linux.tar.gz" \
                    | tar -xzf - -C "$TMP" minisign-linux/x86_64/minisign
                VERIFY="$TMP/minisign-linux/x86_64/minisign" ;;
            Linux/aarch64)
                curl --fail --location --silent --show-error \
                    "https://github.com/jedisct1/minisign/releases/download/0.12/minisign-0.12-linux.tar.gz" \
                    | tar -xzf - -C "$TMP" minisign-linux/aarch64/minisign
                VERIFY="$TMP/minisign-linux/aarch64/minisign" ;;
            Darwin/arm64|Darwin/x86_64)
                curl --fail --location --silent --show-error \
                    "https://github.com/jedisct1/minisign/releases/download/0.12/minisign-0.12-macos.zip" \
                    -o "$TMP/minisign.zip"
                unzip -oq "$TMP/minisign.zip" -d "$TMP"
                VERIFY="$TMP/minisign" ;;
        esac
        chmod +x "$VERIFY" 2>/dev/null || true
    fi

    if [ -n "$VERIFY" ]; then
        # Zig's official minisign public key (from ziglang.org/download/).
        printf 'untrusted comment: Zig minisign public key\nRWSGOq2NVecA2UPNdBUZykf1CCb147pkmdtYxgb3Ti+JO/wCYvhbAb/U\n' \
            > "$TMP/key.pub"
        "$VERIFY" -V -p "$TMP/key.pub" \
            -m "$TMP/zig.tar.xz" -x "$TMP/zig.tar.xz.minisig" >/dev/null
        echo "install-zig: signature verified"
    elif [ "${ALLOW_UNSIGNED:-0}" = 1 ]; then
        echo "install-zig: WARNING: cannot verify plain HTTP download (no minisign/rsig); continuing via ALLOW_UNSIGNED=1" >&2
    else
        echo "install-zig: refusing to install unverified plain HTTP download; install minisign (https://jedisct1.github.io/minisign/) or set ALLOW_UNSIGNED=1 to override" >&2
        exit 1
    fi

    mkdir -p "$DEST"
    tar -xJ --strip-components=1 -C "$DEST" -f "$TMP/zig.tar.xz"
fi

mkdir -p "$HOME/.local/bin"
ln -sf "$DEST/zig" "$HOME/.local/bin/zig"

echo "install-zig: zig $VERSION -> $HOME/.local/bin/zig"
