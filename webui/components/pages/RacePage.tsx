"use client";
// Race (RACE_PLAN.md D3): Defrag's timed courses. The maps are the Maps folder's pk3s that have a .defi
// file (style, author), plus any published from the editor with race settings; your best time on each
// is kept by Godot (Q3RaceRecords). The movement is id 1.32's (Classic), set by the mode itself.
// Courses are grouped by difficulty, easiest first (raceDifficulty). Click one to see it, Run (or a
// double-click) plays it; Add to playlist builds a list that Run playlist plays back to back, the next
// one starting after each finish (Session.race_queue, the race_queue message).
import { useEffect, useMemo, useState } from "react";
import { motion } from "motion/react";
import { useMenu, toast } from "@/lib/store";
import { send } from "@/lib/bridge";
import { sfx } from "@/lib/sound";
import { cn } from "@/lib/cn";
import { shotSrc } from "@/lib/shot";
import { raceTime, RACE_STYLES, RACE_LEVELS, raceDifficulty, plainName } from "@/lib/race";
import type { MapInfo } from "@/lib/types";
import PagePanel, { EmptyState, SectionTitle } from "../PagePanel";
import Button from "../controls/Button";
import type { PageProps } from "./index";

const styleOf = (m: MapInfo) => m.race?.style ?? "run";
const levelOf = (m: MapInfo) => raceDifficulty(m.map, styleOf(m));

function Level({ level, className }: { level: number; className?: string }) {
  return (
    <span className={cn("inline-flex items-center gap-1", className)} title={RACE_LEVELS[level]}>
      {[1, 2, 3, 4, 5].map((i) => (
        <span key={i} className={cn("h-1.5 w-3 rounded-full", i <= level ? "bg-amber" : "bg-white/15")} />
      ))}
    </span>
  );
}

