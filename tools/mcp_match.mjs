// Drive a real match through both MCP servers: set the editor's run args (godot), play the project
// (godot-ai), wait, screenshot the running game, read its logs and monitors, stop, clear the args.
//   node tools/mcp_match.mjs [map] [bots] [seconds]       (the editor open on godot/; frames -> work/mcp_match)
import fs from "fs";
import os from "os";
import { createRequire } from "module";
import { fileURLToPath } from "url";
const require = createRequire(new URL("./godot-mcp/package.json", import.meta.url));
const { Client } = require("@modelcontextprotocol/sdk/client/index.js");
const { StdioClientTransport } = require("@modelcontextprotocol/sdk/client/stdio.js");

const [map = "q3dm17", bots = "3", seconds = "20"] = process.argv.slice(2);
const cfg = JSON.parse(fs.readFileSync(os.homedir() + "/.claude.json", "utf8")).mcpServers;
const OUT = fileURLToPath(new URL("../work/mcp_match", import.meta.url));
fs.mkdirSync(OUT, { recursive: true });

async function connect(name) {
  const s = cfg[name];
  const c = new Client({ name: "mcp-match", version: "1.0.0" });
  await c.connect(new StdioClientTransport({ command: s.command, args: s.args, env: { ...process.env, ...(s.env || {}) }, stderr: "ignore" }));
  return c;
}
async function call(c, name, args, label) {
  const r = await c.callTool({ name, arguments: args }, undefined, { timeout: 180000 });
  for (const [i, part] of (r.content || []).entries()) {
    if (part.type === "image") {
      const f = `${OUT}/${label || name}_${i}.png`;
      fs.writeFileSync(f, Buffer.from(part.data, "base64"));
      console.log(`-- ${name}: image -> ${f}`);
    } else {
      console.log(`-- ${name}${r.isError ? " (isError)" : ""}: ${String(part.text).slice(0, 900)}`);
    }
  }
  return r;
}
const wait = (ms) => new Promise((r) => setTimeout(r, ms));

const godot = await connect("godot");
const ai = await connect("godot-ai");
try {
  await call(godot, "editor_run_gdscript", { code: `ProjectSettings.set_setting("editor/run/main_run_args", "-- map=${map} bots=${bots}")\nreturn ProjectSettings.get_setting("editor/run/main_run_args")` });
  await call(ai, "project_run", { mode: "main" }).catch((e) => console.log("project_run:", e.message));
  await wait(8000);
  // the splash's disclaimer waits for any key
  await call(ai, "game_manage", { op: "input_action", params: { action: "ui_accept", pressed: true } });
  await call(ai, "game_manage", { op: "input_action", params: { action: "ui_accept", pressed: false } });
  await wait(Number(seconds) * 1000);
  await call(ai, "editor_screenshot", { source: "game", max_resolution: 640 }, "game_a");
  await call(ai, "game_manage", { op: "input_action", params: { action: "q3_attack", pressed: true } });
  await wait(1500);
  await call(ai, "editor_screenshot", { source: "game", max_resolution: 640 }, "game_b");
  await call(ai, "game_manage", { op: "input_action", params: { action: "q3_attack", pressed: false } });
  await call(ai, "editor_manage", { op: "monitors_get", params: {} });
  await call(ai, "logs_read", { source: "game", count: 15 });
} finally {
  await call(ai, "project_manage", { op: "stop", params: {} }).catch((e) => console.log("stop failed", e.message));
  await call(godot, "editor_run_gdscript", { code: `ProjectSettings.set_setting("editor/run/main_run_args", "")\nreturn "cleared"` });
  await godot.close();
  await ai.close();
}
process.exit(0);
