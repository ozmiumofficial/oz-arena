"use client";
// Dedicated Server (MULTIPLAYER_PLAN.md M7): run an always-on server from this PC. It is this same game run again,
// headless, so it keeps going after the game closes; here you set it up, start and stop it, watch who is in it, and
// send it console commands. It lists itself in everyone's Server Browser under its own identity.
import { useEffect, useState } from "react";
import { useMenu } from "@/lib/store";
import { send } from "@/lib/bridge";
import type { DedicatedConfig } from "@/lib/types";
import PagePanel, { SectionTitle } from "../PagePanel";
import Button from "../controls/Button";
import Slider from "../controls/Slider";
import Spinner from "../controls/Spinner";
import TextField from "../controls/TextField";
import Toggle from "../controls/Toggle";
import type { PageProps } from "./index";
import type { Value } from "@/lib/types";

const FALLBACK: DedicatedConfig = {
  name: "Dedicated server", password: "", mode: "ffa", rotation: [], max_players: 16, bots: 0, bot_skill: 3, motd: "", rcon_password: "", online: true, idle_sleep: true,
};

function clock(s: number) {
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  return h ? `${h} h ${m} min` : `${m} min ${s % 60} s`;
}

export default function DedicatedPage({ back }: PageProps) {
  const modes = useMenu((s) => s.init?.modes ?? []);
  const maps = useMenu((s) => s.init?.maps ?? []);
  const info = useMenu((s) => s.dedicated);
  const skills = useMenu((s) => s.init?.skills ?? []);
  const online = useMenu((s) => s.social.status === "online");
  const [cfg, setCfg] = useState<DedicatedConfig | null>(null);
  const [line, setLine] = useState("");
  // the page asks Godot for the server's state now and every couple of seconds while it is open
  useEffect(() => {
    void send("dedicated_get", {});
    const t = setInterval(() => void send("dedicated_get", {}), 2000);
    return () => clearInterval(t);
  }, []);
  useEffect(() => {
    if (!cfg && info) setCfg(info.config);
  }, [cfg, info]);
  const c = cfg ?? FALLBACK;
  const status = info?.status;
  const running = !!status?.running;
  const patch = (p: Partial<DedicatedConfig>) => setCfg({ ...c, ...p });
  const modeMaps = maps.filter((m) => modes.find((x) => x.id === c.mode)?.map_types.some((t) => m.types.includes(t)));
  return (
    <PagePanel title="Dedicated Server" subtitle="An always-on server run from this PC. It keeps running after you close the game, and lists itself in the Server Browser." back={back}>
      <div className="grid h-full grid-cols-[1fr_24rem] gap-8 px-10 py-5">
        <div className="flex min-h-0 flex-col gap-2 overflow-y-auto pr-2">
          <SectionTitle>Server</SectionTitle>
          <Field label="Name"><TextField wide max={40} value={c.name} onCommit={(v) => patch({ name: v })} /></Field>
          <Field label="Password"><TextField wide max={40} value={c.password} placeholder="None" onCommit={(v) => patch({ password: v })} /></Field>
          <Field label="Mode">
            <Spinner wide value={c.mode} options={modes.map((m) => [m.id, m.name] as [Value, string])} onChange={(v) => patch({ mode: String(v), rotation: [] })} />
          </Field>
          <Field label="Maps">
            <Spinner
              wide
              value={c.rotation[0] ?? "all"}
              options={[["all", `Every ${modes.find((m) => m.id === c.mode)?.name ?? ""} map in turn`], ...modeMaps.map((m) => [m.map, `${m.name} only`] as [Value, string])]}
              onChange={(v) => patch({ rotation: v === "all" ? [] : [String(v)] })}
            />
          </Field>
          <Field label={`Max players: ${c.max_players}`}><Slider value={c.max_players} min={2} max={32} step={1} onChange={(v) => patch({ max_players: v })} /></Field>
          <Field label={`Fill with bots up to: ${c.bots === 0 ? "off" : c.bots}`}><Slider value={c.bots} min={0} max={16} step={1} onChange={(v) => patch({ bots: v })} /></Field>
          {c.bots > 0 && (
            <Field label="Bot skill"><Spinner wide value={c.bot_skill} options={skills.map((s, k) => [k + 1, `${k + 1} ${s}`] as [Value, string])} onChange={(v) => patch({ bot_skill: Number(v) })} /></Field>
          )}
          <Field label="Message of the day"><TextField wide max={60} value={c.motd} onCommit={(v) => patch({ motd: v })} /></Field>
          <Field label="Remote console password" tip="Players who know it can run console commands with rcon. Leave empty for none."><TextField wide max={40} value={c.rcon_password} placeholder="None" onCommit={(v) => patch({ rcon_password: v })} /></Field>
          <div className="mt-2 flex items-center gap-8">
            <label className="flex items-center gap-3 text-[0.95rem] text-dim">Sleep when empty <Toggle value={c.idle_sleep} onChange={(v) => patch({ idle_sleep: v })} /></label>
            <label className="flex items-center gap-3 text-[0.95rem] text-dim">List online <Toggle value={c.online} onChange={(v) => patch({ online: v })} /></label>
          </div>
        </div>
        <div className="flex min-h-0 flex-col gap-3">
          <SectionTitle>Status</SectionTitle>
          <div className="rounded-sm border border-white/10 bg-black/30 p-4">
            <div className="flex items-center gap-3">
              <span className={`h-3 w-3 rounded-full ${running ? (status?.sleeping ? "bg-amber-400" : "bg-green-500") : "bg-white/25"}`} />
              <span className="font-q3 tracking-[0.1em] text-ink-hot">{running ? (status?.sleeping ? "Running, asleep (empty)" : "Running") : "Stopped"}</span>
            </div>
            {running && (
              <div className="mt-3 flex flex-col gap-1 text-[0.9rem] text-dim">
                <div>{status?.name} - {status?.mode} on {maps.find((m) => m.map === status?.map)?.name ?? status?.map}</div>
                <div>{status?.humans ?? 0} players of {status?.max}; {status?.listed ? "listed in the Server Browser" : "not listed (offline or disabled)"}</div>
                <div>Up {clock(status?.uptime ?? 0)}</div>
              </div>
            )}
            {status?.error && <div className="mt-2 text-[0.85rem] text-red">{status.error}</div>}
          </div>
          <div data-row className="flex gap-3">
            <Button
              primary
              refuse={running ? "It is already running." : c.online && !online ? "The online service isn't connected: untick List online, or reconnect." : undefined}
              onRefuse={() => undefined}
              onClick={() => void send("dedicated_start", { config: c })}
            >
              Start
            </Button>
            <Button refuse={!running ? "It isn't running." : undefined} onClick={() => void send("dedicated_stop", {})}>Stop</Button>
          </div>
          {running && (status?.players?.length ?? 0) > 0 && (
            <div className="min-h-0 overflow-y-auto rounded-sm border border-white/[0.07] bg-black/25 p-3 text-[0.9rem]">
              {status!.players!.map((p) => (
                <div key={p.client} className="flex justify-between py-0.5">
                  <span className="text-ink">{p.name}{p.bot ? " (bot)" : ""}</span>
                  <button data-nav className="text-dim hover:text-ink-hot focus:text-ink-hot" onClick={() => void send("dedicated_command", { line: `clientkick ${p.client}` })}>kick</button>
                </div>
              ))}
            </div>
          )}
          <SectionTitle>Console</SectionTitle>
          <div data-row className="flex gap-2">
            <TextField wide max={120} value={line} placeholder="map q3dm6, say hello, kick name ..." onCommit={setLine} />
            <Button small refuse={!running ? "It isn't running." : undefined} onClick={() => { void send("dedicated_command", { line }); setLine(""); }}>Run</Button>
          </div>
          <label className="mt-2 flex items-center gap-3 text-[0.9rem] text-dim">
            Start with Windows
            <Toggle value={!!info?.autostart} onChange={(v) => void send("dedicated_autostart", { on: v })} />
          </label>
          <p className="text-[0.8rem] text-faint">Settings apply when you press Start. Start with Windows adds an entry to this PC's sign-in list that runs the server; turn it off to remove it.</p>
        </div>
      </div>
    </PagePanel>
  );
}

function Field({ label, tip, children }: { label: string; tip?: string; children: React.ReactNode }) {
  return (
    <div className="mb-1" title={tip}>
      <div className="mb-1 text-[0.9rem] font-medium text-ink/80">{label}</div>
      {children}
    </div>
  );
}
