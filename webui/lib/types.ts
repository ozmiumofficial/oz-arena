// Shapes of the data Godot sends in its "init" message (scenes/main_menu/menu_bridge.gd).

export type Value = string | number | boolean | string[];
export type SettingType = "bool" | "int" | "float" | "enum" | "color" | "string" | "bind";

export interface SettingEntry {
  key: string;
  tab: string;
  group: string;
  label: string;
  type: SettingType;
  default: Value;
  tip: string;
  wired: boolean;
  restart: boolean;
  min?: number;
  max?: number;
  step?: number;
  unit?: string;
  options?: [Value, string][];
}

export interface ModeInfo {
  id: string;
  name: string;
  description: string;
  team: boolean;
  rounds: boolean;
  map_types: string[];
  rules: Record<string, number>;
  modifiers: Record<string, Value>;
}

export interface RuleSpec {
  label: string;
  min: number;
  max: number;
  unit: string;
  tip: string;
}

export interface Modifier {
  id: string;
  label: string;
  default: Value;
  tip: string;
  options: [Value, string][];
}

export interface MapInfo {
  map: string;
  name: string;
  types: string[];
  bots: string[];
  fraglimit: number;
  timelimit: number;
  levelshot: string;
}

export interface BotInfo {
  name: string;
  model: string;
  skin: string;
}

export interface BannerFont {
  file: string;
  size: number;
  ascent: number;
  descent: number;
  margin: number;
  gap: number;
  space: number;
  scale: number;
  glyphs: Record<string, [number, number, number, number]>;
}

export interface CampaignArena {
  map: string;
  name: string;
  levelshot: string;
  fraglimit: number;
  bots: { name: string; model: string }[];
  won: number;
}

export interface CampaignInfo {
  tiers: { name: string; open: boolean; complete: boolean; arenas: CampaignArena[] }[];
  skill: number;
  awards: Record<string, number>;
  award_names: string[];
}

export interface CinematicInfo {
  id: string;
  title: string;
  unlock: string;
  seconds: number;
  thumb: string;
  unlocked: boolean;
}

export interface InitPayload {
  version: string;
  tabs: [string, string][];
  schema: SettingEntry[];
  values: Record<string, Value>;
  modes: ModeInfo[];
  rules: Record<string, RuleSpec>;
  modifiers: Modifier[];
  maps: MapInfo[];
  bots: BotInfo[];
  skills: string[];
  characters: string[];
  /** each character's skins after default: red, blue and its own (Daemia is major's "daemia") */
  skins?: Record<string, string[]>;
  banner: BannerFont;
  crosshairs: string[];
  continue: string;
  m_yaw: number;
  campaign: CampaignInfo;
  cinematics: CinematicInfo[];
  /** "pause" when this page is the in-match pause menu (Gameplay), else the main menu */
  context?: "menu" | "pause";
  /** the player's level, and the level ranked opens at */
  progress?: { level: number; ranked_level: number };
  match?: MatchInfo;
  /** the player card catalog (library/cards/cards.json) and the player's career */
  cards?: CardsCatalog;
  profile?: ProfileState;
}

/** Card slots holding one part, and the catalog list each draws from (Q3Cards.SLOTS). */
export const CARD_SLOTS = { banner: "banners", emblem: "emblems", frame: "frames", title: "titles", accent: "accents", rail: "rails", taunt: "taunts" } as const;
export type CardSlot = keyof typeof CARD_SLOTS;
export type PartKind = (typeof CARD_SLOTS)[CardSlot];

export interface CardPart {
  id: string;
  name: string;
  /** "FREE", "LEVEL 40", "ACHIEVEMENT: RAIL GOD" or the part's own text */
  how: string;
  free: boolean;
  /** the level that unlocks it, 0 when it isn't on the level track */
  level: number;
  /** the achievement that pays it, if that's its rule */
  ach?: string;
  kind?: string;
  group?: string;
  art?: string;
  value?: string;
  fx?: string;
  base?: string;
  style?: string;
  desc?: string;
  sound?: string;
}

export interface Achievement {
  id: string;
  name: string;
  desc: string;
  medal: "bronze" | "silver" | "gold";
  target: number;
  icon: string;
  hidden: boolean;
}

export interface CardData {
  banner: string;
  emblem: string;
  frame: string;
  title: string;
  accent: string;
  rail: string;
  taunt: string;
  stats: string[];
  badges: string[];
}

export interface CardsCatalog {
  parts: Record<PartKind, CardPart[]>;
  achievements: Achievement[];
  card_stats: { id: string; label: string; value: string }[];
  medal_colors: Record<string, string>;
  ranked_tiers: { id: string; name: string; min: number }[];
  ranked?: { division: number; placements: number; start: number };
  default_card: CardData;
}

