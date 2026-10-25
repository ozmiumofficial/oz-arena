"use client";
// The map picker for Create Match and Offline vs Bots: every map the game knows (Quake 3's arenas,
// the Maps folder's pk3s, maps published from the editor), searchable and filtered by where it came
// from, as level-shot cards. Maps made for the chosen mode come first; the rest follow in their own
// section, so a map is never hidden by the mode (Aerowalk is made for Duel only): picking one of
// those switches to a mode it's made for (the page decides which). Keyboard and controller walk the
// search, the source chips and the grids as everywhere else (lib/nav.ts).
import { useMemo, useState } from "react";
import { sfx } from "@/lib/sound";
import { cn } from "@/lib/cn";
import { shotSrc } from "@/lib/shot";
import type { MapInfo, ModeInfo } from "@/lib/types";

type Source = "all" | "q3" | "pack" | "custom";
const SOURCES: [Source, string][] = [["all", "All"], ["q3", "Quake 3"], ["pack", "Maps folder"], ["custom", "My maps"]];

export function sourceOf(m: MapInfo): Source {
  return m.special === "pack" ? "pack" : m.special === "custom" ? "custom" : "q3";
}

export function fits(m: MapInfo, mode: ModeInfo | undefined) {
  return !!mode && m.types.some((t) => mode.map_types.includes(t));
}

export default function MapBrowser({ maps, mode, modes, value, onPick, cols = 4 }: {
  maps: MapInfo[];
  /** the chosen mode: its maps come first */
  mode: ModeInfo | undefined;
  /** the modes this page offers, to name what the other maps are made for */
  modes: ModeInfo[];
  value: string;
  onPick: (m: MapInfo) => void;
  cols?: number;
}) {
  const [query, setQuery] = useState("");
  const [source, setSource] = useState<Source>("all");
  const counts = useMemo(() => {
    const c: Record<Source, number> = { all: maps.length, q3: 0, pack: 0, custom: 0 };
    for (const m of maps) c[sourceOf(m)] += 1;
    return c;
  }, [maps]);
  const q = query.trim().toLowerCase();
  const shown = maps.filter((m) => (source === "all" || sourceOf(m) === source) && (!q || m.name.toLowerCase().includes(q) || m.map.toLowerCase().includes(q)));
  const mine = shown.filter((m) => fits(m, mode));
  const other = shown.filter((m) => !fits(m, mode) && modes.some((x) => fits(m, x)));
  return (
    <div className="flex min-h-0 flex-1 flex-col">
      <div className="mb-3 flex items-center gap-3">
        <input
          data-nav
          value={query}
          placeholder={`Search ${maps.length} maps`}
          spellCheck={false}
          onChange={(e) => setQuery(e.target.value)}
          onKeyDown={(e) => {
            if (e.key === "Escape" && query) {
              e.stopPropagation();
              setQuery("");
            }
          }}
          className="h-10 w-[18rem] rounded-sm border border-white/10 bg-black/50 px-3 text-[1rem] text-ink-hot placeholder:text-faint focus:border-glow/70 focus:bg-black/70"
        />
        <div data-row className="flex gap-2">
          {SOURCES.filter(([s]) => s === "all" || counts[s] > 0).map(([s, label]) => (
            <button
              key={s}
              data-nav
              onClick={() => {
                sfx.move();
                setSource(s);
              }}
              className={cn(
                "h-10 rounded-sm border px-3 font-q3 text-[0.8rem] tracking-[0.12em] transition-colors",
                source === s ? "border-glow/80 bg-red/20 text-ink-hot" : "border-white/10 bg-black/40 text-dim focus:border-ink/60 focus:text-ink",
              )}
            >
              {label} <span className="text-faint">{counts[s]}</span>
            </button>
          ))}
        </div>
      </div>
      <div className="min-h-0 flex-1 overflow-y-auto pr-2">
        {mine.length === 0 && other.length === 0 && (
          <div className="py-10 text-center text-[1rem] text-dim">No maps match{q ? ` "${query.trim()}"` : ""}.</div>
        )}
        {mine.length > 0 && <Cards maps={mine} value={value} onPick={onPick} cols={cols} />}
        {other.length > 0 && (
          <>
            <div className="mb-2 mt-5 font-q3 text-[0.75rem] tracking-[0.25em] text-dim">
              MADE FOR OTHER MODES{mode ? ` THAN ${mode.name.toUpperCase()}` : ""}: PICKING ONE SWITCHES THE MODE
            </div>
            <Cards maps={other} value={value} onPick={onPick} cols={cols} modes={modes} />
          </>
        )}
      </div>
    </div>
  );
}

function Cards({ maps, value, onPick, cols, modes }: { maps: MapInfo[]; value: string; onPick: (m: MapInfo) => void; cols: number; modes?: ModeInfo[] }) {
  return (
    <div data-grid={cols} className="grid gap-3" style={{ gridTemplateColumns: `repeat(${cols}, minmax(0, 1fr))` }}>
      {maps.map((m) => (
        <button
          key={m.map}
          data-nav
          data-autofocus={m.map === value ? "" : undefined}
          onClick={() => {
            sfx.move();
            onPick(m);
          }}
          className={cn(
            "group relative aspect-[4/3] overflow-hidden rounded-sm border-2 text-left transition-all",
            m.map === value ? "border-glow shadow-[0_0_1.4rem_rgb(219_36_16/0.5)]" : "border-transparent focus:border-ink/60",
          )}
        >
          {/* eslint-disable-next-line @next/next/no-img-element */}
          <img src={shotSrc(m.levelshot)} alt="" loading="lazy" className="absolute inset-0 h-full w-full object-cover transition-transform duration-500 group-focus:scale-110" />
          <div className="absolute inset-0 bg-gradient-to-t from-black/90 via-black/20 to-transparent" />
          {sourceOf(m) !== "q3" && (
            <div className="absolute left-2 top-2 rounded-sm bg-black/70 px-1.5 py-0.5 font-q3 text-[0.55rem] tracking-[0.18em] text-amber">
              {sourceOf(m) === "pack" ? "MAPS FOLDER" : "MY MAP"}
            </div>
          )}
          {modes && (
            <div className="absolute right-2 top-2 rounded-sm bg-black/70 px-1.5 py-0.5 font-q3 text-[0.55rem] tracking-[0.18em] text-ink">
              {modes.filter((x) => fits(m, x)).map((x) => x.name).join(" / ").toUpperCase()}
            </div>
          )}
          <div className="absolute bottom-2 left-3 right-3">
            <div className="line-clamp-2 font-q3 text-[0.9rem] leading-tight tracking-[0.04em] text-ink-hot">{m.name}</div>
            <div className="truncate font-q3 text-[0.6rem] tracking-[0.2em] text-dim">{m.map}</div>
          </div>
        </button>
      ))}
    </div>
  );
}