export default function RacePage({ back }: PageProps) {
  const init = useMenu((s) => s.init);
  const records = init?.race_records ?? {};
  const board = init?.race_board ?? {};
  const maps = useMemo(
    () =>
      (init?.maps ?? [])
        .filter((m) => m.types.includes("race"))
        .sort((a, b) => levelOf(a) - levelOf(b) || plainName(a.name).localeCompare(plainName(b.name))),
    [init],
  );
  const [map, setMap] = useState("");
  const [query, setQuery] = useState("");
  const [style, setStyle] = useState("all");
  const [picked, setPicked] = useState<string[]>(() => {
    try {
      const v = JSON.parse(localStorage.getItem("race_playlist") ?? "[]");
      return Array.isArray(v) ? v.filter((x) => typeof x === "string") : [];
    } catch {
      return [];
    }
  });
  const setPlaylist = (next: string[]) => {
    setPicked(next);
    try {
      localStorage.setItem("race_playlist", JSON.stringify(next));
    } catch {}
  };
  useEffect(() => {
    if (!map && maps.length) setMap(maps[0].map);
  }, [maps, map]);
  const styles = useMemo(() => [...new Set(maps.map(styleOf))], [maps]);
  const q = query.trim().toLowerCase();
  const shown = maps.filter(
    (m) =>
      (style === "all" || styleOf(m) === style) &&
      (!q || plainName(m.name).toLowerCase().includes(q) || m.map.toLowerCase().includes(q) || plainName(m.race?.author ?? "").toLowerCase().includes(q)),
  );
  const groups = [1, 2, 3, 4, 5].map((lv) => ({ lv, list: shown.filter((m) => levelOf(m) === lv) })).filter((g) => g.list.length);
  const chosen = maps.find((m) => m.map === map);
  const have = picked.filter((p) => maps.some((m) => m.map === p)); // a course taken out of the folder drops off
  const name = (map: string) => plainName(maps.find((m) => m.map === map)?.name ?? map);
  const run = (m: MapInfo, rest: string[] = []) => {
    void send("race_queue", { maps: rest });
    void send("start", { setup: { mode: "race", map: m.map, rules: {}, modifiers: {}, bots: [], server_type: "offline", source: "menu" } });
  };
  const runPlaylist = () => {
    const first = maps.find((m) => m.map === have[0]);
    if (first) run(first, have.slice(1));
  };
  const inList = chosen ? have.includes(chosen.map) : false;

  return (
    <PagePanel title="Race" subtitle="Timed courses with the classic movement: no health to lose, hold jump to keep hopping, R to restart." back={back}>
      {maps.length === 0 ? (
        <div className="flex h-full flex-col items-center justify-center gap-5 px-10">
          <EmptyState
            title="No race maps yet"
            text="Race plays Defrag courses. Put a course's .pk3 into the game's Maps folder, with the .defi file that names it (most course downloads include one), and it appears here with your best time."
          />
          <Button primary onClick={() => void send("maps_folder")}>Open the Maps folder</Button>
        </div>
      ) : (
        <div className="flex h-full gap-6 px-10 py-6">
          <div className="flex min-h-0 min-w-0 flex-1 flex-col">
            <div className="mb-3 flex items-center gap-3">
              <input
                data-nav
                value={query}
                placeholder={`Search ${maps.length} courses`}
                spellCheck={false}
                onChange={(e) => setQuery(e.target.value)}
                className="h-10 w-[16rem] rounded-sm border border-white/10 bg-black/50 px-3 text-[1rem] text-ink-hot placeholder:text-faint focus:border-glow/70 focus:bg-black/70"
              />
              {styles.length > 1 && (
                <div data-row className="flex gap-2">
                  {["all", ...styles].map((s) => (
                    <button
                      key={s}
                      data-nav
                      onClick={() => {
                        sfx.move();
                        setStyle(s);
                      }}
                      className={cn(
                        "h-10 rounded-sm border px-3 font-q3 text-[0.8rem] tracking-[0.12em] transition-colors",
                        style === s ? "border-glow/80 bg-red/20 text-ink-hot" : "border-white/10 bg-black/40 text-dim focus:border-ink/60 focus:text-ink",
                      )}
                    >
                      {s === "all" ? "All" : RACE_STYLES[s] ?? s}
                    </button>
                  ))}
                </div>
              )}
            </div>
            <div className="min-h-0 flex-1 overflow-y-auto pr-2">
              {groups.map((g) => (
                <div key={g.lv} className="mb-5">
                  <div className="mb-2 flex items-center gap-3 font-q3 text-[0.85rem] tracking-[0.2em] text-amber">
                    {RACE_LEVELS[g.lv].toUpperCase()}
                    <Level level={g.lv} />
                    <span className="text-dim">{g.list.length}</span>
                  </div>
                  <div data-grid="3" className="grid grid-cols-3 gap-3">
                    {g.list.map((m, i) => (
                      <motion.button
                        key={m.map}
                        data-nav
                        data-autofocus={m.map === map ? "" : undefined}
                        initial={{ opacity: 0, y: 10 }}
                        animate={{ opacity: 1, y: 0 }}
                        transition={{ delay: Math.min(i, 12) * 0.03 }}
                        onClick={() => {
                          sfx.move();
                          setMap(m.map);
                        }}
                        onDoubleClick={() => run(m)}
                        className={cn(
                          "group relative aspect-[4/3] overflow-hidden rounded-sm border-2 text-left transition-all",
                          m.map === map ? "border-glow shadow-[0_0_1.4rem_rgb(219_36_16/0.5)]" : "border-transparent focus:border-ink/60",
                        )}
                      >
                        {m.levelshot ? (
                          // eslint-disable-next-line @next/next/no-img-element
                          <img src={shotSrc(m.levelshot)} alt="" loading="lazy" className="absolute inset-0 h-full w-full object-cover transition-transform duration-500 group-focus:scale-110" />
                        ) : (
                          <div className="absolute inset-0 bg-gradient-to-br from-white/5 to-black/60" />
                        )}
                        <div className="absolute inset-0 bg-gradient-to-t from-black/90 via-black/20 to-transparent" />
                        <div className="absolute left-2 top-2 rounded-sm bg-black/70 px-1.5 py-0.5 font-q3 text-[0.55rem] tracking-[0.18em] text-amber">
                          {(RACE_STYLES[styleOf(m)] ?? styleOf(m)).toUpperCase()}
                        </div>
                        {records[m.map] ? (
                          <div className="absolute right-2 top-2 rounded-sm bg-black/70 px-1.5 py-0.5 font-q3 text-[0.65rem] tracking-[0.1em] text-amber">{raceTime(records[m.map])}</div>
                        ) : null}
                        {have.includes(m.map) && (
                          <div className="absolute bottom-2 right-2 grid h-7 min-w-7 place-items-center rounded-full bg-amber px-1.5 font-q3 text-[0.8rem] text-black" title="Its place in the playlist">
                            {have.indexOf(m.map) + 1}
                          </div>
                        )}
                        <div className="absolute bottom-2 left-3 right-10">
                          <div className="line-clamp-2 font-q3 text-[0.9rem] leading-tight tracking-[0.04em] text-ink-hot">{plainName(m.name)}</div>
                          <div className="truncate font-q3 text-[0.6rem] tracking-[0.2em] text-dim">{plainName(m.race?.author ?? "") || m.map}</div>
                        </div>
                      </motion.button>
                    ))}
                  </div>
                </div>
              ))}
              {shown.length === 0 && <div className="py-10 text-center text-[1rem] text-dim">No course matches.</div>}
            </div>
          </div>
          <div className="flex w-[21rem] shrink-0 flex-col gap-3">
            <SectionTitle>Course</SectionTitle>
            {chosen && (
              <>
                <div className="font-q3 text-[1.5rem] leading-tight tracking-[0.05em] text-ink-hot">{plainName(chosen.name)}</div>
                <div className="flex items-center gap-3 text-[0.95rem] text-dim">
                  <Level level={levelOf(chosen)} />
                  <span>{RACE_LEVELS[levelOf(chosen)]}</span>
                  <span>· {RACE_STYLES[styleOf(chosen)] ?? styleOf(chosen)}</span>
                </div>
                {chosen.race?.author ? <div className="text-[0.9rem] text-faint">by {plainName(chosen.race.author)}</div> : null}
                <div className="mt-1 rounded-sm border border-white/10 bg-black/30 p-4">
                  <div className="font-q3 text-[0.7rem] tracking-[0.2em] text-dim">YOUR BEST</div>
                  <div className="font-q3 text-[2.4rem] leading-none text-amber">{records[chosen.map] ? raceTime(records[chosen.map]) : "—"}</div>
                  <p className="mt-2 text-[0.85rem] text-faint">{records[chosen.map] ? "Your best run races beside you as a ghost." : "Finish it once to set a time and a ghost to beat."}</p>
                </div>
                {(board[chosen.map]?.length ?? 0) > 1 && (
                  <div className="rounded-sm border border-white/10 bg-black/30 p-3">
                    <div className="font-q3 text-[0.7rem] tracking-[0.2em] text-dim">YOUR FASTEST</div>
                    {board[chosen.map].slice(0, 3).map((r, i) => (
                      <div key={i} className="flex justify-between font-mono text-[0.9rem] text-ink"><span>{i + 1}</span><span>{raceTime(r.ms)}</span></div>
                    ))}
                  </div>
                )}
                <Button primary onClick={() => run(chosen)} className="py-4 text-[1.3rem]">Run</Button>
                <Button small onClick={() => setPlaylist(inList ? have.filter((p) => p !== chosen.map) : [...have, chosen.map])}>
                  {inList ? "Remove from playlist" : "Add to playlist"}
                </Button>
                <p className="text-[0.85rem] text-faint">R restarts, F5 saves a spot to practice from and F9 returns to it (the timer stops).</p>
              </>
            )}
            <div className="mt-auto flex flex-col gap-2">
              <SectionTitle>Playlist</SectionTitle>
              {have.length === 0 ? (
                <p className="text-[0.85rem] text-faint">Add courses to run them back to back; the next one starts after each finish.</p>
              ) : (
                <>
                  <div className="max-h-[9rem] overflow-y-auto rounded-sm border border-amber/30 bg-black/30 p-3 text-[0.85rem] text-ink">
                    {have.map((p, i) => (
                      <div key={p} className="truncate">{i + 1}. {name(p)}</div>
                    ))}
                  </div>
                  <div data-row className="flex gap-2">
                    <Button primary onClick={runPlaylist} refuse={have.length ? undefined : "Add a course first."} onRefuse={(w) => toast(w, "warn")} className="flex-1">
                      Run playlist ({have.length})
                    </Button>
                    <Button small onClick={() => setPlaylist([])}>Clear</Button>
                  </div>
                </>
              )}
              <Button small onClick={() => setPlaylist([...have, ...shown.map((m) => m.map).filter((x) => !have.includes(x))])}>Add all shown</Button>
              <Button small onClick={() => void send("maps_folder")}>Open the Maps folder</Button>
            </div>
          </div>
        </div>
      )}
    </PagePanel>
  );
}
