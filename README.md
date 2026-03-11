# UE5 Ultimate MCP

An open-source plugin that lets AI coding assistants control the Unreal Engine 5 editor — Blueprints, Materials, Sequencer, AI, World Building, and more.

**Status: Alpha / Untested** — built by combining three great open-source projects and adding new tool categories. Needs real-world testing.

## What it does

You open your UE5 project, start Claude Code (or any MCP-compatible AI), and tell it what to build:

```
> "Add a health variable to the player blueprint, wire up a damage event, and create a HUD widget showing the health bar"
```

The AI uses 158 tools to directly manipulate the editor — creating Blueprints, wiring nodes, spawning actors, editing materials, setting up animations — without you clicking through menus.

## Quick Start

**You need:** UE 5.4+ (5.7 recommended), Node.js 18+, Claude Code

### 1. Get the plugin into your UE5 project

```bash
git clone https://github.com/NodeNestor/UE5UltimateMCP.git
cp -r UE5UltimateMCP YourGame/Plugins/UE5UltimateMCP
```

> **Important:** Your project needs to be a C++ project (not Blueprint-only) since the plugin has C++ source code that compiles with the engine. If you have a Blueprint-only project, add an empty C++ class first: **Tools > New C++ Class > None > Create**. Alternatively, run `scripts/build_plugin.bat` to pre-compile the plugin as a binary — then it works in any project type.

### 2. Build the TypeScript bridge (one time)

The plugin has two parts: C++ code (runs inside UE5) and a TypeScript bridge (translates between Claude and UE5). You need to build the bridge once:

```bash
cd YourGame/Plugins/UE5UltimateMCP/MCP
npm install && npm run build
```

Or just run the setup script from your project root:
```bash
bash Plugins/UE5UltimateMCP/scripts/setup.sh
```

### 3. Tell Claude Code about the plugin

Run this once in your UE project folder (where your `.uproject` file is):

```bash
claude mcp add ue5 -- node Plugins/UE5UltimateMCP/MCP/dist/index.js
```

This registers the tool server with Claude Code. It remembers it for this project — you don't need to do it again.

### 4. Use it

1. Open your project in UE5 — the plugin starts automatically (HTTP server on port 9847)
2. Open a terminal in the same project folder
3. Run `claude`
4. Ask it to do things — it now has full access to the editor

> **First time?** Try: "Check the server status and tell me how many tools are available"

## What's inside

This project stands on the shoulders of three excellent open-source UE5 MCP integrations:

