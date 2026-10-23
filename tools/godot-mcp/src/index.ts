#!/usr/bin/env node
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";
import { readFileSync, writeFileSync, existsSync, mkdirSync, readdirSync, statSync } from "node:fs";
import { join, resolve, dirname } from "node:path";
import {
  findGodot,
  godotVersion,
  runProject,
  stopProject,
  getRun,
  headlessOp,
  PKG_ROOT,
} from "./godot.js";
import { bridgeCall, bridgeAvailable } from "./bridge.js";
import { captureWindow } from "./screenshot.js";

const server = new McpServer({ name: "godot-mcp", version: "1.0.0" });

type ToolResult = {
  content: Array<{ type: "text"; text: string }>;
  isError?: boolean;
};

function ok(data: unknown): ToolResult {
  return {
    content: [
      { type: "text", text: typeof data === "string" ? data : JSON.stringify(data, null, 2) },
    ],
  };
}

function fail(e: unknown): ToolResult {
  const msg = e instanceof Error ? e.message : String(e);
  return { content: [{ type: "text", text: `Error: ${msg}` }], isError: true };
}

function resPath(p: string): string {
  return p.startsWith("res://") ? p : `res://${p.replace(/\\/g, "/")}`;
}

function fsPathInProject(projectPath: string, resOrRel: string): string {
  const rel = resOrRel.replace(/^res:\/\//, "").replace(/\//g, "\\");
  return join(resolve(projectPath), rel);
}

// ---------------------------------------------------------------------------
// Project / CLI tools
// ---------------------------------------------------------------------------

server.registerTool(
  "godot_info",
  {
    title: "Godot info",
    description:
      "Get the Godot executable path and version being used by this MCP server, and whether the live editor bridge is currently reachable.",
    inputSchema: {},
  },
  async () => {
    try {
      const exe = findGodot();
      const version = await godotVersion();
      const bridge = await bridgeAvailable();
      return ok({ executable: exe, version, editor_bridge_connected: bridge });
    } catch (e) {
      return fail(e);
    }
  }
);

const SCENE_EXTS = [".tscn", ".scn"];
const SCRIPT_EXTS = [".gd", ".cs", ".gdshader"];
const RESOURCE_EXTS = [".tres", ".res", ".png", ".jpg", ".jpeg", ".webp", ".svg", ".wav", ".ogg", ".mp3", ".ttf", ".otf", ".glb", ".gltf", ".obj"];

server.registerTool(
  "list_project_files",
  {
    title: "List project files",
    description:
      "List scenes, scripts, or resources in a Godot project directory (recursive, skips .godot cache).",
    inputSchema: {
      project_path: z.string().describe("Absolute path to the Godot project directory"),
      type: z
        .enum(["scenes", "scripts", "resources", "all"])
        .default("all")
        .describe("Which file category to list"),
    },
  },
  async ({ project_path, type }) => {
    try {
      const root = resolve(project_path);
      if (!existsSync(join(root, "project.godot"))) {
        throw new Error(`No project.godot found in ${root}`);
      }
      const out: { scenes: string[]; scripts: string[]; resources: string[] } = {
        scenes: [],
        scripts: [],
        resources: [],
      };
      const walk = (dir: string) => {
        for (const entry of readdirSync(dir)) {
          if (entry === ".godot" || entry.startsWith(".")) continue;
          const full = join(dir, entry);
          const st = statSync(full);
          if (st.isDirectory()) {
            walk(full);
            continue;
          }
          const rel = "res://" + full.slice(root.length + 1).replace(/\\/g, "/");
          const lower = entry.toLowerCase();
          if (SCENE_EXTS.some((e) => lower.endsWith(e))) out.scenes.push(rel);
          else if (SCRIPT_EXTS.some((e) => lower.endsWith(e))) out.scripts.push(rel);
          else if (RESOURCE_EXTS.some((e) => lower.endsWith(e))) out.resources.push(rel);
        }
      };
      walk(root);
      if (type === "scenes") return ok({ scenes: out.scenes });
      if (type === "scripts") return ok({ scripts: out.scripts });
      if (type === "resources") return ok({ resources: out.resources });
      return ok(out);
    } catch (e) {
      return fail(e);
    }
  }
);

server.registerTool(
  "run_project",
  {
    title: "Run project",
    description:
      "Launch a Godot project (or a specific scene in it) in a new window. Output is captured; read it with get_run_output, stop with stop_project.",
    inputSchema: {
      project_path: z.string().describe("Absolute path to the Godot project directory"),
      scene: z
        .string()
        .optional()
        .describe("Optional scene to run instead of the main scene, e.g. res://levels/test.tscn"),
    },
  },
  async ({ project_path, scene }) => {
    try {
      const state = runProject(project_path, scene);
      return ok({
        running: true,
        pid: state.pid,
        project: state.projectPath,
        scene: state.scene ?? "(main scene)",
      });
    } catch (e) {
      return fail(e);
    }
  }
);

server.registerTool(
  "stop_project",
  {
    title: "Stop project",
    description: "Stop the game process started by run_project.",
    inputSchema: {},
  },
  async () => {
    try {
      return ok(stopProject());
    } catch (e) {
      return fail(e);
    }
  }
);

server.registerTool(
  "get_run_output",
  {
    title: "Get run output",
    description:
      "Get status, captured stdout, and captured stderr (script errors, warnings) of the current or most recent run_project process.",
    inputSchema: {},
  },
  async () => {
    try {
      const run = getRun();
      if (!run) return ok({ status: "never_started" });
      return ok({
        status: run.exited ? "exited" : "running",
        pid: run.pid,
        exit_code: run.exitCode,
        uptime_ms: Date.now() - run.startedAt,
        stdout: run.stdout,
        stderr: run.stderr,
      });
    } catch (e) {
      return fail(e);
    }
  }
);

server.registerTool(
  "screenshot_game",
  {
    title: "Screenshot the running game",
    description:
      "Capture a PNG screenshot of the game window started by run_project. Returns the saved file path.",
    inputSchema: {
      output_path: z
        .string()
        .optional()
        .describe("Where to save the PNG. Defaults to a screenshots/ folder inside godot-mcp."),
    },
  },
  async ({ output_path }) => {
    try {
      const run = getRun();
      if (!run || run.exited) throw new Error("No game is currently running. Call run_project first.");
      const out = output_path
        ? resolve(output_path)
        : join(PKG_ROOT, "screenshots", `game_${Date.now()}.png`);
      mkdirSync(dirname(out), { recursive: true });
      const result = await captureWindow(run.pid, out);
      return ok(result);
    } catch (e) {
      return fail(e);
    }
  }
);

// ---------------------------------------------------------------------------
// Scene tools (headless Godot operations — accurate, engine-parsed)
// ---------------------------------------------------------------------------

server.registerTool(
  "read_scene",
  {
    title: "Read scene tree",
    description:
      "Load a .tscn scene with the Godot engine (headless) and return its full node tree: names, types, scripts, children.",
    inputSchema: {
      project_path: z.string().describe("Absolute path to the Godot project directory"),
      scene: z.string().describe("Scene path, e.g. res://main.tscn"),
    },
  },
  async ({ project_path, scene }) => {
    try {
      return ok(await headlessOp(project_path, "get_tree", { scene: resPath(scene) }));
    } catch (e) {
      return fail(e);
    }
  }
);

server.registerTool(
  "create_scene",
  {
    title: "Create scene",
    description: "Create a new .tscn scene file with a root node of the given type.",
    inputSchema: {
      project_path: z.string().describe("Absolute path to the Godot project directory"),
      scene_path: z.string().describe("Path for the new scene, e.g. res://levels/level2.tscn"),
      root_type: z.string().describe("Godot class for the root node, e.g. Node2D, Node3D, Control"),
      root_name: z.string().optional().describe("Name of the root node (defaults to the type name)"),
    },
  },
  async ({ project_path, scene_path, root_type, root_name }) => {
    try {
      return ok(
        await headlessOp(project_path, "create_scene", {
          scene_path: resPath(scene_path),
          root_type,
          root_name: root_name ?? root_type,
        })
      );
    } catch (e) {
      return fail(e);
    }
  }
);

server.registerTool(
  "add_node",
  {
    title: "Add node to scene",
    description:
      "Add a node to an existing scene and save it. Property values may be plain JSON values or Godot literals as strings, e.g. \"Vector2(100, 50)\" or \"Color(1, 0, 0, 1)\".",
    inputSchema: {
      project_path: z.string().describe("Absolute path to the Godot project directory"),
      scene: z.string().describe("Scene to modify, e.g. res://main.tscn"),
      parent: z
        .string()
        .default(".")
        .describe("Node path of the parent inside the scene ('.' = scene root)"),
      type: z.string().describe("Godot class of the new node, e.g. Sprite2D, Label, Area2D"),
      name: z.string().describe("Name for the new node"),
      properties: z
        .record(z.string(), z.any())
        .optional()
        .describe("Initial property values, e.g. {\"text\": \"Hi\", \"position\": \"Vector2(10, 20)\"}"),
    },
  },
  async ({ project_path, scene, parent, type, name, properties }) => {
    try {
      return ok(
        await headlessOp(project_path, "add_node", {
          scene: resPath(scene),
          parent,
          type,
          name,
          properties: properties ?? {},
        })
      );
    } catch (e) {
      return fail(e);
    }
  }
);

server.registerTool(
  "set_node_property",
  {
    title: "Set node property",
    description:
      "Set one property on a node in a scene and save the scene. Value may be a plain JSON value or a Godot literal string like \"Vector2(3, 4)\".",
    inputSchema: {
      project_path: z.string().describe("Absolute path to the Godot project directory"),
      scene: z.string().describe("Scene to modify, e.g. res://main.tscn"),
      node_path: z.string().describe("Path of the node inside the scene ('.' = root)"),
      property: z.string().describe("Property name, e.g. text, position, visible"),
      value: z.any().describe("New value"),
    },
  },
  async ({ project_path, scene, node_path, property, value }) => {
    try {
      return ok(
        await headlessOp(project_path, "set_property", {
          scene: resPath(scene),
          node_path,
          property,
          value,
        })
      );
    } catch (e) {
      return fail(e);
    }
  }
);

server.registerTool(
  "get_node_property",
  {
    title: "Get node property",
    description: "Read one property from a node in a scene (engine-parsed, headless).",
    inputSchema: {
      project_path: z.string().describe("Absolute path to the Godot project directory"),
      scene: z.string().describe("Scene to read, e.g. res://main.tscn"),
      node_path: z.string().describe("Path of the node inside the scene ('.' = root)"),
      property: z.string().describe("Property name"),
    },
  },
  async ({ project_path, scene, node_path, property }) => {
    try {
      return ok(
        await headlessOp(project_path, "get_property", {
          scene: resPath(scene),
          node_path,
          property,
        })
      );
    } catch (e) {
      return fail(e);
    }
  }
);

// ---------------------------------------------------------------------------
// Script tools
// ---------------------------------------------------------------------------

server.registerTool(
  "create_script",
  {
    title: "Create GDScript",
    description:
      "Create a new .gd script file. Optionally attach it to a node in a scene in the same call.",
    inputSchema: {
      project_path: z.string().describe("Absolute path to the Godot project directory"),
      script_path: z.string().describe("Path for the new script, e.g. res://player.gd"),
      content: z.string().describe("Full GDScript source code"),
      attach_to_scene: z
        .string()
        .optional()
        .describe("Optional scene whose node should get this script, e.g. res://main.tscn"),
      attach_to_node: z
        .string()
        .optional()
        .describe("Node path inside that scene to attach the script to ('.' = root)"),
    },
  },
  async ({ project_path, script_path, content, attach_to_scene, attach_to_node }) => {
    try {
      const fsPath = fsPathInProject(project_path, script_path);
      mkdirSync(dirname(fsPath), { recursive: true });
      writeFileSync(fsPath, content, "utf8");
      let attached = null;
      if (attach_to_scene) {
        attached = await headlessOp(project_path, "attach_script", {
          scene: resPath(attach_to_scene),
          node_path: attach_to_node ?? ".",
          script_path: resPath(script_path),
        });
      }
      return ok({ created: resPath(script_path), bytes: content.length, attached });
    } catch (e) {
      return fail(e);
    }
  }
);

server.registerTool(
  "edit_script",
  {
    title: "Edit GDScript",
    description: "Replace the full contents of an existing script file.",
    inputSchema: {
      project_path: z.string().describe("Absolute path to the Godot project directory"),
      script_path: z.string().describe("Script to edit, e.g. res://player.gd"),
      content: z.string().describe("New full GDScript source code"),
    },
  },
  async ({ project_path, script_path, content }) => {
    try {
      const fsPath = fsPathInProject(project_path, script_path);
      if (!existsSync(fsPath)) throw new Error(`Script does not exist: ${script_path}`);
      writeFileSync(fsPath, content, "utf8");
      return ok({ edited: resPath(script_path), bytes: content.length });
    } catch (e) {
      return fail(e);
    }
  }
);

server.registerTool(
  "read_script",
  {
    title: "Read GDScript",
    description: "Read the contents of a script file in a project.",
    inputSchema: {
      project_path: z.string().describe("Absolute path to the Godot project directory"),
      script_path: z.string().describe("Script to read, e.g. res://player.gd"),
    },
  },
  async ({ project_path, script_path }) => {
    try {
      const fsPath = fsPathInProject(project_path, script_path);
      if (!existsSync(fsPath)) throw new Error(`Script does not exist: ${script_path}`);
      return ok(readFileSync(fsPath, "utf8"));
    } catch (e) {
      return fail(e);
    }
  }
);

server.registerTool(
  "validate_script",
  {
    title: "Validate GDScript",
    description:
      "Parse-check a GDScript file with the Godot engine (headless) and report any script errors.",
    inputSchema: {
      project_path: z.string().describe("Absolute path to the Godot project directory"),
      script_path: z.string().describe("Script to validate, e.g. res://player.gd"),
    },
  },
  async ({ project_path, script_path }) => {
    try {
      return ok(
        await headlessOp(project_path, "validate_script", { script_path: resPath(script_path) })
      );
    } catch (e) {
      return fail(e);
    }
  }
);

// ---------------------------------------------------------------------------
// Live editor bridge tools
// ---------------------------------------------------------------------------

server.registerTool(
  "editor_status",
  {
    title: "Editor status",
    description:
      "Check the live Godot editor bridge: editor version, open project, and currently edited scene. Requires the Godot editor to be open with the godot_mcp_bridge addon enabled.",
    inputSchema: {},
  },
  async () => {
    try {
      return ok(await bridgeCall("info"));
    } catch (e) {
      return fail(e);
    }
  }
);

server.registerTool(
  "editor_run_gdscript",
  {
    title: "Run GDScript in the editor",
    description:
      "Execute arbitrary GDScript inside the live Godot editor process. The code runs as the body of a function; use 'return' to send a value back. EditorInterface is available, e.g. 'return EditorInterface.get_edited_scene_root().name'.",
    inputSchema: {
      code: z.string().describe("GDScript statements to execute (function body)"),
    },
  },
  async ({ code }) => {
    try {
      return ok(await bridgeCall("execute", { code }, 30000));
    } catch (e) {
      return fail(e);
    }
  }
);

server.registerTool(
  "editor_get_scene_tree",
  {
    title: "Get live editor scene tree",
    description: "Get the node tree of the scene currently open in the live Godot editor.",
    inputSchema: {},
  },
  async () => {
    try {
      return ok(await bridgeCall("get_scene_tree"));
    } catch (e) {
      return fail(e);
    }
  }
);

server.registerTool(
  "editor_open_scene",
  {
    title: "Open scene in editor",
    description: "Open a scene file in the live Godot editor.",
    inputSchema: {
      scene: z.string().describe("Scene path, e.g. res://main.tscn"),
    },
  },
  async ({ scene }) => {
    try {
      return ok(await bridgeCall("open_scene", { path: resPath(scene) }));
    } catch (e) {
      return fail(e);
    }
  }
);

// ---------------------------------------------------------------------------

const transport = new StdioServerTransport();
await server.connect(transport);
console.error("godot-mcp server running on stdio");
