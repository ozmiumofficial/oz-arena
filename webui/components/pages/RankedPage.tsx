"use client";
// Ranked (PLAN.md "Ranked"): your rank and MMR, how the ladder works, and the six tiers from Bronze
// to Godlike with their icons. Ranked is peer-to-peer for now (PLAN.md "Trust"); the queues refuse
// until online matches are built.
import { motion } from "motion/react";
import { useMenu, toast, queueJoin } from "@/lib/store";
import { cn } from "@/lib/cn";
import { fmt, TIER_COLORS } from "@/lib/cards";
import PagePanel, { SectionTitle } from "../PagePanel";
import Button from "../controls/Button";
import QueueBar from "../QueueBar";
import RankIcon from "../RankIcon";
import type { PageProps } from "./index";

const HOW: [string, string, string][] = [
  ["10", "Placement", "Your first 10 ranked matches place you. Everyone starts at 1,000 MMR, and your rank shows once placement is done."],
  ["±", "MMR", "Your matchmaking rating moves after every match, like chess Elo: beat someone rated above you and you gain more, lose to someone below and you drop more. What one player gains, the other loses."],
  ["K", "Settling in", "Ratings swing fast at first (up to 40 points a match for your first 30 matches), then settle (24), and move slowest in Godlike (16)."],
  ["III", "Divisions", "Every tier has three divisions, III to II to I, each 100 MMR. Godlike has no ceiling."],
  ["Q", "Seasons", "A season lasts three months. When it ends, your rating eases a quarter of the way back toward 1,000, and you keep the frame and title of the highest tier you reached."],
  ["!", "Fair play", "Fixed rules and an official map pool, banned in turn. Matches start in your region, a result counts only when every player's game agrees on it, and leaving is a loss."],
];
const QUEUES = [
  ["duel", "Duel", "1v1 on the tournament maps. The headline ladder."],
  ["tdm", "Team Deathmatch", "Ranked against the other team's average rating."],
  ["ctf", "Capture the Flag", "Full teams; everyone on a team moves together."],
];

