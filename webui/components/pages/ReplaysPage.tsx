"use client";
// Replays (Play, Replays): every match recorded in user://replays, newest first. Godot keeps the
// list (menu_bridge.gd replays_payload); this page asks for it, and watching one loads the Gameplay
// scene with the replay instead of a match (REPLAY_PLAN.md R4).
// Each replay: levelshot, map, mode, date, length, players and your result. Watch, rename,
// favorite (favorites are never auto-cleaned), delete, show the folder. Q/E switch All / Favorites.
import { useEffect, useMemo, useState } from "react";
import { motion } from "motion/react";
import { toast, useMenu } from "@/lib/store";
import { send } from "@/lib/bridge";
import { sfx } from "@/lib/sound";
import { cn } from "@/lib/cn";
import { shotSrc } from "@/lib/shot";
import type { ReplayInfo } from "@/lib/types";
import PagePanel, { Badge, EmptyState, SectionTitle } from "../PagePanel";
import Button from "../controls/Button";
import TextField from "../controls/TextField";
import { Q3Name } from "../Social";
import type { PageProps } from "./index";

const FILTERS: ["all" | "favorites", string][] = [["all", "All"], ["favorites", "Favorites"]];

function clock(ms: number) {
  const s = Math.round(ms / 1000);
  return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, "0")}`;
}

function when(unix: number) {
  if (!unix) return "";
  const d = new Date(unix * 1000);
  return d.toLocaleDateString(undefined, { month: "short", day: "numeric" }) + "  " + d.toLocaleTimeString(undefined, { hour: "2-digit", minute: "2-digit" });
}

function size(bytes: number) {
  return bytes >= 1048576 ? `${(bytes / 1048576).toFixed(1)} MB` : `${Math.max(1, Math.round(bytes / 1024))} KB`;
}

function resultTone(r: ReplayInfo): "amber" | "red" | "dim" {
  return r.result === "VICTORY" ? "amber" : r.result === "DEFEAT" ? "red" : "dim";
}

export default function ReplaysPage({ back }: PageProps) {
  const data = useMenu((s) => s.replays);
  const maps = useMenu((s) => s.init?.maps ?? []);
  const [filter, setFilter] = useState<"all" | "favorites">("all");
  const [pick, setPick] = useState<string | null>(null);
  const [confirm, setConfirm] = useState<string | null>(null);

  useEffect(() => void send("replays"), []);

  const list = useMemo(() => (data?.list ?? []).filter((r) => filter === "all" || r.favorite), [data, filter]);
  const shown = list.find((r) => r.where + r.file === pick) ?? list[0] ?? null;
  const shot = (map: string) => shotSrc(maps.find((m) => m.map === map)?.levelshot ?? "");
  const switchFilter = (d: number) => {
    const i = FILTERS.findIndex((f) => f[0] === filter);
    sfx.move();
    setFilter(FILTERS[(i + d + FILTERS.length) % FILTERS.length][0]);
  };
  const watch = (r: ReplayInfo) => (r.playable === false ? sfx.buzz() : void send("replay_watch", { file: r.file, where: r.where, map: r.map }));

  return (
    <PagePanel
      title="Replays"
      subtitle={`Every match you play is recorded whole: every player, watchable from anyone's eyes or a free camera. The last ${data?.kept ?? 50} are kept; favorites stay forever.`}
      back={back}
      onTab={switchFilter}
    >
      <div className="flex h-full flex-col">
        <div className="flex items-center gap-1 px-10 pt-3">
          {FILTERS.map(([id, label]) => (
            <button
              key={id}
              onClick={() => id !== filter && (sfx.move(), setFilter(id))}
              className={cn("relative px-3 py-2 font-q3 text-[0.8rem] tracking-[0.12em] transition-colors", id === filter ? "text-ink-hot" : "text-dim hover:text-ink")}
            >
              {label}
              {id === filter && <motion.span layoutId="replay-tab" className="absolute inset-x-2 -bottom-px h-[2px] bg-glow shadow-[0_0_0.6rem_rgb(255_74_28)]" />}
            </button>
          ))}
          <div data-row className="ml-auto flex items-center gap-1.5">
            <Button small onClick={() => send("replay_folder")}>Show folder</Button>
            <kbd className="ml-2 rounded-sm border border-white/15 px-1.5 py-0.5 font-q3 text-[0.6rem] text-dim">Q</kbd>
            <kbd className="rounded-sm border border-white/15 px-1.5 py-0.5 font-q3 text-[0.6rem] text-dim">E</kbd>
          </div>
        </div>
        <div className="mx-10 h-px bg-white/[0.06]" />
        {!data ? (
          <div className="grid flex-1 place-items-center text-dim">Loading...</div>
        ) : list.length === 0 ? (
          <div className="grid flex-1 place-items-center px-10">
            <EmptyState
              title={filter === "favorites" ? "No favorites yet" : "No replays yet"}
              text={filter === "favorites" ? "Mark a replay as a favorite to keep it forever." : "Play a match: it's recorded as it goes (Settings, Gameplay, Record replays)."}
            />
          </div>
        ) : (
          <div className="grid min-h-0 flex-1 grid-cols-[1fr_30rem] gap-8 px-10 py-4">
            <div data-grid="1" className="min-h-0 overflow-y-auto pr-2">
              <div className="flex flex-col gap-1.5">
                {list.map((r, i) => (
                  <motion.button
                    key={r.where + r.file}
                    data-nav
                    initial={{ opacity: 0, x: 12 }}
                    animate={{ opacity: 1, x: 0 }}
                    transition={{ delay: Math.min(i, 12) * 0.03 }}
                    onFocus={() => setPick(r.where + r.file)}
                    onClick={() => (shown === r ? watch(r) : (sfx.move(), setPick(r.where + r.file)))}
                    onDoubleClick={() => watch(r)}
                    className={cn(
                      "group grid grid-cols-[7.5rem_1fr_auto] items-center gap-4 rounded-sm border p-1.5 text-left transition-colors",
                      shown === r ? "border-glow/60 bg-red/10" : "border-white/[0.06] bg-black/20 focus:border-glow/60",
                    )}
                  >
                    {/* eslint-disable-next-line @next/next/no-img-element */}
                    <img src={shot(r.map)} alt="" className="aspect-[4/3] w-full rounded-[2px] object-cover" />
                    <div className="min-w-0">
                      <div className="flex items-center gap-2">
                        {r.favorite && <span className="text-amber">&#9733;</span>}
                        <span className="truncate font-q3 text-[1rem] tracking-[0.04em] text-ink-hot">{r.name || r.map_name}</span>
                      </div>
                      <div className="mt-0.5 truncate text-[0.85rem] text-dim">
                        {r.name ? `${r.map_name}, ` : ""}{r.mode_name}  -  {r.players.length} {r.players.length === 1 ? "player" : "players"}
                      </div>
                      <div className="mt-0.5 font-mono text-[0.72rem] text-faint">{when(r.unix)}</div>
                    </div>
                    <div className="flex flex-col items-end gap-1.5 pr-2">
                      {r.demo ? <Badge tone="dim">Q3 DEMO</Badge> : r.result ? <Badge tone={resultTone(r)}>{r.result}</Badge> : <Badge tone="dim">{r.complete ? r.ended.toUpperCase() || "ENDED" : "UNFINISHED"}</Badge>}
                      <span className="font-mono text-[0.8rem] text-dim">{r.length_ms ? clock(r.length_ms) : "--:--"}</span>
                    </div>
                  </motion.button>
                ))}
              </div>
            </div>
            {shown && <Detail key={shown.where + shown.file} r={shown} shot={shot(shown.map)} confirm={confirm === shown.where + shown.file} setConfirm={(on) => setConfirm(on ? shown.where + shown.file : null)} watch={() => watch(shown)} />}
          </div>
        )}
      </div>
    </PagePanel>
  );
}

