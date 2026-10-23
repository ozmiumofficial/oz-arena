// Browser preview (npm run dev): stands in for Godot. Loads the menu state Godot dumped
// (public/q3/dev_state.json, written by tools/build_webui.py), keeps changes in localStorage,
// plays the Quake 3 menu sounds through <audio>, and fakes the showcase captions.

import { emit, type Msg } from "./bridge";
import { codeToGodot, mouseToGodot, bindLabel } from "./binds";
import type { InitPayload, Value } from "./types";
import type { Friend, Social, SocialState } from "./social";

const STORE = "q3menu.values";
const SOUNDS: Record<string, string> = {
  in: "misc/menu1.wav", move: "misc/menu2.wav", out: "misc/menu3.wav", buzz: "misc/menu4.wav", change: "weapons/change.wav",
};
let state: InitPayload | null = null;
let captionTimer: ReturnType<typeof setInterval> | null = null;
const audio = new Map<string, HTMLAudioElement>();

function save() {
  if (!state) return;
  const diff: Record<string, Value> = {};
  for (const e of state.schema) if (JSON.stringify(state.values[e.key]) !== JSON.stringify(e.default)) diff[e.key] = state.values[e.key];
  try {
    localStorage.setItem(STORE, JSON.stringify(diff));
  } catch {}
}

function set(key: string, value: Value) {
  if (!state) return;
  state.values[key] = value;
  save();
  emit({ type: "changed", key, value });
  const e = state.schema.find((s) => s.key === key);
  if (e?.type === "bind") emit({ type: "conflicts", map: conflicts() });
  if (key === "graphics/preset" && value !== "custom") notice("Preset applied (the real preset table lives in Godot).");
}

function conflicts() {
  const out: Record<string, string[]> = {};
  if (!state) return out;
  const binds = state.schema.filter((e) => e.type === "bind");
  for (const a of binds) {
    const mine = (state.values[a.key] as string[]).filter(Boolean);
    const hits = binds.filter((b) => b.key !== a.key && (state!.values[b.key] as string[]).some((x) => x && mine.includes(x)));
    if (hits.length) out[a.key] = hits.map((h) => h.key);
  }
  return out;
}

function notice(text: string) {
  emit({ type: "dev_notice", text });
}

function capture(key: string, slot: number) {
  emit({ type: "capture_started", key, slot });
  const done = (value: string | null, cancelled = false) => {
    window.removeEventListener("keydown", onKey, true);
    window.removeEventListener("mousedown", onMouse, true);
    window.removeEventListener("wheel", onWheel, true);
    if (cancelled) return emit({ type: "capture_cancelled", key });
    const v = [...(state!.values[key] as string[])];
    v[slot] = value ?? "";
    set(key, v);
    emit({ type: "bind_captured", key, slot, value: value ?? "", label: bindLabel(value ?? "") });
  };
  const onKey = (e: KeyboardEvent) => {
    e.preventDefault();
    e.stopPropagation();
    if (e.code === "Escape") return done(null, true);
    if (e.code === "Backspace" || e.code === "Delete") return done(null);
    done("key:" + codeToGodot(e.code));
  };
  const onMouse = (e: MouseEvent) => {
    e.preventDefault();
    e.stopPropagation();
    done("mouse:" + mouseToGodot(e.button));
  };
  const onWheel = (e: WheelEvent) => {
    e.preventDefault();
    done("mouse:" + (e.deltaY < 0 ? 4 : 5));
  };
  setTimeout(() => {
    window.addEventListener("keydown", onKey, true);
    window.addEventListener("mousedown", onMouse, true);
    window.addEventListener("wheel", onWheel, { capture: true, passive: false });
  }, 50);
}

async function ready() {
  const r = await fetch("/q3/dev_state.json");
  state = (await r.json()) as InitPayload;
  try {
    Object.assign(state.values, JSON.parse(localStorage.getItem(STORE) ?? "{}"));
  } catch {}
  // ?pause previews the in-match pause menu over a level shot
  if (location.search.includes("pause")) {
    state.context = "pause";
    state.match = {
      title: "FREE FOR ALL  -  THE LONGEST YARD", map: "q3dm17", map_name: "The Longest Yard", clock: "7:32", warmup: false,
      ready_up: false, ready: false, can_restart: true, weapon: 7,
      standings: [{ name: "Sarge", score: 9, local: false }, { name: "Player", score: 7, local: true }, { name: "Xaero", score: 4, local: false }],
    };
    document.body.style.background = "url(/q3/levelshots/q3dm17.jpg) center / cover";
  }
  emit({ ...state, type: "init" } as unknown as Msg);
  emit({ type: "conflicts", map: conflicts() });
  emitSocial();
  const names = [...state.characters.map((c) => [c, "Character"]), ["Rocket Launcher", "Weapon"], ["Quad Damage", "Powerup"], ["Railgun", "Weapon"]];
  let i = 0;
  if (captionTimer) clearInterval(captionTimer);
  const next = () => {
    const [name, kind] = names[i++ % names.length];
    emit({ type: "subject", name, kind });
  };
  next();
  captionTimer = setInterval(next, 9000);
}