export interface ProfileState {
  level: number;
  xp: number;
  next: number;
  total: number;
  max_level: number;
  /** the card as saved, and as others see it (unearned parts back to the default) */
  card: CardData;
  shown: CardData;
  /** showcase stat id -> the value as the card prints it */
  stats: Record<string, string>;
  unlocked: Partial<Record<PartKind, string[]>>;
  achievements: { id: string; progress: number; done: boolean; missing: string[] }[];
  /** "kind:id" of parts unlocked since the Profile page last showed them */
  new: string[];
  ranked_level: number;
  ranked?: RankedState;
  career: {
    matches: number;
    wins: number;
    frags: number;
    deaths: number;
    playtime: number;
    frags_weapon: Record<string, number>;
    medal: Record<string, number>;
    wins_mode: Record<string, number>;
    matches_model: Record<string, number>;
  };
  banner_ctx: { favorite_map: string; best_weapon: string; medals: { id: string; n: number }[]; campaign_won: number; ranked_peak: number };
}

/** The match behind the pause menu (Gameplay sends it with every "pause"). */
export interface MatchInfo {
  title: string;
  map: string;
  map_name: string;
  clock: string;
  warmup: boolean;
  ready_up: boolean;
  ready: boolean;
  can_restart: boolean;
  weapon: number;
  standings: { name: string; score: number; local: boolean }[];
  /** you're a spectator (id's team spectator): the menu offers Join match, else Spectate */
  spectating?: boolean;
  /** Spectate is offered: not in the campaign, and in a duel only with someone waiting */
  can_spectate?: boolean;
}

export interface BotSlot {
  name: string;
  skill: number;
  team: "" | "red" | "blue";
}

export interface MatchSetup {
  mode: string;
  map: string;
  rules: Record<string, number>;
  modifiers: Record<string, Value>;
  bots: BotSlot[];
  server_type: string;
  source: string;
  training?: string;
  /** join as a spectator (Create Match, You) */
  spectate?: boolean;
  /** a listen server's listing (server_type "listen"): shown in other players' Server Browser */
  online?: { name: string; password: string; ranked: boolean };
}

/** A match in the hub's directory (online/src/hub.ts net_matches). */
export interface NetMatch {
  host: string;
  owner: string;
  region: string;
  name: string;
  mode: string;
  map: string;
  players: number;
  max: number;
  password: boolean;
  ranked: boolean;
  version: string;
  /** a dedicated server (always on) rather than a player's match */
  dedicated?: boolean;
}

/** A dedicated server's config and what it reports (the dedicated message; Q3Dedicated / Q3DedicatedLauncher). */
export interface DedicatedConfig {
  name: string;
  password: string;
  mode: string;
  rotation: string[];
  max_players: number;
  bots: number;
  bot_skill: number;
  motd: string;
  rcon_password: string;
  online: boolean;
  idle_sleep: boolean;
}

export interface DedicatedStatus {
  running: boolean;
  name?: string;
  map?: string;
  mode?: string;
  max?: number;
  humans?: number;
  sleeping?: boolean;
  listed?: boolean;
  uptime?: number;
  error?: string;
  players?: { client: number; name: string; bot: boolean }[];
}

/** The matchmaking queue as Godot sends it (queue message): the hub's queue_state, and the ratings it keeps. */
export interface QueueState {
  status: "queued" | "found" | "idle" | "locked" | "refused" | string;
  since?: number;
  modes?: string[];
  ranked?: boolean;
  reason?: string;
  until?: number;
}

export interface QueueRating {
  rating: number;
  rated: number;
  wins: number;
  losses: number;
  peak: number;
}

export interface NetJoin {
  status: "asking" | "connecting" | "failed" | "open";
  reason: string;
  host: string;
}

export interface MenuItemDef {
  id: string;
  label: string;
  hint: string;
  page?: string;
  action?: string;
  when?: string;
}

/** A rating as the ladder shows it (Q3Cards.rank): division 3, 2, 1 (III, II, I), 0 for Godlike */
export interface Rank {
  id: string;
  name: string;
  division: number;
  label: string;
  /** the rating the next division starts at, -1 at the top */
  next: number;
  from: number;
}

export interface RankedState {
  open: boolean;
  matches: number;
  placements: number;
  placed: boolean;
  rating: number;
  peak: number;
  rank: Rank;
  peak_rank: Rank | Record<string, never>;
  wins: number;
}

/** One replay in user://replays (Godot's replays_payload, menu_bridge.gd). */
export interface ReplayInfo {
  file: string;
  /** "replays" (the game's folder) or "quake3" (the install's baseq3/demos) */
  where: string;
  /** a Quake 3 demo (.dm_68): one player's view */
  demo: boolean;
  /** its map is one the remaster can draw (demos may be on maps it doesn't have) */
  playable?: boolean;
  /** your rename, "" if none */
  name: string;
  favorite: boolean;
  /** has its trailer: false when the game closed mid-match */
  complete: boolean;
  title: string;
  map: string;
  map_name: string;
  mode: string;
  mode_name: string;
  date: string;
  unix: number;
  length_ms: number;
  /** VICTORY / DEFEAT / DRAW, "" if the match didn't reach its end */
  result: string;
  place: number;
  /** intermission, left, quit */
  ended: string;
  player: string;
  bytes: number;
  players: { name: string; score: number; local: boolean; bot: boolean; model: string; team: number }[];
}

export interface ReplayList {
  list: ReplayInfo[];
  /** Gameplay > Replays kept */
  kept: number;
}
