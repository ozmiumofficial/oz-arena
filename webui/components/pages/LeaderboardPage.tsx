"use client";
// Leaderboards: the level board (total XP, synced to the online hub by every player's game) and the
// ranked board (MMR, set only by ranked matches whose players agree on the result, so it stays empty
// until online matches run).
// Global or just you and your friends; Q/E switch boards. Any row opens that player's full card.
import { useEffect, useMemo, useRef, useState } from "react";
import { motion } from "motion/react";
import { useMenu } from "@/lib/store";
import { sfx } from "@/lib/sound";
import { cn } from "@/lib/cn";
import { accentOf, art, fmt, part, rankOf, TIER_COLORS } from "@/lib/cards";
import { social, type BoardKind, type BoardRow, type BoardScope } from "@/lib/social";
import type { CardsCatalog, RankedState } from "@/lib/types";
import PagePanel, { EmptyState } from "../PagePanel";
import Button from "../controls/Button";
import PlayerCard, { Banner } from "../PlayerCard";
import RankIcon from "../RankIcon";
import { Q3Name } from "../Social";
import RaceBoard from "./RaceBoard";
import type { PageProps } from "./index";

type Tab = BoardKind | "race";
const BOARDS: [Tab, string][] = [["level", "Level"], ["ranked", "Ranked MMR"], ["race", "Race"]];
const SCOPES: [BoardScope, string][] = [["global", "Everyone"], ["friends", "Friends"]];
const PODIUM = ["#ffd24a", "#c9d6df", "#e08a4f"];

