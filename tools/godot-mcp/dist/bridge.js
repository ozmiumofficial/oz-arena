import net from "node:net";
const BRIDGE_HOST = "127.0.0.1";
const BRIDGE_PORT = Number(process.env.GODOT_BRIDGE_PORT ?? 9080);
let socket = null;
let buffer = "";
let nextId = 1;
const pending = new Map();
function teardown(err) {
    const e = err ?? new Error("Bridge connection closed");
    for (const [, p] of pending) {
        clearTimeout(p.timer);
        p.reject(e);
    }
    pending.clear();
    socket?.destroy();
    socket = null;
    buffer = "";
}
function connect(timeoutMs) {
    if (socket && !socket.destroyed)
        return Promise.resolve(socket);
    return new Promise((resolve, reject) => {
        const s = net.createConnection({ host: BRIDGE_HOST, port: BRIDGE_PORT });
        const timer = setTimeout(() => {
            s.destroy();
            reject(new Error(`Could not connect to the Godot editor bridge on ${BRIDGE_HOST}:${BRIDGE_PORT}. ` +
                `Is the Godot editor open with the godot_mcp_bridge addon enabled?`));
        }, timeoutMs);
        s.once("connect", () => {
            clearTimeout(timer);
            socket = s;
            buffer = "";
            s.setEncoding("utf8");
            s.on("data", (chunk) => {
                buffer += chunk;
                let idx;
                while ((idx = buffer.indexOf("\n")) >= 0) {
                    const line = buffer.slice(0, idx).trim();
                    buffer = buffer.slice(idx + 1);
                    if (!line)
                        continue;
                    try {
                        const msg = JSON.parse(line);
                        const p = pending.get(msg.id);
                        if (p) {
                            pending.delete(msg.id);
                            clearTimeout(p.timer);
                            if (msg.ok)
                                p.resolve(msg.result);
                            else
                                p.reject(new Error(msg.error ?? "bridge command failed"));
                        }
                    }
                    catch {
                        /* ignore malformed line */
                    }
                }
            });
            s.on("error", (e) => teardown(e));
            s.on("close", () => teardown());
            resolve(s);
        });
        s.once("error", (e) => {
            clearTimeout(timer);
            reject(new Error(`Could not connect to the Godot editor bridge on ${BRIDGE_HOST}:${BRIDGE_PORT} (${e.message}). ` +
                `Is the Godot editor open with the godot_mcp_bridge addon enabled?`));
        });
    });
}
export async function bridgeCall(cmd, args = {}, timeoutMs = 20000) {
    const s = await connect(5000);
    const id = nextId++;
    const line = JSON.stringify({ id, cmd, args }) + "\n";
    return new Promise((resolve, reject) => {
        const timer = setTimeout(() => {
            pending.delete(id);
            reject(new Error(`Bridge command '${cmd}' timed out after ${timeoutMs}ms`));
        }, timeoutMs);
        pending.set(id, { resolve, reject, timer });
        s.write(line, (err) => {
            if (err) {
                pending.delete(id);
                clearTimeout(timer);
                reject(err);
            }
        });
    });
}
export async function bridgeAvailable() {
    try {
        await bridgeCall("ping", {}, 3000);
        return true;
    }
    catch {
        return false;
    }
}
