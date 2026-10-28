"use client";
// Create match: mode, map (MapBrowser: every map, the mode's first), rules, modifiers, bots and
// server type. Builds the match setup Godot validates and hands to the Gameplay scene.
// Offline vs bots is the same thing cut down to four choices.
import { useEffect, useMemo, useState } from "react";
import { motion } from "motion/react";
import { useMenu, toast, takeCreatePreset } from "@/lib/store";
import { send } from "@/lib/bridge";
import { sfx } from "@/lib/sound";
import { cn } from "@/lib/cn";
import { shotSrc } from "@/lib/shot";
import type { BotSlot, MapInfo, MatchSetup, ModeInfo, Value } from "@/lib/types";
import MapBrowser, { fits } from "../MapBrowser";
import PagePanel, { SectionTitle } from "../PagePanel";
import Spinner from "../controls/Spinner";
import Slider from "../controls/Slider";
import Button from "../controls/Button";
import TextField from "../controls/TextField";
import type { PageProps } from "./index";

const SERVER_TYPES: [Value, string][] = [["offline", "Offline"], ["listen", "Online (listed in the Server Browser)"]];

function useSetup(initialMode: string, serverType = "offline") {
  const init = useMenu((s) => s.init);
  const [setup, setSetup] = useState<MatchSetup>({
    mode: initialMode, map: "", rules: {}, modifiers: {}, bots: [], server_type: serverType, source: "menu",
    ...(serverType === "listen" ? { online: { name: "", password: "", ranked: false } } : {}),
  });
  const mode = init?.modes.find((m) => m.id === setup.mode);
  const maps = useMemo(() => (init?.maps ?? []).filter((m) => mode && m.types.some((t) => mode.map_types.includes(t))), [init, mode]);
  // a map that suits the mode, and the map's own bots as a starting lineup
  useEffect(() => {
    if (!maps.length || maps.some((m) => m.map === setup.map)) return;
    const pick = maps.find((m) => m.map === "q3dm17") ?? maps.find((m) => m.map === "q3tourney2") ?? maps[0];
    setSetup((s) => ({ ...s, map: pick.map }));
  }, [maps, setup.map]);
  const patch = (p: Partial<MatchSetup>) => setSetup((s) => ({ ...s, ...p }));
  // a map made for other modes: the first of the page's modes that plays it (Aerowalk -> Duel)
  const pickMap = (m: MapInfo, offered: ModeInfo[]) => {
    if (fits(m, mode)) return patch({ map: m.map });
    const to = offered.find((x) => fits(m, x));
    if (to) setSetup((s) => ({ ...s, mode: to.id, map: m.map, rules: {}, bots: s.bots.map((b) => ({ ...b, team: "" })) }));
  };
  return { init, setup, patch, mode, maps, pickMap };
}

function start(setup: MatchSetup) {
  void send("start", { setup });
}