export default function LeaderboardPage({ back }: PageProps) {
  const init = useMenu((s) => s.init);
  const status = useMenu((s) => s.social.status);
  const reason = useMenu((s) => s.social.reason);
  const [tab, setBoard] = useState<Tab>("level");
  const raceTab = tab === "race";
  const board: BoardKind = tab === "race" ? "level" : tab;
  const [scope, setScope] = useState<BoardScope>("global");
  const data = useMenu((s) => s.boards[`${board}|${scope}`]);
  const [asked, setAsked] = useState(0);
  const [slow, setSlow] = useState(false);
  const [pick, setPick] = useState<string | null>(null);
  const list = useRef<HTMLDivElement>(null);
  const cat = init?.cards;

  // ask whenever the board, the scope or the connection changes; an old hub never answers
  useEffect(() => {
    if (status !== "online" || raceTab) return;
    social("leaderboard", { board, scope });
    setSlow(false);
    const t = setTimeout(() => setSlow(true), 6000);
    return () => clearTimeout(t);
  }, [board, scope, status, asked, raceTab]);
  useEffect(() => setPick(null), [board, scope]);

  const switchBoard = (d: number) => {
    const i = BOARDS.findIndex((b) => b[0] === tab);
    sfx.move();
    setBoard(BOARDS[(i + d + BOARDS.length) % BOARDS.length][0]);
  };
  const rows = data?.rows ?? [];
  const me = data?.me ?? null;
  const shown = rows.find((r) => r.id === pick) ?? me ?? rows[0] ?? null;
  const fresh = !!data;

  return (
    <PagePanel
      title="Leaderboards"
      subtitle={raceTab ? "Your fastest finishes on every Defrag course, best first." : board === "level" ? "Every player's level, from the XP their matches earn. Self-reported, like any offline career, and cosmetic only." : "Ranked MMR from matches whose players all agreed on the result, highest first."}
      back={back}
      onTab={switchBoard}
    >
      <div className="flex h-full flex-col">
        <div className="flex items-center gap-1 px-10 pt-3">
          {BOARDS.map(([id, label]) => (
            <button
              key={id}
              onClick={() => id !== tab && (sfx.move(), setBoard(id))}
              className={cn("relative px-3 py-2 font-q3 text-[0.8rem] tracking-[0.12em] transition-colors", id === tab ? "text-ink-hot" : "text-dim hover:text-ink")}
            >
              {label}
              {id === tab && <motion.span layoutId="board-tab" className="absolute inset-x-2 -bottom-px h-[2px] bg-glow shadow-[0_0_0.6rem_rgb(255_74_28)]" />}
            </button>
          ))}
          <div data-row className="ml-auto flex items-center gap-1.5">
            {!raceTab && SCOPES.map(([id, label]) => (
              <button
                key={id}
                data-nav
                onClick={() => id !== scope && (sfx.move(), setScope(id))}
                className={cn("rounded-sm border px-3 py-1 font-q3 text-[0.65rem] tracking-[0.14em] transition-colors", id === scope ? "border-glow/70 bg-red/20 text-ink-hot" : "border-white/10 text-dim focus:border-ink/40 focus:text-ink")}
              >
                {label}
              </button>
            ))}
            {!raceTab && <Button small onClick={() => setAsked((n) => n + 1)}>Refresh</Button>}
            <kbd className="ml-2 rounded-sm border border-white/15 px-1.5 py-0.5 font-q3 text-[0.6rem] text-dim">Q</kbd>
            <kbd className="rounded-sm border border-white/15 px-1.5 py-0.5 font-q3 text-[0.6rem] text-dim">E</kbd>
          </div>
        </div>
        <div className="mx-10 h-px bg-white/[0.06]" />
        {raceTab ? <RaceBoard /> : !cat ? null : status !== "online" ? (
          <div className="grid flex-1 place-items-center px-10">
            <div className="flex flex-col items-center gap-4">
              <EmptyState
                title={status === "disabled" ? "Online is off" : status === "connecting" ? "Connecting..." : "Offline"}
                text={status === "disabled" ? "Turn on online play in Settings, Network to see the leaderboards." : reason || "Leaderboards need the online service."}
              />
              {status === "offline" && <Button small onClick={() => social("retry")}>Try again</Button>}
            </div>
          </div>
        ) : (
          <div className="grid min-h-0 flex-1 grid-cols-[1fr_30rem] gap-8 px-10 py-4">
            <div className="flex min-h-0 flex-col">
              <div className="grid grid-cols-[3.2rem_1fr_11rem] gap-3 px-3 pb-2 font-q3 text-[0.6rem] tracking-[0.2em] text-faint">
                <span>RANK</span>
                <span>PLAYER</span>
                <span className="text-right">{board === "level" ? "LEVEL" : "MMR"}</span>
              </div>
              <div ref={list} data-grid="1" className="min-h-0 flex-1 overflow-y-auto pr-2">
                {!fresh ? (
                  <div className="grid h-full place-items-center text-[0.95rem] text-dim">
                    {slow ? "The online service didn't answer. It may not have leaderboards yet." : "Loading..."}
                  </div>
                ) : rows.length === 0 ? (
                  board === "ranked" ? (
                    <RankedEmpty cat={cat} />
                  ) : (
                    <div className="grid h-full place-items-center">
                      <EmptyState title="Nobody here yet" text={scope === "friends" ? "Your friends' levels show up here once they've played online." : "Finish a match while online to put yourself on the board."} />
                    </div>
                  )
                ) : (
                  <div className="flex flex-col gap-1.5">
                    {rows.map((r) => (
                      <Row key={r.id} r={r} cat={cat} board={board} mine={r.id === me?.id} active={shown?.id === r.id} onPick={() => setPick(r.id)} />
                    ))}
                  </div>
                )}
              </div>
              {fresh && me && !rows.some((r) => r.id === me.id) && (
                <div className="mt-2 border-t border-white/[0.08] pt-2 pr-2">
                  <Row r={me} cat={cat} board={board} mine active={shown?.id === me.id} onPick={() => setPick(me.id)} />
                </div>
              )}
              {fresh && (
                <div className="mt-2 flex justify-between font-mono text-[0.7rem] text-faint">
                  <span>{fmt(data.total)} {data.total === 1 ? "player" : "players"}{rows.length < data.total ? `, top ${rows.length} shown` : ""}</span>
                  <span>{me ? `You: #${fmt(me.rank)}` : board === "level" ? "You'll appear after your next sync" : ""}</span>
                </div>
              )}
            </div>
            <div className="flex min-h-0 flex-col items-center overflow-y-auto">
              {shown ? (
                <>
                  <div className="mb-3 self-stretch font-q3 text-[0.7rem] tracking-[0.2em] text-dim">
                    #{fmt(shown.rank)} <span className="text-faint">{board === "level" ? "BY LEVEL" : "BY MMR"}</span>
                    {shown.online && <span className="ml-3 text-[#5ccf4a]">ONLINE</span>}
                  </div>
                  <PlayerCard card={shown.card} cat={cat} name={shown.name} level={shown.level} stats={shown.stats} ranked={rankedOf(cat, shown)} id="LOCAL" className="text-[1rem]" />
                  <div className="mt-4 grid w-full grid-cols-2 gap-2">
                    <Stat k="Total XP" v={fmt(shown.xp)} />
                    <Stat k="Level" v={String(shown.level)} />
                    <Stat k="Ranked MMR" v={shown.rated ? fmt(shown.rating) : "-"} />
                    <Stat k="Ranked matches" v={fmt(shown.rated)} />
                  </div>
                  <p className="mt-3 text-center text-[0.75rem] leading-snug text-faint">LOCAL means the career was recorded by the player&apos;s own game. Parts this game doesn&apos;t have show as the default.</p>
                </>
              ) : (
                <div className="grid h-full place-items-center text-[0.9rem] text-faint">Pick a player to see their card.</div>
              )}
            </div>
          </div>
        )}
      </div>
    </PagePanel>
  );
}

