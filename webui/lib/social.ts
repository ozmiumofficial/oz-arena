// Friends and parties: the shapes Godot's Online autoload forwards from the hub (online/src/hub.ts),
// and the requests the menu sends back as {type: "social", op, ...}.
import { send } from "./bridge";

export type OnlineStatus = "online" | "connecting" | "offline" | "disabled";
export type Presence = "online" | "match" | "editor" | "offline";

export interface Person {
  id: string;
  name: string;
  model: string;
  color: string;
}

export interface Friend extends Person {
  online: boolean;
  status: Presence;
  detail: string;
  join: string;
  seen: number;
  since: number;
}

export interface Member extends Friend {
  leader: boolean;
}

export interface Party {
  id: string;
  leader: string;
  max: number;
  members: Member[];
  pending: Person[];
}

export interface PartyInvite {
  party: string;
  at: number;
  expires: number;
  size: number;
  from: Person;
}

export interface SocialState {
  me: Person & { code: string };
  friends: Friend[];
  incoming: (Person & { at: number })[];
  outgoing: (Person & { at: number })[];
  party: Party | null;
  invites: PartyInvite[];
  limits: { friends: number; party: number };
}

export interface ChatLine {
  from: { id: string; name: string };
  text: string;
  at: number;
}

export interface Social {
  status: OnlineStatus;
  reason: string;
  state: SocialState | null;
  chat: ChatLine[];
}

export type SocialOp =
  | "friend_add" | "friend_accept" | "friend_decline" | "friend_cancel" | "friend_remove"
  | "party_invite" | "party_accept" | "party_decline" | "party_leave" | "party_kick" | "party_promote" | "party_chat"
  | "sync" | "retry" | "copy_code";

export function social(op: SocialOp, data: { id?: string; code?: string; party?: string; text?: string } = {}) {
  void send("social", { op, ...data });
}

// Quake 3's name colors: ^0-^7 (q_shared.h ColorIndex wraps any other character to 0-7 too). Black and
// blue are lifted a little: id's pure values vanish on the menu's dark glass.
const Q3_COLORS = ["#5a5a5a", "#ff0000", "#00ff00", "#ffff00", "#4a6bff", "#00ffff", "#ff00ff", "#ffffff"];

export function nameRuns(name: string): { text: string; color: string | null }[] {
  const out: { text: string; color: string | null }[] = [];
  let color: string | null = null;
  let buf = "";
  for (let i = 0; i < name.length; i++) {
    if (name[i] === "^" && i + 1 < name.length && name[i + 1] !== "^") {
      if (buf) out.push({ text: buf, color });
      buf = "";
      color = Q3_COLORS[(name.charCodeAt(i + 1) - 48) & 7];
      i++;
      continue;
    }
    buf += name[i];
  }
  if (buf) out.push({ text: buf, color });
  return out;
}

export function plainName(name: string): string {
  return nameRuns(name).map((r) => r.text).join("");
}

/** "In the menu" / "Duel on q3tourney2" / "Last seen 3 h ago". */
export function statusLine(f: Friend): string {
  if (f.online) return f.detail || (f.status === "match" ? "In a match" : f.status === "editor" ? "Map editor" : "Online");
  return "Last seen " + ago(f.seen);
}

export function ago(ms: number): string {
  const s = Math.max(0, (Date.now() - ms) / 1000);
  if (s < 60) return "just now";
  if (s < 3600) return `${Math.floor(s / 60)} min ago`;
  if (s < 86400) return `${Math.floor(s / 3600)} h ago`;
  const d = Math.floor(s / 86400);
  return d === 1 ? "yesterday" : `${d} days ago`;
}
