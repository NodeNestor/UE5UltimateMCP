<p align="center">
  <img src="https://img.shields.io/badge/UE5-5.4%2B-blue?style=for-the-badge&logo=unrealengine" alt="UE5 5.4+">
  <img src="https://img.shields.io/badge/Tools-158-brightgreen?style=for-the-badge" alt="158 Tools">
  <img src="https://img.shields.io/badge/License-MIT-yellow?style=for-the-badge" alt="MIT License">
  <img src="https://img.shields.io/badge/MCP-Compatible-purple?style=for-the-badge" alt="MCP Compatible">
  <img src="https://img.shields.io/badge/Price-Free-red?style=for-the-badge" alt="Free">
</p>

# UE5 Ultimate MCP

### The ultimate free AI integration for Unreal Engine 5 — 158 tools via Model Context Protocol

Let Claude build your game. Blueprints, Materials, Sequencer, AI, Niagara, UI, World Building — controlled entirely through natural language. No clicking, no searching, no manual wiring.

> "Create a third-person character with a health system, death animation, and HUD"
>
> Claude spawns the Blueprint, adds variables, wires up the event graph, creates the animation blueprint with state machine, builds the UMG widget, and binds it all together. **You just watch.**

---

## Architecture

```
┌─────────────────────┐     stdio (MCP)     ┌──────────────────────┐     HTTP :9847     ┌─────────────────────────────┐
│                     │ ◄─────────────────► │                      │ ◄────────────────► │                             │
│   Claude Code CLI   │                     │   TypeScript Bridge  │                    │   C++ UE5 Plugin            │
│   (or any MCP       │   JSON-RPC over     │                      │   REST API         │                             │
│    compatible AI)   │   stdin/stdout      │   - Tool routing     │   /api/tools       │   - 158 registered tools    │
│                     │                     │   - Schema convert   │   /api/tool         │   - Game thread execution   │
│                     │                     │   - Context inject   │   /api/health       │   - Full editor API access  │
└─────────────────────┘                     └──────────────────────┘                    └─────────────────────────────┘
                                                     │                                          │
                                              ┌──────┴──────┐                           ┌──────┴──────────┐
                                              │  Smart Tool  │                           │  27 Handler     │
                                              │  Router      │                           │  Categories     │
                                              │              │                           │                 │
                                              │  15 simple   │                           │  Blueprints     │
                                              │  14 mega     │                           │  Materials      │
                                              │  (collapsed) │                           │  Animation      │
                                              └──────────────┘                           │  Sequencer      │
                                                                                         │  AI / BT        │
                                                                                         │  Niagara        │
                                                                                         │  UI / UMG       │
                                                                                         │  World Gen      │
                                                                                         │  ...and more    │
                                                                                         └─────────────────┘
```

**Key insight:** 158 tools is too many for an LLM context window. The TypeScript bridge uses a **smart router** that exposes ~15 core tools directly, collapses the rest into category mega-tools (`ue_blueprint`, `ue_material`, etc.), and hides internal tools. The C++ plugin dynamically reports its tool list — add a new handler in C++ and it appears in Claude automatically.

---

## Why This Exists

| Feature | **UE5 Ultimate MCP** | CLAUDIUS ($60) | BlueprintMCP | UnrealClaude | flopperam MCP |
|---|---|---|---|---|---|
| **Price** | **Free (MIT)** | $60 | Free | Free | Free |
| **Total Tools** | **158** | ~40 | ~70 | ~20 | ~15 |
| **Blueprints** (read/write/graphs/nodes) | 26 tools | Limited | 26 tools | -- | -- |
| **Materials** (create/expressions/instances) | 22 tools | Basic | 22 tools | -- | -- |
| **Animation** (state machines/blend spaces) | 10 tools | -- | -- | -- | -- |
| **Sequencer** (keyframes/camera cuts/render) | 7 tools | -- | -- | -- | -- |
| **Behavior Trees** (tasks/decorators/blackboard) | 9 tools | -- | -- | -- | -- |
| **Navigation** (nav mesh/pathfinding) | 5 tools | -- | -- | -- | -- |
| **Niagara** (particle systems/emitters) | 5 tools | -- | -- | -- | -- |
| **UI / UMG** (widget blueprints/binding) | 5 tools | -- | -- | -- | -- |
| **World Generation** (castles/towns/mazes) | 9 tools | -- | -- | -- | 9 tools |
| **Data Tables** (create/import CSV) | 5 tools | -- | -- | -- | -- |
| **Foliage** (scatter/types/paint) | 4 tools | -- | -- | -- | -- |
| **Build & Package** (cook/lighting/commandlet) | 6 tools | -- | -- | -- | -- |
| **Viewport** (capture/console/logs) | 5 tools | -- | -- | 5 tools | -- |
| **Smart Tool Routing** | Yes | No | No | Yes | No |
| **Context Injection** | Yes | No | No | Yes | No |
| **Snapshot & Diff** | Yes | No | Yes | No | No |
| **Auto-Discovery** | Yes | No | No | No | No |
| **UE 5.7 Support** | Yes | Unknown | 5.4+ | 5.5+ | 5.5 |

