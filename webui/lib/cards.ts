// Player card helpers shared by the card renderer and the Profile page. The catalog comes from
// Godot (Q3Cards, library/cards/cards.json); unlocking is decided there, the page only draws.
import type { Achievement, CardPart, CardsCatalog, PartKind, ProfileState, Rank } from "./types";

export function art(path: string | undefined) {
  return path ? `/q3/${path}` : "";
}

export function part(cat: CardsCatalog | undefined, kind: PartKind, id: string): CardPart | undefined {
  return cat?.parts[kind]?.find((p) => p.id === id);
}

export function isUnlocked(profile: ProfileState | undefined, kind: PartKind, id: string) {
  return !!profile?.unlocked[kind]?.includes(id);
}

// "emblem:medal_excellent" -> that emblem's art
export function iconArt(cat: CardsCatalog | undefined, icon: string) {
  const [kind, id] = icon.split(":");
  return kind === "emblem" ? art(part(cat, "emblems", id)?.art) : "";
}

export function accentOf(cat: CardsCatalog | undefined, id: string) {
  return part(cat, "accents", id)?.value ?? "#ff2020";
}

// Achievements grouped the way PLAN.md lists them; the catalog has no group field.
const GROUPS: [string, (id: string) => boolean][] = [
  ["Medals", (i) => i.startsWith("medal_") && i !== "medal_collector"],
  ["Weapons", (i) => i.startsWith("weapon_") || i === "arsenal"],
  ["Powerups", (i) => i.startsWith("powerup_") || i === "every_powerup" || i === "quad_god"],
  ["Feats", (i) => ["perfect", "untouchable", "rail_streak", "rail_accuracy", "air_rocket", "telefragger", "trifecta"].includes(i)],
  ["Movement", (i) => i.startsWith("speed_") || i === "rocket_jumper" || i === "plasma_climber"],
  ["Accidents", (i) => ["lava_swimmer", "lava_bath", "cratered", "sank", "telefragged"].includes(i)],
  ["Modes", (i) => ["duelist", "duel_100", "flag_returns", "clutch", "thaws", "instagib"].includes(i)],
  ["Career", (i) => /^(matches|wins|frags)_\d+$/.test(i)],
  ["Campaign", (i) => i.startsWith("campaign_")],
  ["Collections", (i) => ["tourist", "home_turf", "roster", "medal_collector"].includes(i)],
  ["Ranked", (i) => i.startsWith("ranked_")],
];
export const ACH_GROUPS = [...GROUPS.map((g) => g[0]), "Other"];

export function achievementGroup(a: Achievement) {
  return GROUPS.find(([, f]) => f(a.id))?.[0] ?? "Other";
}

export function rankTier(cat: CardsCatalog | undefined, rating: number) {
  const tiers = cat?.ranked_tiers ?? [];
  return [...tiers].reverse().find((t) => rating >= t.min) ?? tiers[0];
}

// Ranked tiers' colors (icons, frames, titles, the Rank Crest banner)
export const TIER_COLORS: Record<string, string> = {
  bronze: "#c27a45", silver: "#c3ccd6", gold: "#ffc93c", platinum: "#4fe0c8", diamond: "#6ab8ff", godlike: "#ff3d22",
};

// A rail swatch: the core color, or the special trails
export function railStyle(p: CardPart | undefined): React.CSSProperties {
  const v = p?.value ?? "#0050ff";
  if (v === "spectrum") return { background: "linear-gradient(90deg,#ff3030,#ffb020,#f0ff30,#30ff60,#30c0ff,#6040ff,#ff30d0)" };
  if (v === "team") return { background: "linear-gradient(90deg,#ff2a2a 0 50%,#2a5aff 50% 100%)" };
  return { background: v };
}

// Big numbers as the card prints them ("12,480")
export function fmt(n: number) {
  return Math.round(n).toLocaleString("en-US");
}

// A rating as the ladder shows it: Q3Cards.rank, for other players' rows (leaderboards)
export function rankOf(cat: CardsCatalog | undefined, rating: number): Rank | undefined {
  const tiers = cat?.ranked_tiers ?? [];
  if (!tiers.length) return undefined;
  const step = cat?.ranked?.division ?? 100;
  let i = 0;
  tiers.forEach((t, n) => rating >= t.min && (i = n));
  const t = tiers[i];
  if (i === tiers.length - 1) return { id: t.id, name: t.name, division: 0, label: t.name, next: -1, from: t.min };
  const top = tiers[i + 1].min;
  const division = Math.min(3, Math.max(1, Math.ceil((top - rating) / step)));
  const next = top - (division - 1) * step;
  return { id: t.id, name: t.name, division, label: `${t.name} ${["I", "II", "III"][division - 1]}`, next, from: division < 3 ? Math.max(t.min, next - step) : t.min };
}
