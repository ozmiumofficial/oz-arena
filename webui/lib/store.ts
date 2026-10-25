// The menu's state: what Godot sent in "init" plus live updates, in one small store that
// components read with useMenu(selector).
"use client";

import { useSyncExternalStore } from "react";
import { on, send, type Msg } from "./bridge";
import type { DedicatedConfig, DedicatedStatus, InitPayload, MatchInfo, NetJoin, NetMatch, QueueRating, QueueState, ReplayList, Value, SettingEntry } from "./types";
import type { Board, ChatLine, Social } from "./social";

export interface Toast {
  id: number;
  text: string;
  tone: "info" | "warn" | "error";
}

/** A newer release (Godot's Q3UpdateCheck): the menu asks whether to get it. */
export interface UpdateInfo {
  tag: string;
  version: string;
  name: string;
  notes: string;
  url: string;
  size: number;
  published: string;
  beta: boolean;
}

export interface MenuState {
  init: InitPayload | null;
  values: Record<string, Value>;
  conflicts: Record<string, string[]>;
  subject: { name: string; kind: string } | null;
  capture: { key: string; slot: number } | null;
  pad: boolean;
  toasts: Toast[];
  validation: string[];
  lastCode: string;
  social: Social;
  socialOpen: boolean;
  /** pause menu: bumped every time Godot opens it (pages reset to the root) */
  pauseOpen: number;
  match: MatchInfo | null;
  /** "level|global" -> the last board the hub sent */
  boards: Record<string, Board>;
  /** the Replays page's list, null until Godot answers */
  replays: ReplayList | null;
  /** a newer release to offer; null once the player says Later or Skip */
  update: UpdateInfo | null;
  /** the Server Browser: the hub's directory (null until it answers), and the join in progress */
  net: { matches: NetMatch[] | null; region: string; offline: boolean; join: NetJoin | null };
  /** matchmaking: the queue, the ratings per ranked queue (duel, tdm, ctf), and the round trip to the hub */
  queue: { state: QueueState; ratings: Record<string, QueueRating>; season: string; rtt: number };
  /** this PC's dedicated server: its config, what it reports, and whether it starts with Windows (null until Godot answers) */
  dedicated: { config: DedicatedConfig; status: DedicatedStatus; autostart: boolean } | null;
}

let state: MenuState = {
  init: null, values: {}, conflicts: {}, subject: null, capture: null, pad: false, toasts: [], validation: [], lastCode: "",
  social: { status: "connecting", reason: "", state: null, chat: [] }, socialOpen: false, pauseOpen: 0, match: null, boards: {},
  replays: null, update: null, net: { matches: null, region: "", offline: false, join: null },
  queue: { state: { status: "idle" }, ratings: {}, season: "", rtt: 0 }, dedicated: null,
};
const subs = new Set<() => void>();
let toastId = 0;

function update(patch: Partial<MenuState>) {
  state = { ...state, ...patch };
  subs.forEach((f) => f());
}

export function getState() {
  return state;
}

export function useMenu<T>(sel: (s: MenuState) => T): T {
  return useSyncExternalStore(
    (f) => (subs.add(f), () => subs.delete(f)),
    () => sel(state),
    () => sel(state),
  );
}

export function toast(text: string, tone: Toast["tone"] = "info") {
  const id = ++toastId;
  update({ toasts: [...state.toasts, { id, text, tone }].slice(-4) });
  setTimeout(() => update({ toasts: state.toasts.filter((t) => t.id !== id) }), 4200);
}

/** The Server Browser's list: asks the hub (Godot answers with net_matches). */
export function netBrowse() {
  void send("net_browse", {});
}

/** Joins a listed match (or a friend's): the host decides, then the connection opens and Gameplay starts. */
export function netJoin(host: string, password = "") {
  update({ net: { ...state.net, join: { status: "asking", reason: "", host } } });
  void send("net_join", { host, password });
}

export function netCancel() {
  update({ net: { ...state.net, join: null } });
  void send("net_cancel", {});
}

export function setSocialOpen(open: boolean) {
  update({ socialOpen: open });
}

export function entry(key: string): SettingEntry | undefined {
  return state.init?.schema.find((e) => e.key === key);
}

export function setValue(key: string, value: Value) {
  update({ values: { ...state.values, [key]: value } });
  void send("set", { key, value });
}

// Card edits show at once; Godot checks the part is earned and answers with the whole profile.
export function setCard(slot: string, value: string | string[]) {
  const p = state.init?.profile;
  if (!p || !state.init) return;
  const card = { ...p.card, [slot]: value };
  update({ init: { ...state.init, profile: { ...p, card, shown: { ...p.shown, [slot]: value } } } });
  if (Array.isArray(value)) void send("card_showcase", { kind: slot, ids: value });
  else void send("card_set", { slot, id: value });
}

export function resetValue(key: string) {
  void send("reset", { key });
}

export function isDefault(key: string) {
  const e = entry(key);
  return !!e && JSON.stringify(e.default) === JSON.stringify(state.values[key]);
}

function state_ratings(): Record<string, QueueRating> {
  return state.queue.ratings;
}

/** Queues for a match: the playlists (duel, ffa, tdm, ctf), ranked or casual. Godot asks the hub; the queue message answers. */
export function queueJoin(modes: string[], ranked: boolean) {
  void send("queue_join", { modes, ranked });
}

