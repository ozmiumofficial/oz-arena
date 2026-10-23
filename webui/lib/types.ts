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
}

export interface MenuItemDef {
  id: string;
  label: string;
  hint: string;
  page?: string;
  action?: string;
  when?: string;
}
