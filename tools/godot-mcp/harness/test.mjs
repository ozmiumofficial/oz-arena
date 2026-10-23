#!/usr/bin/env node
// Verification harness for godot-mcp.
// Spawns the MCP server over stdio, speaks real JSON-RPC, and exercises EVERY tool.
// Exit code 0 only if all tests pass.

import { spawn } from "node:child_process";
import { existsSync, rmSync, statSync } from "node:fs";
import { join, dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const TESTPROJECT = join(ROOT, "testproject");
const GODOT = process.env.GODOT_PATH ?? "C:\\Users\\Ozmium\\Desktop\\Godot_v4.7.2-stable_win64.exe";

// ---------------------------------------------------------------------------
// MCP stdio client
// ---------------------------------------------------------------------------

let child;
let nextId = 1;
const pending = new Map();
let stderrLog = "";

function startServer() {
  child = spawn("node", [join(ROOT, "dist", "index.js")], {
    stdio: ["pipe", "pipe", "pipe"],
    env: { ...process.env, GODOT_PATH: GODOT },
  });
  let buf = "";
  child.stdout.on("data", (d) => {
    buf += d.toString();
    let idx;
    while ((idx = buf.indexOf("\n")) >= 0) {
      const line = buf.slice(0, idx).trim();
      buf = buf.slice(idx + 1);
      if (!line) continue;
      try {
        const msg = JSON.parse(line);
        if (msg.id !== undefined && pending.has(msg.id)) {
          const p = pending.get(msg.id);
          pending.delete(msg.id);
          clearTimeout(p.timer);
          if (msg.error) p.reject(new Error(msg.error.message ?? JSON.stringify(msg.error)));
          else p.resolve(msg.result);
        }
      } catch {
        /* ignore non-JSON lines */
      }
    }
  });
  child.stderr.on("data", (d) => (stderrLog += d.toString()));
}

function rpc(method, params = {}, timeoutMs = 120000) {
  const id = nextId++;
  const msg = { jsonrpc: "2.0", id, method, params };
  return new Promise((res, rej) => {
    const timer = setTimeout(() => {
      pending.delete(id);
      rej(new Error(`RPC ${method} timed out after ${timeoutMs}ms`));
    }, timeoutMs);
    pending.set(id, { resolve: res, reject: rej, timer });
    child.stdin.write(JSON.stringify(msg) + "\n");
  });
}

function notify(method, params = {}) {
  child.stdin.write(JSON.stringify({ jsonrpc: "2.0", method, params }) + "\n");
}

async function callTool(name, args = {}, timeoutMs = 120000) {
  const result = await rpc("tools/call", { name, arguments: args }, timeoutMs);
  const text = (result.content ?? []).map((c) => c.text ?? "").join("\n");
  return { text, isError: !!result.isError, raw: result };
}

function parseJson(text) {
  try {
    return JSON.parse(text);
  } catch {
    return null;
  }
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// ---------------------------------------------------------------------------
// Test runner
// ---------------------------------------------------------------------------

const results = [];
async function test(name, fn) {
  try {
    const detail = await fn();
    results.push({ name, pass: true, detail: detail ?? "" });
    console.log(`  PASS  ${name}${detail ? `  (${detail})` : ""}`);
  } catch (e) {
    results.push({ name, pass: false, detail: e.message });
    console.log(`  FAIL  ${name}\n        ${e.message.split("\n").join("\n        ")}`);
  }
}

function assert(cond, msg) {
  if (!cond) throw new Error(msg);
}

// ---------------------------------------------------------------------------

let editorProc = null;

async function main() {
  console.log("godot-mcp verification harness\n");

  // Clean artifacts from previous runs
  rmSync(join(TESTPROJECT, "gen"), { recursive: true, force: true });
  rmSync(join(ROOT, "screenshots"), { recursive: true, force: true });

  startServer();
  await rpc("initialize", {
    protocolVersion: "2025-06-18",
    capabilities: {},
    clientInfo: { name: "godot-mcp-harness", version: "1.0.0" },
  });
  notify("notifications/initialized");

  // ---- 1. tools/list --------------------------------------------------------
  await test("tools/list exposes all tools", async () => {
    const { tools } = await rpc("tools/list");
    const names = tools.map((t) => t.name).sort();
    const expected = [
      "add_node", "create_scene", "create_script", "edit_script", "editor_get_scene_tree",
      "editor_open_scene", "editor_run_gdscript", "editor_status", "get_node_property",
      "get_run_output", "godot_info", "list_project_files", "read_scene", "read_script",
      "run_project", "screenshot_game", "set_node_property", "stop_project", "validate_script",
    ];
    for (const e of expected) assert(names.includes(e), `missing tool: ${e}`);
    return `${names.length} tools`;
  });

  // ---- 2. godot_info --------------------------------------------------------
  await test("godot_info", async () => {
    const r = await callTool("godot_info");
    assert(!r.isError, r.text);
    const j = parseJson(r.text);
    assert(j.version.includes("4.7.2"), `unexpected version: ${j.version}`);
    return j.version;
  });

  // ---- 3. create_scene ------------------------------------------------------
  await test("create_scene", async () => {
    const r = await callTool("create_scene", {
      project_path: TESTPROJECT,
      scene_path: "res://gen/test_scene.tscn",
      root_type: "Node2D",
      root_name: "TestRoot",
    });
    assert(!r.isError, r.text);
    assert(existsSync(join(TESTPROJECT, "gen", "test_scene.tscn")), "scene file not on disk");
    return "res://gen/test_scene.tscn";
  });

  // ---- 4. add_node ----------------------------------------------------------
  await test("add_node (Label with properties)", async () => {
    const r = await callTool("add_node", {
      project_path: TESTPROJECT,
      scene: "res://gen/test_scene.tscn",
      parent: ".",
      type: "Label",
      name: "Greeting",
      properties: { text: "hi from mcp", position: "Vector2(50, 80)" },
    });
    assert(!r.isError, r.text);
    const j = parseJson(r.text);
    assert(j.added === "Greeting", `unexpected node path: ${j.added}`);
    return "Greeting added";
  });

  // ---- 5. get_node_property -------------------------------------------------
  await test("get_node_property", async () => {
    const r = await callTool("get_node_property", {
      project_path: TESTPROJECT,
      scene: "res://gen/test_scene.tscn",
      node_path: "Greeting",
      property: "text",
    });
    assert(!r.isError, r.text);
    const j = parseJson(r.text);
    assert(j.value === "hi from mcp", `wrong value: ${JSON.stringify(j.value)}`);
    return `text = "${j.value}"`;
  });

  // ---- 6. set_node_property + readback -------------------------------------
  await test("set_node_property persists", async () => {
    let r = await callTool("set_node_property", {
      project_path: TESTPROJECT,
      scene: "res://gen/test_scene.tscn",
      node_path: "Greeting",
      property: "text",
      value: "updated text",
    });
    assert(!r.isError, r.text);
    r = await callTool("get_node_property", {
      project_path: TESTPROJECT,
      scene: "res://gen/test_scene.tscn",
      node_path: "Greeting",
      property: "text",
    });
    const j = parseJson(r.text);
    assert(j.value === "updated text", `value did not persist: ${JSON.stringify(j.value)}`);
    return "readback ok";
  });

  // ---- 7. Vector2 conversion ------------------------------------------------
  await test("Godot literal conversion (Vector2)", async () => {
    const r = await callTool("get_node_property", {
      project_path: TESTPROJECT,
      scene: "res://gen/test_scene.tscn",
      node_path: "Greeting",
      property: "position",
    });
    assert(!r.isError, r.text);
    const j = parseJson(r.text);
    assert(String(j.value).includes("50") && String(j.value).includes("80"), `position wrong: ${j.value}`);
    return `position = ${j.value}`;
  });

  // ---- 8. create_script + attach -------------------------------------------
  await test("create_script with attach", async () => {
    const r = await callTool("create_script", {
      project_path: TESTPROJECT,
      script_path: "res://gen/spinner.gd",
      content: 'extends Node2D\n\nfunc _ready() -> void:\n\tprint("SPINNER_READY")\n',
      attach_to_scene: "res://gen/test_scene.tscn",
      attach_to_node: ".",
    });
    assert(!r.isError, r.text);
    const j = parseJson(r.text);
    assert(j.attached && j.attached.attached === "res://gen/spinner.gd", `attach failed: ${r.text}`);
    return "spinner.gd created + attached";
  });

  // ---- 9. read_scene --------------------------------------------------------
  await test("read_scene shows tree + script", async () => {
    const r = await callTool("read_scene", {
      project_path: TESTPROJECT,
      scene: "res://gen/test_scene.tscn",
    });
    assert(!r.isError, r.text);
    const j = parseJson(r.text);
    assert(j.tree.name === "TestRoot" && j.tree.type === "Node2D", "root wrong");
    assert(j.tree.script === "res://gen/spinner.gd", `root script missing: ${JSON.stringify(j.tree)}`);
    const kids = j.tree.children ?? [];
    assert(kids.some((c) => c.name === "Greeting" && c.type === "Label"), "Greeting Label missing");
    return "TestRoot(Node2D)+script, Greeting(Label)";
  });

  // ---- 10. read_script ------------------------------------------------------
  await test("read_script", async () => {
    const r = await callTool("read_script", {
      project_path: TESTPROJECT,
      script_path: "res://gen/spinner.gd",
    });
    assert(!r.isError, r.text);
    assert(r.text.includes("SPINNER_READY"), "content mismatch");
    return `${r.text.length} chars`;
  });

  // ---- 11. edit_script ------------------------------------------------------
  await test("edit_script", async () => {
    const newContent =
      'extends Node2D\n\nfunc _ready() -> void:\n\tprint("SPINNER_READY")\n\tprint("SPINNER_V2")\n';
    let r = await callTool("edit_script", {
      project_path: TESTPROJECT,
      script_path: "res://gen/spinner.gd",
      content: newContent,
    });
    assert(!r.isError, r.text);
    r = await callTool("read_script", {
      project_path: TESTPROJECT,
      script_path: "res://gen/spinner.gd",
    });
    assert(r.text.includes("SPINNER_V2"), "edit did not persist");
    return "edit persisted";
  });

  // ---- 12. validate_script (good + bad) -------------------------------------
  await test("validate_script good/bad", async () => {
    let r = await callTool("validate_script", {
      project_path: TESTPROJECT,
      script_path: "res://gen/spinner.gd",
    });
    assert(!r.isError, r.text);
    let j = parseJson(r.text);
    assert(j.valid === true, `spinner.gd should be valid: ${r.text}`);
    await callTool("create_script", {
      project_path: TESTPROJECT,
      script_path: "res://gen/broken.gd",
      content: "extends Node\nfunc _ready(:\n\tthis is not gdscript!!!\n",
    });
    r = await callTool("validate_script", {
      project_path: TESTPROJECT,
      script_path: "res://gen/broken.gd",
    });
    assert(!r.isError, r.text);
    j = parseJson(r.text);
    assert(j.valid === false, `broken.gd should be invalid: ${r.text}`);
    return "good=valid, broken=invalid";
  });

  // ---- 13. list_project_files ----------------------------------------------
  await test("list_project_files", async () => {
    const r = await callTool("list_project_files", { project_path: TESTPROJECT, type: "all" });
    assert(!r.isError, r.text);
    const j = parseJson(r.text);
    assert(j.scenes.includes("res://main.tscn"), "main.tscn missing");
    assert(j.scenes.includes("res://gen/test_scene.tscn"), "test_scene.tscn missing");
    assert(j.scripts.includes("res://main.gd"), "main.gd missing");
    assert(j.scripts.includes("res://gen/spinner.gd"), "spinner.gd missing");
    return `${j.scenes.length} scenes, ${j.scripts.length} scripts, ${j.resources.length} resources`;
  });

  // ---- 14. run_project + output capture -------------------------------------
  await test("run_project captures game output", async () => {
    const r = await callTool("run_project", { project_path: TESTPROJECT });
    assert(!r.isError, r.text);
    const deadline = Date.now() + 30000;
    let out = null;
    while (Date.now() < deadline) {
      const o = await callTool("get_run_output");
      out = parseJson(o.text);
      if (out.stdout.some((l) => l.includes("HELLO_FROM_GAME"))) break;
      if (out.status === "exited") throw new Error(`game exited early: ${JSON.stringify(out)}`);
      await sleep(1000);
    }
    assert(out.stdout.some((l) => l.includes("HELLO_FROM_GAME")), `no HELLO_FROM_GAME in: ${JSON.stringify(out?.stdout)}`);
    return "HELLO_FROM_GAME captured";
  });

  // ---- 15. screenshot_game --------------------------------------------------
  await test("screenshot_game", async () => {
    const r = await callTool("screenshot_game", {});
    assert(!r.isError, r.text);
    const j = parseJson(r.text);
    assert(existsSync(j.path), "png missing on disk");
    assert(j.bytes > 1000, `png too small: ${j.bytes} bytes`);
    return `${j.path} (${j.bytes} bytes)`;
  });

  // ---- 16. stop_project -----------------------------------------------------
  await test("stop_project", async () => {
    const r = await callTool("stop_project");
    assert(!r.isError, r.text);
    const deadline = Date.now() + 15000;
    while (Date.now() < deadline) {
      const o = parseJson((await callTool("get_run_output")).text);
      if (o.status === "exited") return "process exited";
      await sleep(500);
    }
    throw new Error("process did not exit after stop_project");
  });

  // ---- 17. run a specific scene --------------------------------------------
  await test("run_project with specific scene", async () => {
    const r = await callTool("run_project", {
      project_path: TESTPROJECT,
      scene: "res://gen/test_scene.tscn",
    });
    assert(!r.isError, r.text);
    const deadline = Date.now() + 30000;
    let ok = false;
    while (Date.now() < deadline) {
      const o = parseJson((await callTool("get_run_output")).text);
      if (o.stdout.some((l) => l.includes("SPINNER_READY"))) { ok = true; break; }
      if (o.status === "exited") throw new Error(`scene exited early: ${JSON.stringify(o)}`);
      await sleep(1000);
    }
    assert(ok, "SPINNER_READY not captured from specific scene");
    await callTool("stop_project");
    return "SPINNER_READY captured";
  });

  // ---- Editor bridge phase --------------------------------------------------
  console.log("\n  --- launching Godot editor for bridge tests ---");
  editorProc = spawn(GODOT, ["-e", "--path", TESTPROJECT], { stdio: "ignore", detached: false });

  await test("editor_status (bridge connects)", async () => {
    const deadline = Date.now() + 120000;
    let last = "";
    while (Date.now() < deadline) {
      const r = await callTool("editor_status", {}, 15000);
      if (!r.isError) {
        const j = parseJson(r.text);
        assert(j.project_name === "GodotMcpTest", `wrong project: ${r.text}`);
        return `editor ${j.godot_version}`;
      }
      last = r.text;
      await sleep(2000);
    }
    throw new Error(`bridge never came up: ${last}`);
  });

  await test("editor_open_scene", async () => {
    const r = await callTool("editor_open_scene", { scene: "res://main.tscn" });
    assert(!r.isError, r.text);
    return "opened res://main.tscn";
  });

  await test("editor_get_scene_tree", async () => {
    const deadline = Date.now() + 30000;
    let last = "";
    while (Date.now() < deadline) {
      const r = await callTool("editor_get_scene_tree");
      if (!r.isError) {
        const j = parseJson(r.text);
        if (j.tree && j.tree.name === "Main") {
          const kids = (j.tree.children ?? []).map((c) => c.name);
          assert(kids.includes("Rect"), `Rect child missing: ${kids}`);
          return `Main with [${kids.join(", ")}]`;
        }
        last = r.text;
      } else last = r.text;
      await sleep(1500);
    }
    throw new Error(`edited scene never became Main: ${last}`);
  });

  await test("editor_run_gdscript (arithmetic)", async () => {
    const r = await callTool("editor_run_gdscript", { code: "return 2 + 2" });
    assert(!r.isError, r.text);
    const j = parseJson(r.text);
    assert(j.returned === 4, `expected 4, got: ${r.text}`);
    return "2 + 2 = 4";
  });

  await test("editor_run_gdscript (EditorInterface)", async () => {
    const r = await callTool("editor_run_gdscript", {
      code: "var root = EditorInterface.get_edited_scene_root()\nreturn str(root.name)",
    });
    assert(!r.isError, r.text);
    const j = parseJson(r.text);
    assert(j.returned === "Main", `expected Main, got: ${r.text}`);
    return `edited root = ${j.returned}`;
  });

  await test("editor_run_gdscript (parse error is reported)", async () => {
    const r = await callTool("editor_run_gdscript", { code: "this is (not gdscript" });
    assert(r.isError, "expected an error result for invalid code");
    return "parse error surfaced";
  });

  // ---- report ---------------------------------------------------------------
  finishAndReport();
}

function finishAndReport() {
  if (editorProc && editorProc.pid) {
    spawn("taskkill", ["/PID", String(editorProc.pid), "/T", "/F"], { stdio: "ignore" });
  }
  try {
    child.kill();
  } catch {}
  const passed = results.filter((r) => r.pass).length;
  const failed = results.length - passed;
  console.log(`\n=== RESULT: ${passed}/${results.length} passed, ${failed} failed ===`);
  if (failed > 0) {
    console.log("\nFailed:");
    for (const r of results.filter((x) => !x.pass)) console.log(`  - ${r.name}: ${r.detail}`);
    if (stderrLog) console.log(`\nserver stderr:\n${stderrLog.slice(-3000)}`);
  }
  process.exit(failed > 0 ? 1 : 0);
}

main().catch((e) => {
  console.error("HARNESS CRASH:", e);
  finishAndReport();
});
