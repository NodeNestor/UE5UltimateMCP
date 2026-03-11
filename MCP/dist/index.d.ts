#!/usr/bin/env node
/**
 * UE5 Ultimate MCP Server
 *
 * Main entry point. Bridges Claude Code (MCP over stdio) to the C++ HTTP
 * server running inside Unreal Engine.
 *
 * Key design: the tool list is fetched dynamically from /api/tools at startup.
 * Adding new C++ tools automatically makes them available — no TS changes needed.
 *
 * Environment Variables:
 *   UE_PORT            - UE5 server port (default: 9847)
 *   UE_PROJECT_DIR     - Path to .uproject directory
 *   UE_TIMEOUT_MS      - HTTP request timeout (default: 300000)
 *   INJECT_CONTEXT     - Auto-inject context docs on tool calls (default: false)
 *   DEBUG              - Enable debug logging
 */
export {};
