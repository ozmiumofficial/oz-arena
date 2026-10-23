"use client";
// Everything around the menu: top bar, bottom bar (news, hints, version), the showcase caption,
// toasts, the screen shading, and the stand-in backdrop for the browser preview.
import { useEffect, useState } from "react";
import { AnimatePresence, motion } from "motion/react";
import { useMenu } from "@/lib/store";
import { cn } from "@/lib/cn";
import { FriendsButton, OnlinePill, PartyStrip } from "./Social";

const NEWS = [
  ["Remaster", "29 characters rebuilt with real skeletons, 60 fps animation and ragdolls."],
  ["Library", "1,415 world materials converted to matte PBR, every Quake 3 shader kept."],
  ["Coming next", "The map importer: Quake 3's .bsp and .map files into the built-in editor."],
  ["Multiplayer", "Friends and parties are in. Player-hosted matches, a match browser and ranked duels come next."],
];

export function TopBar() {
  const name = useMenu((s) => String(s.values["profile/name"] ?? "Player"));
  const model = useMenu((s) => String(s.values["profile/model"] ?? "sarge"));
  const color = useMenu((s) => String(s.values["profile/team_color"] ?? "#db2410"));
  return (
    <motion.div
      initial={{ opacity: 0, y: -12 }}
      animate={{ opacity: 1, y: 0 }}
      transition={{ delay: 0.25, duration: 0.5 }}
      className="absolute right-[4.5rem] top-7 flex items-center gap-5"
    >
      <OnlinePill />
      <PartyStrip />
      <div className="font-q3 text-[0.7rem] tracking-[0.2em] text-dim">Unranked</div>
      <FriendsButton />
      <div className="relative grid h-9 w-9 place-items-center rounded-full border border-white/10 bg-black/45 text-dim" title="Notifications">
        <svg viewBox="0 0 24 24" className="h-4 w-4 fill-none stroke-current stroke-2"><path d="M6 8a6 6 0 1 1 12 0c0 7 3 9 3 9H3s3-2 3-9M10 21h4" /></svg>
      </div>
      <div className="flex items-center gap-3 rounded-sm border border-white/10 bg-black/45 py-1 pl-1 pr-4">
        {/* eslint-disable-next-line @next/next/no-img-element */}
        <img src={`/q3/icons/${model}.png`} alt="" className="h-10 w-10 rounded-sm border-b-2 object-cover" style={{ borderColor: color }} />
        <div className="leading-tight">
          <div className="font-q3 text-[1rem] tracking-wider text-ink-hot">{name}</div>
          <div className="text-[0.75rem] capitalize text-dim">{model}</div>
        </div>
      </div>
    </motion.div>
  );
}

export function BottomBar({ hints }: { hints: [string, string][] }) {
  const version = useMenu((s) => s.init?.version ?? "");
  const pad = useMenu((s) => s.pad);
  const [n, setN] = useState(0);
  useEffect(() => {
    const t = setInterval(() => setN((x) => x + 1), 7000);
    return () => clearInterval(t);
  }, []);
  const item = NEWS[n % NEWS.length];
  return (
    <div className="absolute bottom-6 left-[5rem] right-[4.5rem] flex items-center justify-between gap-8">
      <div className="flex min-w-0 items-center gap-3">
        <span className="rounded-[2px] bg-red px-2 py-0.5 font-q3 text-[0.65rem] tracking-[0.2em] text-ink-hot">News</span>
        <AnimatePresence mode="wait">
          <motion.div
            key={n}
            initial={{ opacity: 0, y: 8 }}
            animate={{ opacity: 1, y: 0 }}
            exit={{ opacity: 0, y: -8 }}
            transition={{ duration: 0.35 }}
            className="truncate text-[0.95rem] text-dim"
          >
            <span className="mr-2 font-semibold text-ink">{item[0]}</span>
            {item[1]}
          </motion.div>
        </AnimatePresence>
      </div>
      <div className="flex shrink-0 items-center gap-5">
        {hints.map(([k, label]) => (
          <span key={label} className="flex items-center gap-2 font-q3 text-[0.65rem] tracking-[0.18em] text-dim">
            <kbd className="min-w-[1.6rem] rounded-sm border border-white/20 bg-white/5 px-1.5 py-0.5 text-center text-[0.65rem] text-ink">{pad ? padGlyph(k) : k}</kbd>
            {label}
          </span>
        ))}
        <span className="font-q3 text-[0.6rem] tracking-[0.2em] text-faint">v{version}</span>
      </div>
    </div>
  );
}

