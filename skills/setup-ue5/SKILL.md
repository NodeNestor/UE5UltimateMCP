---
name: setup-ue5
description: Set up the UE5 Ultimate MCP plugin in the current Unreal Engine project. Use when the user says "setup ue5", "install ue5 plugin", "connect to unreal", or wants to start using Claude Code with their UE5 project.
---

# Setup UE5 Ultimate MCP Plugin

Install the UE5 Ultimate MCP plugin into the user's current Unreal Engine 5 project so Claude Code can control the editor.

## Steps

### 1. Find the .uproject file

Search the current working directory for a `.uproject` file. If not found, search one level up. If still not found, ask the user where their UE5 project is.

```
Glob for **/*.uproject in the current directory (max depth 2)
```

### 2. Detect project type

Read the `.uproject` file. Check if it has `"Modules"` — if yes, it's a C++ project. If no modules or only plugin references, it's Blueprint-only.

### 3. Create Plugins directory

Ensure `<ProjectRoot>/Plugins/UE5UltimateMCP/` exists.

### 4. Copy or download the plugin

The plugin files are at `${CLAUDE_PLUGIN_ROOT}` (the directory where this skill lives, two levels up from this SKILL.md).

Copy these directories/files from the plugin root to `<ProjectRoot>/Plugins/UE5UltimateMCP/`:
- `Source/` (C++ source)
- `UE5UltimateMCP.uplugin`

For **Blueprint-only projects**, also check if `BuiltPlugin/` exists at the plugin root OR download the latest release binary:
- If `BuiltPlugin/Binaries/` exists locally, copy `BuiltPlugin/Binaries/` to `<ProjectRoot>/Plugins/UE5UltimateMCP/Binaries/`
- Otherwise, tell the user to download the pre-built binary from https://github.com/NodeNestor/UE5UltimateMCP/releases and extract `Binaries/` into the plugin folder

For **C++ projects**, just the Source/ and .uplugin is enough — the engine compiles it.

### 5. Verify the MCP server connection

The plugin starts an HTTP server on port 9847 when UE5 opens. Try to reach it:

```bash
curl -s http://localhost:9847/api/health
```

If it responds, the plugin is already running. If not, tell the user to:
1. Open (or restart) their project in UE5
2. Wait for the editor to fully load
3. Check the Output Log for `[UltimateMCP] Server started on port 9847`

### 6. Confirm tool count

Once the server responds, check available tools:

```bash
curl -s http://localhost:9847/api/tools | python -c "import sys,json; d=json.load(sys.stdin); print(f'{len(d[\"tools\"])} tools available')"
```

Expected: 158 tools.

### 7. Report success

Tell the user:
- The plugin is installed at `Plugins/UE5UltimateMCP/`
- The MCP server connects automatically (no extra config needed since this plugin provides the MCP server)
- They can now ask Claude to do things like:
  - "List all blueprints in my project"
  - "Create a new Actor blueprint called BP_Enemy"
  - "Add a health variable to BP_Player"
  - "Spawn a point light at 0,0,200"
  - "Capture the viewport"

## Error handling

- If no .uproject found: "I can't find a UE5 project in the current directory. Navigate to your project folder and try again, or tell me the path."
- If UE5 is not running: "The plugin is installed but UE5 isn't running yet. Open your project in UE5 and the MCP server will start automatically."
- If port 9847 is already in use: "Port 9847 is busy. Another instance of UE5 might be running with the plugin. Close it first or check your running processes."
