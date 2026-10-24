"use client";
// The matchmaking queue's status line, on Quick Play and Ranked: what you are queued for, how long, what the search is
// doing (the rating window widening, other regions after a minute), and Cancel.
import { useEffect, useState } from "react";
import { useMenu, queueLeave } from "@/lib/store";
import Button from "./controls/Button";

const NAMES: Record<string, string> = { duel: "Duel", ffa: "Free For All", tdm: "Team Deathmatch", ctf: "Capture the Flag" };

function clock(ms: number) {
  const s = Math.max(0, Math.floor(ms / 1000));
  return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, "0")}`;
}

export default function QueueBar() {
  const q = useMenu((s) => s.queue.state);
  const region = useMenu((s) => s.values["network/region"] ?? "auto");
  const [now, setNow] = useState(() => Date.now());
  useEffect(() => {
    const t = setInterval(() => setNow(Date.now()), 500);
    return () => clearInterval(t);
  }, []);
  if (q.status === "found") {
    return <div className="rounded-sm border border-glow/60 bg-red/15 px-4 py-2 font-q3 tracking-[0.1em] text-ink-hot">Match found, connecting...</div>;
  }
  if (q.status === "locked" && q.until && q.until > now) {
    return <div className="rounded-sm border border-white/15 bg-black/40 px-4 py-2 text-[0.95rem] text-dim">Queue locked for {clock(q.until - now)}: you left a match you were sent to.</div>;
  }
  if (q.status !== "queued") return null;
  const waited = now - (q.since ?? now);
  const what = (q.modes ?? []).map((m) => NAMES[m] ?? m).join(", ");
  const hint =
    waited >= 60_000 && region === "auto" ? "Looking in neighboring regions too." : q.ranked && waited >= 10_000 ? "Taking the closest rating available." : q.ranked ? "Looking near your rating." : "";
  return (
    <div data-row className="flex items-center gap-4 rounded-sm border border-glow/50 bg-red/10 px-4 py-2">
      <span className="h-2.5 w-2.5 animate-pulse rounded-full bg-glow" />
      <span className="font-q3 tracking-[0.08em] text-ink-hot">
        Searching{q.ranked ? " (ranked)" : ""}: {what}
      </span>
      <span className="font-mono tabular-nums text-ink">{clock(waited)}</span>
      {hint && <span className="text-[0.85rem] text-dim">{hint}</span>}
      <Button small className="ml-auto" onClick={() => queueLeave()}>Cancel</Button>
    </div>
  );
}
