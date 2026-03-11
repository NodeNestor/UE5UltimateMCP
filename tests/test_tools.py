#!/usr/bin/env python3
"""
UE5UltimateMCP Integration Tests

Tests core tool operations against a running UE5 editor with the plugin loaded.

Requires:
    - Unreal Editor running with UE5UltimateMCP plugin enabled
    - Plugin HTTP server listening on port 9847 (or set --port)

Usage:
    python test_tools.py [--port 9847]
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


def api_get(endpoint, timeout=10):
    """GET request. Returns parsed JSON or None."""
    url = f"{BASE_URL}{endpoint}"
    try:
        req = urllib.request.Request(url)
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return json.loads(resp.read().decode("utf-8"))
    except Exception as e:
        return {"_error": str(e)}


def api_post(endpoint, body, timeout=30):
    """POST request. Returns parsed JSON or error dict."""
    url = f"{BASE_URL}{endpoint}"
    try:
        data = json.dumps(body).encode("utf-8")
        req = urllib.request.Request(url, data=data, method="POST")
        req.add_header("Content-Type", "application/json")
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return json.loads(resp.read().decode("utf-8"))
    except Exception as e:
        return {"_error": str(e), "success": False}


def call_tool(tool_name, params=None):
    """Call a tool via /api/tool."""
    body = {"tool": tool_name}
    if params:
        body.update(params)
    return api_post("/api/tool", body)


def print_result(name, passed, detail=""):
    status = "PASS" if passed else "FAIL"
    line = f"  [{status}] {name}"
    if detail:
        line += f" -- {detail}"
    print(line)
    return passed


# ---------------------------------------------------------------------------
# Individual tests
# ---------------------------------------------------------------------------

def test_health():
    """Check server health endpoint."""
    result = api_get("/api/health")
    if result and "_error" not in result:
        status = result.get("status", "")
        return print_result(
            "health",
            status in ("ok", "ready", "healthy"),
            f"status={status}, tools={result.get('toolCount', '?')}",
        )
    return print_result("health", False, str(result.get("_error", "no response")))


def test_spawn_actor():
    """Spawn a PointLight at (0, 0, 200)."""
    result = call_tool("spawn_actor", {
        "actor_class": "PointLight",
        "location": {"x": 0, "y": 0, "z": 200},
        "label": "TestLight_IntegrationTest",
    })
    success = result.get("success", False)
    actor_name = ""
    if success:
        data = result.get("data", result)
        if isinstance(data, dict):
            actor_name = data.get("actor_name", data.get("label", data.get("name", "")))
        else:
            actor_name = str(data)
    return print_result(
        "spawn_actor (PointLight at 0,0,200)",
        success,
        f"actor={actor_name}" if success else str(result.get("error", result.get("message", "unknown"))),
    )


def test_get_actors_in_level():
    """List actors in the current level."""
    result = call_tool("get_level_actors")
    success = result.get("success", False)
    count = 0
    if success:
        data = result.get("data", result)
        if isinstance(data, list):
            count = len(data)
        elif isinstance(data, dict):
            actors = data.get("actors", data.get("items", []))
            count = len(actors) if isinstance(actors, list) else 0
    return print_result(
        "get_level_actors",
        success,
        f"actors={count}" if success else str(result.get("error", "unknown")),
    )


def test_list_blueprints():
    """Search for blueprint assets."""
    result = call_tool("asset_search", {
        "query": "Blueprint",
        "class_filter": "Blueprint",
    })
    success = result.get("success", False)
    count = 0
    if success:
        data = result.get("data", result)
        if isinstance(data, list):
            count = len(data)
        elif isinstance(data, dict):
            items = data.get("assets", data.get("results", data.get("items", [])))
            count = len(items) if isinstance(items, list) else 0
    return print_result(
        "asset_search (Blueprints)",
        success,
        f"found={count}" if success else str(result.get("error", "unknown")),
    )


def test_capture_viewport():
    """Capture the viewport as an image."""
    result = call_tool("capture_viewport", {
        "width": 640,
        "height": 480,
    })
    success = result.get("success", False)
    detail = ""
    if success:
        data = result.get("data", result)
        if isinstance(data, dict) and "image_base64" in data:
            b64_len = len(data["image_base64"])
            detail = f"image_base64={b64_len} chars"
        else:
            detail = "no image data in response"
    else:
        detail = str(result.get("error", result.get("message", "unknown")))
    return print_result("capture_viewport (640x480)", success, detail)


def test_get_level_info():
    """Get info about the current level."""
    # Try common tool names for level info
    for tool_name in ["get_level_info", "get_level_actors"]:
        result = call_tool(tool_name)
        if result.get("success", False):
            data = result.get("data", result)
            if isinstance(data, dict):
                level = data.get("level_name", data.get("map_name", data.get("name", "?")))
                return print_result(
                    f"get_level_info (via {tool_name})",
                    True,
                    f"level={level}",
                )
            return print_result(f"get_level_info (via {tool_name})", True, str(data)[:100])
    return print_result("get_level_info", False, "no working level info tool found")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    print("=" * 60)
    print("UE5UltimateMCP Integration Tests")
    print("=" * 60)
    print(f"Server: {BASE_URL}")
    print("-" * 40)

    tests = [
        test_health,
        test_spawn_actor,
        test_get_actors_in_level,
        test_list_blueprints,
        test_capture_viewport,
        test_get_level_info,
    ]

    results = []
    for test_fn in tests:
        try:
            passed = test_fn()
            results.append(passed)
        except Exception as e:
            print_result(test_fn.__name__, False, f"exception: {e}")
            results.append(False)

    passed = sum(1 for r in results if r)
    total = len(results)
    print("\n" + "=" * 60)
    print(f"Results: {passed}/{total} passed")
    if passed == total:
        print("All tests PASSED")
    else:
        print(f"{total - passed} test(s) FAILED")
    print("=" * 60)

    sys.exit(0 if passed == total else 1)


if __name__ == "__main__":
    port = parse_args()
    BASE_URL = f"http://localhost:{port}"
    main()
