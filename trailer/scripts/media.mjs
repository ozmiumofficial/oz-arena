// Links the trailer's media into public/media (gitignored), where the compositions find them
// with staticFile(): the footage from work/trailer (capture_q3.py, capture_remaster.py), the
// track from work/trailer/music and Quake 3's announcer lines from extracted/. Hard links where
// the drive allows them (no copies of 500 MB of footage), else copies.
// Also copies work/trailer/music/beats.json (tools/trailer/beats.py) to edit/beats.json, and gathers
// the directed action's shot lists (<name>_shots.json from capture_remaster.py --action) into
// edit/shots.json, without the replays' local paths.
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const trailer = path.resolve(here, "..");
const root = path.resolve(trailer, "..");
const media = path.join(trailer, "public", "media");
const work = path.join(root, "work", "trailer");

function put(src, dst) {
  if (!fs.existsSync(src)) {
    console.warn(`media: missing ${path.relative(root, src)}`);
    return false;
  }
  fs.mkdirSync(path.dirname(dst), { recursive: true });
  if (fs.existsSync(dst)) fs.rmSync(dst);
  try {
    fs.linkSync(src, dst);
  } catch {
    fs.copyFileSync(src, dst);
  }
  return true;
}

let n = 0;
for (const side of ["retail", "remaster"]) {
  const dir = path.join(work, side);
  if (!fs.existsSync(dir)) continue;
  for (const f of fs.readdirSync(dir)) {
    if (f.endsWith(".mp4")) n += put(path.join(dir, f), path.join(media, side, f)) ? 1 : 0;
  }
}
const music = fs.existsSync(path.join(work, "music"))
  ? fs.readdirSync(path.join(work, "music")).find((f) => /\.(mp3|wav|ogg|flac|m4a)$/i.test(f))
  : undefined;
if (music) n += put(path.join(work, "music", music), path.join(media, "music" + path.extname(music))) ? 1 : 0;
for (const s of ["fight", "prepare"]) {
  n += put(path.join(root, "extracted", "baseq3", "sound", "feedback", `${s}.wav`), path.join(media, "sound", `${s}.wav`)) ? 1 : 0;
}
const beats = path.join(work, "music", "beats.json");
if (fs.existsSync(beats)) fs.copyFileSync(beats, path.join(trailer, "edit", "beats.json"));
const shots = {};
const remaster = path.join(work, "remaster");
for (const f of fs.existsSync(remaster) ? fs.readdirSync(remaster).sort() : []) {
  if (!f.endsWith("_shots.json")) continue;
  const list = JSON.parse(fs.readFileSync(path.join(remaster, f), "utf8")).shots;
  shots[f.slice(0, -"_shots.json".length)] = list.map(({ frame, frames, kill, weapon, camera }) => ({ frame, frames, kill: kill ?? 182, weapon, camera }));
}
if (Object.keys(shots).length) fs.writeFileSync(path.join(trailer, "edit", "shots.json"), JSON.stringify(shots, null, 1) + "\n");
console.log(`media: ${n} files in public/media${fs.existsSync(beats) ? ", edit/beats.json updated" : ""}, ${Object.keys(shots).length} action matches in edit/shots.json`);