export default function RankedPage({ back, open }: PageProps) {
  const init = useMenu((s) => s.init);
  const ranked = init?.profile?.ranked;
  const tiers = init?.cards?.ranked_tiers ?? [];
  const need = init?.progress?.ranked_level ?? 30;
  const level = init?.profile?.level ?? init?.progress?.level ?? 1;
  const locked = level < need;
  const lockReason = `Ranked opens at level ${need}. You're level ${level}.`;
  const rank = ranked?.placed && ranked.rank?.id ? ranked.rank : undefined;
  const toNext = rank && rank.next > 0 ? (ranked!.rating - rank.from) / Math.max(1, rank.next - rank.from) : 1;
  const placements = ranked?.placements ?? 10;
  const queue = useMenu((s) => s.queue);
  const online = useMenu((s) => s.social.status === "online");
  const busy = queue.state.status === "queued" || queue.state.status === "found";
  return (
    <PagePanel
      title="Ranked"
      subtitle={
        locked
          ? `Competitive queues with an MMR ladder from Bronze to Godlike, open from level ${need}. Every match you finish earns XP toward it.`
          : "Competitive queues with an MMR ladder from Bronze to Godlike. Ten placement matches set your starting rank."
      }
      back={back}
    >
      <div className="grid h-full grid-cols-[22rem_1fr] gap-10 px-10 py-5">
        <div className="flex min-h-0 flex-col">
          <div className="flex flex-col items-center rounded-sm border border-white/10 bg-black/30 px-6 py-5 text-center">
            <div className="relative grid h-36 w-36 place-items-center">
              {!rank && (
                <motion.div className="absolute inset-1 rounded-full border-2 border-dashed border-red/30" animate={{ rotate: 360 }} transition={{ duration: 40, repeat: Infinity, ease: "linear" }} />
              )}
              <RankIcon tier={rank?.id} division={rank?.division} className="h-28 w-24" />
            </div>
            {locked ? (
              <>
                <div className="mt-2 font-q3 text-[1.4rem] tracking-[0.1em] text-ink">Locked</div>
                <div className="mt-1 text-[0.95rem] text-dim">Reach level {need} to play ranked</div>
                <div className="mt-4 h-2 w-full overflow-hidden rounded-full bg-white/10">
                  <div className="h-full bg-gradient-to-r from-red to-glow" style={{ width: `${Math.min(100, (level / need) * 100)}%` }} />
                </div>
                <div className="mt-2 font-q3 text-[0.75rem] tracking-[0.2em] text-dim">
                  Level {level} / {need}
                </div>
              </>
            ) : rank ? (
              <>
                <div className="mt-2 font-q3 text-[1.5rem] tracking-[0.1em]" style={{ color: TIER_COLORS[rank.id] }}>{rank.label}</div>
                <div className="font-mono text-[1rem] tabular-nums text-ink-hot">{fmt(ranked!.rating)} MMR</div>
                <div className="mt-4 h-2 w-full overflow-hidden rounded-full bg-white/10">
                  <div className="h-full rounded-full" style={{ width: `${Math.min(100, toNext * 100)}%`, background: TIER_COLORS[rank.id] }} />
                </div>
                <div className="mt-2 flex w-full justify-between font-mono text-[0.7rem] text-faint">
                  <span>{rank.next > 0 ? `${fmt(rank.next - ranked!.rating)} to the next division` : "The top of the ladder"}</span>
                  <span>Peak {fmt(ranked!.peak)}</span>
                </div>
                <div className="mt-3 font-q3 text-[0.7rem] tracking-[0.18em] text-dim">
                  {ranked!.wins} W - {ranked!.matches - ranked!.wins} L
                </div>
              </>
            ) : (
              <>
                <div className="mt-2 font-q3 text-[1.4rem] tracking-[0.1em] text-ink">Unranked</div>
                <div className="mt-1 text-[0.95rem] text-dim">
                  {ranked?.matches ?? 0} / {placements} placement matches
                </div>
                <div className="mt-4 flex w-full gap-1">
                  {Array.from({ length: placements }, (_, i) => (
                    <span key={i} className={cn("h-2 flex-1 rounded-full", i < (ranked?.matches ?? 0) ? "bg-glow" : "bg-white/10")} />
                  ))}
                </div>
              </>
            )}
          </div>
          <Button small className="mt-3" onClick={() => open("leaderboards")}>Leaderboards</Button>
          <SectionTitle className="mt-5">Queues</SectionTitle>
          <div className="flex min-h-0 flex-col gap-2 overflow-y-auto">
            {QUEUES.map(([id, n, t], i) => {
              const r = queue.ratings[id];
              return (
                <div key={id} data-row className="flex items-center gap-4 rounded-sm border border-white/[0.07] bg-black/25 px-4 py-2.5">
                  <div className="min-w-0 flex-1">
                    <div className="font-q3 text-[0.95rem] tracking-[0.06em] text-ink-hot">{n}</div>
                    <div className="truncate text-[0.8rem] text-dim">{r && r.rated > 0 ? `${r.rated >= placements ? fmt(r.rating) + " MMR" : `${r.rated} / ${placements} placement matches`} - ${r.wins} W ${r.losses} L` : t}</div>
                  </div>
                  <Button
                    small
                    autoFocus={i === 0}
                    refuse={locked ? lockReason : busy ? "Already searching." : !online ? "The online service isn't connected." : undefined}
                    onRefuse={(w) => toast(w, "warn")}
                    onClick={() => queueJoin([id], true)}
                  >
                    Queue
                  </Button>
                </div>
              );
            })}
            <QueueBar />
          </div>
        </div>
        <div className="flex min-h-0 flex-col overflow-y-auto pr-2">
          <SectionTitle>Tiers</SectionTitle>
          <div className="grid grid-cols-6 gap-2">
            {tiers.map((t, i) => {
              const top = tiers[i + 1]?.min;
              const mine = rank?.id === t.id;
              const c = TIER_COLORS[t.id];
              return (
                <div
                  key={t.id}
                  className={cn("flex flex-col items-center rounded-sm border px-2 pb-3 pt-4 text-center", mine ? "bg-white/[0.05]" : "border-white/[0.07] bg-black/30")}
                  style={mine ? { borderColor: c, boxShadow: `0 0 1.2rem ${c}44` } : undefined}
                >
                  <RankIcon tier={t.id} className="h-20 w-16" />
                  <div className="mt-2 font-q3 text-[0.85rem] tracking-[0.1em]" style={{ color: c }}>{t.name}</div>
                  <div className="font-mono text-[0.68rem] tabular-nums text-dim">{top ? `${fmt(t.min)}-${fmt(top - 1)}` : `${fmt(t.min)}+`}</div>
                  {mine && <div className="mt-1 font-q3 text-[0.55rem] tracking-[0.2em] text-ink-hot">YOU</div>}
                </div>
              );
            })}
          </div>
          <p className="mt-2 text-[0.8rem] leading-snug text-faint">
            Reaching Silver or higher earns that tier&apos;s card frame and title for good, and Gold to Godlike are achievements too. Your card shows your tier and MMR to everyone.
          </p>
          <SectionTitle>How ranked works</SectionTitle>
          <div className="grid grid-cols-2 gap-2">
            {HOW.map(([k, n, t]) => (
              <div key={n} className="flex gap-3 rounded-sm border border-white/[0.07] bg-black/25 p-3">
                <div className="grid h-10 w-10 shrink-0 place-items-center rounded-sm border border-red/40 bg-red/10 font-q3 text-[0.95rem] text-glow">{k}</div>
                <div className="min-w-0">
                  <div className="font-q3 text-[0.8rem] tracking-[0.12em] text-ink-hot">{n}</div>
                  <div className="text-[0.8rem] leading-snug text-dim">{t}</div>
                </div>
              </div>
            ))}
          </div>
        </div>
      </div>
    </PagePanel>
  );
}
