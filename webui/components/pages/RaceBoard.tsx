"use client";
// The Race leaderboard (Leaderboards page, Race tab): your fastest finishes on every course, kept by
// Godot (Q3RaceRecords, the ten best runs a course). It works offline; an online board waits for runs
// the hub can verify (RACE_PLAN.md D7).
import { useMemo, useState } from "react";
import { useMenu } from "@/lib/store";
import { sfx } from "@/lib/sound";
import { cn } from "@/lib/cn";
import { raceTime } from "@/lib/race";
import { EmptyState } from "../PagePanel";

const PODIUM = ["#ffd24a", "#c9d6df", "#e08a4f"];

export default function RaceBoard() {
  const init = useMenu((s) => s.init);
  const board = init?.race_board ?? {};
  const maps = useMemo(() => (init?.maps ?? []).filter((m) => m.types.includes("race")), [init]);
  const done = maps.filter((m) => board[m.map]?.length).sort((a, b) => a.name.localeCompare(b.name));
  const todo = maps.filter((m) => !board[m.map]?.length).sort((a, b) => a.name.localeCompare(b.name));
  const [pick, setPick] = useState("");
  const cur = maps.find((m) => m.map === pick) ?? done[0] ?? todo[0];
  const runs = cur ? board[cur.map] ?? [] : [];
  const best = runs[0]?.ms ?? 0;
  if (maps.length === 0) {
    return (
      <div className="grid flex-1 place-items-center px-10">
        <EmptyState title="No race courses yet" text="Put Defrag courses in the game's Maps folder (Single Player, Race). Your times on each are ranked here." />
      </div>
    );
  }
  const total = done.reduce((n, m) => n + (board[m.map]?.[0]?.ms ?? 0), 0);
  return (
    <div className="grid min-h-0 flex-1 grid-cols-[1fr_30rem] gap-8 px-10 py-4">
      <div className="flex min-h-0 flex-col">
        <div className="grid grid-cols-[1fr_8rem_7rem] gap-3 px-3 pb-2 font-q3 text-[0.6rem] tracking-[0.2em] text-faint">
          <span>COURSE</span>
          <span className="text-right">BEST</span>
          <span className="text-right">RUNS</span>
        </div>
        <div data-grid="1" className="flex min-h-0 flex-1 flex-col gap-1.5 overflow-y-auto pr-2">
          {[...done, ...todo].map((m) => {
            const r = board[m.map] ?? [];
            return (
              <button
                key={m.map}
                data-nav
                onClick={() => (sfx.move(), setPick(m.map))}
                onFocus={() => setPick(m.map)}
                className={cn(
                  "grid w-full grid-cols-[1fr_8rem_7rem] items-center gap-3 rounded-sm border px-3 py-2 text-left transition-colors",
                  cur?.map === m.map ? "border-glow/70 bg-red/10" : "border-white/[0.06] bg-black/30 focus:border-ink/40",
                  r.length === 0 && "opacity-50",
                )}
              >
                <span className="min-w-0 truncate text-[1rem] text-ink-hot">{m.name}</span>
                <span className="text-right font-q3 text-[1.05rem] tabular-nums text-amber">{r.length ? raceTime(r[0].ms) : "-"}</span>
                <span className="text-right font-mono text-[0.8rem] text-dim">{r.length || ""}</span>
              </button>
            );
          })}
        </div>
        <div className="mt-2 flex justify-between font-mono text-[0.7rem] text-faint">
          <span>{done.length} of {maps.length} courses finished</span>
          <span>{done.length ? `Sum of bests ${raceTime(total)}` : ""}</span>
        </div>
      </div>
      <div className="flex min-h-0 flex-col overflow-y-auto">
        {cur && (
          <>
            <div className="font-q3 text-[1.5rem] leading-tight tracking-[0.05em] text-ink-hot">{cur.name}</div>
            <div className="mb-3 text-[0.9rem] text-dim">{cur.race?.author ? `by ${cur.race.author}` : cur.map}</div>
            {runs.length === 0 ? (
              <EmptyState title="No time yet" text="Finish this course once to put a time on the board." />
            ) : (
              <div className="flex flex-col gap-1.5">
                {runs.map((r, i) => (
                  <div key={i} className={cn("grid grid-cols-[2.4rem_1fr_6rem] items-center gap-3 rounded-sm border px-3 py-2", i === 0 ? "border-amber/40 bg-amber/[0.05]" : "border-white/[0.06] bg-black/30")}>
                    <span className="font-q3 text-[1.2rem] tabular-nums" style={{ color: PODIUM[i] ?? "var(--color-dim)" }}>{i + 1}</span>
                    <span>
                      <span className="font-q3 text-[1.25rem] tabular-nums text-ink-hot">{raceTime(r.ms)}</span>
                      <span className="ml-3 text-[0.75rem] text-faint">{r.when ? new Date(r.when * 1000).toLocaleDateString() : ""}</span>
                    </span>
                    <span className="text-right font-mono text-[0.8rem] text-dim">{i === 0 ? "BEST" : `+${raceTime(r.ms - best)}`}</span>
                  </div>
                ))}
              </div>
            )}
            <p className="mt-4 text-[0.75rem] leading-snug text-faint">Your ten fastest finishes on each course. An online board comes once the service can verify runs.</p>
          </>
        )}
      </div>
    </div>
  );
}