function Detail({ r, shot, confirm, setConfirm, watch }: { r: ReplayInfo; shot: string; confirm: boolean; setConfirm: (on: boolean) => void; watch: () => void }) {
  return (
    <motion.div initial={{ opacity: 0 }} animate={{ opacity: 1 }} className="flex min-h-0 flex-col overflow-y-auto">
      <div className="relative aspect-[16/9] overflow-hidden rounded-sm border border-white/10">
        {/* eslint-disable-next-line @next/next/no-img-element */}
        <img src={shot} alt="" className="absolute inset-0 h-full w-full object-cover" />
        <div className="absolute inset-x-0 bottom-0 bg-gradient-to-t from-black/90 to-transparent px-4 pb-3 pt-10">
          <div className="font-q3 text-[1.25rem] tracking-[0.05em] text-ink-hot">{r.map_name}</div>
          <div className="text-[0.85rem] text-dim">{r.mode_name}  -  {when(r.unix)}</div>
        </div>
      </div>
      <div data-row className="mt-4 flex flex-wrap gap-2">
        <Button primary autoFocus onClick={watch} refuse={r.playable === false ? `${r.map} isn't one of the remaster's maps` : undefined} onRefuse={(why) => toast(why, "error")}>Watch</Button>
        <Button onClick={() => send("replay_favorite", { file: r.file, where: r.where, on: !r.favorite })}>{r.favorite ? "Unfavorite" : "Favorite"}</Button>
        {r.where !== "replays" ? null : confirm ? (
          <>
            <Button onClick={() => (setConfirm(false), send("replay_delete", { file: r.file, where: r.where }))}>Delete it</Button>
            <Button onClick={() => setConfirm(false)}>Keep</Button>
          </>
        ) : (
          <Button onClick={() => setConfirm(true)}>Delete</Button>
        )}
      </div>
      <SectionTitle className="mt-6">Name</SectionTitle>
      <TextField wide value={r.name} max={64} placeholder={r.map_name} onCommit={(v) => send("replay_rename", { file: r.file, where: r.where, name: v })} />
      <SectionTitle className="mt-6">Match</SectionTitle>
      <div className="grid grid-cols-3 gap-2 font-mono text-[0.85rem]">
        <Stat label="Length" value={r.length_ms ? clock(r.length_ms) : "--:--"} />
        <Stat label="Result" value={r.result ? `${r.result}${r.place ? ` (#${r.place})` : ""}` : r.demo ? "Q3 demo" : r.complete ? "Left early" : "Unfinished"} />
        <Stat label="File" value={size(r.bytes)} />
      </div>
      {r.players.length > 0 && (
        <>
          <SectionTitle className="mt-6">Players</SectionTitle>
          <div className="flex flex-col gap-1">
            {r.players.map((p, i) => (
              <div key={i} className={cn("flex items-center justify-between rounded-sm px-3 py-1.5", p.local ? "bg-red/15" : "bg-white/[0.03]")}>
                <span className="flex items-center gap-3">
                  <span className="w-5 font-mono text-[0.8rem] text-faint">{i + 1}</span>
                  <Q3Name name={p.name} className="font-q3 text-[0.95rem] tracking-[0.04em]" />
                  {p.bot && <span className="font-q3 text-[0.55rem] tracking-[0.18em] text-faint">BOT</span>}
                </span>
                <span className="font-mono text-[0.95rem] text-ink">{p.score}</span>
              </div>
            ))}
          </div>
        </>
      )}
      {!r.complete && <p className="mt-4 text-[0.85rem] text-dim">The game closed during this match; it plays up to where the recording stopped.</p>}
      {r.demo && <p className="mt-4 text-[0.85rem] text-dim">A classic demo{r.player ? <> recorded by <Q3Name name={r.player} /></> : null}: one player&apos;s view, so a free camera sees only what they saw.{r.where === "quake3" ? " It's in the original game's demos folder." : ""}</p>}
    </motion.div>
  );
}

function Stat({ label, value }: { label: string; value: string }) {
  return (
    <div className="rounded-sm border border-white/[0.06] bg-black/20 px-3 py-2">
      <div className="font-q3 text-[0.55rem] tracking-[0.2em] text-faint">{label.toUpperCase()}</div>
      <div className="mt-1 text-ink">{value}</div>
    </div>
  );
}
