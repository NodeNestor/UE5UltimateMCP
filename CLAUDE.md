# UE5 Ultimate MCP

The ultimate AI integration for Unreal Engine 5.7 — 150+ tools via Model Context Protocol.

## Architecture

```
Claude Code CLI ←stdio→ TypeScript MCP Wrapper ←HTTP :9847→ C++ UE5 Plugin
                                                                  ↓
                                                          Tool Registry
                                                                  ↓
                                                    UE5 Editor APIs (game thread)
```

## Tool Categories

| Category | Tools | Source |
|----------|-------|--------|
| Blueprint Read | list, get, search, describe | BlueprintMCP |
| Blueprint Mutation | add_node, connect_pins, delete, rename | BlueprintMCP |
| Blueprint Graphs | create, delete, rename, reparent | BlueprintMCP |
| Variables | add, remove, change_type, metadata | BlueprintMCP |
| Parameters | add, remove, change_type | BlueprintMCP |
| Interfaces | list, add, remove | BlueprintMCP |
| Dispatchers | add, list | BlueprintMCP |
| Components | list, add, remove | BlueprintMCP |
| Snapshots | snapshot, diff, restore, analyze | BlueprintMCP |
| Validation | validate, validate_all | BlueprintMCP |
| Discovery | list_classes, functions, properties, pins | BlueprintMCP |
| User Types | create_struct, create_enum, properties | BlueprintMCP |
| Material Read | list, get, describe, search | BlueprintMCP |
| Material Mutation | create, expressions, connect, instances | BlueprintMCP |
| Animation | state machines, transitions, blend spaces | BlueprintMCP |
| Actors | spawn, delete, transform, find, properties | WorldBuilder |
| Viewport | capture, output_log, console_command, level | UnrealClaude |
| World Gen | castle, town, maze, tower, house, bridge | WorldBuilder |
| Sequencer | create, keyframes, camera cuts, render | NEW |
| Behavior Trees | create, tasks, decorators, blackboard | NEW |
| Navigation | build, test_path, nav_mesh, modifiers | NEW |
| Data Tables | create, add_row, import_csv | NEW |
| Foliage | scatter, add_type, clear | NEW |
| Niagara | create_system, spawn, emitters, params | NEW |
| UI/UMG | widget_blueprint, add_child, properties | NEW |
| Build | lighting, cook, package, commandlet | NEW |

## Adding New Tools

1. Create `Source/.../Private/Handlers/YourCategory.cpp`
2. Add tool classes inheriting `FMCPToolBase`
3. Add registration function in `namespace UltimateMCPTools`
4. Call it from `UltimateMCPSubsystem::Initialize()`
5. TypeScript wrapper auto-discovers new tools via `/api/tools`

## Setup

```bash
# 1. Copy plugin to YourProject/Plugins/UE5UltimateMCP/
# 2. Build TypeScript wrapper
cd MCP && npm install && npm run build
# 3. Add to .mcp.json at project root
# 4. Open UE5 editor → plugin starts on port 9847
# 5. Run `claude` in project directory
```
