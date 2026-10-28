"use client";
// Everything around the menu: top bar, bottom bar (news, hints, version), the showcase caption,
// toasts, the screen shading, and the stand-in backdrop for the browser preview.
import { useEffect, useState } from "react";
import { AnimatePresence, motion } from "motion/react";
import { useMenu } from "@/lib/store";
import { send } from "@/lib/bridge";
import { sfx } from "@/lib/sound";
import { cn } from "@/lib/cn";
import { FriendsButton, OnlinePill, PartyStrip, Q3Name } from "./Social";
import { Banner } from "./PlayerCard";
import { accentOf, art, fmt, part, TIER_COLORS } from "@/lib/cards";
import RankIcon from "./RankIcon";

const NEWS = [
  ["Remaster", "29 characters rebuilt with real skeletons, 60 fps animation and ragdolls."],
  ["Library", "1,415 world materials converted to matte PBR, every Quake 3 shader kept."],
  ["Coming next", "The map importer: Quake 3's .bsp and .map files into the built-in editor."],
  ["Multiplayer", "Friends and parties are in. Player-hosted matches, a match browser and ranked duels come next."],
];

/** The top bar. `go` opens a page as the main menu does (your card: Profile; your rank: Ranked). */
export function TopBar({ go }: { go: (page: string) => void }) {
  const name = useMenu((s) => String(s.values["profile/name"] ?? "Player"));
  const model = useMenu((s) => String(s.values["profile/model"] ?? "sarge"));
  const color = useMenu((s) => String(s.values["profile/team_color"] ?? "#db2410"));
  const cat = useMenu((s) => s.init?.cards);
  const profile = useMenu((s) => s.init?.profile);
  // your card in miniature: its emblem, accent and title, and your level
  const card = profile?.shown;
  const emblem = card && part(cat, "emblems", card.emblem)?.art;
  const accent = card ? accentOf(cat, card.accent) : color;
  const title = card ? part(cat, "titles", card.title)?.name : undefined;
  const rank = profile?.ranked?.placed ? profile.ranked.rank : undefined;
  return (
    <motion.div
      initial={{ opacity: 0, y: -12 }}
      animate={{ opacity: 1, y: 0 }}
      transition={{ delay: 0.25, duration: 0.5 }}
      className="absolute right-[4.5rem] top-7 flex items-center gap-5"
    >
      <DiscordButton />
      <OnlinePill />
      <PartyStrip />
      <button
        onClick={() => go("ranked")}
        title="Ranked"
        className="flex items-center gap-2 rounded-sm px-1 font-q3 text-[0.7rem] tracking-[0.2em] text-dim transition-colors hover:text-ink"
      >
        <RankIcon tier={rank ? rank.id : undefined} division={rank?.division} className="h-7 w-6" />
        {rank ? (
          <span className="leading-tight">
            <span className="block" style={{ color: TIER_COLORS[rank.id] }}>{rank.label}</span>
            <span className="block font-mono text-[0.6rem] tracking-normal">{fmt(profile!.ranked!.rating)} MMR</span>
          </span>
        ) : (
          "Unranked"
        )}
      </button>
      <FriendsButton />
      <button
        onClick={() => go("profile")}
        title="Profile: your card, character and stats"
        className="relative flex items-center gap-3 overflow-hidden rounded-sm border border-white/10 bg-black/45 py-1 pl-1 pr-3 text-left transition-colors hover:border-white/30"
      >
        {card && cat && (
          <span className="pointer-events-none absolute inset-0 opacity-35">
            <Banner p={part(cat, "banners", card.banner)} cat={cat} ctx={profile?.banner_ctx} className="h-full w-full text-[0.4rem]" />
          </span>
        )}
        {/* eslint-disable-next-line @next/next/no-img-element */}
        <img src={emblem ? art(emblem) : `/q3/icons/${model}.png`} alt="" className="relative h-10 w-10 rounded-sm border-b-2 object-cover" style={{ borderColor: accent }} />
        <div className="relative leading-tight">
          <Q3Name name={name} className="block font-q3 text-[1rem] tracking-wider text-ink-hot" />
          <div className="font-q3 text-[0.6rem] tracking-[0.18em]" style={{ color: title ? accent : undefined }}>{title ?? model}</div>
        </div>
        {profile && (
          <div className="relative ml-1 grid h-8 w-8 place-items-center rounded-sm border bg-black/60 font-q3 text-[0.85rem] text-ink-hot" style={{ borderColor: accent }} title={`Level ${profile.level}`}>
            {profile.level}
          </div>
        )}
      </button>
    </motion.div>
  );
}

// The community server; Godot opens it in the player's browser (open_url allows discord.gg).
const DISCORD = "https://discord.gg/HbrAUzkEbJ";

function DiscordButton() {
  return (
    <button
      onClick={() => {
        sfx.move();
        void send("open_url", { url: DISCORD });
      }}
      title="Join us on Discord"
      className="group flex h-9 items-center gap-2 rounded-full border border-[#5865f2]/50 bg-[#5865f2]/15 px-3.5 text-ink transition-colors hover:border-[#5865f2] hover:bg-[#5865f2]/35 hover:text-ink-hot"
    >
      <svg viewBox="0 0 24 24" className="h-4 w-4 fill-current text-[#8b93ff] transition-colors group-hover:text-ink-hot">
        <path d="M20.3 4.4A19.6 19.6 0 0 0 15.4 3l-.6 1.3a18.2 18.2 0 0 0-5.5 0L8.6 3a19.6 19.6 0 0 0-4.9 1.5C.6 9.1-.3 13.7.2 18.2a19.8 19.8 0 0 0 6 3l1.3-2.1a12.8 12.8 0 0 1-2-1l.5-.4a14.1 14.1 0 0 0 12 0l.5.4c-.6.4-1.3.7-2 1l1.3 2.1a19.7 19.7 0 0 0 6-3c.5-5.2-.9-9.8-3.5-13.8ZM8.3 15.4c-1.2 0-2.1-1.1-2.1-2.4s.9-2.4 2.1-2.4 2.2 1.1 2.1 2.4c0 1.3-.9 2.4-2.1 2.4Zm7.4 0c-1.2 0-2.1-1.1-2.1-2.4s.9-2.4 2.1-2.4 2.2 1.1 2.1 2.4c0 1.3-.9 2.4-2.1 2.4Z" />
      </svg>
      <span className="font-q3 text-[0.7rem] tracking-[0.15em]">Join us on Discord</span>
    </button>
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
