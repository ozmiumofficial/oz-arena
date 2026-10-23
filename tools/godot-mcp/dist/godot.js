import { spawn, execFile } from "node:child_process";
import { existsSync, readdirSync } from "node:fs";
import { join, resolve, dirname } from "node:path";
import { fileURLToPath } from "node:url";
import os from "node:os";
const __dirname = dirname(fileURLToPath(import.meta.url));
// dist/ -> package root
export const PKG_ROOT = resolve(__dirname, "..");
let cachedGodotPath = null;
export function findGodot() {
    if (cachedGodotPath)
        return cachedGodotPath;
    const candidates = [];
    if (process.env.GODOT_PATH)
        candidates.push(process.env.GODOT_PATH);
    // Scan common locations for a Godot 4.x executable
    const dirs = [
        join(os.homedir(), "Desktop"),
        join(os.homedir(), "Downloads"),
        "C:\\Program Files\\Godot",
        "C:\\Godot",
        // Steam build (godot.windows.opt.tools.64.exe)
        "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Godot Engine",
        "C:\\Program Files\\Steam\\steamapps\\common\\Godot Engine",
        "D:\\SteamLibrary\\steamapps\\common\\Godot Engine",
    ];
    for (const dir of dirs) {
        try {
            for (const f of readdirSync(dir)) {
                if (/godot.*\.exe$/i.test(f) && !/console/i.test(f)) {
                    candidates.push(join(dir, f));
                }
            }
        }
        catch {
            /* dir missing */
        }
    }
    candidates.push("godot"); // PATH fallback
    for (const c of candidates) {
        if (c === "godot" || existsSync(c)) {
            cachedGodotPath = c;
            return c;
        }
    }
    throw new Error("Godot executable not found. Set the GODOT_PATH environment variable to the full path of your Godot 4.x .exe");
}
export function godotVersion() {
    return new Promise((res, rej) => {
        execFile(findGodot(), ["--version"], { timeout: 20000 }, (err, stdout) => {
            if (err)
                return rej(err);
            res(stdout.trim());
        });
    });
}
let currentRun = null;
const MAX_LINES = 500;
function pushCapped(arr, chunk) {
    for (const line of chunk.split(/\r?\n/)) {
        if (line.length === 0)
            continue;
        arr.push(line);
        if (arr.length > MAX_LINES)
            arr.shift();
    }
}
export function runProject(projectPath, scene) {
    if (currentRun && !currentRun.exited) {
        throw new Error(`A project is already running (pid ${currentRun.pid}). Call stop_project first.`);
    }
    const abs = resolve(projectPath);
    if (!existsSync(join(abs, "project.godot"))) {
        throw new Error(`No project.godot found in ${abs}`);
    }
    const args = ["--path", abs];
    if (scene) {
        args.push(scene.startsWith("res://") ? scene : `res://${scene.replace(/\\/g, "/")}`);
    }
    const proc = spawn(findGodot(), args, { stdio: ["ignore", "pipe", "pipe"] });
    const state = {
        proc,
        pid: proc.pid ?? -1,
        projectPath: abs,
        scene: scene ?? null,
        startedAt: Date.now(),
        stdout: [],
        stderr: [],
        exitCode: null,
        exited: false,
    };
    proc.stdout.on("data", (d) => pushCapped(state.stdout, d.toString()));
    proc.stderr.on("data", (d) => pushCapped(state.stderr, d.toString()));
    proc.on("exit", (code) => {
        state.exitCode = code;
        state.exited = true;
    });
    proc.on("error", (e) => {
        pushCapped(state.stderr, `spawn error: ${e.message}`);
        state.exited = true;
    });
    currentRun = state;
    return state;
}
export function getRun() {
    return currentRun;
}
export function stopProject() {
    if (!currentRun || currentRun.exited) {
        return { stopped: false };
    }
    const pid = currentRun.pid;
    try {
        // taskkill kills the whole tree; plain kill() can miss GPU child processes
        spawn("taskkill", ["/PID", String(pid), "/T", "/F"], { stdio: "ignore" });
    }
    catch {
        currentRun.proc.kill();
    }
    return { stopped: true, pid };
}
// ---------------------------------------------------------------------------
// Headless operations via godot/ops.gd
// ---------------------------------------------------------------------------
export async function headlessOp(projectPath, op, args, timeoutMs = 60000) {
    const abs = resolve(projectPath);
    if (!existsSync(join(abs, "project.godot"))) {
        throw new Error(`No project.godot found in ${abs}`);
    }
    const opsScript = join(PKG_ROOT, "godot", "ops.gd").replace(/\\/g, "/");
    const payload = Buffer.from(JSON.stringify({ op, ...args }), "utf8").toString("base64");
    return new Promise((res, rej) => {
        execFile(findGodot(), ["--headless", "--path", abs, "--script", opsScript, "--", payload], { timeout: timeoutMs, maxBuffer: 10 * 1024 * 1024 }, (err, stdout, stderr) => {
            const marker = "GODOT_MCP_RESULT:";
            const line = stdout.split(/\r?\n/).find((l) => l.startsWith(marker));
            if (line) {
                try {
                    const parsed = JSON.parse(line.slice(marker.length));
                    if (parsed.ok)
                        return res(parsed.result);
                    return rej(new Error(parsed.error ?? "operation failed"));
                }
                catch (e) {
                    return rej(new Error(`Bad ops result JSON: ${e.message}`));
                }
            }
            if (err)
                return rej(new Error(`Godot headless op failed: ${err.message}\n${stderr}\n${stdout}`));
            rej(new Error(`No result marker in Godot output.\nstdout: ${stdout}\nstderr: ${stderr}`));
        });
    });
}
