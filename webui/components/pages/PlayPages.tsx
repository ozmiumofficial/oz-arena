"use client";
// Quick play and the Server browser are the real pages, waiting on online matches (PLAN.md
// "Multiplayer and online services"): their lists are empty and their search buttons buzz (menu4)
// until the netcode and the hub's match directory are in. Training starts real setups.
import { useEffect, useState } from "react";
import { motion } from "motion/react";
import { useMenu, toast, setValue, netBrowse, netJoin, netCancel, queueJoin } from "@/lib/store";
import { send } from "@/lib/bridge";
import { sfx } from "@/lib/sound";
import { cn } from "@/lib/cn";
import { shotSrc } from "@/lib/shot";
import type { MatchSetup, NetMatch, Value } from "@/lib/types";
import PagePanel, { EmptyState, SectionTitle } from "../PagePanel";
import QueueBar from "../QueueBar";
import Button from "../controls/Button";
import Spinner from "../controls/Spinner";
import Toggle from "../controls/Toggle";
import TextField from "../controls/TextField";
import type { PageProps } from "./index";

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

const QUEUE_MODES = ["duel", "ffa", "tdm", "ctf"];

export function QuickPlayPage({ back }: PageProps) {
  const allModes = useMenu((s) => s.init?.modes);
  const modes = (allModes ?? []).filter((m) => QUEUE_MODES.includes(m.id));
  const region = useMenu((s) => s.init?.schema.find((e) => e.key === "network/region")?.options?.find((o) => o[0] === s.values["network/region"])?.[1] ?? "Automatic");
  const queue = useMenu((s) => s.queue.state);
  const online = useMenu((s) => s.social.status === "online");
  const [on, setOn] = useState<Set<string>>(new Set(["ffa", "tdm", "ctf"]));
  const busy = queue.status === "queued" || queue.status === "found";
  return (
    <PagePanel title="Quick Play" subtitle="Pick the playlists you're in the mood for; matchmaking finds the closest game." back={back}>
      <div className="flex h-full flex-col px-10 py-6">
        <SectionTitle>Playlists</SectionTitle>
        <div data-grid="4" className="grid grid-cols-4 gap-3">
          {modes.map((m) => (
            <PlaylistCard key={m.id} name={m.name} text={m.description.split(".")[0] + "."} on={on.has(m.id)} toggle={() => setOn((s) => { const n = new Set(s); if (n.has(m.id)) n.delete(m.id); else n.add(m.id); return n; })} />
          ))}
        </div>
        <p className="mt-3 text-[0.85rem] text-faint">A match starts when enough players are queued for the same playlist: 2 for a duel, 4 for free for all and team deathmatch, 6 for capture the flag. Matches are hosted by the best connected player, through a relay.</p>
        <div className="mt-auto flex flex-col gap-3">
          <QueueBar />
          <div className="flex items-end justify-between">
            <div className="text-[0.95rem] text-dim">Region: <span className="text-ink">{region}</span> <span className="text-faint">(Settings, Network)</span></div>
            <Button
              primary
              refuse={busy ? "Already searching." : !online ? "The online service isn't connected." : on.size === 0 ? "Pick a playlist." : undefined}
              onRefuse={refused}
              onClick={() => queueJoin([...on], false)}
              className="px-14 py-4 text-[1.4rem]"
            >
              Find match
            </Button>
          </div>
        </div>
      </div>
    </PagePanel>
  );
}

