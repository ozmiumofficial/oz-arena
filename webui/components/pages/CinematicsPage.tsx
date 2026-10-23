"use client";
// Quake 3's Cinematics menu (q3_ui ui_cinematics.c): the id logo and intro are always there, each
// tier movie unlocks when the campaign tier before it is won, the ending after the final duel.
// Godot plays them full screen (Q3CinematicPlayer); this page only picks one.
import { motion } from "motion/react";
import { useMenu } from "@/lib/store";
import { send } from "@/lib/bridge";
import { sfx } from "@/lib/sound";
import { cn } from "@/lib/cn";
import PagePanel from "../PagePanel";
import type { PageProps } from "./index";

function lockText(unlock: string) {
  if (unlock === "final") return "Win the final duel";
  if (unlock.startsWith("tier:")) {
    const n = Number(unlock.split(":")[1]);
    return n === 1 ? "Finish Training" : `Finish Tier ${n - 1}`;
  }
  return "";
}

function mmss(s: number) {
  return `${Math.floor(s / 60)}:${String(Math.round(s % 60)).padStart(2, "0")}`;
}

export default function CinematicsPage({ back }: PageProps) {
  const movies = useMenu((s) => s.init?.cinematics ?? []);
  return (
    <PagePanel title="Cinematics" subtitle="Quake III Arena's movies, remastered to 4:3 at 1280x960. Tier movies unlock as you win the campaign." back={back}>
      <div data-grid="4" className="grid grid-cols-4 gap-4 overflow-y-auto px-10 py-6">
        {movies.map((c, i) => (
          <motion.button
            key={c.id}
            data-nav
            initial={{ opacity: 0, y: 12 }}
            animate={{ opacity: 1, y: 0 }}
            transition={{ delay: i * 0.04 }}
            onClick={() => {
              if (!c.unlocked) return sfx.buzz();
              sfx.move();
              void send("cinematic", { id: c.id });
            }}
            className="group relative overflow-hidden rounded-sm border-2 border-transparent text-left transition-all focus:border-glow/80 focus:shadow-[0_0_1.4rem_rgb(219_36_16/0.45)]"
          >
            <div className="relative aspect-[4/3] overflow-hidden bg-black">
              {/* eslint-disable-next-line @next/next/no-img-element */}
              <img src={`/q3/cinematics/${c.id}.jpg`} alt="" className={cn("h-full w-full object-cover transition-transform duration-500 group-focus:scale-110", !c.unlocked && "brightness-[0.25] blur-[2px] grayscale")} />
              {c.unlocked ? (
                <div className="absolute inset-0 grid place-items-center opacity-0 transition-opacity group-focus:opacity-100">
                  <div className="grid h-14 w-14 place-items-center rounded-full border-2 border-ink-hot/80 bg-black/50">
                    <svg viewBox="0 0 10 10" className="ml-1 h-5 w-5 fill-ink-hot"><path d="M1 0 9 5 1 10z" /></svg>
                  </div>
                </div>
              ) : (
                <div className="absolute inset-0 grid place-items-center text-center">
                  <div>
                    <svg viewBox="0 0 24 24" className="mx-auto h-8 w-8 fill-none stroke-dim stroke-2"><rect x="5" y="11" width="14" height="10" rx="1" /><path d="M8 11V7a4 4 0 1 1 8 0v4" /></svg>
                    <div className="mt-2 font-q3 text-[0.65rem] tracking-[0.15em] text-dim">{lockText(c.unlock)}</div>
                  </div>
                </div>
              )}
              <div className="absolute bottom-0 left-0 right-0 bg-gradient-to-t from-black/90 to-transparent px-3 pb-2 pt-6">
                <div className="flex items-end justify-between">
                  <span className="font-q3 text-[1.05rem] tracking-[0.05em] text-ink-hot">{c.title}</span>
                  <span className="font-q3 text-[0.65rem] tracking-[0.15em] text-dim">{mmss(c.seconds)}</span>
                </div>
              </div>
            </div>
          </motion.button>
        ))}
        {!movies.length && <p className="col-span-4 py-16 text-center text-dim">No cinematics converted yet: run python tools/make_cinematics.py.</p>}
      </div>
    </PagePanel>
  );
}
