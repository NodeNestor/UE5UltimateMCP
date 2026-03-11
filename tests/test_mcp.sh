#!/usr/bin/env bash
#
# UE5UltimateMCP - MCP Server Smoke Test
#
# Checks that the Node.js MCP server can start up without errors.
# Does NOT require Unreal Editor to be running.
#
# Usage:
#     bash tests/test_mcp.sh
#

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
MCP_DIR="$PROJECT_DIR/MCP"
DIST_INDEX="$MCP_DIR/dist/index.js"
LOG_FILE="/tmp/ue5mcp_test_$$.log"
SERVER_PID=""

passed=0
failed=0

pass() {
    echo "  [PASS] $1"
    passed=$((passed + 1))
}

fail() {
    echo "  [FAIL] $1 -- $2"
    failed=$((failed + 1))
}

cleanup() {
    if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null || true
    fi
    rm -f "$LOG_FILE"
}

trap cleanup EXIT

echo "============================================================"
echo "UE5UltimateMCP - MCP Server Smoke Test"
echo "============================================================"
echo "  Project: $PROJECT_DIR"
echo "  MCP dir: $MCP_DIR"
echo "------------------------------------------------------------"

# Test 1: Node.js installed
echo ""
echo "[1] Checking node is installed..."
if command -v node >/dev/null 2>&1; then
    NODE_VERSION=$(node --version)
    pass "node $NODE_VERSION"
else
    fail "node" "node is not installed or not in PATH"
fi

# Test 2: npm packages built
echo ""
echo "[2] Checking dist/index.js exists..."
if [ -f "$DIST_INDEX" ]; then
    pass "dist/index.js exists"
else
    fail "dist/index.js" "not found -- run 'npm run build' in MCP/"
fi

# Test 3: node_modules present
echo ""
echo "[3] Checking node_modules..."
if [ -d "$MCP_DIR/node_modules/@modelcontextprotocol" ]; then
    pass "node_modules/@modelcontextprotocol present"
else
    fail "node_modules" "missing -- run 'npm install' in MCP/"
fi

# Test 4: Start MCP server, check it doesn't crash immediately
echo ""
echo "[4] Starting MCP server process..."
if [ -f "$DIST_INDEX" ]; then
    # The MCP server uses stdio transport, so it will hang waiting for input.
    # We start it, wait a moment, then check it's still alive.
    node "$DIST_INDEX" >"$LOG_FILE" 2>&1 &
    SERVER_PID=$!

    # Give it time to initialize
    sleep 3

    if kill -0 "$SERVER_PID" 2>/dev/null; then
        pass "MCP server started (PID $SERVER_PID) -- process alive after 3s"
    else
        wait "$SERVER_PID" 2>/dev/null
        EXIT_CODE=$?
        fail "MCP server" "exited with code $EXIT_CODE"
        if [ -f "$LOG_FILE" ]; then
            echo "    Server log:"
            head -20 "$LOG_FILE" | sed 's/^/      /'
        fi
    fi

    # Check stderr log for fatal errors
    if [ -f "$LOG_FILE" ]; then
        if grep -qi "fatal\|unhandled\|cannot find module" "$LOG_FILE" 2>/dev/null; then
            fail "MCP server log" "contains fatal errors"
            grep -i "fatal\|unhandled\|cannot find module" "$LOG_FILE" | head -5 | sed 's/^/      /'
        else
            pass "MCP server log clean (no fatal errors)"
        fi
    fi
else
    fail "MCP server start" "dist/index.js not found, skipping"
fi

# Summary
echo ""
echo "============================================================"
total=$((passed + failed))
echo "Results: $passed/$total passed"
if [ "$failed" -eq 0 ]; then
    echo "All checks PASSED"
else
    echo "$failed check(s) FAILED"
fi
echo "============================================================"

exit "$failed"