---

## Quick Start

**Prerequisites:** UE 5.4+ (5.7 for full features), Node.js 18+, Claude Code subscription

### 1. Clone

```bash
git clone https://github.com/NodeNestor/UE5UltimateMCP.git
```

### 2. Copy the plugin into your project

```bash
# Copy the entire repo as a plugin folder
cp -r UE5UltimateMCP YourProject/Plugins/UE5UltimateMCP
```

### 3. Build the TypeScript bridge

```bash
cd YourProject/Plugins/UE5UltimateMCP/MCP
npm install
npm run build
```

### 4. Add MCP config to your project root

Create `.mcp.json` in your UE project root:

```json
{
  "mcpServers": {
    "ue5-ultimate": {
      "command": "node",
      "args": ["Plugins/UE5UltimateMCP/MCP/dist/index.js"],
      "env": { "UE_PORT": "9847" }
    }
  }
}
```

### 5. Open Unreal Editor, then start Claude

```bash
# Open your project in UE5 (plugin auto-starts HTTP server on :9847)
# Then in a terminal at your project root:
claude
```

Claude will auto-detect the MCP server and gain access to all 158 tools.

> **Tip:** Run `server_status` as your first command to verify the connection and see the full tool breakdown.

---

## Full Tool Reference

### Actors (8 tools)
| Tool | Description |
|------|-------------|
| `spawn_actor` | Spawn an actor by class name with location/rotation/scale |
| `delete_actor` | Remove an actor from the level |
| `get_actors_in_level` | List all actors in the current level |
| `find_actors_by_name` | Search for actors by name pattern |
| `set_actor_transform` | Set location, rotation, and scale of an actor |
| `spawn_blueprint_actor` | Spawn an instance of a Blueprint asset |
| `get_actor_properties` | Get all properties of an actor |
| `set_actor_property` | Set a specific property on an actor |

### Blueprint Read (8 tools)
| Tool | Description |
|------|-------------|
| `list_blueprints` | List all Blueprint assets, optionally filtered by path |
| `get_blueprint` | Get detailed info about a Blueprint (graphs, variables, functions) |
| `get_blueprint_graph` | Get the full node graph of a Blueprint function/event graph |
| `search_blueprints` | Search Blueprints by name, class, or content |
| `get_blueprint_summary` | Get a concise summary of a Blueprint's structure |
| `describe_graph` | Human-readable description of a graph's logic flow |
| `find_asset_references` | Find all assets that reference or are referenced by a Blueprint |
| `search_by_type` | Search for assets by UClass type |

### Blueprint Mutation (13 tools)
| Tool | Description |
|------|-------------|
| `add_node` | Add a node to a Blueprint graph (functions, events, macros, flow control) |
| `delete_node` | Remove a node from a graph |
| `move_node` | Reposition a node in the graph editor |
| `connect_pins` | Wire two pins together |
| `disconnect_pin` | Break a pin connection |
| `set_pin_default` | Set the default value of an input pin |
| `duplicate_nodes` | Duplicate selected nodes |
| `set_node_comment` | Add or edit a comment on a node |
| `refresh_all_nodes` | Refresh all nodes in a graph (fixes stale references) |
| `replace_function_calls` | Bulk-replace calls to one function with another |
| `rename_asset` | Rename any asset with proper redirectors |
| `delete_asset` | Delete an asset from the project |
| `set_blueprint_default` | Set a default value on a Blueprint CDO |

### Blueprint Graphs (5 tools)
| Tool | Description |
|------|-------------|
| `create_blueprint` | Create a new Blueprint asset (Actor, Pawn, Interface, etc.) |
| `create_graph` | Create a new function or macro graph |
| `delete_graph` | Remove a graph from a Blueprint |
| `rename_graph` | Rename a function or macro graph |
| `reparent_blueprint` | Change a Blueprint's parent class |

