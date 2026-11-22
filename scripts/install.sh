#!/usr/bin/env bash
# scripts/install.sh
# ───────────────────
# Installs axon to /usr/local/bin (Linux & macOS).
# Usage:
#   ./scripts/install.sh              # installs from ./build/axon
#   ./scripts/install.sh /custom/path # installs from custom path

set -e

BINARY=${1:-"./build/axon"}
INSTALL_DIR="/usr/local/bin"

echo "╔══════════════════════════════════════╗"
echo "║  Axon Language — Install             ║"
echo "╚══════════════════════════════════════╝"

# ── Verify binary exists ──────────────────────────────────────
if [ ! -f "$BINARY" ]; then
    echo "❌ Binary not found: $BINARY"
    echo "   Build first with: ./scripts/build.sh release"
    exit 1
fi

# ── Install ───────────────────────────────────────────────────
echo "Installing axon → $INSTALL_DIR/axon"

if [ -w "$INSTALL_DIR" ]; then
    cp "$BINARY" "$INSTALL_DIR/axon"
    chmod +x "$INSTALL_DIR/axon"
else
    echo "  (requires sudo)"
    sudo cp "$BINARY" "$INSTALL_DIR/axon"
    sudo chmod +x "$INSTALL_DIR/axon"
fi

# ── Verify ────────────────────────────────────────────────────
if command -v axon &> /dev/null; then
    echo ""
    echo "✅ axon installed successfully."
    echo ""
    axon version
else
    echo ""
    echo "⚠️  axon was copied but is not in PATH."
    echo "   Add this to your shell profile:"
    echo ""
    echo '   export PATH="$PATH:/usr/local/bin"'
    echo ""
    case "$SHELL" in
        */zsh)  echo "   echo 'export PATH=\"\$PATH:/usr/local/bin\"' >> ~/.zshrc"  ;;
        */bash) echo "   echo 'export PATH=\"\$PATH:/usr/local/bin\"' >> ~/.bashrc" ;;
        *)      echo "   Add /usr/local/bin to your PATH manually." ;;
    esac
    echo ""
fi
