"use client";
// Profile (real: name, character, accent color; drives the showcase), Workshop (waits on Steam),
// Credits (the remaster plus id Software's credits from ui_credits.c).
import { useEffect, useRef, useState } from "react";
import { motion } from "motion/react";
import { useMenu, setValue, toast } from "@/lib/store";
import { send } from "@/lib/bridge";
import { sfx } from "@/lib/sound";
import { cn } from "@/lib/cn";
import type { Value } from "@/lib/types";
import PagePanel, { EmptyState, SectionTitle } from "../PagePanel";
import TextField from "../controls/TextField";
import Spinner from "../controls/Spinner";
import ColorField from "../controls/ColorField";
import Button from "../controls/Button";
import type { PageProps } from "./index";

export function ProfilePage({ back }: PageProps) {
  const init = useMenu((s) => s.init);
  const v = useMenu((s) => s.values);
  const model = String(v["profile/model"] ?? "sarge");
  // the showcase shows your character while this page is open
  useEffect(() => {
    void send("profile_preview", { model });
  }, [model]);
  useEffect(() => () => void send("profile_preview", { model: "" }), []);
  if (!init) return null;
  const stats = [["Matches", "0"], ["Frags", "0"], ["Accuracy", "-"], ["Favorite weapon", "-"]];
  return (
    <PagePanel title="Profile" subtitle="Your name, character and color. Stats, history and the ladder fill in once you play online." back={back}>
      <div className="grid h-full grid-cols-[1fr_24rem] gap-10 px-10 py-5">
        <div className="flex min-h-0 flex-col">
          <SectionTitle>Name</SectionTitle>
          <TextField wide value={String(v["profile/name"] ?? "")} max={32} onCommit={(s) => setValue("profile/name", s.trim() || "Player")} />
          <SectionTitle>Character</SectionTitle>
          <div className="min-h-0 flex-1 overflow-y-auto pr-2">
            <div data-grid="7" className="grid grid-cols-7 gap-2">
              {init.characters.map((c) => (
                <button
                  key={c}
                  data-nav
                  data-autofocus={c === model ? "" : undefined}
                  onClick={() => {
                    sfx.move();
                    setValue("profile/model", c);
                  }}
                  title={c}
                  className={cn(
                    "group relative aspect-square overflow-hidden rounded-sm border-2 bg-black/40 transition-all",
                    c === model ? "border-glow shadow-[0_0_1rem_rgb(219_36_16/0.6)]" : "border-transparent focus:border-ink/60",
                  )}
                >
                  {/* eslint-disable-next-line @next/next/no-img-element */}
                  <img src={`/q3/icons/${c}.png`} alt="" className="h-full w-full object-cover transition-transform duration-300 group-focus:scale-110" />
                  <span className="absolute inset-x-0 bottom-0 bg-black/70 py-0.5 text-center font-q3 text-[0.6rem] tracking-[0.12em] text-ink">{c}</span>
                </button>
              ))}
            </div>
          </div>
        </div>
        <div className="min-h-0 overflow-y-auto">
          <SectionTitle>Skin</SectionTitle>
          <Spinner
            wide
            value={String(v["profile/skin"] ?? "default")}
            options={[["default", "Default"], ["red", "Red team"], ["blue", "Blue team"]] as [Value, string][]}
            disabled={(s) => (s === "default" ? false : "Team skins aren't converted yet")}
            onChange={(s) => setValue("profile/skin", s)}
          />
          <SectionTitle>Accent color</SectionTitle>
          <ColorField wide value={String(v["profile/team_color"] ?? "#db2410")} onChange={(c) => setValue("profile/team_color", c)} />
          <p className="mt-2 text-[0.85rem] text-dim">Lights the showcase rim and, later, your rail trail.</p>
          <SectionTitle>Stats</SectionTitle>
          <div className="grid grid-cols-2 gap-2">
            {stats.map(([k, n]) => (
              <div key={k} className="rounded-sm border border-white/[0.07] bg-black/30 p-3">
                <div className="font-q3 text-[0.6rem] tracking-[0.2em] text-dim">{k}</div>
                <div className="font-q3 text-[1.5rem] text-ink-hot">{n}</div>
              </div>
            ))}
          </div>
          <SectionTitle>Achievements</SectionTitle>
          <p className="text-[0.9rem] text-dim">Excellent, Impressive, Humiliation and the rest arrive with Steam stats.</p>
        </div>
      </div>
    </PagePanel>
  );
}