export function handle(m: Msg) {
  switch (m.type) {
    case "ready":
      void ready();
      break;
    case "sound": {
      const file = SOUNDS[m.name as string];
      if (!file) break;
      let a = audio.get(file);
      if (!a) audio.set(file, (a = new Audio("/q3/sounds/" + file)));
      const p = a.cloneNode() as HTMLAudioElement;
      p.volume = 0.6;
      void p.play().catch(() => {});
      break;
    }
    case "set":
      set(m.key as string, m.value as Value);
      break;
    case "reset": {
      const e = state?.schema.find((s) => s.key === m.key);
      if (e) set(e.key, structuredClone(e.default));
      break;
    }
    case "reset_tab":
      state?.schema.filter((s) => s.tab === m.tab).forEach((e) => set(e.key, structuredClone(e.default)));
      break;
    case "capture_bind":
      capture(m.key as string, m.slot as number);
      break;
    case "clear_bind": {
      const v = [...(state!.values[m.key as string] as string[])];
      v[m.slot as number] = "";
      set(m.key as string, v);
      break;
    }
    case "export_code": {
      const diff: Record<string, Value> = {};
      for (const e of state?.schema ?? []) if (e.key.startsWith((m.prefix as string) ?? "") && JSON.stringify(state!.values[e.key]) !== JSON.stringify(e.default)) diff[e.key] = state!.values[e.key];
      const code = btoa(JSON.stringify(diff));
      void navigator.clipboard?.writeText(code).catch(() => {});
      emit({ type: "code", prefix: m.prefix, code });
      break;
    }
    case "import_code": {
      try {
        const d = JSON.parse(atob(String(m.code).trim())) as Record<string, Value>;
        Object.entries(d).forEach(([k, v]) => set(k, v));
        emit({ type: "imported", count: Object.keys(d).length });
      } catch {
        emit({ type: "imported", count: -1 });
      }
      break;
    }
    case "validate":
    case "start": {
      const s = m.setup as { mode: string; map: string };
      const mode = state?.modes.find((x) => x.id === s?.mode);
      const map = state?.maps.find((x) => x.map === s?.map);
      const errors: string[] = [];
      if (!mode) errors.push("Unknown game mode.");
      if (!map) errors.push("Unknown map.");
      else if (mode && !map.types.some((t) => mode.map_types.includes(t))) errors.push(`${map.name} isn't a ${mode.name} map.`);
      if (m.type === "validate") emit({ type: "validation", errors });
      else if (errors.length) emit({ type: "start_failed", errors });
      else notice(`Browser preview: Godot would now load Gameplay (${mode!.name} on ${map!.name}).`);
      break;
    }
    case "cinematic":
      notice(`Browser preview: Godot would play ${m.id} full screen.`);
      break;
    case "campaign_start":
      notice(`Browser preview: Godot would start ${m.map} at skill ${m.skill}.`);
      break;
    case "campaign_reset":
      notice("Browser preview: the campaign would be reset.");
      break;
    case "continue":
      notice("Browser preview: Godot would rejoin the last match.");
      break;
    case "map_editor":
      notice("Browser preview: Godot would now load the Map editor scene.");
      break;
    case "quit":
      notice("Browser preview: Godot would quit here.");
      break;
    case "social":
      socialOp(m);
      break;
  }
}

// ---- friends and parties: a pretend hub with a few friends, so the panel can be worked on in a browser