export function ServerBrowserPage({ back }: PageProps) {
  const modes = useMenu((s) => s.init?.modes ?? []);
  const maps = useMenu((s) => s.init?.maps ?? []);
  const regions = useMenu((s) => s.init?.schema.find((e) => e.key === "network/region")?.options ?? []);
  const region = useMenu((s) => s.values["network/region"] ?? "auto");
  const net = useMenu((s) => s.net);
  const online = useMenu((s) => s.social.status === "online");
  const me = useMenu((s) => s.social.state?.me.id ?? "");
  const [mode, setMode] = useState<Value>("any");
  const [full, setFull] = useState(true);
  const [empty, setEmpty] = useState(false);
  const [query, setQuery] = useState("");
  const [asking, setAsking] = useState<NetMatch | null>(null);
  const [password, setPassword] = useState("");
  // the directory: when the page opens, and every few seconds while it is open
  useEffect(() => {
    if (!online) return;
    netBrowse();
    const t = setInterval(netBrowse, 8000);
    return () => clearInterval(t);
  }, [online]);
  const q = query.trim().toLowerCase();
  const shown = (net.matches ?? []).filter(
    (m) =>
      m.host !== me &&
      (region === "auto" || m.region === region) &&
      (mode === "any" || m.mode === mode) &&
      !(full && m.players >= m.max) &&
      !(empty && m.players === 0) &&
      (!q || `${m.name} ${m.owner} ${m.map}`.toLowerCase().includes(q)),
  );
  const modeName = (id: string) => modes.find((m) => m.id === id)?.name ?? id;
  const mapName = (id: string) => maps.find((m) => m.map === id)?.name ?? id;
  const regionName = (id: string) => regions.find((r) => r[0] === id)?.[1] ?? id;
  const joining = net.join && (net.join.status === "asking" || net.join.status === "connecting") ? net.join : null;
  const join = (m: NetMatch) => {
    if (joining) return;
    if (m.password && !asking) {
      setAsking(m);
      setPassword("");
      return;
    }
    setAsking(null);
    netJoin(m.host, password);
  };
  return (
    <PagePanel title="Server Browser" subtitle="Player-hosted matches, your region first. Connections go through a relay, so no one sees anyone else's IP." back={back}>
      <div className="flex h-full flex-col gap-4 px-10 py-5">
        <div className="grid grid-cols-[1fr_20rem_14rem] items-end gap-4">
          <TextField wide value={query} onCommit={setQuery} placeholder="Search matches, hosts and maps" />
          <Spinner wide value={region} options={regions.length ? regions.map(([v, t]) => [v, v === "auto" ? "All regions" : t] as [Value, string]) : [["auto", "All regions"]]} onChange={(v) => setValue("network/region", v)} />
          <Spinner wide value={mode} options={[["any", "Any mode"], ...modes.map((m) => [m.id, m.name] as [Value, string])]} onChange={setMode} />
        </div>
        <div className="flex items-center gap-8">
          <label className="flex items-center gap-3 text-[0.95rem] text-dim">Hide full <Toggle value={full} onChange={setFull} /></label>
          <label className="flex items-center gap-3 text-[0.95rem] text-dim">Hide empty <Toggle value={empty} onChange={setEmpty} /></label>
          <span className="text-[0.85rem] text-faint">Ping shows once you are in a match: it is not measured through the relay beforehand.</span>
        </div>
        <div className="flex min-h-0 flex-1 flex-col overflow-hidden rounded-sm border border-white/[0.07]">
          <div className="grid grid-cols-[1fr_12rem_10rem_7rem_10rem] bg-white/[0.04] px-4 py-2 font-q3 text-[0.7rem] tracking-[0.2em] text-dim">
            {["Match", "Map", "Mode", "Players", "Region"].map((c) => <span key={c}>{c}</span>)}
          </div>
          <div data-list className="flex-1 overflow-y-auto">
            {!online ? (
              <div className="grid h-full place-items-center p-6"><EmptyState title="Offline" text="The online service is not connected, so there is no list to show. Check Settings, Network." /></div>
            ) : net.matches === null ? (
              <div className="grid h-full place-items-center p-6 text-dim">Looking for matches...</div>
            ) : shown.length === 0 ? (
              <div className="grid h-full place-items-center p-6"><EmptyState title="No matches" text={net.matches.length ? "Nothing matches these filters. Loosen them, or pick All regions." : "No one is hosting right now. Host one from Create Match (Online) and it lists here."} /></div>
            ) : (
              shown.map((m) => (
                <button
                  key={m.host}
                  data-nav
                  onClick={() => { sfx.move(); join(m); }}
                  className="grid w-full grid-cols-[1fr_12rem_10rem_7rem_10rem] items-center border-b border-white/[0.05] px-4 py-2.5 text-left text-[0.95rem] focus:bg-red/15 focus:outline-none"
                >
                  <span className="truncate text-ink-hot">{m.name}{m.password ? " (password)" : ""}{m.ranked ? " (ranked)" : ""}{m.dedicated ? " (dedicated)" : ""} <span className="text-dim">by {m.owner}</span></span>
                  <span className="truncate text-ink">{mapName(m.map)}</span>
                  <span className="truncate text-ink">{modeName(m.mode)}</span>
                  <span className={m.players >= m.max ? "text-dim" : "text-ink"}>{m.players}/{m.max}</span>
                  <span className="truncate text-dim">{regionName(m.region)}</span>
                </button>
              ))
            )}
          </div>
        </div>
        {asking && (
          <div data-row className="flex items-center gap-3">
            <span className="text-[0.95rem] text-dim">Password for {asking.name}:</span>
            <TextField value={password} onCommit={setPassword} max={40} placeholder="Password" />
            <Button small primary onClick={() => join(asking)}>Join</Button>
            <Button small onClick={() => setAsking(null)}>Cancel</Button>
          </div>
        )}
        <div data-row className="flex items-center gap-3">
          <Button small onClick={() => netBrowse()}>Refresh</Button>
          {joining && (
            <>
              <span className="text-[0.95rem] text-ink-hot">{joining.status === "asking" ? "Asking the host..." : "Connecting through the relay..."}</span>
              <Button small onClick={() => netCancel()}>Cancel</Button>
            </>
          )}
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
              {shot && <img src={shotSrc(shot)} alt="" className="absolute inset-0 h-full w-full object-cover opacity-60 transition-all duration-500 group-focus:scale-105 group-focus:opacity-90" />}
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