export default function CreateMatchPage({ back }: PageProps) {
  // in a party, a match is hosted online by default: the party follows its leader in
  const inParty = useMenu((s) => s.social.status === "online" && !!s.social.state?.party);
  const leader = useMenu((s) => !!s.social.state?.party && s.social.state.party.leader === s.social.state.me.id);
  // from Quick Play's "Host one": that mode, online
  const [preset] = useState(takeCreatePreset);
  const { init, setup, patch, mode, pickMap } = useSetup(preset?.mode ?? "ffa", preset || inParty ? "listen" : "offline");
  const errors = useMenu((s) => s.validation);
  useEffect(() => {
    if (setup.map) void send("validate", { setup });
  }, [setup]);
  if (!init) return null;
  const rules = { ...(mode?.rules ?? {}), ...setup.rules };
  const botNames: [Value, string][] = init.bots.map((b) => [b.name, b.name]);
  const team = !!mode?.team;
  const addBot = () => {
    const max = rules.max_players ?? 12;
    if (setup.bots.length + 1 >= max) return sfx.buzz();
    const used = new Set(setup.bots.map((b) => b.name));
    const next = init.bots.find((b) => !used.has(b.name)) ?? init.bots[0];
    const reds = setup.bots.filter((b) => b.team === "red").length;
    const blues = setup.bots.filter((b) => b.team === "blue").length;
    sfx.move();
    patch({ bots: [...setup.bots, { name: next.name, skill: 3, team: team ? (reds <= blues ? "red" : "blue") : "" }] });
  };
  const setBot = (i: number, b: Partial<BotSlot>) => patch({ bots: setup.bots.map((x, k) => (k === i ? { ...x, ...b } : x)) });
  return (
    <PagePanel
      title="Create Match"
      subtitle="Every mode is a rules file; modifiers stack on top. The same setup starts offline games, listen servers and editor play-tests."
      back={back}
      footer={
        <div data-row className="flex w-full items-center gap-4">
          <div className="min-w-0 flex-1 truncate text-[0.95rem] text-amber">{errors[0] ?? ""}</div>
          <Button primary onClick={() => (errors.length ? toast(errors[0], "error") : start({ ...setup, rules: setup.rules }))}>
            Fight
          </Button>
        </div>
      }
    >
      <div className="grid h-full grid-cols-[1fr_27rem] gap-8 px-10 py-5">
        <div className="flex min-h-0 flex-col">
          <SectionTitle>Game mode</SectionTitle>
          <Spinner wide value={setup.mode} options={init.modes.filter((m) => m.id !== "race").map((m) => [m.id, m.name])} onChange={(v) => patch({ mode: String(v), rules: {}, bots: setup.bots.map((b) => ({ ...b, team: "" })) })} />
          <p className="mt-2 min-h-[2.8rem] text-[0.95rem] text-dim">{mode?.description}</p>
          <SectionTitle>Map</SectionTitle>
          <MapBrowser maps={init.maps} mode={mode} modes={init.modes.filter((m) => m.id !== "race")} value={setup.map} onPick={(m) => pickMap(m, init.modes.filter((x) => x.id !== "race"))} cols={3} />
        </div>
        <div className="min-h-0 overflow-y-auto pr-2">
          <SectionTitle>Rules</SectionTitle>
          {Object.entries(rules).map(([k, v]) => {
            const spec = init.rules[k];
            return (
              <Field key={k} label={spec?.label ?? k} tip={spec?.tip}>
                <Slider wide value={v} min={spec?.min ?? 0} max={spec?.max ?? 99} step={1} onChange={(n) => patch({ rules: { ...setup.rules, [k]: n } })} format={(n) => (n === 0 && spec?.min === 0 ? "None" : `${n}${spec?.unit ? " " + spec.unit : ""}`)} />
              </Field>
            );
          })}
          <SectionTitle>Modifiers</SectionTitle>
          {init.modifiers.map((m) => {
            const forced = mode?.modifiers[m.id];
            return (
              <Field key={m.id} label={m.label} tip={forced !== undefined ? `${m.tip} Set by ${mode?.name}.` : m.tip}>
                <Spinner
                  wide
                  value={forced ?? setup.modifiers[m.id] ?? m.default}
                  options={m.options}
                  disabled={() => (forced !== undefined ? `${mode?.name} sets this` : false)}
                  onChange={(v) => patch({ modifiers: { ...setup.modifiers, [m.id]: v } })}
                />
              </Field>
            );
          })}
          <SectionTitle>Bots</SectionTitle>
          {setup.bots.map((b, i) => (
            <motion.div key={i} layout initial={{ opacity: 0, x: 10 }} animate={{ opacity: 1, x: 0 }} className="mb-2 rounded-sm border border-white/[0.06] bg-black/25 p-2">
              <div className="mb-2 flex items-center gap-2">
                {/* eslint-disable-next-line @next/next/no-img-element */}
                <img src={`/q3/icons/${init.bots.find((x) => x.name === b.name)?.model ?? "sarge"}.png`} alt="" className="h-9 w-9 rounded-sm" />
                <div className="flex-1"><Spinner wide value={b.name} options={botNames} onChange={(v) => setBot(i, { name: String(v) })} /></div>
                <button
                  data-nav
                  title="Remove bot"
                  onClick={() => {
                    sfx.move();
                    patch({ bots: setup.bots.filter((_, k) => k !== i) });
                  }}
                  className="grid h-10 w-10 place-items-center rounded-sm border border-white/10 text-dim focus:border-glow/70 focus:text-glow"
                >
                  ✕
                </button>
              </div>
              <div className="flex gap-2">
                <div className="flex-1"><Spinner wide value={b.skill} options={init.skills.map((s, k) => [k + 1, `${k + 1} ${s}`])} onChange={(v) => setBot(i, { skill: Number(v) })} /></div>
                {team && <div className="w-[8.5rem]"><Spinner wide value={b.team} options={[["red", "Red"], ["blue", "Blue"]]} onChange={(v) => setBot(i, { team: v as "red" | "blue" })} /></div>}
              </div>
            </motion.div>
          ))}
          <Button small onClick={addBot} className="w-full">+ Add bot</Button>
          <SectionTitle>Server</SectionTitle>
          <Spinner wide value={setup.server_type} options={SERVER_TYPES} onChange={(v) => patch({ server_type: String(v), online: setup.online ?? { name: "", password: "", ranked: false } })} />
          {setup.server_type === "listen" && (
            <>
              <Field label="Match name" tip="What other players see in the Server Browser.">
                <TextField wide max={40} value={setup.online?.name ?? ""} placeholder="My match" onCommit={(v) => patch({ online: { ...(setup.online ?? { name: "", password: "", ranked: false }), name: v } })} />
              </Field>
              <Field label="Password" tip="Leave empty for an open match.">
                <TextField wide max={40} value={setup.online?.password ?? ""} placeholder="None" onCommit={(v) => patch({ online: { ...(setup.online ?? { name: "", password: "", ranked: false }), password: v } })} />
              </Field>
              <p className="text-[0.85rem] text-faint">You host from your PC; players connect through a relay and never see your IP. The match goes on while the menu is open.</p>
              {inParty && <p className="text-[0.85rem] text-ink">{leader ? "Your party joins you as soon as the match starts." : "Only the party leader brings the party along; your party can join from Friends."}</p>}
            </>
          )}
          <SectionTitle>You</SectionTitle>
          <Spinner wide value={setup.spectate ? "spectate" : "play"} options={[["play", "Play"], ["spectate", "Spectate: watch the bots"]]} onChange={(v) => patch({ spectate: v === "spectate" })} />
        </div>
      </div>
    </PagePanel>
  );
}