### Variables (4 tools)
| Tool | Description |
|------|-------------|
| `add_variable` | Add a variable to a Blueprint |
| `remove_variable` | Remove a variable from a Blueprint |
| `change_variable_type` | Change the type of an existing variable |
| `set_variable_metadata` | Set metadata (tooltip, category, replication, etc.) |

### Function Parameters (3 tools)
| Tool | Description |
|------|-------------|
| `add_function_parameter` | Add an input or output parameter to a function |
| `remove_function_parameter` | Remove a parameter from a function |
| `change_function_parameter_type` | Change a parameter's type |

### Components (3 tools)
| Tool | Description |
|------|-------------|
| `list_components` | List all components on a Blueprint |
| `add_component` | Add a component (StaticMesh, Skeletal, Audio, etc.) |
| `remove_component` | Remove a component from a Blueprint |

### Interfaces (3 tools)
| Tool | Description |
|------|-------------|
| `list_interfaces` | List all interfaces implemented by a Blueprint |
| `add_interface` | Implement an interface on a Blueprint |
| `remove_interface` | Remove an interface implementation |

### Event Dispatchers (2 tools)
| Tool | Description |
|------|-------------|
| `add_event_dispatcher` | Create an event dispatcher with parameters |
| `list_event_dispatchers` | List all event dispatchers on a Blueprint |

### User Types (4 tools)
| Tool | Description |
|------|-------------|
| `create_struct` | Create a new UStruct (data-only Blueprint struct) |
| `create_enum` | Create a new UEnum |
| `add_struct_property` | Add a property to a struct |
| `remove_struct_property` | Remove a property from a struct |

### Discovery (5 tools)
| Tool | Description |
|------|-------------|
| `list_classes` | List available UClass types (filterable) |
| `list_functions` | List functions available on a class |
| `list_properties` | List UPROPERTY fields on a class |
| `get_pin_info` | Get detailed info about a node's pins |
| `check_pin_compatibility` | Check if two pins can be connected |

### Snapshot & Diff (6 tools)
| Tool | Description |
|------|-------------|
| `snapshot_graph` | Capture a graph state for later comparison |
| `diff_graph` | Compare current graph against a snapshot |
| `restore_graph` | Restore a graph to a previous snapshot |
| `find_disconnected_pins` | Find all unconnected pins in a graph |
| `analyze_rebuild_impact` | Analyze the impact of rebuilding a Blueprint |
| `diff_blueprints` | Compare two Blueprint assets side by side |

### Validation (2 tools)
| Tool | Description |
|------|-------------|
| `validate_blueprint` | Compile and validate a single Blueprint |
| `validate_all_blueprints` | Batch-validate all Blueprints in the project |

### Material Read (8 tools)
| Tool | Description |
|------|-------------|
| `list_materials` | List all Material assets |
| `get_material` | Get detailed material info (expressions, parameters) |
| `get_material_graph` | Get the full expression graph of a Material |
| `describe_material` | Human-readable description of material setup |
| `search_materials` | Search materials by name or property |
| `find_material_references` | Find all assets using a material |
| `list_material_functions` | List available Material Functions |
| `get_material_function` | Get details of a Material Function |

### Material Mutation (14 tools)
| Tool | Description |
|------|-------------|
| `create_material` | Create a new Material asset |
| `set_material_property` | Set material properties (blend mode, shading model, etc.) |
| `add_material_expression` | Add an expression node to a material |
| `delete_material_expression` | Remove an expression from a material |
| `connect_material_pins` | Connect two expression pins |
| `disconnect_material_pin` | Break a material pin connection |
| `set_expression_value` | Set a value on a material expression |
| `move_material_expression` | Reposition an expression in the graph editor |
| `create_material_instance` | Create a Material Instance from a parent |
| `set_material_instance_parameter` | Set a scalar/vector/texture parameter override |
| `snapshot_material_graph` | Capture material graph state |
| `diff_material_graph` | Compare material graph against snapshot |
| `restore_material_graph` | Restore material to a previous state |
| `validate_material` | Compile and validate a material |

### Animation (10 tools)
| Tool | Description |
|------|-------------|
| `create_anim_blueprint` | Create an Animation Blueprint for a skeleton |
| `add_anim_state` | Add a state to an anim state machine |
| `remove_anim_state` | Remove a state from a state machine |
| `add_anim_transition` | Add a transition rule between states |
| `set_transition_rule` | Configure transition conditions |
| `add_anim_node` | Add an animation node (play sequence, blend, etc.) |
| `add_state_machine` | Create a new state machine in the anim graph |
| `set_state_animation` | Assign an animation asset to a state |
| `create_blend_space` | Create a 1D or 2D Blend Space |
| `set_blend_space_samples` | Add animation samples to a blend space |