export function queueLeave() {
  void send("queue_leave", {});
}

let wired = false;
export function connect() {
  if (wired) return;
  wired = true;
  on("init", (m: Msg) => {
    const init = m as unknown as InitPayload;
    update({ init, values: { ...init.values }, match: init.match?.title ? init.match : null });
    void send("log", { text: `init received: ${init.schema.length} settings, ${window.innerWidth}x${window.innerHeight} @${window.devicePixelRatio}` });
  });
  // the page's errors end up in Godot's log
  window.addEventListener("error", (e) => void send("log", { text: `error: ${e.message} (${e.filename}:${e.lineno})` }));
  window.addEventListener("unhandledrejection", (e) => void send("log", { text: `unhandled: ${String(e.reason)}` }));
  on("campaign", (m) =>
    state.init && update({ init: { ...state.init, campaign: m.campaign as InitPayload["campaign"], cinematics: (m.cinematics as InitPayload["cinematics"]) ?? state.init.cinematics } }),
  );
  on("changed", (m) => update({ values: { ...state.values, [m.key as string]: m.value as Value } }));
  on("conflicts", (m) => update({ conflicts: m.map as Record<string, string[]> }));
  on("subject", (m) => update({ subject: { name: m.name as string, kind: m.kind as string } }));
  on("capture_started", (m) => update({ capture: { key: m.key as string, slot: m.slot as number } }));
  on("bind_captured", () => update({ capture: null }));
  on("capture_cancelled", () => update({ capture: null }));
  on("input_mode", (m) => update({ pad: !!m.pad }));
  on("validation", (m) => update({ validation: m.errors as string[] }));
  on("start_failed", (m) => {
    update({ validation: m.errors as string[] });
    (m.errors as string[]).forEach((e) => toast(e, "error"));
  });
  on("code", (m) => {
    update({ lastCode: m.code as string });
    toast("Code copied to the clipboard.");
  });
  on("imported", (m) => toast((m.count as number) < 0 ? "That code isn't valid." : `Imported ${m.count} settings.`, (m.count as number) < 0 ? "error" : "info"));
  on("pause", (m) => {
    const match = m.match as MatchInfo | undefined;
    update({ match: match?.title ? { ...match, standings: match.standings ?? [] } : state.match, pauseOpen: state.pauseOpen + 1 });
  });
  on("dev_notice", (m) => toast(m.text as string, "warn"));
  // the player card: Godot answers every card edit with the whole profile
  on("profile", (m) => state.init && update({ init: { ...state.init, profile: m.profile as InitPayload["profile"] } }));
  // friends and parties (Godot's Online autoload, components/Social.tsx)
  // Godot sends an empty object until the hub's first picture arrives
  on("social", (m) => {
    const st = m.state as Social["state"] | Record<string, never> | null;
    update({ social: { status: m.status as Social["status"], reason: String(m.reason ?? ""), state: st && "me" in st ? (st as Social["state"]) : null, chat: (m.chat as ChatLine[]) ?? [] } });
  });
  on("social_notice", (m) => toast(m.text as string, m.tone === "error" ? "error" : m.tone === "warn" ? "warn" : "info"));
  on("leaderboard", (m) => {
    const b = m.board as Board;
    update({ boards: { ...state.boards, [`${b.board}|${b.scope}`]: b } });
  });
  on("queue", (m) => {
    const r = (m.ratings ?? {}) as { queues?: Record<string, QueueRating>; season?: string };
    const state = (m.state ?? { status: "idle" }) as QueueState;
    update({ queue: { state, ratings: r.queues ?? state_ratings(), season: r.season ?? "", rtt: Number(m.rtt ?? 0) } });
    if (state.status === "refused" && state.reason) toast(state.reason, "warn");
    if (state.status === "locked") toast(state.reason ?? "You can't queue yet.", "warn");
  });
  on("dedicated", (m) => update({ dedicated: { config: m.config as DedicatedConfig, status: m.status as DedicatedStatus, autostart: !!m.autostart } }));
  on("net_matches", (m) =>
    update({ net: { ...state.net, matches: (m.matches as NetMatch[]) ?? [], region: String(m.region ?? ""), offline: !!m.offline } }),
  );
  on("net_join_state", (m) => {
    const join = { status: m.status as NetJoin["status"], reason: String(m.reason ?? ""), host: String(m.host ?? "") };
    update({ net: { ...state.net, join: join.status === "failed" || join.status === "open" ? (join.status === "open" ? join : null) : join } });
    if (join.status === "failed") toast(join.reason || "Couldn't join that match.", "error");
  });
  on("update", (m) => update({ update: m.info as UpdateInfo }));
  on("replays", (m) => update({ replays: { list: m.list as ReplayList["list"], kept: m.kept as number } }));
  on("party_chat", (m) => update({ social: { ...state.social, chat: [...state.social.chat, m as unknown as ChatLine].slice(-50) } }));
  void send("ready");
}

/** Later: hide the offer until the next launch. Skip: never offer this version again. */
export function dismissUpdate(skip: boolean) {
  const tag = state.update?.tag;
  if (skip && tag) void send("update_skip", { tag });
  update({ update: null });
}