function Row({ r, cat, board, mine, active, onPick }: { r: BoardRow; cat: CardsCatalog; board: BoardKind; mine: boolean; active: boolean; onPick: () => void }) {
  const accent = accentOf(cat, r.card.accent);
  const title = part(cat, "titles", r.card.title)?.name;
  const emblem = part(cat, "emblems", r.card.emblem)?.art;
  const rank = board === "ranked" ? rankOf(cat, r.rating) : undefined;
  const podium = r.rank <= 3 ? PODIUM[r.rank - 1] : undefined;
  return (
    <button
      data-nav
      onClick={() => (sfx.move(), onPick())}
      onFocus={onPick}
      className={cn(
        "group relative grid w-full grid-cols-[3.2rem_1fr_11rem] items-center gap-3 overflow-hidden rounded-sm border px-3 py-1.5 text-left transition-colors",
        active ? "border-glow/70 bg-red/10" : mine ? "border-amber/40 bg-amber/[0.05]" : "border-white/[0.06] bg-black/30 focus:border-ink/40",
      )}
    >
      <span className="pointer-events-none absolute inset-y-0 left-[3.2rem] right-0 opacity-30 [mask-image:linear-gradient(90deg,black,transparent_75%)]">
        <Banner p={part(cat, "banners", r.card.banner)} cat={cat} className="h-full w-full text-[0.4rem]" />
      </span>
      <span className="relative font-q3 text-[1.25rem] tabular-nums" style={{ color: podium ?? "var(--color-dim)", textShadow: podium ? `0 0 0.8rem ${podium}88` : undefined }}>
        {r.rank}
      </span>
      <span className="relative flex min-w-0 items-center gap-3">
        <span className="relative h-11 w-11 shrink-0 overflow-hidden rounded-[3px] border-2 bg-black/60" style={{ borderColor: accent }}>
          {/* eslint-disable-next-line @next/next/no-img-element */}
          {emblem && <img src={art(emblem)} alt="" className="h-full w-full object-cover" />}
          {r.online && <span className="absolute bottom-0.5 right-0.5 h-2 w-2 rounded-full bg-[#5ccf4a] shadow-[0_0_0.3rem_#5ccf4a]" />}
        </span>
        <span className="min-w-0 leading-tight">
          <Q3Name name={r.name} className="block truncate font-q3 text-[1rem] tracking-wider text-ink-hot" />
          <span className="block truncate font-q3 text-[0.58rem] tracking-[0.18em]" style={{ color: accent }}>{title}{mine ? "  -  YOU" : ""}</span>
        </span>
      </span>
      <span className="relative flex items-center justify-end gap-2 text-right">
        {rank ? (
          <>
            <span className="leading-tight">
              <span className="block font-q3 text-[0.7rem] tracking-[0.12em]" style={{ color: TIER_COLORS[rank.id] }}>{rank.label}</span>
              <span className="block font-mono text-[0.85rem] tabular-nums text-ink-hot">{fmt(r.rating)}</span>
            </span>
            <RankIcon tier={rank.id} division={rank.division} className="h-9 w-7" />
          </>
        ) : (
          <span className="leading-tight">
            <span className="block font-q3 text-[1.25rem] text-ink-hot">{r.level}</span>
            <span className="block font-mono text-[0.65rem] tabular-nums text-faint">{fmt(r.xp)} XP</span>
          </span>
        )}
      </span>
    </button>
  );
}

function RankedEmpty({ cat }: { cat: CardsCatalog }) {
  const tiers = useMemo(() => cat.ranked_tiers, [cat]);
  return (
    <div className="flex h-full flex-col items-center justify-center gap-6 text-center">
      <div className="flex items-end gap-4">
        {tiers.map((t) => (
          <RankIcon key={t.id} tier={t.id} className="w-12 opacity-80" title={t.name} />
        ))}
      </div>
      <div>
        <div className="font-q3 text-[1.3rem] tracking-[0.1em] text-ink">No ranked players yet</div>
        <p className="mx-auto mt-2 max-w-[34rem] text-[0.95rem] text-dim">
          Ranked needs online matches, which aren&apos;t built yet. Once they are, every ranked match whose players agree on the result moves ratings, and the ladder from Bronze to Godlike fills in here, highest MMR first.
        </p>
      </div>
    </div>
  );
}

function Stat({ k, v }: { k: string; v: string }) {
  return (
    <div className="rounded-sm border border-white/[0.07] bg-black/30 px-3 py-2">
      <div className="font-q3 text-[0.55rem] tracking-[0.2em] text-dim">{k}</div>
      <div className="font-q3 text-[1.1rem] text-ink-hot">{v}</div>
    </div>
  );
}

// a row's ranked block for its card: nothing before a first ranked match
function rankedOf(cat: CardsCatalog, r: BoardRow): RankedState | undefined {
  if (!r.rated) return undefined;
  const placements = cat.ranked?.placements ?? 10;
  const rank = rankOf(cat, r.rating);
  if (!rank) return undefined;
  return { open: true, matches: r.rated, placements, placed: r.rated >= placements, rating: r.rating, peak: r.peak, rank, peak_rank: {}, wins: 0 };
}
