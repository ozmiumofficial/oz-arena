"use client";
// Credits: the remaster plus id Software's credits from ui_credits.c.
import { useRef, useState } from "react";
import { motion } from "motion/react";
import PagePanel from "../PagePanel";
import type { PageProps } from "./index";

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
