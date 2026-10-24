// The edit is data (edit/trailer.json): segments placed in bars of the track, which
// tools/trailer/beats.py measured (edit/beats.json). This turns bars into frames.
import beatsJson from "../../edit/beats.json";
import editJson from "../../edit/trailer.json";
import shotsJson from "../../edit/shots.json";

export const FPS = 60;
export const WIDTH = 1920;
export const HEIGHT = 1080;

export type Beats = {
  audio: string;
  duration: number;
  tempo: number;
  beats: number[];
  bars: number[];
  drop: number;
};

export type Title = {
  style: "card" | "feature";
  text: string;
  second?: string;       // a second headline line at the same size (FREE TO PLAY / REQUIRES QUAKE III ARENA)
  sub?: string;          // the line that explains it (feature) or a small line under (card)
  footnote?: string;     // small print at the bottom of the frame
  stamp?: string;        // COMING SOON
  from?: number;         // bars into the segment the title appears (default 0)
  to?: number;           // bars into the segment it goes (default: the segment's end)
};

/**
 * A piece of footage: `from` seconds in, or for directed action (capture_remaster.py --action) its
 * shot number and `at` seconds from that shot's kill (negative: before it), which edit/shots.json
 * turns into seconds, so the edit survives a refilm.
 */
export type ClipRef = { src: string; from?: number; rate?: number; shot?: number; at?: number };

/** A directed-action shot: its first frame and length in the match's video, the kill's frame in it. */
export type Shot = { frame: number; frames: number; kill: number; weapon: string; camera: string };

export type Segment = {
  id: string;
  from: number;          // bars (0 = the trailer's first frame, fractions allowed)
  to: number | "end";
  kind: "opener" | "compare" | "clip" | "black" | "placeholder" | "logo" | "montage" | "end";
  retail?: ClipRef;
  remaster?: ClipRef;
  clip?: ClipRef;
  clips?: ClipRef[];
  beatsEach?: number;    // montage: beats per cut
  dir?: 1 | -1;          // compare: 1 = the remaster on the right of the line, the line sweeps right to left
  widen?: boolean;       // compare: opens from Quake 3's 4:3 to 16:9 first
  map?: { name: string; code: string };
  label?: string;        // placeholder: what goes here
  note?: string;
  title?: Title;
  blur?: number;         // clip: a soft-focus background (logo smoke)
};

export type Sound = { file: string; at: number; volume?: number };

/** A title over whatever segments run between two bars. */
export type Overlay = { from: number; to: number; title: Title };

export type Edit = {
  music: { file: string; volume: number };
  sounds: Sound[];
  segments: Segment[];
  overlays: Overlay[];
};

export const beats = beatsJson as Beats;
export const edit = editJson as unknown as Edit;
export const shots = shotsJson as Record<string, Shot[]>;

/** Where a clip starts in its video, in seconds. */
export function clipStart(clip: ClipRef): number {
  if (clip.shot === undefined) return clip.from ?? 0;
  const name = clip.src.split("/").pop()!.replace(/\.mp4$/, "");
  const s = shots[name]?.[clip.shot];
  if (!s) throw new Error(`edit: ${clip.src} has no shot ${clip.shot} (npm run media updates edit/shots.json)`);
  return (s.frame + s.kill) / FPS + (clip.at ?? 0);
}

/** Seconds at a bar position: bar 0 is the trailer's start, fractions interpolate. */
export function barTime(bar: number): number {
  const b = beats.bars;
  if (bar <= 0) return 0;
  const i = Math.floor(bar);
  const f = bar - i;
  const avg = (b[b.length - 1] - b[0]) / (b.length - 1);
  const at = (k: number) => (k < b.length ? b[k] : b[b.length - 1] + (k - b.length + 1) * avg);
  return at(i) + f * (at(i + 1) - at(i));
}

export const durationInFrames = Math.round(beats.duration * FPS);

export function barFrame(bar: number | "end"): number {
  return bar === "end" ? durationInFrames : Math.round(barTime(bar) * FPS);
}

/** The frames of the beats from `start` (frames) on: montage cuts. */
export function beatFramesFrom(startFrame: number): number[] {
  return beats.beats.map((t) => Math.round(t * FPS)).filter((f) => f >= startFrame);
}

export const media = (p: string) => `media/${p}`;
