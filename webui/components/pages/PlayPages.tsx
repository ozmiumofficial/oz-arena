"use client";
// Quick play, Ranked and the Server browser are the real pages, waiting on Steam: their lists are
// empty and their search buttons buzz (menu4) until GodotSteam is in. Training starts real setups.
import { useState } from "react";
import { motion } from "motion/react";
import { useMenu, toast } from "@/lib/store";
import { send } from "@/lib/bridge";
import { sfx } from "@/lib/sound";
import { cn } from "@/lib/cn";
import type { MatchSetup, Value } from "@/lib/types";
import PagePanel, { EmptyState, SectionTitle } from "../PagePanel";
import Button from "../controls/Button";
import Spinner from "../controls/Spinner";
import Slider from "../controls/Slider";
import Toggle from "../controls/Toggle";
import TextField from "../controls/TextField";
import type { PageProps } from "./index";

const NEEDS_STEAM = "Needs Steam (GodotSteam), which isn't wired yet.";
const refused = (why: string) => toast(why, "warn");

function PlaylistCard({ name, text, on, toggle }: { name: string; text: string; on: boolean; toggle: () => void }) {
  return (
    <button
      data-nav
      onClick={() => {
        sfx.move();
        toggle();
      }}
      className={cn(
        "relative flex flex-col gap-1 rounded-sm border p-4 text-left transition-all",
        on ? "border-glow/70 bg-red/15 shadow-[0_0_1.2rem_rgb(219_36_16/0.25)]" : "border-white/10 bg-black/30 focus:border-white/30",
      )}
    >
      <span className={cn("font-q3 text-[1.15rem] tracking-[0.06em]", on ? "text-ink-hot" : "text-ink")}>{name}</span>
      <span className="text-[0.85rem] text-dim">{text}</span>
      <span className={cn("absolute right-3 top-3 h-3 w-3 rounded-full border", on ? "border-glow bg-glow shadow-[0_0_0.6rem_rgb(255_74_28)]" : "border-white/20")} />
    </button>
  );
}

export function QuickPlayPage({ back }: PageProps) {
  const modes = useMenu((s) => s.init?.modes ?? []);
  const region = useMenu((s) => s.init?.schema.find((e) => e.key === "network/region")?.options?.find((o) => o[0] === s.values["network/region"])?.[1] ?? "Automatic");
  const [on, setOn] = useState<Set<string>>(new Set(["ffa", "tdm", "ctf"]));
  return (
    <PagePanel title="Quick Play" subtitle="Pick the playlists you're in the mood for; matchmaking finds the closest game." back={back}>
      <div className="flex h-full flex-col px-10 py-6">
        <SectionTitle>Playlists</SectionTitle>
        <div data-grid="4" className="grid grid-cols-4 gap-3">
          {modes.map((m) => (
            <PlaylistCard key={m.id} name={m.name} text={m.description.split(".")[0] + "."} on={on.has(m.id)} toggle={() => setOn((s) => { const n = new Set(s); if (n.has(m.id)) n.delete(m.id); else n.add(m.id); return n; })} />
          ))}
        </div>
        <div className="mt-auto flex items-end justify-between">
          <div className="text-[0.95rem] text-dim">Region: <span className="text-ink">{region}</span> <span className="text-faint">(Settings, Network)</span></div>
          <Button primary refuse={NEEDS_STEAM} onRefuse={refused} className="px-14 py-4 text-[1.4rem]">Find match</Button>
        </div>
      </div>
    </PagePanel>
  );
}