### Sequencer (7 tools)
| Tool | Description |
|------|-------------|
| `create_level_sequence` | Create a new Level Sequence asset |
| `add_actor_to_sequence` | Bind an actor to a sequence for animation |
| `add_transform_keyframe` | Add a transform key at a specific frame |
| `add_camera_cut` | Add a camera cut track with bindings |
| `set_sequence_length` | Set the playback range of a sequence |
| `render_sequence_to_video` | Render a sequence to video via Movie Render Queue |
| `play_sequence` | Play a sequence in the editor viewport |

### Behavior Trees (9 tools)
| Tool | Description |
|------|-------------|
| `create_behavior_tree` | Create a new Behavior Tree asset |
| `create_blackboard` | Create a Blackboard Data asset |
| `add_blackboard_key` | Add a key (bool, float, object, vector, etc.) |
| `add_bt_task` | Add a task node (MoveTo, Wait, custom) |
| `add_bt_decorator` | Add a decorator (Blackboard, CoolDown, Loop, etc.) |
| `add_bt_service` | Add a service node |
| `add_bt_selector` | Add a selector composite node |
| `add_bt_sequence` | Add a sequence composite node |
| `link_blackboard_to_tree` | Assign a Blackboard to a Behavior Tree |

### Navigation (5 tools)
| Tool | Description |
|------|-------------|
| `build_navigation` | Build the navigation mesh |
| `add_nav_mesh_bounds` | Add a NavMeshBoundsVolume to the level |
| `test_path` | Test pathfinding between two points |
| `get_nav_mesh_info` | Get nav mesh statistics and coverage info |
| `add_nav_modifier` | Add a NavModifierVolume (area class overrides) |

### Data Tables (5 tools)
| Tool | Description |
|------|-------------|
| `create_data_table` | Create a new DataTable asset with a row struct |
| `add_data_table_row` | Add a row to a DataTable |
| `get_data_table_rows` | Read all rows from a DataTable |
| `remove_data_table_row` | Remove a row by name |
| `import_csv_to_data_table` | Import CSV data into a DataTable |

### Foliage (4 tools)
| Tool | Description |
|------|-------------|
| `scatter_foliage` | Procedurally scatter foliage instances |
| `add_foliage_type` | Register a new static mesh as a foliage type |
| `clear_foliage` | Clear foliage instances in an area |
| `get_foliage_info` | Get foliage statistics and type info |

### Niagara (5 tools)
| Tool | Description |
|------|-------------|
| `create_niagara_system` | Create a new Niagara particle system |
| `spawn_niagara_actor` | Spawn a Niagara system into the level |
| `add_niagara_emitter` | Add an emitter to a Niagara system |
| `set_niagara_parameter` | Set a user parameter on a Niagara system |
| `list_niagara_systems` | List all Niagara systems in the project |

### UI / UMG (5 tools)
| Tool | Description |
|------|-------------|
| `create_widget_blueprint` | Create a new Widget Blueprint |
| `add_widget_child` | Add a child widget (Button, Text, Image, etc.) |
| `set_widget_property` | Set properties on a widget (text, color, anchors) |
| `list_widget_blueprints` | List all Widget Blueprints in the project |
| `bind_widget_event` | Bind a widget event to a Blueprint function |

### World Generation (9 tools)
| Tool | Description |
|------|-------------|
| `create_wall` | Generate a wall with configurable dimensions |
| `create_tower` | Generate a cylindrical or square tower |
| `create_staircase` | Generate a staircase (straight, spiral) |
| `create_arch` | Generate an architectural arch |
| `create_pyramid` | Generate a stepped or smooth pyramid |
| `create_maze` | Generate a randomized maze |
| `create_castle` | Generate a complete castle with walls and towers |
| `create_town` | Generate a procedural town layout |
| `create_house` | Generate a house with rooms and doors |

### Build & Package (6 tools)
| Tool | Description |
|------|-------------|
| `build_lighting` | Build static lighting for the current level |
| `build_navigation_only` | Build only navigation (faster than full build) |
| `cook_project` | Cook content for a target platform |
| `package_project` | Package the project for distribution |
| `get_build_status` | Check current build/cook status |
| `run_commandlet` | Run any UE commandlet by name |

### Viewport & Editor (5 tools)
| Tool | Description |
|------|-------------|
| `capture_viewport` | Screenshot the editor viewport (returns base64 image) |
| `get_output_log` | Read recent output log entries |
| `run_console_command` | Execute a console command |
| `open_level` | Open a level by asset path |
| `get_level_info` | Get info about the current level |

