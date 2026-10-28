// Browser preview (npm run dev): stands in for Godot. Loads the menu state Godot dumped
// (public/q3/dev_state.json, written by tools/build_webui.py), keeps changes in localStorage,
// plays the Quake 3 menu sounds through <audio>, and fakes the showcase captions.

import { emit, type Msg } from "./bridge";
import { codeToGodot, mouseToGodot, bindLabel } from "./binds";
import type { InitPayload, PartKind, ProfileState, ReplayInfo, Value } from "./types";
import type { Friend, Social, SocialState } from "./social";

let dedicatedRunning = 0;
const STORE = "q3menu.values";
const CARD_STORE = "q3menu.card";
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
  // ?demo swaps in a mid-career profile (dump_menu_state.gd makes both), so the card editor has
  // a mix of earned and locked parts to show
  const demo = (state as InitPayload & { profile_demo?: ProfileState }).profile_demo;
  if (location.search.includes("demo") && demo) state.profile = demo;
  try {
    const card = JSON.parse(localStorage.getItem(CARD_STORE) ?? "null");
    if (card && state.profile) state.profile.card = state.profile.shown = { ...state.profile.card, ...card };
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
  // ?update previews the "update available" offer (?update=old: a build that can't install it)
  if (location.search.includes("update")) {
    emit({
      type: "update",
      can_install: !location.search.includes("update=old"),
      info: {
        tag: "v0.4.0-beta", version: "0.4.0-beta", name: "v0.4.0-beta: replays and the duel maps", published: "2026-10-25", beta: true,
        size: 1_240_000_000, url: "https://github.com/ozmiumofficial/quake-3-arena-remastered-fan-project/releases/tag/v0.4.0-beta",
        notes: [
          "## What's new", "- Replays and the replay viewer", "- Spectator mode and the cinematic camera", "- The 15-map duel pool", "",
          "## Fixed", "- Additive glow layers drew as faint squares", "- Item timers read the right respawn",
        ].join("\n"),
      },
    } as unknown as Msg);
  }
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
    // the player card: the dump says which parts are earned; refuse anything else, as Godot does
    case "card_set":
    case "card_showcase": {
      const p = state?.profile;
      if (!p || !state?.cards) break;
      const slot = (m.slot ?? m.kind) as string;
      const value = (m.id ?? m.ids) as string | string[];
      const kinds: Record<string, PartKind> = { banner: "banners", emblem: "emblems", frame: "frames", title: "titles", accent: "accents", rail: "rails", taunt: "taunts" };
      const done = new Set(p.achievements.filter((a) => a.done).map((a) => a.id));
      const ok = Array.isArray(value)
        ? value.length <= 3 && (slot === "stats" ? value.every((v) => p.stats[v] !== undefined) : value.every((v) => done.has(v)))
        : !!p.unlocked[kinds[slot]]?.includes(value);
      if (ok) {
        p.card = { ...p.card, [slot]: value };
        p.shown = { ...p.shown, [slot]: value };
        try {
          localStorage.setItem(CARD_STORE, JSON.stringify(p.card));
        } catch {}
      } else handle({ type: "sound", name: "buzz" });
      emit({ type: "profile", profile: structuredClone(p) });
      break;
    }
    case "card_seen":
      if (state?.profile) state.profile.new = state.profile.new.filter((k) => !(m.keys as string[]).includes(k));
      break;
    case "taunt":
      notice("Taunts play in Godot (the preview has no character sounds).");
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
    // online matches (the Server Browser): a few made-up listings, and a join that connects after a moment
    case "net_browse":
      emit({
        type: "net_matches",
        region: "na_east",
        matches: [
          { host: "a".repeat(20), owner: "Ranger", region: "na_east", name: "Open FFA", mode: "ffa", map: "q3dm17", players: 3, max: 8, password: false, ranked: false, version: "0.3.0-beta" },
          { host: "b".repeat(20), owner: "Visor", region: "eu", name: "Duel night", mode: "duel", map: "q3tourney2", players: 1, max: 2, password: true, ranked: false, version: "0.3.0-beta" },
          { host: "c".repeat(20), owner: "Orbb", region: "na_east", name: "Full house", mode: "tdm", map: "q3dm6", players: 8, max: 8, password: false, ranked: true, version: "0.3.0-beta" },
        ],
      } as unknown as Msg);
      break;
    case "net_join":
      emit({ type: "net_join_state", status: "asking", reason: "", host: m.host });
      setTimeout(() => emit({ type: "net_join_state", status: "connecting", reason: "", host: m.host }), 700);
      setTimeout(() => emit({ type: "net_join_state", status: "failed", reason: "Browser preview: Godot would connect and start the match here.", host: m.host }), 2200);
      break;
    case "queue_join":
      emit({ type: "queue", state: { status: "queued", since: Date.now(), modes: m.modes, ranked: m.ranked }, ratings: { season: "2026-Q4", queues: { duel: { rating: 1000, rated: 3, wins: 2, losses: 1, peak: 1020 } } }, rtt: 40 } as unknown as Msg);
      break;
    case "dedicated_get":
    case "dedicated_start":
    case "dedicated_stop":
    case "dedicated_autostart": {
      if (m.type === "dedicated_start") dedicatedRunning = Date.now();
      if (m.type === "dedicated_stop") dedicatedRunning = 0;
      const config = (m.config as object | undefined) ?? { name: "Dedicated server", password: "", mode: "ffa", rotation: [], max_players: 16, bots: 2, bot_skill: 3, motd: "", rcon_password: "", online: true, idle_sleep: true };
      emit({
        type: "dedicated", config, autostart: false,
        status: dedicatedRunning
          ? { running: true, name: "Dedicated server", mode: "ffa", map: "q3dm17", max: 16, humans: 2, sleeping: false, listed: true, uptime: Math.floor((Date.now() - dedicatedRunning) / 1000), players: [{ client: 0, name: "Sarge", bot: true }, { client: 1, name: "Ranger", bot: false }] }
          : { running: false },
      } as unknown as Msg);
      break;
    }
    case "dedicated_command":
      break;
    case "queue_leave":
      emit({ type: "queue", state: { status: "idle" }, ratings: { season: "2026-Q4", queues: {} }, rtt: 40 } as unknown as Msg);
      break;
    case "net_cancel":
      emit({ type: "net_join_state", status: "failed", reason: "Cancelled.", host: "" });
      break;
    case "open_url":
      window.open(String(m.url), "_blank", "noopener");
      break;
    case "update_start": {
      // a pretend download of a 38 MB patch over about 4 s
      const total = 38_000_000;
      const st = (state: string, done: number, file = "patch.pck") =>
        emit({ type: "update_status", status: { state, tag: "v0.4.0-beta", version: "0.4.0-beta", done, total, file, error: "" } } as unknown as Msg);
      st("checking", 0, "");
      let done = 0;
      const t = setInterval(() => {
        done = Math.min(total, done + 4_000_000);
        st(done < total ? "downloading" : "ready", done);
        if (done >= total) clearInterval(t);
      }, 400);
      break;
    }
    case "update_apply":
      emit({ type: "update_status", status: { state: "applying", tag: "v0.4.0-beta", version: "0.4.0-beta", done: 0, total: 0, file: "", error: "" } } as unknown as Msg);
      break;
    case "social":
      socialOp(m);
      break;
    case "replays":
    case "replay_rename":
    case "replay_favorite":
    case "replay_delete":
      replayOp(m);
      break;
    case "spectate":
      notice(m.on ? "Browser preview: Godot would make you a spectator here." : "Browser preview: Godot would put you back in the match here.");
      break;
    case "replay_watch":
      notice("Browser preview: Godot would play the replay here.");
      break;
    case "replay_folder":
      notice("Browser preview: Godot would open the replays folder here.");
      break;
    case "maps_folder":
      notice("Browser preview: Godot would open the Maps folder here.");
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

// a pretend level board: the mock friends plus strangers, highest XP first
function mockBoard(board: string, scope: string) {
  const names = scope === "friends"
    ? [["^4Visor", "visor"], ["Klesk", "klesk"], ["^1Ranger", "ranger"], ["^3Anarki", "anarki"], ["Orbb", "orbb"]]
    : [["^1F^7atal", "xaero"], ["^4Visor", "visor"], ["Strafe^3God", "sarge"], ["Klesk", "klesk"], ["^5Railz", "slash"], ["^1Ranger", "ranger"],
      ["Mynx", "mynx"], ["^2Doom", "doom"], ["^3Anarki", "anarki"], ["Orbb", "orbb"], ["Hunter", "hunter"], ["Crash", "crash"]];
  const banners = ["map_q3dm17", "sky_hellsky", "mat_lava", "char_xaero", "map_q3tourney2", "mat_skull_arch", "void"];
  const titles = ["title_xaeros_equal", "title_warlord", "title_gladiator", "title_contender", "title_fragger", "title_initiate"];
  const rows = names.map(([name, model], i) => {
    const xp = Math.round(190000 / (1 + i * 0.55));
    let level = 1, left = xp;
    while (level < 100 && left >= 1000 + 25 * (level - 1)) left -= 1000 + 25 * (level++ - 1);
    return {
      rank: i + 1, id: name === "^1Ranger" ? "me" : "p" + i, name, model, color: "#db2410", xp, level, rating: 0, rated: 0, peak: 0, online: i % 3 !== 2,
      card: { banner: banners[i % banners.length], emblem: "head_" + model, frame: i < 3 ? "frame_gold" : "plain", title: titles[i % titles.length],
        accent: ["accent_gold", "accent_plasma", "accent_red", "accent_bronze"][i % 4], rail: "rail_red", taunt: "taunt_" + model, stats: ["frags", "wins", "kd"], badges: [] },
      stats: { frags: String(9000 - i * 600), wins: String(700 - i * 50), kd: (2.1 - i * 0.1).toFixed(2) },
    };
  });
  const out = board === "ranked" ? [] : rows;
  emit({ type: "leaderboard", board: { board, scope, rows: out, me: out.find((r) => r.id === "me") ?? null, total: out.length } });
}

function socialOp(m: Msg) {
  if (m.op === "leaderboard") return void setTimeout(() => mockBoard(String(m.board), String(m.scope)), 250);
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

// The Replays page: a few made-up matches, kept in memory while the preview runs.
const BOTS = ["Sarge", "Xaero", "^1Klesk", "Anarki", "Crash", "Hunter", "Doom"];
let mockReplays: ReplayInfo[] | null = null;

function makeReplays(): ReplayInfo[] {
  const maps: [string, string, string, string][] = [
    ["q3dm17", "The Longest Yard", "ffa", "Free for All"], ["q3tourney2", "The Proving Grounds", "duel", "Duel"],
    ["q3dm6", "The Camping Grounds", "ffa", "Free for All"], ["q3ctf1", "Dueling Keeps", "ctf", "Capture the Flag"],
    ["q3dm7", "Temple of Retribution", "tdm", "Team Deathmatch"],
  ];
  const now = Math.floor(Date.now() / 1000);
  return maps.map(([map, mapName, mode, modeName], i) => {
    const n = mode === "duel" ? 1 : 3 + (i % 4);
    const players = [{ name: "^3Ozmium", score: 20 - i * 3, local: true, bot: false, model: "sarge", team: mode === "ffa" || mode === "duel" ? 0 : 1 }]
      .concat(BOTS.slice(0, n).map((b, k) => ({ name: b, score: 18 - k * 4 - i, local: false, bot: true, model: b.replace(/\^\d/g, "").toLowerCase(), team: mode === "ffa" || mode === "duel" ? 0 : 1 + (k % 2) })))
      .sort((a, b) => b.score - a.score);
    const place = players.findIndex((p) => p.local) + 1;
    const t = now - i * 7200 - 600;
    return {
      file: `2026-10-23_${String(20 - i).padStart(2, "0")}0000_${map}_${mode}.q3replay`, where: "replays", demo: false, name: i === 1 ? "That rail" : "", favorite: i === 1,
      complete: i !== 4, title: `${modeName.toUpperCase()}  -  ${mapName.toUpperCase()}`, map, map_name: mapName, mode, mode_name: modeName,
      date: new Date(t * 1000).toISOString().slice(0, 19), unix: t, length_ms: i === 4 ? 0 : (6 + i * 2) * 60000 + 13000,
      result: i === 4 ? "" : place === 1 ? "VICTORY" : "DEFEAT", place, ended: i === 4 ? "" : "intermission", player: "^3Ozmium",
      bytes: (6 + i * 2) * 900000, players,
    };
  });
}

function replayOp(m: Msg) {
  if (!mockReplays) mockReplays = makeReplays();
  const file = String(m.file ?? "");
  const r = mockReplays.find((x) => x.file === file);
  if (m.type === "replay_rename" && r) r.name = String(m.name ?? "").slice(0, 64);
  if (m.type === "replay_favorite" && r) r.favorite = !!m.on;
  if (m.type === "replay_delete") mockReplays = mockReplays.filter((x) => x.file !== file);
  setTimeout(() => emit({ type: "replays", list: mockReplays, kept: 50 }), 120);
}