export function RankedPage({ back }: PageProps) {
  const progress = useMenu((s) => s.init?.progress);
  const need = progress?.ranked_level ?? 30;
  const level = progress?.level ?? 1;
  const locked = level < need;
  const lockReason = `Ranked opens at level ${need}. You're level ${level}.`;
  const queues = [
    ["Duel", "1v1 on the tournament maps. The classic ladder."],
    ["2v2 TDM", "Team deathmatch with a partner, best of three maps."],
    ["4v4 CTF", "Capture the flag, full teams, ranked by team rating."],
  ];
  return (
    <PagePanel
      title="Ranked"
      subtitle={locked ? `Competitive queues with an ELO ladder, open from level ${need}. Matches and the campaign earn XP.` : "Competitive queues with an ELO ladder. Ten placement matches set your starting rank."}
      back={back}
    >
      <div className="grid h-full grid-cols-[22rem_1fr] gap-10 px-10 py-6">
        <div className="flex flex-col items-center justify-center rounded-sm border border-white/10 bg-black/30 p-8 text-center">
          <div className="relative grid h-36 w-36 place-items-center">
            <motion.div className="absolute inset-0 rounded-full border-2 border-dashed border-red/40" animate={{ rotate: 360 }} transition={{ duration: 40, repeat: Infinity, ease: "linear" }} />
            <div className="font-q3 text-[3rem] text-faint">?</div>
          </div>
          {locked ? (
            <>
              <div className="mt-5 font-q3 text-[1.4rem] tracking-[0.1em] text-ink">Locked</div>
              <div className="mt-1 text-[0.95rem] text-dim">Reach level {need} to play ranked</div>
              <div className="mt-4 h-2 w-full overflow-hidden rounded-full bg-white/10">
                <div className="h-full bg-gradient-to-r from-red to-glow" style={{ width: `${Math.min(100, (level / need) * 100)}%` }} />
              </div>
              <div className="mt-2 font-q3 text-[0.75rem] tracking-[0.2em] text-dim">Level {level} / {need}</div>
            </>
          ) : (
            <>
              <div className="mt-5 font-q3 text-[1.4rem] tracking-[0.1em] text-ink">Unranked</div>
              <div className="mt-1 text-[0.95rem] text-dim">0 / 10 placement matches</div>
            </>
          )}
        </div>
        <div className="flex flex-col gap-3">
          <SectionTitle>Queues</SectionTitle>
          {queues.map(([n, t]) => (
            <div key={n} className="flex items-center gap-6 rounded-sm border border-white/[0.07] bg-black/25 p-5">
              <div className="flex-1">
                <div className="font-q3 text-[1.3rem] tracking-[0.06em] text-ink-hot">{n}</div>
                <div className="text-[0.95rem] text-dim">{t}</div>
              </div>
              <Button refuse={locked ? lockReason : NEEDS_STEAM} onRefuse={refused}>Queue</Button>
            </div>
          ))}
        </div>
      </div>
    </PagePanel>
  );
}

export function ServerBrowserPage({ back }: PageProps) {
  const modes = useMenu((s) => s.init?.modes ?? []);
  const [source, setSource] = useState<Value>("internet");
  const [mode, setMode] = useState<Value>("any");
  const [full, setFull] = useState(true);
  const [empty, setEmpty] = useState(false);
  const [ping, setPing] = useState(120);
  const cols = ["Server", "Map", "Mode", "Players", "Ping"];
  return (
    <PagePanel title="Server Browser" subtitle="Dedicated and player-hosted servers from the Steam master list and your LAN." back={back}>
      <div className="flex h-full flex-col gap-4 px-10 py-5">
        <div className="grid grid-cols-[14rem_1fr_14rem] items-end gap-4">
          <Spinner wide value={source} options={[["internet", "Internet"], ["lan", "LAN"], ["favorites", "Favorites"], ["history", "History"]]} onChange={setSource} />
          <TextField wide value="" onCommit={() => undefined} placeholder="Search servers and maps" />
          <Spinner wide value={mode} options={[["any", "Any mode"], ...modes.map((m) => [m.id, m.name] as [Value, string])]} onChange={setMode} />
        </div>
        <div className="flex items-center gap-8">
          <label className="flex items-center gap-3 text-[0.95rem] text-dim">Hide full <Toggle value={full} onChange={setFull} /></label>
          <label className="flex items-center gap-3 text-[0.95rem] text-dim">Hide empty <Toggle value={empty} onChange={setEmpty} /></label>
          <div className="flex flex-1 items-center gap-3 text-[0.95rem] text-dim">Max ping <Slider value={ping} min={20} max={400} step={10} onChange={setPing} format={(v) => `${v} ms`} /></div>
        </div>
        <div className="flex min-h-0 flex-1 flex-col overflow-hidden rounded-sm border border-white/[0.07]">
          <div className="grid grid-cols-[1fr_12rem_10rem_7rem_6rem] bg-white/[0.04] px-4 py-2 font-q3 text-[0.7rem] tracking-[0.2em] text-dim">
            {cols.map((c) => <span key={c}>{c}</span>)}
          </div>
          <div className="grid flex-1 place-items-center p-6">
            <EmptyState title="No servers yet" text={`The server list comes from Steam's master server and LAN discovery. ${NEEDS_STEAM}`} />
          </div>
        </div>
        <div data-row className="flex gap-3">
          <Button small refuse={NEEDS_STEAM} onRefuse={refused}>Refresh</Button>
          <Button small refuse="Connecting needs the netcode, which isn't built yet." onRefuse={refused}>Connect to address</Button>
        </div>
      </div>
    </PagePanel>
  );
}

