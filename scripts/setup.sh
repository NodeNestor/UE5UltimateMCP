#!/bin/bash
# Quick setup script — run from your UE5 project root
# Usage: bash path/to/UE5UltimateMCP/scripts/setup.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PLUGIN_DIR="$SCRIPT_DIR/.."

echo ""
echo "========================================"
echo " UE5 Ultimate MCP — Setup"
echo "========================================"
echo ""

# 1. Check Node.js
if ! command -v node &> /dev/null; then
    echo "ERROR: Node.js not found. Install Node.js 18+ first."
    exit 1
fi
echo "[OK] Node.js $(node --version)"

# 2. Build TypeScript bridge if needed
if [ ! -f "$PLUGIN_DIR/MCP/dist/index.js" ]; then
    echo "[..] Building TypeScript bridge..."
    cd "$PLUGIN_DIR/MCP"
    npm install --silent
    npm run build
    echo "[OK] Bridge built"
else
    echo "[OK] Bridge already built"
fi

# 3. Find project root (look for .uproject file)
PROJECT_ROOT="$(pwd)"
if ! ls "$PROJECT_ROOT"/*.uproject &> /dev/null 2>&1; then
    echo ""
    echo "WARNING: No .uproject found in current directory."
    echo "Run this script from your UE5 project root."
    echo ""
fi

# 4. Register MCP server with Claude Code
BRIDGE_PATH="$PLUGIN_DIR/MCP/dist/index.js"
echo "[..] Registering MCP server with Claude Code..."
claude mcp add ue5 -- node "$BRIDGE_PATH" 2>/dev/null && echo "[OK] MCP server registered" || echo "[!!] Could not register (is Claude Code installed?)"

echo ""
echo "========================================"
echo " Setup complete!"
echo "========================================"
echo ""
echo " Next steps:"
echo "   1. Open your project in UE5"
echo "   2. Run 'claude' in this folder"
echo "   3. Ask Claude to do something!"
echo ""