function Field({ label, tip, children }: { label: string; tip?: string; children: React.ReactNode }) {
  return (
    <div className="mb-2" title={tip}>
      <div className="mb-1 text-[0.9rem] font-medium text-ink/80">{label}</div>
      {children}
    </div>
  );
}

const QUICK_MODES = ["ffa", "duel", "tdm", "ctf"];

export function OfflineBotsPage({ back }: PageProps) {
  const { init, setup, patch, pickMap } = useSetup("ffa");
  const [count, setCount] = useState(5);
  const [skill, setSkill] = useState(3);
  if (!init) return null;
  const map = init.maps.find((m) => m.map === setup.map);
  const mode = init.modes.find((m) => m.id === setup.mode);
  const quick = init.modes.filter((m) => QUICK_MODES.includes(m.id));
  const go = () => {
    const n = setup.mode === "duel" ? 1 : count;
    const pool = [...(map?.bots ?? []), ...init.bots.map((b) => b.name.toLowerCase())];
    const names: string[] = [];
    for (const p of pool) {
      const b = init.bots.find((x) => x.name.toLowerCase() === p.toLowerCase());
      if (b && !names.includes(b.name)) names.push(b.name);
      if (names.length >= n) break;
    }
    const bots: BotSlot[] = names.map((name, i) => ({ name, skill, team: mode?.team ? (i % 2 ? "blue" : "red") : "" }));
    start({ ...setup, bots });
  };
  return (
    <PagePanel title="Skirmish" subtitle="Pick a mode and an arena; the map's own bots come first, as in the original single-player tiers." back={back}>
      <div className="grid h-full grid-cols-[1fr_26rem] gap-10 px-10 py-6">
        <div className="flex min-h-0 flex-col">
          <MapBrowser maps={init.maps} mode={mode} modes={quick} value={setup.map} onPick={(m) => pickMap(m, quick)} cols={3} />
        </div>
        <div className="flex min-h-0 flex-col">
          <div className="relative aspect-video w-full overflow-hidden rounded-sm border border-white/10">
            {map && (
              <motion.img key={map.map} src={shotSrc(map.levelshot)} alt="" className="absolute inset-0 h-full w-full object-cover" initial={{ opacity: 0, scale: 1.08 }} animate={{ opacity: 1, scale: 1 }} transition={{ duration: 0.6 }} />
            )}
            <div className="absolute inset-0 bg-gradient-to-t from-black/90 via-transparent to-transparent" />
            <div className="absolute bottom-3 left-4 right-4">
              <div className="truncate font-q3 text-[1.6rem] leading-none tracking-[0.04em] text-ink-hot">{map?.name}</div>
              <div className="mt-1 font-q3 text-[0.7rem] tracking-[0.3em] text-dim">{map?.map}</div>
            </div>
          </div>
          <SectionTitle>Mode</SectionTitle>
          <Spinner wide value={setup.mode} options={quick.map((m) => [m.id, m.name])} onChange={(v) => patch({ mode: String(v) })} />
          {setup.mode !== "duel" && (
            <>
              <SectionTitle>Bots</SectionTitle>
              <Slider wide value={count} min={1} max={11} step={1} onChange={setCount} />
            </>
          )}
          <SectionTitle>Skill</SectionTitle>
          <Spinner wide value={skill} options={init.skills.map((s, k) => [k + 1, s])} onChange={(v) => setSkill(Number(v))} />
          <div className="mt-auto pt-6">
            <Button primary onClick={go} className="w-full py-4 text-[1.4rem]">Fight</Button>
          </div>
        </div>
      </div>
    </PagePanel>
  );
}