const TRAINING: { id: string; name: string; text: string; setup: Partial<MatchSetup> }[] = [
  { id: "movement", name: "Movement", text: "Strafe jumping, circle jumps and rocket jumps on Introduction, alone.", setup: { mode: "ffa", map: "q3dm0", bots: [], modifiers: { item_set: "no_items" } } },
  { id: "aim", name: "Railgun aim", text: "Instagib against three moving bots on The Longest Yard.", setup: { mode: "instagib", map: "q3dm17", bots: [{ name: "Orbb", skill: 2, team: "" }, { name: "Crash", skill: 2, team: "" }, { name: "Klesk", skill: 2, team: "" }] } },
  { id: "duel", name: "Duel practice", text: "Ten minutes against a Hardcore Sarge on Powerstation 0218.", setup: { mode: "duel", map: "q3tourney1", bots: [{ name: "Sarge", skill: 4, team: "" }] } },
  { id: "items", name: "Item timing", text: "Learn the armor and mega health cycles on The Campgrounds, timers on.", setup: { mode: "ffa", map: "q3dm6", bots: [{ name: "Doom", skill: 3, team: "" }] } },
];

export function TrainingPage({ back }: PageProps) {
  const maps = useMenu((s) => s.init?.maps ?? []);
  return (
    <PagePanel title="Training" subtitle="Practice sessions built from the same match setup as every other game." back={back}>
      <div data-grid="2" className="grid h-full grid-cols-2 gap-4 overflow-y-auto px-10 py-6">
        {TRAINING.map((t, i) => {
          const shot = maps.find((m) => m.map === t.setup.map)?.levelshot;
          return (
            <motion.button
              key={t.id}
              data-nav
              initial={{ opacity: 0, y: 12 }}
              animate={{ opacity: 1, y: 0 }}
              transition={{ delay: i * 0.06 }}
              onClick={() => {
                sfx.move();
                void send("start", { setup: { rules: {}, modifiers: {}, server_type: "offline", source: "training", training: t.id, ...t.setup } });
              }}
              className="group relative min-h-[12rem] overflow-hidden rounded-sm border-2 border-transparent text-left focus:border-glow/70"
            >
              {/* eslint-disable-next-line @next/next/no-img-element */}
              {shot && <img src={`/q3/${shot}`} alt="" className="absolute inset-0 h-full w-full object-cover opacity-60 transition-all duration-500 group-focus:scale-105 group-focus:opacity-90" />}
              <div className="absolute inset-0 bg-gradient-to-r from-black/90 via-black/60 to-transparent" />
              <div className="relative p-6">
                <div className="font-q3 text-[1.6rem] tracking-[0.05em] text-ink-hot">{t.name}</div>
                <p className="mt-2 max-w-[22rem] text-[0.95rem] text-ink/80">{t.text}</p>
              </div>
            </motion.button>
          );
        })}
      </div>
    </PagePanel>
  );
}
