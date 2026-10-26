"use client";
// Race (RACE_PLAN.md D3): Defrag's timed courses. The maps are the Maps folder's pk3s that have a .defi
// file (style, author), plus any published from the editor with race settings; your best time on each
// is kept by Godot (Q3RaceRecords). The movement is id 1.32's (Classic), set by the mode itself.
import { useEffect, useMemo, useState } from "react";
import { motion } from "motion/react";
import { useMenu, toast } from "@/lib/store";
import { send } from "@/lib/bridge";
import { sfx } from "@/lib/sound";
import { cn } from "@/lib/cn";
import { shotSrc } from "@/lib/shot";
import { raceTime, RACE_STYLES } from "@/lib/race";
import type { MapInfo } from "@/lib/types";
import PagePanel, { EmptyState, SectionTitle } from "../PagePanel";
import Button from "../controls/Button";
import type { PageProps } from "./index";

export default function RacePage({ back }: PageProps) {
  const init = useMenu((s) => s.init);
  const records = init?.race_records ?? {};
  const maps = useMemo(() => (init?.maps ?? []).filter((m) => m.types.includes("race")), [init]);
  const [map, setMap] = useState("");
  const [query, setQuery] = useState("");
  const [style, setStyle] = useState("all");
  useEffect(() => {
    if (!map && maps.length) setMap(maps[0].map);
  }, [maps, map]);
  const styles = useMemo(() => [...new Set(maps.map((m) => m.race?.style ?? "run"))], [maps]);
  const q = query.trim().toLowerCase();
  const shown = maps.filter((m) => (style === "all" || (m.race?.style ?? "run") === style) && (!q || m.name.toLowerCase().includes(q) || m.map.toLowerCase().includes(q)));
  const chosen = maps.find((m) => m.map === map);
  const run = (m: MapInfo) => void send("start", { setup: { mode: "race", map: m.map, rules: {}, modifiers: {}, bots: [], server_type: "offline", source: "menu" } });

  return (
    <PagePanel title="Race" subtitle="Run the course from the start line to the finish as fast as you can. Quake 3's own movement, no health to lose, and a restart on R." back={back}>
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
            </div>
            <div data-grid="3" className="grid min-h-0 flex-1 grid-cols-3 content-start gap-3 overflow-y-auto pr-2">
              {shown.map((m, i) => (
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
                    {(RACE_STYLES[m.race?.style ?? "run"] ?? m.race?.style ?? "Run").toUpperCase()}
                  </div>
                  {records[m.map] ? (
                    <div className="absolute right-2 top-2 rounded-sm bg-black/70 px-1.5 py-0.5 font-q3 text-[0.65rem] tracking-[0.1em] text-amber">{raceTime(records[m.map])}</div>
                  ) : null}
                  <div className="absolute bottom-2 left-3 right-3">
                    <div className="line-clamp-2 font-q3 text-[0.9rem] leading-tight tracking-[0.04em] text-ink-hot">{m.name}</div>
                    <div className="truncate font-q3 text-[0.6rem] tracking-[0.2em] text-dim">{m.race?.author || m.map}</div>
                  </div>
                </motion.button>
              ))}
              {shown.length === 0 && <div className="col-span-3 py-10 text-center text-[1rem] text-dim">No course matches.</div>}
            </div>
          </div>
          <div className="flex w-[21rem] shrink-0 flex-col gap-3">
            <SectionTitle>Course</SectionTitle>
            {chosen && (
              <>
                <div className="font-q3 text-[1.5rem] leading-tight tracking-[0.05em] text-ink-hot">{chosen.name}</div>
                <div className="text-[0.95rem] text-dim">
                  {RACE_STYLES[chosen.race?.style ?? "run"] ?? chosen.race?.style}
                  {chosen.race?.author ? ` · by ${chosen.race.author}` : ""}
                </div>
                <div className="mt-2 rounded-sm border border-white/10 bg-black/30 p-4">
                  <div className="font-q3 text-[0.7rem] tracking-[0.2em] text-dim">YOUR BEST</div>
                  <div className="font-q3 text-[2.4rem] leading-none text-amber">{records[chosen.map] ? raceTime(records[chosen.map]) : "—"}</div>
                  <p className="mt-2 text-[0.85rem] text-faint">{records[chosen.map] ? "Your best run races beside you as a ghost." : "Finish it once to set a time and a ghost to beat."}</p>
                </div>
                <p className="text-[0.85rem] text-faint">R restarts, F5 saves a spot to practice from and F9 returns to it (the timer stops).</p>
              </>
            )}
            <div className="mt-auto flex flex-col gap-2">
              <Button primary onClick={() => chosen && run(chosen)} refuse={chosen ? undefined : "Pick a course."} onRefuse={(w) => toast(w, "warn")} className="py-4 text-[1.3rem]">
                Run
              </Button>
              <Button small onClick={() => void send("maps_folder")}>Open the Maps folder</Button>
            </div>
          </div>
        </div>
      )}
    </PagePanel>
  );
}