| Source Project | What we took | Credit |
|---|---|---|
| **[BlueprintMCP](https://github.com/mirno-ehf/ue5-mcp)** by mirno-ehf | Blueprint read/write, Materials, Animation, Snapshots — the surgical graph editing tools (83 tools) | Core of our Blueprint and Material systems |
| **[UnrealClaude](https://github.com/Natfii/UnrealClaude)** by Natfii | Tool registry pattern, viewport capture, smart tool routing, context injection | Architecture inspiration + viewport/editor tools |
| **[unreal-engine-mcp](https://github.com/flopperam/unreal-engine-mcp)** by flopperam | Actor management, procedural world generation (castles, towns, mazes) | World gen algorithms + actor tools |

We combined the best parts into a single plugin and added **46 new tools** for categories none of them covered:

- **Sequencer** — keyframes, camera cuts, render to video
- **Behavior Trees** — tasks, decorators, blackboard, composites
- **Navigation** — nav mesh building, pathfinding, modifiers
- **Data Tables** — create, add rows, import CSV
- **Foliage** — scatter instances, foliage types
- **Niagara** — particle systems, emitters, parameters
- **UI / UMG** — widget blueprints, child widgets, event binding
- **Build & Package** — lighting, cook, package, commandlets

> **Note:** [CLAUDIUS](https://claudiuscode.com/) ($60 on FAB) covers similar ground with 130+ commands and is a tested, production product. This is the free open-source alternative — less polished, but MIT licensed and extensible.

## All 158 Tools

<details>
<summary><b>Actors (8)</b> — Spawn, delete, transform, inspect actors</summary>

| Tool | Description |
|------|-------------|
| `spawn_actor` | Spawn by class (StaticMesh, PointLight, Camera, etc.) |
| `delete_actor` | Remove an actor from the level |
| `get_actors_in_level` | List all actors with transforms |
| `find_actors_by_name` | Search by name pattern |
| `set_actor_transform` | Set location, rotation, scale |
| `spawn_blueprint_actor` | Spawn a Blueprint instance |
| `get_actor_properties` | Read all editable properties |
| `set_actor_property` | Set a property by name |
</details>

<details>
<summary><b>Blueprint Read (8)</b> — Inspect Blueprints without modifying</summary>

| Tool | Description |
|------|-------------|
| `list_blueprints` | List all Blueprint assets |
| `get_blueprint` | Get graphs, variables, functions |
| `get_blueprint_graph` | Full node graph with pins |
| `search_blueprints` | Search by name, class, content |
| `get_blueprint_summary` | Concise structure overview |
| `describe_graph` | Human-readable logic description |
| `find_asset_references` | Find what references a Blueprint |
| `search_by_type` | Search assets by UClass |
</details>

<details>
<summary><b>Blueprint Mutation (13)</b> — Edit Blueprint graphs</summary>

| Tool | Description |
|------|-------------|
| `add_node` | Add function, event, flow control nodes |
| `delete_node` | Remove a node |
| `move_node` | Reposition in graph |
| `connect_pins` | Wire two pins together |
| `disconnect_pin` | Break a connection |
| `set_pin_default` | Set default pin value |
| `duplicate_nodes` | Copy nodes |
| `set_node_comment` | Add/edit comment |
| `refresh_all_nodes` | Fix stale references |
| `replace_function_calls` | Bulk-replace function calls |
| `rename_asset` | Rename with redirectors |
| `delete_asset` | Delete an asset |
| `set_blueprint_default` | Set CDO default value |
</details>

<details>
<summary><b>Blueprint Graphs (5)</b> — Create and manage graphs</summary>

| Tool | Description |
|------|-------------|
| `create_blueprint` | Create new Blueprint (Actor, Pawn, Interface, etc.) |
| `create_graph` | Create function or macro graph |
| `delete_graph` | Remove a graph |
| `rename_graph` | Rename a graph |
| `reparent_blueprint` | Change parent class |
</details>

<details>
<summary><b>Variables (4)</b></summary>

`add_variable`, `remove_variable`, `change_variable_type`, `set_variable_metadata`
</details>

<details>
<summary><b>Parameters (3)</b></summary>

`add_function_parameter`, `remove_function_parameter`, `change_function_parameter_type`
</details>

<details>
<summary><b>Components (3)</b></summary>

`list_components`, `add_component`, `remove_component`
</details>

<details>
<summary><b>Interfaces (3)</b></summary>

`list_interfaces`, `add_interface`, `remove_interface`
</details>

<details>
<summary><b>Event Dispatchers (2)</b></summary>

`add_event_dispatcher`, `list_event_dispatchers`
</details>

<details>
<summary><b>User Types (4)</b></summary>

`create_struct`, `create_enum`, `add_struct_property`, `remove_struct_property`
</details>

<details>
<summary><b>Discovery (5)</b></summary>

`list_classes`, `list_functions`, `list_properties`, `get_pin_info`, `check_pin_compatibility`
</details>

<details>
<summary><b>Snapshots & Diff (6)</b></summary>

`snapshot_graph`, `diff_graph`, `restore_graph`, `find_disconnected_pins`, `analyze_rebuild_impact`, `diff_blueprints`
</details>

<details>
<summary><b>Validation (2)</b></summary>

`validate_blueprint`, `validate_all_blueprints`
</details>

<details>
<summary><b>Material Read (8)</b></summary>

`list_materials`, `get_material`, `get_material_graph`, `describe_material`, `search_materials`, `find_material_references`, `list_material_functions`, `get_material_function`
</details>

<details>
<summary><b>Material Mutation (14)</b></summary>

`create_material`, `set_material_property`, `add_material_expression`, `delete_material_expression`, `connect_material_pins`, `disconnect_material_pin`, `set_expression_value`, `move_material_expression`, `create_material_instance`, `set_material_instance_parameter`, `snapshot_material_graph`, `diff_material_graph`, `restore_material_graph`, `validate_material`
</details>

<details>
<summary><b>Animation (10)</b></summary>

`create_anim_blueprint`, `add_anim_state`, `remove_anim_state`, `add_anim_transition`, `set_transition_rule`, `add_anim_node`, `add_state_machine`, `set_state_animation`, `create_blend_space`, `set_blend_space_samples`
</details>

<details>
<summary><b>Sequencer (7)</b> — NEW</summary>

`create_level_sequence`, `add_actor_to_sequence`, `add_transform_keyframe`, `add_camera_cut`, `set_sequence_length`, `render_sequence_to_video`, `play_sequence`
</details>

<details>
<summary><b>Behavior Trees (9)</b> — NEW</summary>

`create_behavior_tree`, `create_blackboard`, `add_blackboard_key`, `add_bt_task`, `add_bt_decorator`, `add_bt_service`, `add_bt_selector`, `add_bt_sequence`, `link_blackboard_to_tree`
</details>

<details>
<summary><b>Navigation (5)</b> — NEW</summary>

`build_navigation`, `add_nav_mesh_bounds`, `test_path`, `get_nav_mesh_info`, `add_nav_modifier`
</details>

<details>
<summary><b>Data Tables (5)</b> — NEW</summary>

`create_data_table`, `add_data_table_row`, `get_data_table_rows`, `remove_data_table_row`, `import_csv_to_data_table`
</details>

<details>
<summary><b>Foliage (4)</b> — NEW</summary>

`scatter_foliage`, `add_foliage_type`, `clear_foliage`, `get_foliage_info`
</details>

<details>
<summary><b>Niagara (5)</b> — NEW</summary>

`create_niagara_system`, `spawn_niagara_actor`, `add_niagara_emitter`, `set_niagara_parameter`, `list_niagara_systems`
</details>

<details>
<summary><b>UI / UMG (5)</b> — NEW</summary>

`create_widget_blueprint`, `add_widget_child`, `set_widget_property`, `list_widget_blueprints`, `bind_widget_event`
</details>

<details>
<summary><b>World Generation (9)</b></summary>

`create_wall`, `create_tower`, `create_staircase`, `create_arch`, `create_pyramid`, `create_maze`, `create_castle`, `create_town`, `create_house`
</details>

<details>
<summary><b>Build & Package (6)</b> — NEW</summary>

`build_lighting`, `build_navigation_only`, `cook_project`, `package_project`, `get_build_status`, `run_commandlet`
</details>

<details>
<summary><b>Viewport & Editor (5)</b></summary>

`capture_viewport`, `get_output_log`, `run_console_command`, `open_level`, `get_level_info`
</details>

## How it works

```
Claude Code  ←stdio→  TypeScript Bridge  ←HTTP :9847→  C++ Plugin in UE5 Editor
                       (translates MCP          (runs tools on the
                        protocol to HTTP)         game thread)
```

1. The C++ plugin loads when you open UE5 and starts an HTTP server on port 9847
2. The TypeScript bridge connects Claude Code to that server
3. When Claude calls a tool, the bridge POSTs to the plugin, which executes on the game thread
4. New C++ tools auto-appear in Claude — no TypeScript changes needed

## Testing

Once UE5 is running with the plugin:

```bash
# Check if the server is responding
python tests/test_health.py

# Run integration tests (spawns actors, captures viewport, etc.)
python tests/test_tools.py

# Test world generation (creates and cleans up structures)
python tests/test_worldgen.py
```

## Adding new tools

Create a handler in `Source/.../Private/Handlers/`:

```cpp
#include "Tools/MCPToolBase.h"
#include "Tools/MCPToolRegistry.h"

class FTool_MyThing : public FMCPToolBase
{
public:
    FMCPToolInfo GetInfo() const override
    {
        FMCPToolInfo Info;
        Info.Name = TEXT("my_thing");
        Info.Description = TEXT("Does a thing");
        Info.Annotations.Category = TEXT("MyCategory");
        return Info;
    }

    FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
    {
        // UE5 API calls here...
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("status"), TEXT("done"));
        return FMCPToolResult::Ok(Result);
    }
};

namespace UltimateMCPTools {
    void RegisterMyTools() {
        FMCPToolRegistry::Get().Register(MakeShared<FTool_MyThing>());
    }
}
```

Register it in `UltimateMCPSubsystem.cpp` and it auto-appears in Claude.

## Requirements

- Unreal Engine 5.4+ (5.7 recommended)
- Node.js 18+
- Claude Code (or any MCP-compatible AI client)

## License

MIT — do whatever you want with it.

## Acknowledgments

This project wouldn't exist without:

- **[mirno-ehf/ue5-mcp](https://github.com/mirno-ehf/ue5-mcp)** — The Blueprint and Material editing tools are adapted from this project. Their HTTP server architecture, queued game-thread execution, and SEH crash safety patterns form the backbone of our plugin.
- **[Natfii/UnrealClaude](https://github.com/Natfii/UnrealClaude)** — The tool registry pattern, smart tool routing (mega-tool collapsing), and context injection system are inspired by this project. Their MCP bridge architecture influenced our TypeScript wrapper.
- **[flopperam/unreal-engine-mcp](https://github.com/flopperam/unreal-engine-mcp)** — The actor management and procedural world generation (castles, towns, mazes) are ported from this project's Python algorithms.
- **[CLAUDIUS](https://claudiuscode.com/)** — The paid plugin that showed what a comprehensive UE5 AI integration should cover. Our new tool categories (Sequencer, Behavior Trees, Navigation, etc.) were inspired by their feature set.
