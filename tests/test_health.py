#!/usr/bin/env python3
"""
UE5UltimateMCP Health Check

Connects to the UE5 HTTP server and prints server status, version, tool count,
and lists all tools grouped by category.

Usage:
    python test_health.py [--port 9847]
"""

import json
import sys
import urllib.request
import urllib.error

BASE_URL = "http://localhost:9847"


def parse_args():
    port = 9847
    args = sys.argv[1:]
    i = 0
    while i < len(args):
        if args[i] == "--port" and i + 1 < len(args):
            port = int(args[i + 1])
            i += 2
        else:
            i += 1
    return port


def api_get(endpoint, timeout=5):
    """GET request to the UE5 server. Returns parsed JSON or None."""
    url = f"{BASE_URL}{endpoint}"
    try:
        req = urllib.request.Request(url)
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return json.loads(resp.read().decode("utf-8"))
    except urllib.error.URLError as e:
        print(f"  Connection failed: {e.reason}")
        return None
    except Exception as e:
        print(f"  Error: {e}")
        return None


def check_health():
    print("=" * 60)
    print("UE5UltimateMCP Health Check")
    print("=" * 60)
    print(f"\nServer: {BASE_URL}")
    print("-" * 40)

    # Health endpoint
    print("\n[1] Checking /api/health ...")
    health = api_get("/api/health")
    if health is None:
        print("  FAIL - Server is not reachable")
        print("  Make sure Unreal Editor is running with the UE5UltimateMCP plugin enabled.")
        return False

    print(f"  Status:         {health.get('status', 'unknown')}")
    print(f"  Mode:           {health.get('mode', 'unknown')}")
    print(f"  Engine Version: {health.get('engineVersion', 'unknown')}")
    print(f"  Project Name:   {health.get('projectName', 'unknown')}")
    print(f"  Plugin Version: {health.get('pluginVersion', 'unknown')}")
    print(f"  Tool Count:     {health.get('toolCount', 'unknown')}")

    # Tools endpoint
    print("\n[2] Fetching /api/tools ...")
    tools_data = api_get("/api/tools", timeout=10)
    if tools_data is None:
        print("  FAIL - Could not fetch tool list")
        return False

    tools = tools_data.get("tools", [])
    print(f"  Total tools: {len(tools)}")

    # Group by category
    categories = {}
    for tool in tools:
        cat = tool.get("category", "uncategorized")
        if cat not in categories:
            categories[cat] = []
        categories[cat].append(tool["name"])

    print(f"  Categories: {len(categories)}")
    print("\n[3] Tools by Category:")
    print("-" * 40)

    for cat in sorted(categories.keys()):
        tool_names = sorted(categories[cat])
        print(f"\n  {cat} ({len(tool_names)} tools):")
        for name in tool_names:
            print(f"    - {name}")

    print("\n" + "=" * 60)
    print("Health check PASSED")
    print("=" * 60)
    return True


if __name__ == "__main__":
    port = parse_args()
    BASE_URL = f"http://localhost:{port}"
    success = check_health()
    sys.exit(0 if success else 1)
