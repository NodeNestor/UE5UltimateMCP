#!/usr/bin/env python3
"""
UE5UltimateMCP World Gen Smoke Test

Creates basic structures (wall, tower, house), verifies they exist in the level,
then cleans them up by deleting the spawned actors.

Requires:
    - Unreal Editor running with UE5UltimateMCP plugin enabled
    - Plugin HTTP server listening on port 9847 (or set --port)

Usage:
    python test_worldgen.py [--port 9847]
"""

import json
import sys
import time
import urllib.request
import urllib.error

BASE_URL = "http://localhost:9847"

# Tag prefix to identify our test actors
TAG = "WorldGenTest"


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


def spawn_cube(label, x, y, z, scale_x=1.0, scale_y=1.0, scale_z=1.0):
    """Spawn a static mesh cube at the given location with scale."""
    result = call_tool("spawn_actor", {
        "actor_class": "StaticMeshActor",
        "location": {"x": x, "y": y, "z": z},
        "scale": {"x": scale_x, "y": scale_y, "z": scale_z},
        "label": label,
    })
    return result


def get_all_actors():
    """Get all actors in the level."""
    result = call_tool("get_level_actors")
    if not result.get("success", False):
        return []
    data = result.get("data", result)
    if isinstance(data, list):
        return data
    if isinstance(data, dict):
        return data.get("actors", data.get("items", []))
    return []


def find_actors_by_prefix(prefix, actors=None):
    """Find actors whose label/name starts with the given prefix."""
    if actors is None:
        actors = get_all_actors()
    matched = []
    for actor in actors:
        name = ""
        if isinstance(actor, dict):
            name = actor.get("label", actor.get("name", actor.get("actor_name", "")))
        elif isinstance(actor, str):
            name = actor
        if name.startswith(prefix):
            matched.append(actor)
    return matched


def delete_actor(label):
    """Delete an actor by label/name."""
    # Try delete_actor (singular) first, then delete_actors (plural)
    for tool_name in ["delete_actor", "delete_actors"]:
        result = call_tool(tool_name, {"actor_name": label})
        if result.get("success", False):
            return True
        # Some servers use "actors" as list param
        result = call_tool(tool_name, {"actors": [label]})
        if result.get("success", False):
            return True
    return False


def print_result(name, passed, detail=""):
    status = "PASS" if passed else "FAIL"
    line = f"  [{status}] {name}"
    if detail:
        line += f" -- {detail}"
    print(line)
    return passed


# ---------------------------------------------------------------------------
# World gen structures
# ---------------------------------------------------------------------------

def build_wall():
    """Create a wall: 5 cubes in a row."""
    labels = []
    for i in range(5):
        label = f"{TAG}_Wall_{i}"
        result = spawn_cube(label, x=i * 200, y=0, z=100, scale_x=1.0, scale_y=0.2, scale_z=2.0)
        if result.get("success", False):
            labels.append(label)
        else:
            print(f"    Warning: failed to spawn {label}: {result.get('error', 'unknown')}")
    return labels


def build_tower():
    """Create a tower: 4 cubes stacked vertically."""
    labels = []
    for i in range(4):
        label = f"{TAG}_Tower_{i}"
        result = spawn_cube(label, x=1500, y=0, z=100 + i * 200, scale_x=1.0, scale_y=1.0, scale_z=1.0)
        if result.get("success", False):
            labels.append(label)
        else:
            print(f"    Warning: failed to spawn {label}: {result.get('error', 'unknown')}")
    return labels


def build_house():
    """Create a simple house: 4 walls + floor."""
    labels = []
    house_parts = [
        # Floor
        (f"{TAG}_House_Floor", 3000, 0, 0, 4.0, 4.0, 0.1),
        # Front wall
        (f"{TAG}_House_Front", 3000, -400, 200, 4.0, 0.1, 2.0),
        # Back wall
        (f"{TAG}_House_Back", 3000, 400, 200, 4.0, 0.1, 2.0),
        # Left wall
        (f"{TAG}_House_Left", 2600, 0, 200, 0.1, 4.0, 2.0),
        # Right wall
        (f"{TAG}_House_Right", 3400, 0, 200, 0.1, 4.0, 2.0),
    ]
    for label, x, y, z, sx, sy, sz in house_parts:
        result = spawn_cube(label, x, y, z, sx, sy, sz)
        if result.get("success", False):
            labels.append(label)
        else:
            print(f"    Warning: failed to spawn {label}: {result.get('error', 'unknown')}")
    return labels


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    print("=" * 60)
    print("UE5UltimateMCP World Gen Smoke Test")
    print("=" * 60)
    print(f"Server: {BASE_URL}")
    print("-" * 40)

    all_labels = []
    results = []

    # Pre-check: server health
    print("\n[0] Health check...")
    health = api_post("/api/tool", {"tool": "get_level_actors"})
    if not health.get("success", False) and "_error" in health:
        print(f"  Server unreachable: {health['_error']}")
        print("  Make sure UE5 editor is running with the plugin loaded.")
        sys.exit(1)
    print("  Server is reachable")

    # Build wall
    print("\n[1] Building wall (5 cubes)...")
    wall_labels = build_wall()
    results.append(print_result("build_wall", len(wall_labels) == 5, f"spawned {len(wall_labels)}/5"))
    all_labels.extend(wall_labels)

    # Build tower
    print("\n[2] Building tower (4 cubes)...")
    tower_labels = build_tower()
    results.append(print_result("build_tower", len(tower_labels) == 4, f"spawned {len(tower_labels)}/4"))
    all_labels.extend(tower_labels)

    # Build house
    print("\n[3] Building house (5 parts)...")
    house_labels = build_house()
    results.append(print_result("build_house", len(house_labels) == 5, f"spawned {len(house_labels)}/5"))
    all_labels.extend(house_labels)

    # Verify actors exist
    print("\n[4] Verifying actors in level...")
    time.sleep(0.5)  # brief pause for UE to register actors
    actors = get_all_actors()
    found = find_actors_by_prefix(TAG, actors)
    expected = len(all_labels)
    results.append(print_result(
        "verify_actors",
        len(found) >= expected,
        f"found {len(found)}/{expected} test actors in level",
    ))

    # Cleanup
    print("\n[5] Cleaning up (deleting test actors)...")
    deleted = 0
    for label in all_labels:
        if delete_actor(label):
            deleted += 1
        else:
            print(f"    Warning: could not delete {label}")
    results.append(print_result(
        "cleanup",
        deleted == len(all_labels),
        f"deleted {deleted}/{len(all_labels)}",
    ))

    # Verify cleanup
    print("\n[6] Verifying cleanup...")
    time.sleep(0.5)
    actors_after = get_all_actors()
    remaining = find_actors_by_prefix(TAG, actors_after)
    results.append(print_result(
        "verify_cleanup",
        len(remaining) == 0,
        f"{len(remaining)} test actors remaining",
    ))

    # Summary
    passed = sum(1 for r in results if r)
    total = len(results)
    print("\n" + "=" * 60)
    print(f"Results: {passed}/{total} passed")
    if passed == total:
        print("All world gen tests PASSED")
    else:
        print(f"{total - passed} test(s) FAILED")
    print("=" * 60)

    sys.exit(0 if passed == total else 1)


if __name__ == "__main__":
    port = parse_args()
    BASE_URL = f"http://localhost:{port}"
    main()