export function WorkshopPage({ back }: PageProps) {
  const [kind, setKind] = useState<Value>("maps");
  return (
    <PagePanel title="Workshop" subtitle="Maps, models, skins and mods from Steam Workshop: browse, subscribe, and they download in the background." back={back}>
      <div className="flex h-full flex-col gap-5 px-10 py-6">
        <div className="grid grid-cols-[16rem_1fr] gap-4">
          <Spinner wide value={kind} options={[["maps", "Maps"], ["models", "Models"], ["skins", "Skins"], ["mods", "Mods"]]} onChange={setKind} />
          <TextField wide value="" onCommit={() => undefined} placeholder="Search the Workshop" />
        </div>
        <div className="grid flex-1 place-items-center">
          <EmptyState title="The Workshop needs Steam" text="Subscriptions, ratings and downloads go through Steam Workshop (GodotSteam), which isn't wired yet. Maps you build in the editor will publish from here." />
        </div>
        <div data-row className="flex gap-3">
          <Button small refuse="Needs Steam, which isn't wired yet." onRefuse={(w) => toast(w, "warn")}>Browse</Button>
          <Button small refuse="Needs Steam, which isn't wired yet." onRefuse={(w) => toast(w, "warn")}>Subscribed</Button>
        </div>
      </div>
    </PagePanel>
  );
}

const CREDITS: [string, string[]][] = [
  ["Q3Remaster", ["Ozmium: direction, tools and remaster","Built with Godot Engine (MIT), Blender, godot_wry (MIT), Next.js, React and Motion"]],
  ["Original game", ["Quake III Arena, id Software, 1999"]],
  ["id Software is", []],
  ["Programming", ["John Carmack", "Robert A. Duffy", "Jim Dose'"]],
  ["Art", ["Adrian Carmack", "Kevin Cloud", "Kenneth Scott", "Seneca Menard", "Fred Nilsson"]],
  ["Game Designer", ["Graeme Devine"]],
  ["Level Design", ["Tim Willits", "Christian Antkow", "Paul Jaquays"]],
  ["CEO", ["Todd Hollenshead"]],
  ["Director of Business Development", ["Marty Stratton"]],
  ["Biz Assist and id Mom", ["Donna Jackson"]],
  ["Development Assistance", ["Eric Webb"]],
  ["Code", ["Game logic, movement and bots from the GPL Quake III Arena source (ioquake3)"]],
  ["Fan project", ["All models, textures, sounds and maps are derived from id Software's Quake III Arena and belong to id Software.", "Quake III Arena on Steam is required to play."]],
];

export function CreditsPage({ back }: PageProps) {
  const ref = useRef<HTMLDivElement>(null);
  const [paused, setPaused] = useState(false);
  return (
    <PagePanel title="Credits" back={back}>
      <div ref={ref} className="relative h-full overflow-hidden" onMouseEnter={() => setPaused(true)} onMouseLeave={() => setPaused(false)}>
        <div className="pointer-events-none absolute inset-x-0 top-0 z-10 h-16 bg-gradient-to-b from-[#0a0807] to-transparent" />
        <div className="pointer-events-none absolute inset-x-0 bottom-0 z-10 h-16 bg-gradient-to-t from-[#0a0807] to-transparent" />
        <motion.div
          className="flex flex-col items-center gap-9 px-10 py-[30vh] text-center"
          animate={paused ? undefined : { y: ["0%", "-62%"] }}
          transition={{ duration: 45, ease: "linear", repeat: Infinity }}
        >
          {CREDITS.map(([role, names]) => (
            <div key={role}>
              <div className="font-q3 text-[0.85rem] tracking-[0.3em] text-red">{role}</div>
              {names.map((n) => (
                <div key={n} className="mt-1.5 max-w-[46rem] text-[1.35rem] text-ink-hot">{n}</div>
              ))}
            </div>
          ))}
        </motion.div>
        <button data-nav data-autofocus className="absolute bottom-4 right-6 z-20 font-q3 text-[0.7rem] tracking-[0.2em] text-dim focus:text-ink-hot" onClick={() => setPaused((p) => !p)}>
          {paused ? "Resume" : "Pause"}
        </button>
      </div>
    </PagePanel>
  );
}
