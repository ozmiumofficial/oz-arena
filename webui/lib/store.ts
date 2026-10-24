// The menu's state: what Godot sent in "init" plus live updates, in one small store that
// components read with useMenu(selector).
"use client";

import { useSyncExternalStore } from "react";
import { on, send, type Msg } from "./bridge";
import type { InitPayload, MatchInfo, ReplayList, Value, SettingEntry } from "./types";
import type { Board, ChatLine, Social } from "./social";

export interface Toast {
  id: number;
  text: string;
  tone: "info" | "warn" | "error";
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
}

let state: MenuState = {
  init: null, values: {}, conflicts: {}, subject: null, capture: null, pad: false, toasts: [], validation: [], lastCode: "",
  social: { status: "connecting", reason: "", state: null, chat: [] }, socialOpen: false, pauseOpen: 0, match: null, boards: {},
  replays: null,
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
  on("replays", (m) => update({ replays: { list: m.list as ReplayList["list"], kept: m.kept as number } }));
  on("party_chat", (m) => update({ social: { ...state.social, chat: [...state.social.chat, m as unknown as ChatLine].slice(-50) } }));
  void send("ready");
}