function padGlyph(k: string) {
  return ({ Enter: "A", Esc: "B", "Q/E": "LB/RB", Arrows: "D-pad", F: "Y" } as Record<string, string>)[k] ?? k;
}

// "SARGE / Character" in the lower right, like the caption on a Skyrim loading screen.
export function SubjectCaption({ hidden }: { hidden: boolean }) {
  const subject = useMenu((s) => s.subject);
  return (
    <div className={cn("pointer-events-none absolute bottom-[5.5rem] right-[4.5rem] text-right transition-opacity duration-500", hidden ? "opacity-0" : "opacity-100")}>
      <AnimatePresence mode="wait">
        {subject && (
          <motion.div key={subject.name} initial={{ opacity: 0, x: 20 }} animate={{ opacity: 1, x: 0 }} exit={{ opacity: 0, x: -10 }} transition={{ duration: 0.6, ease: [0.2, 0.9, 0.1, 1] }}>
            <div className="font-q3 text-[0.7rem] tracking-[0.35em] text-dim">{subject.kind}</div>
            <div className="font-q3 text-[2.6rem] leading-none tracking-[0.08em] text-ink-hot [text-shadow:0_0.2rem_1.5rem_rgb(0_0_0/0.9)]">{subject.name}</div>
            <motion.div className="ml-auto mt-2 h-[2px] bg-gradient-to-l from-glow to-transparent" initial={{ width: 0 }} animate={{ width: "12rem" }} transition={{ delay: 0.2, duration: 0.8 }} />
          </motion.div>
        )}
      </AnimatePresence>
    </div>
  );
}

export function Toasts() {
  const toasts = useMenu((s) => s.toasts);
  return (
    <div className="pointer-events-none absolute right-[4.5rem] top-24 z-[60] flex w-[26rem] flex-col gap-2">
      <AnimatePresence>
        {toasts.map((t) => (
          <motion.div
            key={t.id}
            layout
            initial={{ opacity: 0, x: 40 }}
            animate={{ opacity: 1, x: 0 }}
            exit={{ opacity: 0, x: 40 }}
            className={cn(
              "rounded-sm border-l-[3px] bg-[#0d0a09]/95 px-4 py-3 text-[0.95rem] shadow-[0_0.6rem_2rem_rgb(0_0_0/0.6)]",
              t.tone === "error" ? "border-glow text-ink-hot" : t.tone === "warn" ? "border-amber text-ink" : "border-white/40 text-ink",
            )}
          >
            {t.text}
          </motion.div>
        ))}
      </AnimatePresence>
    </div>
  );
}

// Shading over Godot's 3D showcase: dark behind the menu column, a vignette, and a dim when a page is open.
export function Shade({ dim }: { dim: number }) {
  return (
    <>
      <div className="pointer-events-none absolute inset-0 bg-[linear-gradient(90deg,rgb(0_0_0/0.88)_0%,rgb(0_0_0/0.62)_22%,rgb(0_0_0/0.18)_42%,transparent_58%)]" />
      <div className="pointer-events-none absolute inset-0 bg-[radial-gradient(ellipse_at_62%_50%,transparent_45%,rgb(0_0_0/0.65)_100%)]" />
      <div className="pointer-events-none absolute inset-0 bg-black transition-opacity duration-500" style={{ opacity: dim }} />
    </>
  );
}

// Browser preview only: something smoky where Godot's showcase would be.
export function DevBackdrop() {
  return (
    <div className="absolute inset-0 overflow-hidden bg-[#050404]">
      <div className="absolute left-[58%] top-[18%] h-[38rem] w-[38rem] rounded-full bg-[radial-gradient(circle,rgb(219_36_16/0.28),transparent_65%)] blur-2xl" />
      <motion.div
        className="absolute left-[45%] top-[30%] h-[30rem] w-[50rem] rounded-full bg-[radial-gradient(ellipse,rgb(120_110_105/0.16),transparent_70%)] blur-3xl"
        animate={{ x: [0, 60, 0], y: [0, -30, 0] }}
        transition={{ duration: 18, repeat: Infinity, ease: "easeInOut" }}
      />
      <div className="absolute left-[62%] top-[46%] -translate-x-1/2 font-q3 text-[0.8rem] tracking-[0.4em] text-white/15">3D showcase renders here in Godot</div>
    </div>
  );
}
