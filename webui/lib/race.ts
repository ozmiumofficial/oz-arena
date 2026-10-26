// Race times as players read them: "1:02.345", "12.345".
export function raceTime(ms: number): string {
  const m = Math.floor(ms / 60000);
  const s = Math.floor((ms % 60000) / 1000);
  const t = ms % 1000;
  return m > 0 ? `${m}:${String(s).padStart(2, "0")}.${String(t).padStart(3, "0")}` : `${s}.${String(t).padStart(3, "0")}`;
}

export const RACE_STYLES: Record<string, string> = {
  training: "Training",
  run: "Run",
  accuracy: "Accuracy",
  level: "Level",
};
