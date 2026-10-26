"use client";
// Quake III Arena's single-player campaign (q3_ui ui_splevel.c), modernized: the tiers of arenas
// from arenas.txt with their bots, Quake 3's five skills, completion marks per skill and the
// medal case. Godot owns the progress (scripts/match/campaign.gd); a tier opens when every arena
// of the one before it is won.
import { useEffect, useRef, useState } from "react";
import { AnimatePresence, motion } from "motion/react";
import { useMenu } from "@/lib/store";
import { send } from "@/lib/bridge";
import { sfx } from "@/lib/sound";
import { useLayer } from "@/lib/nav";
import { cn } from "@/lib/cn";
import type { Value } from "@/lib/types";
import PagePanel, { SectionTitle } from "../PagePanel";
import Spinner from "../controls/Spinner";
import Button from "../controls/Button";
import type { PageProps } from "./index";

const AWARD_LABELS: Record<string, string> = {
  accuracy: "Accuracy", impressive: "Impressive", excellent: "Excellent", gauntlet: "Humiliation", frags: "Frags", perfect: "Perfect",
};

export default function SinglePlayerPage({ back }: PageProps) {
  const camp = useMenu((s) => s.init?.campaign);
  const skills = useMenu((s) => s.init?.skills ?? []);
  const [tier, setTier] = useState(0);
  const [skill, setSkill] = useState(3);
  const [resetting, setResetting] = useState(false);
  // open on the furthest tier the player can play, like Quake 3 does
  useEffect(() => {
    if (!camp) return;
    setSkill(camp.skill);
    let t = 0;
    camp.tiers.forEach((x, i) => {
      if (x.open) t = i;
    });
    setTier(t);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [!!camp]);
  if (!camp) return null;
  const cur = camp.tiers[tier];
  const total = camp.tiers.reduce((n, t) => n + t.arenas.length, 0);
  const won = camp.tiers.reduce((n, t) => n + t.arenas.filter((a) => a.won > 0).length, 0);
  const pickSkill = (s: number) => {
    sfx.move();
    setSkill(s);
    void send("campaign_skill", { skill: s });
  };
  return (
    <PagePanel
      title="Campaign"
      subtitle="Quake III Arena's campaign: fight through the tiers of arenas to reach the final duel. Win every arena of a tier to open the next."
      back={back}
      onTab={(d) => {
        const n = Math.min(camp.tiers.length - 1, Math.max(0, tier + d));
        if (n === tier) return sfx.buzz();
        sfx.move();
        setTier(n);
      }}
    >
      <div className="grid h-full grid-cols-[1fr_22rem] gap-8 px-10 py-5">
        <div className="flex min-h-0 flex-col">
          <div className="mb-4 flex items-center gap-5">
            <div className="w-[20rem]">
              <Spinner
                wide
                value={tier}
                options={camp.tiers.map((t, i) => [i, `${t.name}${t.complete ? "  ✓" : ""}`] as [Value, string])}
                disabled={(v) => (camp.tiers[Number(v)].open ? false : "Locked")}
                onChange={(v) => setTier(Number(v))}
              />
            </div>
            <div className="text-[0.95rem] text-dim">
              <span className="font-q3 text-[1.2rem] text-ink-hot">{won}</span> / {total} arenas won
            </div>
            <div className="ml-auto h-1.5 w-[14rem] overflow-hidden rounded-full bg-white/10">
              <motion.div className="h-full bg-gradient-to-r from-blood to-glow" initial={{ width: 0 }} animate={{ width: `${(won / Math.max(total, 1)) * 100}%` }} transition={{ duration: 0.8 }} />
            </div>
          </div>
          <AnimatePresence mode="wait">
            <motion.div
              key={tier}
              initial={{ opacity: 0, x: 20 }}
              animate={{ opacity: 1, x: 0 }}
              exit={{ opacity: 0, x: -20 }}
              transition={{ duration: 0.22 }}
              data-grid={Math.min(cur.arenas.length, 4)}
              className={cn("grid min-h-0 flex-1 gap-4", cur.arenas.length === 1 ? "grid-cols-1 px-[20%]" : "grid-cols-4")}
            >
              {cur.arenas.map((a, i) => (
                <ArenaCard key={a.map} arena={a} index={i} locked={!cur.open} skill={skill} />
              ))}
            </motion.div>
          </AnimatePresence>
          {!cur.open && <p className="mt-3 text-[0.95rem] text-amber">Locked: win every arena of {camp.tiers[tier - 1]?.name} to open {cur.name}.</p>}
        </div>
        <aside className="min-h-0 overflow-y-auto">
          <SectionTitle>Skill</SectionTitle>
          <div data-row className="grid grid-cols-5 gap-2">
            {skills.map((name, i) => (
              <button
                key={name}
                data-nav
                title={name}
                onClick={() => pickSkill(i + 1)}
                className={cn(
                  "flex flex-col items-center gap-1 rounded-sm border p-1.5 transition-all",
                  skill === i + 1 ? "border-glow/80 bg-red/20 shadow-[0_0_1rem_rgb(219_36_16/0.4)]" : "border-white/10 bg-black/30 focus:border-white/40",
                )}
              >
                {/* eslint-disable-next-line @next/next/no-img-element */}
                <img src={`/q3/campaign/skill${i + 1}.png`} alt="" className="h-10 w-10" />
              </button>
            ))}
          </div>
          <div className="mt-2 text-center font-q3 text-[1rem] tracking-[0.08em] text-ink-hot">{skills[skill - 1]}</div>
          <SectionTitle className="mt-6">Medals</SectionTitle>
          <div className="grid grid-cols-3 gap-2">
            {camp.award_names.map((k) => (
              <div key={k} className="flex flex-col items-center rounded-sm border border-white/[0.07] bg-black/30 p-2" title={AWARD_LABELS[k]}>
                {/* eslint-disable-next-line @next/next/no-img-element */}
                <img src={`/q3/campaign/medal_${k}.png`} alt="" className={cn("h-11 w-11", !camp.awards[k] && "opacity-30 grayscale")} />
                <div className="mt-1 font-q3 text-[0.6rem] tracking-[0.15em] text-dim">{AWARD_LABELS[k]}</div>
                <div className="font-q3 text-[1rem] text-ink-hot">{camp.awards[k] ?? 0}</div>
              </div>
            ))}
          </div>
          <p className="mt-4 text-[0.85rem] text-faint">Medals add up over every campaign match, won or lost. Each arena keeps the highest skill you beat it on.</p>
          <div className="mt-4">
            <Button small onClick={() => setResetting(true)}>Reset campaign</Button>
          </div>
        </aside>
      </div>
      <AnimatePresence>{resetting && <ResetConfirm onDone={() => setResetting(false)} />}</AnimatePresence>
    </PagePanel>
  );
}

function ArenaCard({ arena, index, locked, skill }: {
  arena: { map: string; name: string; levelshot: string; fraglimit: number; bots: { name: string; model: string }[]; won: number };
  index: number;
  locked: boolean;
  skill: number;
}) {
  return (
    <motion.button
      data-nav
      initial={{ opacity: 0, y: 14 }}
      animate={{ opacity: 1, y: 0 }}
      transition={{ delay: index * 0.06 }}
      onClick={() => {
        if (locked) return sfx.buzz();
        sfx.move();
        void send("campaign_start", { map: arena.map, skill });
      }}
      className={cn("group relative flex min-h-0 flex-col overflow-hidden rounded-sm border-2 text-left transition-all", locked ? "border-transparent" : "border-transparent focus:border-glow/80 focus:shadow-[0_0_1.6rem_rgb(219_36_16/0.45)]")}
    >
      <div className="relative min-h-[10rem] flex-1 overflow-hidden">
        {/* eslint-disable-next-line @next/next/no-img-element */}
        <img src={`/q3/${arena.levelshot}`} alt="" className={cn("absolute inset-0 h-full w-full object-cover transition-transform duration-500 group-focus:scale-110", locked && "brightness-[0.3] grayscale")} />
        <div className="absolute inset-0 bg-gradient-to-t from-black via-black/30 to-transparent" />
        {arena.won > 0 && (
          // eslint-disable-next-line @next/next/no-img-element
          <img src={`/q3/campaign/level_complete${arena.won}.png`} alt="Won" title={`Won at skill ${arena.won}`} className="absolute right-2 top-2 h-12 w-12 drop-shadow-[0_0_0.6rem_rgb(0_0_0)]" />
        )}
        {locked && (
          <div className="absolute inset-0 grid place-items-center">
            <svg viewBox="0 0 24 24" className="h-10 w-10 fill-none stroke-dim stroke-2"><rect x="5" y="11" width="14" height="10" rx="1" /><path d="M8 11V7a4 4 0 1 1 8 0v4" /></svg>
          </div>
        )}
        <div className="absolute bottom-2 left-3 right-3">
          <div className="font-q3 text-[1.05rem] leading-tight tracking-[0.04em] text-ink-hot">{arena.name}</div>
          <div className="font-q3 text-[0.6rem] tracking-[0.2em] text-dim">{arena.map} &middot; {arena.fraglimit} frags</div>
        </div>
      </div>
      <div className="flex items-center gap-2 bg-black/50 px-3 py-2">
        <span className="font-q3 text-[0.6rem] tracking-[0.2em] text-faint">Vs</span>
        {arena.bots.map((b) => (
          <span key={b.name} className="flex items-center gap-1.5">
            {/* eslint-disable-next-line @next/next/no-img-element */}
            {b.model && <img src={`/q3/icons/${b.model}.png`} alt="" className="h-7 w-7 rounded-sm" />}
            <span className="text-[0.85rem] text-ink">{b.name}</span>
          </span>
        ))}
      </div>
    </motion.button>
  );
}

function ResetConfirm({ onDone }: { onDone: () => void }) {
  const ref = useRef<HTMLDivElement>(null);
  useLayer(ref, { onBack: () => (sfx.out(), onDone()) });
  return (
    <motion.div ref={ref} className="absolute inset-0 z-40 grid place-items-center bg-black/70" initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }}>
      <div className="flex w-[30rem] flex-col items-center gap-5 rounded-sm border border-white/10 bg-glass p-9 text-center">
        <div className="font-q3 text-[1.5rem] tracking-[0.08em] text-ink-hot">Reset the campaign?</div>
        <div className="text-dim">Every win and medal is cleared.</div>
        <div data-row className="flex gap-3">
          <Button onClick={() => (void send("campaign_reset"), onDone())}>Reset</Button>
          <Button primary autoFocus onClick={onDone}>Cancel</Button>
        </div>
      </div>
    </motion.div>
  );
}
