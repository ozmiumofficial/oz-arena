// Race times as players read them: "1:02.345", "12.345".
export function raceTime(ms: number): string {
  const m = Math.floor(ms / 60000);
  const s = Math.floor((ms % 60000) / 1000);
  const t = ms % 1000;
  return m > 0 ? `${m}:${String(s).padStart(2, "0")}.${String(t).padStart(3, "0")}` : `${s}.${String(t).padStart(3, "0")}`;
}

/** A Quake 3 name without its ^ color codes ("^3HighWay" -> "HighWay"). */
export function plainName(s: string): string {
  return s.replace(/\^[^^]/g, "").trim();
}

/** Difficulty, 1 (Beginner) to 5 (Expert). Defrag's .defi files don't rate courses, so the known ones
 * are rated here by hand (Ozmium, 2026-10-25: the Race page sorted by difficulty); others get a guess:
 * training courses are Beginner, competition maps (comp, dfwc, cup) Expert, the rest Medium. */
export const RACE_LEVELS = ["", "Beginner", "Easy", "Medium", "Hard", "Expert"];
const RATED: Record<string, number> = {
  race_warmup: 1, race_gaps: 2, race_targets: 2, race_shards: 2,
  jump: 1, st1: 1,
  "bardok-strafe1": 2, "bardok-strafe2": 3, "bardok-strafe3": 4,
  coldrun: 2, momentum: 3, "mvn-fortress": 3, palmslane: 3, cityrocket: 3, "hgb-tofu": 3, "zeel-recapture": 3, "elco-arca": 3,
  "pornstar-goethe": 4, "pornstar-slopin": 4, "killua-pornstar": 4, lovetillua: 4,
  "ameisen-comp2": 5, "dfwc01-2": 5, "dfwc2017-6": 5, "un-dead!021": 5,
};
export function raceDifficulty(map: string, style = "run"): number {
  const m = map.toLowerCase();
  if (RATED[m]) return RATED[m];
  if (style === "training") return 1;
  if (/comp|dfwc|cup/.test(m)) return 5;
  return 3;
}

export const RACE_STYLES: Record<string, string> = {
  training: "Training",
  run: "Run",
  accuracy: "Accuracy",
  level: "Level",
};
