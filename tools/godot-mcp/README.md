# godot-mcp

MCP server for controlling **Godot 4.x** from Claude Code (or any MCP client) on Windows.
Built and verified against Godot 4.7.2 stable.

Three layers:

1. **Headless engine ops** — scene creation/editing is done by real Godot (`--headless`), not by text-templating `.tscn` files, so everything is engine-validated.
2. **Game process control** — launch the project or any scene in a window, capture its stdout/stderr live, screenshot the window, kill it.
3. **Live editor bridge** — a tiny editor addon (`godot_mcp_bridge`) listens on `127.0.0.1:9080` and lets the MCP server drive the *open* editor: run arbitrary GDScript with `EditorInterface` access, read the edited scene tree, open scenes.

## Setup

Already registered for this machine at user scope:

```bash
claude mcp add -s user -e GODOT_PATH="C:\Users\Ozmium\Desktop\Godot_v4.7.2-stable_win64.exe" godot -- node C:\Users\Ozmium\godot-mcp\dist\index.js
```

Restart your Claude Code session after registering; the `godot` server then appears with 19 tools.

If the Godot exe moves, update `GODOT_PATH` (the server also auto-scans Desktop/Downloads/Program Files for a `Godot*.exe` as a fallback).

### Enabling the editor bridge in a project

The 15 CLI/headless tools work with **no setup at all** — just pass a `project_path`.
The 4 `editor_*` tools need the addon in whichever project the editor has open:

1. Copy `addon\godot_mcp_bridge` into the project as `addons\godot_mcp_bridge`
2. In the editor: **Project → Project Settings → Plugins → Godot MCP Bridge → Enable**
   (or add `enabled=PackedStringArray("res://addons/godot_mcp_bridge/plugin.cfg")` under `[editor_plugins]` in `project.godot`)
3. The editor's Output panel prints `godot_mcp_bridge: listening on 127.0.0.1:9080`

## Tools

| Tool | What it does |
|---|---|
| `godot_info` | Godot exe path, version, whether the editor bridge is reachable |
| `list_project_files` | List scenes / scripts / resources in a project |
| `run_project` | Launch the project (or a specific scene) in a window |
| `stop_project` | Kill the running game |
| `get_run_output` | Status + captured stdout/stderr of the current/last run |
| `screenshot_game` | PNG screenshot of the running game window |
| `read_scene` | Full node tree of a `.tscn` (engine-parsed) |
| `create_scene` | New scene with a chosen root node type |
| `add_node` | Add a node with initial properties, save the scene |
| `set_node_property` | Set one property on a node, save the scene |
| `get_node_property` | Read one property from a node |
| `create_script` | Create a `.gd` file, optionally attach to a scene node |
| `edit_script` | Replace a script's contents |
| `read_script` | Read a script's contents |
| `validate_script` | Engine parse-check of a script |
| `editor_status` | Live editor: version, open project, edited scene |
| `editor_run_gdscript` | Execute arbitrary GDScript inside the editor (use `return` for a result; `EditorInterface` available) |
| `editor_get_scene_tree` | Node tree of the scene open in the editor |
| `editor_open_scene` | Open a scene in the editor |

Property values accept plain JSON or Godot literals as strings: `"Vector2(100, 50)"`, `"Color(1, 0, 0, 1)"`.

## Verification

```bash
npm test
```

`harness/test.mjs` spawns the server over stdio, speaks real MCP JSON-RPC, and exercises **every** tool against `testproject/`, including launching a real editor instance for the bridge tools. 23 checks; exits non-zero on any failure.

## Layout

```
src/            TypeScript MCP server  →  dist/ (npm run build)
godot/ops.gd    headless operations runner (invoked with --headless --script)
addon/          the godot_mcp_bridge editor addon (copy into projects)
testproject/    minimal Godot project used by the harness
harness/        verification harness (npm test)
scripts/        screenshot.ps1 (Win32 window capture)
```