const ago = (min: number) => Date.now() - min * 60_000;
const friend = (id: string, name: string, model: string, color: string, f: Partial<Friend>): Friend => ({
  id, name, model, color, online: true, status: "online", detail: "In the menu", join: "", seen: ago(0), since: ago(9000), ...f,
});
const sc: SocialState = {
  me: { id: "me", code: "7KQ2-M9XD", name: "^1Ranger", model: "ranger", color: "#db2410" },
  friends: [
    friend("f1", "^4Visor", "visor", "#2266ff", { status: "match", detail: "Duel on q3tourney2", join: "203.0.113.7:27960" }),
    friend("f2", "Klesk", "klesk", "#5ccf4a", {}),
    friend("f3", "^3Anarki", "anarki", "#ffad33", { status: "editor", detail: "Map editor" }),
    friend("f4", "Orbb", "orbb", "#ff00ff", { online: false, status: "offline", detail: "", seen: ago(180) }),
    friend("f5", "^2Doom", "doom", "#00ff00", { online: false, status: "offline", detail: "", seen: ago(3 * 1440) }),
  ],
  incoming: [{ id: "r1", name: "^5Slash", model: "slash", color: "#00ffff", at: ago(5) }],
  outgoing: [],
  party: null,
  invites: [{ party: "pX", at: ago(1), expires: ago(-4), size: 2, from: { id: "f2", name: "Klesk", model: "klesk", color: "#5ccf4a" } }],
  limits: { friends: 200, party: 8 },
};
const social: Social = { status: "online", reason: "", state: sc, chat: [] };

function emitSocial() {
  emit({ type: "social", ...social } as unknown as Msg);
}

function socialOp(m: Msg) {
  const id = m.id as string;
  const me = { ...sc.me, online: true, status: "online" as const, detail: "In the menu", join: "", seen: Date.now(), since: 0 };
  const startParty = () =>
    (sc.party ??= { id: "p1", leader: "me", max: 8, members: [{ ...me, leader: true }], pending: [] });
  switch (m.op) {
    case "friend_add":
      sc.outgoing.unshift({ id: "o" + Date.now(), name: String(m.code), model: "sarge", color: "#db2410", at: Date.now() });
      emit({ type: "social_notice", text: `Friend request sent to ${m.code}.`, tone: "info" });
      break;
    case "friend_accept": {
      const r = sc.incoming.find((x) => x.id === id);
      sc.incoming = sc.incoming.filter((x) => x.id !== id);
      if (r) sc.friends.push(friend(r.id, r.name, r.model, r.color, {}));
      break;
    }
    case "friend_decline":
      sc.incoming = sc.incoming.filter((x) => x.id !== id);
      break;
    case "friend_cancel":
      sc.outgoing = sc.outgoing.filter((x) => x.id !== id);
      break;
    case "friend_remove":
      sc.friends = sc.friends.filter((x) => x.id !== id);
      break;
    case "party_invite": {
      const f = sc.friends.find((x) => x.id === id)!;
      startParty().pending.push(f);
      emit({ type: "social_notice", text: "Party invite sent.", tone: "info" });
      setTimeout(() => {
        if (!sc.party) return;
        sc.party.pending = sc.party.pending.filter((x) => x.id !== id);
        sc.party.members.push({ ...f, leader: false });
        emit({ type: "social_notice", text: `${f.name.replace(/\^./g, "")} joined the party.`, tone: "info" });
        emitSocial();
      }, 1500);
      break;
    }
    case "party_accept": {
      const inv = sc.invites.find((x) => x.party === m.party);
      sc.invites = sc.invites.filter((x) => x.party !== m.party);
      if (inv) {
        const host = sc.friends.find((x) => x.id === inv.from.id)!;
        sc.party = { id: inv.party, leader: host.id, max: 8, members: [{ ...host, leader: true }, { ...me, leader: false }], pending: [] };
      }
      break;
    }
    case "party_decline":
      sc.invites = sc.invites.filter((x) => x.party !== m.party);
      break;
    case "party_leave":
      sc.party = null;
      social.chat = [];
      break;
    case "party_kick":
    case "party_promote":
      if (sc.party) {
        if (m.op === "party_kick") sc.party.members = sc.party.members.filter((x) => x.id !== id);
        else sc.party.members.forEach((x) => (x.leader = x.id === id)), (sc.party.leader = id);
        if (sc.party.members.length <= 1) sc.party = null;
      }
      break;
    case "party_chat":
      emit({ type: "party_chat", from: { id: "me", name: sc.me.name }, text: m.text, at: Date.now() });
      setTimeout(() => sc.party && emit({ type: "party_chat", from: { id: "f2", name: "Klesk" }, text: "rdy", at: Date.now() }), 900);
      return;
    case "copy_code":
      void navigator.clipboard?.writeText(sc.me.code).catch(() => {});
      return;
  }
  emitSocial();
}