### Meta Tools (2 tools, TypeScript-side)
| Tool | Description |
|------|-------------|
| `server_status` | Check connection, tool count, and server health |
| `get_ue_context` | Load UE5 API documentation for any category |

---

## Configuration

### `.mcp.json` (project root)

```json
{
  "mcpServers": {
    "ue5-ultimate": {
      "command": "node",
      "args": ["Plugins/UE5UltimateMCP/MCP/dist/index.js"],
      "env": {
        "UE_PORT": "9847",
        "UE_PROJECT_DIR": ".",
        "UE_TIMEOUT_MS": "300000",
        "INJECT_CONTEXT": "false",
        "DEBUG": "false"
      }
    }
  }
}
```

### Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `UE_PORT` | `9847` | HTTP port the C++ plugin listens on |
| `UE_PROJECT_DIR` | `.` | Path to the `.uproject` directory |
| `UE_TIMEOUT_MS` | `300000` | HTTP request timeout (5 min for long builds) |
| `INJECT_CONTEXT` | `false` | Auto-inject UE5 API docs with tool responses |
| `DEBUG` | `false` | Enable verbose debug logging to stderr |
| `UE_EDITOR_CMD` | auto-detect | Path to `UnrealEditor-Cmd.exe` |
| `MCP_TOOL_CACHE_TTL_MS` | `30000` | How long to cache the tool list (ms) |

---

## Adding New Tools

The plugin uses a handler pattern. Each handler file registers tools that auto-discover through `/api/tools`.

### 1. Create a handler

```cpp
// Source/UE5UltimateMCP/Private/Handlers/MyCategory.cpp
#include "Tools/MCPToolBase.h"

class FMyTool : public FMCPToolBase
{
public:
    FMCPToolInfo GetInfo() const override
    {
        FMCPToolInfo Info;
        Info.Name = TEXT("my_tool_name");
        Info.Description = TEXT("What this tool does");
        Info.Annotations.Category = TEXT("MyCategory");
        Info.Parameters.Add({TEXT("param1"), TEXT("string"), TEXT("Description"), true});
        return Info;
    }

    FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
    {
        FString Param1 = Params->GetStringField(TEXT("param1"));
        // ... do work on game thread ...
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetBoolField(TEXT("success"), true);
        return FMCPToolResult::Success(Result);
    }
};
```

### 2. Register in the tool registry

```cpp
// In MCPToolRegistry.cpp — RegisterAllTools()
Registry.Add(MakeShared<FMyTool>());
```

### 3. Done

No TypeScript changes needed. The bridge auto-discovers new tools via `/api/tools` and routes them through the smart router. If your tool name matches an existing category pattern (e.g., `blueprint_*`, `material_*`), it auto-collapses into the right mega-tool.

---

## How the Smart Router Works

With 158 tools, listing everything would burn ~40% of the LLM's context window. The router solves this:

```
158 C++ tools
    │
    ├─► 15 "simple" tools    → Listed individually with full schemas
    │   (spawn_actor, set_property, capture_viewport, etc.)
    │
    ├─► ~140 "mega" tools    → Collapsed into 14 category routers
    │   (ue_blueprint, ue_material, ue_animation, etc.)
    │
    └─► 3 "hidden" tools     → Callable by name but never listed
        (task queue, script management)
```

The LLM sees ~30 tools instead of 158, but can invoke any of the 158 by name through the category routers.

---

## Requirements

- **Unreal Engine** 5.4+ (5.7 recommended for all features)
- **Node.js** 18+
- **Claude Code** subscription (or any MCP-compatible AI client)
- **Platforms:** Windows, Linux, macOS

---

## Credits

Built by combining and extending the best open-source UE5 AI integrations:

- **[BlueprintMCP](https://github.com/mirno-ehf/BlueprintMCP)** by mirno-ehf — Blueprint and Material read/write tools, snapshot/diff system
- **[UnrealClaude](https://github.com/Natfii/UnrealClaude)** by Natfii — Smart tool routing, context injection architecture
- **[unreal-engine-mcp](https://github.com/flopperam/unreal-engine-mcp)** by flopperam — World generation tools, actor management

Plus **46 new tools** covering Sequencer, Behavior Trees, Navigation, Data Tables, Foliage, Niagara, UI/UMG, and Build/Package — areas none of the originals touched.

---

## License

[MIT](LICENSE) -- use it, fork it, ship it.
