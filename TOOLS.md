# Software for the home PC

## Required

| Tool | Version used here | Get it | Why |
|---|---|---|---|
| Quake III Arena | Steam release (app 2200) | https://store.steampowered.com/app/2200/ | Source files (`baseq3/pak0.pk3` ... `pak8.pk3`). GOG works too. |
| Python | 3.12 (3.10+ should work) | https://www.python.org/downloads/ | Runs the tools. Tick "Add python.exe to PATH" in the installer. |
| Python packages | numpy 2.x, scipy 1.13+, opencv-python 4.10+, pillow 10+ | `pip install -r tools/requirements.txt` | Math, sparse matrices, image filtering, TGA reading |
| Blender | 5.2.2 LTS (4.4+ should work) | https://www.blender.org/download/ | Builds the rigged mesh and exports .blend / .fbx / .glb |

Check them all with `python tools/setup_check.py`.

## Engine

| Tool | Version | Get it | Notes |
|---|---|---|---|
| Godot (recommended) | 4.7.2 stable (Steam build on the work PC) | Steam "Godot Engine", or https://godotengine.org/download | Runs `godot/` directly; `build_library.py` finds the Steam copy or `GODOT_PATH` |
| Node.js | 24.x | https://nodejs.org | Only for the godot-mcp server (`tools/godot-mcp`) |
| Unity (alternative) | Unity 6 LTS (this PC: 6000.3.26f1, URP) | https://unity.com/download (Unity Hub) | Import the .fbx; textures from `export/unity/` |

## Claude setup (optional)

| Item | How |
|---|---|
| Godot MCP server | `claude mcp add -s user -e GODOT_PATH="<Godot exe>" godot -- node <Q3Remaster>/tools/godot-mcp/dist/index.js`, then restart Claude Code. `npm test` in `tools/godot-mcp` checks it (23 checks) |
| Blender MCP (optional) | `pip install blender-mcp`, add-on from the package's `bundled/addon.py`; not needed by the tools |

## Optional, for the production stages in PLAN.md

| Tool | Use |
|---|---|
| Real-ESRGAN (ncnn-vulkan portable build) or Topaz Gigapixel | AI upscaling of the original textures for reference detail |
| Substance 3D Painter | Hand-quality texturing over the generated masks |
| ZBrush, or Blender's sculpt mode | High-poly sculpts over the remastered silhouette |
| Blender Retopoflow / Quad Remesher, or manual retopo in Blender | New game meshes with joint loops |
| Claude Code (desktop app or CLI) | Open the `Q3Remaster` folder and Claude reads `CLAUDE.md` for the project context |

## Bringing the work home

1. Copy `Q3Remaster_YYYY-MM-DD.zip` and unzip it anywhere (paths are all relative).
2. Install the required tools above.
3. Run `python tools/setup_check.py`, then `python tools/remaster_model.py --list`.
4. `extracted/` and `work/` are included in the zip, so you can look at results before rerunning anything. Delete them any time to start fresh.
