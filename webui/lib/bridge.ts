// Messages between this menu and Godot. Inside Godot (godot_wry) the page has window.ipc:
// we send with ipc.postMessage(json) and Godot's post_message arrives as a document "message"
// CustomEvent whose detail is the JSON string. In a normal browser (npm run dev) a mock stands
// in for Godot, driven by public/q3/dev_state.json; see mock.ts.

export interface Msg {
  type: string;
  [k: string]: unknown;
}
type Handler = (m: Msg) => void;

interface IpcWindow {
  ipc?: { postMessage: (s: string) => void };
}

const handlers = new Map<string, Set<Handler>>();
let mock: ((m: Msg) => void) | null = null;
let listening = false;

export function inGodot(): boolean {
  return typeof window !== "undefined" && !!(window as IpcWindow).ipc;
}

function listen() {
  if (listening || typeof document === "undefined") return;
  listening = true;
  document.addEventListener("message", (e) => {
    const detail = (e as CustomEvent).detail;
    try {
      emit(typeof detail === "string" ? JSON.parse(detail) : detail);
    } catch {
      console.warn("bridge: bad message", detail);
    }
  });
}

export function emit(m: Msg) {
  handlers.get(m.type)?.forEach((h) => h(m));
  handlers.get("*")?.forEach((h) => h(m));
}

export function on(type: string, h: Handler): () => void {
  listen();
  if (!handlers.has(type)) handlers.set(type, new Set());
  handlers.get(type)!.add(h);
  return () => handlers.get(type)?.delete(h);
}

export async function send(type: string, data: Record<string, unknown> = {}) {
  const m: Msg = { type, ...data };
  if (inGodot()) {
    (window as IpcWindow).ipc!.postMessage(JSON.stringify(m));
    return;
  }
  if (!mock) mock = (await import("./mock")).handle;
  mock(m);
}
