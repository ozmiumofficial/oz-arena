"use client";
// Profile (PLAN.md "Player cards, progression and ranked"), four tabs (Q/E switch):
//   Card          the card editor: every slot, every part, locked ones with how to earn them
//   Character     name in Quake 3 colors, character (the showcase turns to it), skin, player color
//   Achievements  all 89 with medals, progress, what's still missing and what each one pays
//   Career        level and XP, the next unlocks, career stats, weapons and medals
// Godot owns the rules: it sends which parts are earned (Q3Profile.menu_payload) and refuses
// anything else; the page draws.
import { useEffect, useMemo, useRef, useState } from "react";
import { AnimatePresence, motion } from "motion/react";
import { useMenu, setValue, setCard, toast } from "@/lib/store";
import { send } from "@/lib/bridge";
import { sfx } from "@/lib/sound";
import { focusFirst } from "@/lib/nav";
import { cn } from "@/lib/cn";
import { ACH_GROUPS, achievementGroup, art, fmt, iconArt, isUnlocked, part, railStyle } from "@/lib/cards";
import type { CardPart, CardsCatalog, CardSlot, InitPayload, PartKind, ProfileState, Value } from "@/lib/types";
import { CARD_SLOTS } from "@/lib/types";
import PagePanel, { Badge as Tag, EmptyState, SectionTitle } from "../PagePanel";
import TextField from "../controls/TextField";
import Spinner from "../controls/Spinner";
import ColorField from "../controls/ColorField";
import Toggle from "../controls/Toggle";
import Button from "../controls/Button";
import PlayerCard, { Badge, Banner, frameStyle } from "../PlayerCard";
import { Q3Name } from "../Social";
import { Q3_COLORS } from "@/lib/social";
import type { PageProps } from "./index";

const TABS: [string, string][] = [["card", "Card"], ["character", "Character"], ["achievements", "Achievements"], ["career", "Career"]];

export default function ProfilePage({ back }: PageProps) {
  const init = useMenu((s) => s.init);
  const v = useMenu((s) => s.values);
  const [tab, setTab] = useState("card");
  const body = useRef<HTMLDivElement>(null);
  const model = String(v["profile/model"] ?? "sarge");
  // the showcase shows your character while this page is open
  useEffect(() => {
    void send("profile_preview", { model });
  }, [model]);
  useEffect(() => () => void send("profile_preview", { model: "" }), []);
  if (!init) return null;
  const cat = init.cards;
  const profile = init.profile;
  const go = (id: string) => {
    if (id === tab) return;
    sfx.move();
    setTab(id);
    requestAnimationFrame(() => requestAnimationFrame(() => focusFirst(body.current)));
  };
  const switchTab = (d: number) => go(TABS[(TABS.findIndex((t) => t[0] === tab) + d + TABS.length) % TABS.length][0]);
  return (
    <PagePanel title="Profile" back={back} onTab={switchTab}>
      <div className="flex h-full flex-col">
        <div className="flex items-center gap-1 px-10 pt-3">
          {TABS.map(([id, label]) => (
            <button key={id} onClick={() => go(id)} className={cn("relative px-3 py-2 font-q3 text-[0.8rem] tracking-[0.12em] transition-colors", id === tab ? "text-ink-hot" : "text-dim hover:text-ink")}>
              {label}
              {id === tab && <motion.span layoutId="profile-tab" className="absolute inset-x-2 -bottom-px h-[2px] bg-glow shadow-[0_0_0.6rem_rgb(255_74_28)]" />}
            </button>
          ))}
          {profile && (
            <div className="ml-auto flex items-center gap-3 font-q3 text-[0.7rem] tracking-[0.18em] text-dim">
              <span>
                LEVEL <span className="text-ink-hot">{profile.level}</span>
              </span>
              <span className="h-3 w-px bg-white/15" />
              <span>
                <span className="text-ink-hot">{profile.achievements.filter((a) => a.done).length}</span> / {profile.achievements.length} ACHIEVEMENTS
              </span>
              <kbd className="ml-2 rounded-sm border border-white/15 px-1.5 py-0.5 text-[0.6rem]">Q</kbd>
              <kbd className="rounded-sm border border-white/15 px-1.5 py-0.5 text-[0.6rem]">E</kbd>
            </div>
          )}
        </div>
        <div className="mx-10 h-px bg-white/[0.06]" />
        <div ref={body} className="relative min-h-0 flex-1">
          {!cat || !profile ? (
            <div className="grid h-full place-items-center px-10">
              <EmptyState title="No card catalog" text="The player card catalog (library/cards) isn't in this build. Run tools/build_cards.py." />
            </div>
          ) : (
            <AnimatePresence mode="wait">
              <motion.div key={tab} className="absolute inset-0" initial={{ opacity: 0, y: 8 }} animate={{ opacity: 1, y: 0 }} exit={{ opacity: 0, y: -6 }} transition={{ duration: 0.18 }}>
                {tab === "card" && <CardTab cat={cat} profile={profile} />}
                {tab === "character" && <CharacterTab init={init} values={v} />}
                {tab === "achievements" && <AchievementsTab init={init} cat={cat} profile={profile} />}
                {tab === "career" && <CareerTab cat={cat} profile={profile} />}
              </motion.div>
            </AnimatePresence>
          )}
        </div>
      </div>
    </PagePanel>
  );
}

// ---------------------------------------------------------------- Card

type EditSlot = CardSlot | "stats" | "badges";
const EDIT_SLOTS: [EditSlot, string][] = [
  ["banner", "Banner"], ["emblem", "Emblem"], ["frame", "Frame"], ["title", "Title"], ["accent", "Accent"], ["rail", "Rail"], ["taunt", "Taunt"], ["stats", "Stats"], ["badges", "Badges"],
];
const SLOT_TIPS: Record<EditSlot, string> = {
  banner: "Wide art across the top of your card. Arena banners come from winning there, characters from beating them in the campaign.",
  emblem: "The portrait on your card. Every default head is free; skins, weapons, items and medals are earned.",
  frame: "The border around your card. The level track pays one every 10 levels; powerup frames pulse.",
  title: "The line under your name.",
  accent: "Your card's trim, glow and XP bar. Quake 3's seven colors are free.",
  rail: "Your rail trail's color in matches. Forced rail colors and competitive settings override it for whoever turns them on.",
  taunt: "The voice line when your card appears on the duel versus screen. Selecting one plays it.",
  stats: "Three career stats shown on your card, in the order you pick them.",
  badges: "Up to three achievement medals shown on your card.",
};

function groupOf(kind: PartKind, p: CardPart) {
  if (kind === "banners") {
    if (p.kind === "stat") return "Career";
    if (p.kind === "prestige") return "Prestige";
    return ({ map: "Arenas", sky: "Skies", char: "Characters", mat: "Materials" } as Record<string, string>)[p.id.split("_")[0]] ?? "Default";
  }
  if (kind === "emblems") return ({ heads: "Characters", skins: "Skins", weapons: "Weapons", items: "Items", medals: "Medals" } as Record<string, string>)[p.group ?? ""] ?? "Other";
  if (kind === "frames") return ({ plain: "Default", material: "Level track", shell: "Powerups", campaign: "Campaign", ranked: "Ranked" } as Record<string, string>)[p.style ?? ""] ?? "Other";
  if (kind === "titles" || kind === "accents" || kind === "rails") return p.free ? "Free" : p.level ? "Level track" : "Earned";
  return "Characters";
}

function CardTab({ cat, profile }: { cat: CardsCatalog; profile: ProfileState }) {
  const name = useMenu((s) => String(s.values["profile/name"] ?? "Player"));
  const [slot, setSlot] = useState<EditSlot>("banner");
  const [locked, setLocked] = useState(true);
  const [hover, setHover] = useState<{ name: string; how: string; desc?: string; locked: boolean } | null>(null);
  // NEW tags stay for this visit; Godot forgets them once their slot has been opened
  const [fresh] = useState(() => new Set(profile.new));
  useEffect(() => {
    if (slot === "stats" || slot === "badges") return;
    const keys = profile.new.filter((k) => k.startsWith(CARD_SLOTS[slot] + ":"));
    if (keys.length) void send("card_seen", { keys });
  }, [slot, profile.new]);
  const grid = useRef<HTMLDivElement>(null);
  const openSlot = (s: EditSlot) => {
    if (s === slot) return;
    sfx.move();
    setSlot(s);
    setHover(null);
    grid.current?.scrollTo({ top: 0 });
  };
  const card = profile.card;
  return (
    <div className="grid h-full grid-cols-[30rem_1fr] gap-8 px-10 py-5">
      <div className="flex min-h-0 flex-col items-center">
        <PlayerCard card={card} cat={cat} name={name} level={profile.level} xp={[profile.xp, profile.next]} stats={profile.stats} ctx={profile.banner_ctx} id="LOCAL" ranked={profile.ranked} focus={slot} className="text-[1rem]" />
        <div className="mt-4 w-full rounded-sm border border-white/[0.06] bg-black/25 p-4">
          <div className="font-q3 text-[0.7rem] tracking-[0.2em] text-red">{EDIT_SLOTS.find((e) => e[0] === slot)?.[1]}</div>
          <p className="mt-1 text-[0.9rem] leading-snug text-dim">{SLOT_TIPS[slot]}</p>
        </div>
        <p className="mt-auto pt-3 text-center text-[0.8rem] leading-snug text-faint">
          Cards are cosmetic only. Everyone who sees yours checks it against your career, so parts you haven't earned show as the default.
        </p>
      </div>
      <div className="flex min-h-0 flex-col">
        <div data-row className="grid grid-cols-9 gap-1.5">
          {EDIT_SLOTS.map(([id, label]) => {
            const kind = id === "stats" || id === "badges" ? null : CARD_SLOTS[id];
            const has = kind ? (profile.unlocked[kind]?.length ?? 0) : 0;
            const total = kind ? cat.parts[kind].length : 0;
            const isNew = kind && [...fresh].some((k) => k.startsWith(kind + ":"));
            return (
              <button
                key={id}
                data-nav
                data-autofocus={id === slot ? "" : undefined}
                onClick={() => openSlot(id)}
                className={cn(
                  "relative min-w-0 rounded-sm border px-2 py-1.5 text-left transition-colors",
                  id === slot ? "border-glow/70 bg-red/20 text-ink-hot" : "border-white/10 bg-white/[0.03] text-dim focus:border-ink/40 focus:text-ink",
                )}
              >
                <div className="truncate font-q3 text-[0.7rem] tracking-[0.1em]">{label}</div>
                <div className="font-mono text-[0.6rem] text-faint">{kind ? `${has}/${total}` : id === "stats" ? `${card.stats.length}/3` : `${card.badges.length}/3`}</div>
                {isNew && <span className="absolute -right-1 -top-1 h-2.5 w-2.5 rounded-full bg-amber shadow-[0_0_0.5rem_#ffad33]" />}
              </button>
            );
          })}
        </div>
        <div className="mt-3 flex items-center gap-4">
          {slot !== "stats" && (
            <>
              <span className="font-q3 text-[0.7rem] tracking-[0.18em] text-dim">Show locked</span>
              <Toggle value={locked} onChange={setLocked} />
            </>
          )}
          <span className="ml-auto truncate text-[0.85rem] text-dim">
            {hover ? (
              <>
                <span className={hover.locked ? "text-dim" : "text-ink-hot"}>{hover.name}</span>
                <span className="mx-2 text-faint">/</span>
                <span className={hover.locked ? "text-amber" : "text-faint"}>{hover.locked ? hover.how : hover.desc || "Unlocked"}</span>
              </>
            ) : (
              <span className="text-faint">Locked parts say how to earn them.</span>
            )}
          </span>
        </div>
        <div ref={grid} className="mt-3 min-h-0 flex-1 overflow-y-auto pr-2">
          {slot === "stats" ? (
            <StatsPicker cat={cat} profile={profile} onHover={setHover} />
          ) : slot === "badges" ? (
            <BadgePicker cat={cat} profile={profile} locked={locked} onHover={setHover} />
          ) : (
            <PartPicker cat={cat} profile={profile} slot={slot} locked={locked} fresh={fresh} onHover={setHover} />
          )}
        </div>
      </div>
    </div>
  );
}

const COLS: Record<PartKind, number> = { banners: 3, emblems: 8, frames: 5, titles: 3, accents: 5, rails: 3, taunts: 7 };

function PartPicker({ cat, profile, slot, locked, fresh, onHover }: {
  cat: CardsCatalog; profile: ProfileState; slot: CardSlot; locked: boolean; fresh: Set<string>;
  onHover: (h: { name: string; how: string; desc?: string; locked: boolean } | null) => void;
}) {
  const kind = CARD_SLOTS[slot];
  const groups = useMemo(() => {
    const out: [string, CardPart[]][] = [];
    const ordered = [...cat.parts[kind]].sort((a, b) => Number(isUnlocked(profile, kind, b.id)) - Number(isUnlocked(profile, kind, a.id)));
    for (const p of kind === "titles" || kind === "accents" || kind === "rails" ? ordered : cat.parts[kind]) {
      if (!locked && !isUnlocked(profile, kind, p.id)) continue;
      const g = groupOf(kind, p);
      const hit = out.find((x) => x[0] === g);
      if (hit) hit[1].push(p);
      else out.push([g, [p]]);
    }
    return out;
  }, [cat, kind, profile, locked]);
  const current = profile.card[slot];
  const pick = (p: CardPart) => {
    if (!isUnlocked(profile, kind, p.id)) {
      sfx.buzz();
      toast(`${p.name}: ${p.how.charAt(0) + p.how.slice(1).toLowerCase()}`, "warn");
      return;
    }
    if (kind === "taunts") void send("taunt", { id: p.id });
    if (p.id === current) return;
    sfx.move();
    setCard(slot, p.id);
  };
  return (
    <div>
      {groups.map(([g, list]) => (
        <div key={g}>
          <SectionTitle>
            {g}
            <span className="ml-2 font-mono text-[0.65rem] tracking-normal text-faint">
              {list.filter((p) => isUnlocked(profile, kind, p.id)).length}/{list.length}
            </span>
          </SectionTitle>
          <div data-grid={COLS[kind]} className="grid gap-2" style={{ gridTemplateColumns: `repeat(${COLS[kind]}, minmax(0, 1fr))` }}>
            {list.map((p) => {
              const open = isUnlocked(profile, kind, p.id);
              return (
                <button
                  key={p.id}
                  data-nav
                  data-autofocus={p.id === current ? "" : undefined}
                  onClick={() => pick(p)}
                  onFocus={() => onHover({ name: p.name, how: p.how, desc: p.desc, locked: !open })}
                  onMouseEnter={() => onHover({ name: p.name, how: p.how, desc: p.desc, locked: !open })}
                  className={cn(
                    "group relative overflow-hidden rounded-sm border-2 bg-black/40 text-left transition-all duration-150",
                    p.id === current ? "border-glow shadow-[0_0_1rem_rgb(219_36_16/0.55)]" : "border-white/[0.06] focus:border-ink/60",
                  )}
                >
                  <PartSwatch kind={kind} p={p} cat={cat} profile={profile} />
                  {!open && (
                    <div className="absolute inset-0 grid place-items-center bg-black/60">
                      <LockIcon />
                    </div>
                  )}
                  {!open && p.level > 0 && <span className="absolute right-1 top-1 rounded-[2px] bg-black/80 px-1 font-mono text-[0.6rem] text-amber">L{p.level}</span>}
                  {fresh.has(`${kind}:${p.id}`) && <span className="absolute left-1 top-1 rounded-[2px] bg-amber px-1 font-q3 text-[0.55rem] tracking-[0.1em] text-black">NEW</span>}
                </button>
              );
            })}
          </div>
        </div>
      ))}
    </div>
  );
}

// One part as its picker tile shows it
function PartSwatch({ kind, p, cat, profile }: { kind: PartKind; p: CardPart; cat: CardsCatalog; profile: ProfileState }) {
  const label = <div className="truncate bg-black/70 px-1.5 py-0.5 font-q3 text-[0.6rem] tracking-[0.1em] text-ink">{p.name}</div>;
  if (kind === "banners")
    return (
      <>
        <Banner p={p} cat={cat} ctx={profile.banner_ctx} className="aspect-[4/1] w-full text-[0.55rem] transition-transform duration-300 group-focus:scale-105" />
        {label}
      </>
    );
  if (kind === "emblems" || kind === "taunts") {
    const src = kind === "emblems" ? art(p.art) : art(part(cat, "emblems", p.id.replace("taunt_", "head_"))?.art) || `/q3/icons/${p.id.replace("taunt_", "")}.png`;
    return (
      <>
        {/* eslint-disable-next-line @next/next/no-img-element */}
        <img src={src} alt="" className="aspect-square w-full object-cover transition-transform duration-300 group-focus:scale-110" />
        {kind === "taunts" ? (
          <div className="flex items-center gap-1 bg-black/70 px-1.5 py-0.5 font-q3 text-[0.55rem] tracking-[0.1em] text-ink">
            <svg viewBox="0 0 24 24" className="h-2.5 w-2.5 shrink-0 fill-current"><path d="M4 9h4l5-4v14l-5-4H4zM16 8.5a5 5 0 0 1 0 7" /></svg>
            <span className="truncate">{p.name}</span>
          </div>
        ) : (
          <div className="truncate bg-black/70 px-1 py-0.5 text-center font-q3 text-[0.5rem] tracking-[0.08em] text-ink">{p.name}</div>
        )}
      </>
    );
  }
  if (kind === "frames")
    return (
      <>
        <div className="p-2">
          <div className="aspect-[3/2] w-full rounded-[0.2rem]" style={{ ...frameStyle(p, "#db2410"), boxShadow: "none" }}>
            <div className="h-full w-full rounded-[0.1rem] bg-[#0b0908]" />
          </div>
        </div>
        {label}
      </>
    );
  if (kind === "titles")
    return (
      <div className="px-3 py-2.5">
        <div className="truncate font-q3 text-[0.85rem] tracking-[0.12em] text-ink-hot">{p.name}</div>
        <div className="truncate text-[0.7rem] text-faint">{p.free ? "Free" : p.desc || p.how.toLowerCase()}</div>
      </div>
    );
  if (kind === "accents")
    return (
      <div className="flex items-center gap-2 px-2 py-2">
        <span className="h-6 w-6 shrink-0 rounded-full border border-white/20" style={{ background: p.value, boxShadow: `0 0 0.7rem ${p.value}88` }} />
        <span className="truncate font-q3 text-[0.65rem] tracking-[0.1em] text-ink">{p.name}</span>
      </div>
    );
  // rails: the core beam with the spiral around it
  return (
    <div className="px-3 py-2.5">
      <div className="relative h-3 overflow-hidden rounded-full bg-black">
        <div className={cn("absolute inset-x-0 top-1/2 h-[3px] -translate-y-1/2", p.fx === "pulse" && "q3-anim q3-rail-pulse")} style={{ ...railStyle(p), boxShadow: `0 0 0.5rem ${p.value?.startsWith("#") ? p.value : "#fff"}` }} />
        <div className="absolute inset-0 opacity-70" style={{ background: `repeating-linear-gradient(110deg, transparent 0 6px, ${p.value?.startsWith("#") ? p.value : "#ffffff"}aa 6px 8px)` }} />
      </div>
      <div className="mt-1.5 truncate font-q3 text-[0.65rem] tracking-[0.1em] text-ink">{p.name}</div>
    </div>
  );
}

function StatsPicker({ cat, profile, onHover }: { cat: CardsCatalog; profile: ProfileState; onHover: (h: { name: string; how: string; desc?: string; locked: boolean } | null) => void }) {
  const chosen = profile.card.stats;
  const toggle = (id: string) => {
    const has = chosen.includes(id);
    if (!has && chosen.length >= 3) {
      sfx.buzz();
      toast("Three stats at most: take one off first.", "warn");
      return;
    }
    sfx.move();
    setCard("stats", has ? chosen.filter((s) => s !== id) : [...chosen, id]);
  };
  return (
    <div data-grid="2" className="grid grid-cols-2 gap-2">
      {cat.card_stats.map((s) => {
        const i = chosen.indexOf(s.id);
        return (
          <button
            key={s.id}
            data-nav
            onClick={() => toggle(s.id)}
            onFocus={() => onHover({ name: s.label, how: "", desc: i >= 0 ? `Slot ${i + 1} on your card` : "Select to show it on your card", locked: false })}
            className={cn("flex items-center gap-3 rounded-sm border-2 bg-black/30 px-3 py-2 text-left transition-colors", i >= 0 ? "border-glow/80 bg-red/10" : "border-white/[0.06] focus:border-ink/50")}
          >
            <span className={cn("grid h-6 w-6 shrink-0 place-items-center rounded-full border font-q3 text-[0.7rem]", i >= 0 ? "border-glow bg-red text-ink-hot" : "border-white/15 text-faint")}>{i >= 0 ? i + 1 : ""}</span>
            <span className="min-w-0 flex-1 truncate font-q3 text-[0.75rem] tracking-[0.15em] text-ink">{s.label}</span>
            <span className="font-q3 text-[1rem] text-ink-hot">{profile.stats[s.id] ?? "-"}</span>
          </button>
        );
      })}
    </div>
  );
}

function BadgePicker({ cat, profile, locked, onHover }: { cat: CardsCatalog; profile: ProfileState; locked: boolean; onHover: (h: { name: string; how: string; desc?: string; locked: boolean } | null) => void }) {
  const chosen = profile.card.badges;
  const done = new Set(profile.achievements.filter((a) => a.done).map((a) => a.id));
  const list = cat.achievements.filter((a) => done.has(a.id) || (locked && !a.hidden));
  const toggle = (id: string) => {
    if (!done.has(id)) {
      sfx.buzz();
      toast("Earn this achievement to wear its medal.", "warn");
      return;
    }
    const has = chosen.includes(id);
    if (!has && chosen.length >= 3) {
      sfx.buzz();
      toast("Three badges at most: take one off first.", "warn");
      return;
    }
    sfx.move();
    setCard("badges", has ? chosen.filter((s) => s !== id) : [...chosen, id]);
  };
  if (!list.length) return <EmptyState title="No medals yet" text="Earn achievements in matches and the campaign, then wear up to three of their medals here." />;
  return (
    <div data-grid="8" className="grid grid-cols-8 gap-2">
      {list.map((a) => {
        const i = chosen.indexOf(a.id);
        const open = done.has(a.id);
        return (
          <button
            key={a.id}
            data-nav
            onClick={() => toggle(a.id)}
            onFocus={() => onHover({ name: a.name, how: a.desc, desc: i >= 0 ? `Badge ${i + 1} on your card` : a.desc, locked: !open })}
            onMouseEnter={() => onHover({ name: a.name, how: a.desc, desc: i >= 0 ? `Badge ${i + 1} on your card` : a.desc, locked: !open })}
            className={cn("relative grid aspect-square place-items-center rounded-sm border-2 bg-black/30 transition-colors", i >= 0 ? "border-glow/80" : "border-white/[0.06] focus:border-ink/50", !open && "opacity-40 grayscale")}
          >
            <Badge cat={cat} id={a.id} size="72%" />
            {i >= 0 && <span className="absolute right-1 top-1 grid h-4 w-4 place-items-center rounded-full bg-red font-q3 text-[0.55rem] text-ink-hot">{i + 1}</span>}
          </button>
        );
      })}
    </div>
  );
}

function LockIcon() {
  return (
    <svg viewBox="0 0 24 24" className="h-5 w-5 fill-none stroke-dim stroke-2">
      <rect x="5" y="11" width="14" height="9" rx="1.5" />
      <path d="M8 11V8a4 4 0 0 1 8 0v3" />
    </svg>
  );
}

// ---------------------------------------------------------------- Character

function CharacterTab({ init, values: v }: { init: InitPayload; values: Record<string, Value> }) {
  const model = String(v["profile/model"] ?? "sarge");
  return (
    <div className="grid h-full grid-cols-[1fr_22rem] gap-10 px-10 py-5">
      <div className="flex min-h-0 flex-col">
        <SectionTitle>Name</SectionTitle>
        <TextField wide value={String(v["profile/name"] ?? "")} max={32} onCommit={(s) => setValue("profile/name", s.trim() || "Player")} />
        {/* Quake 3's color codes: ^ and a digit colors the rest of the name, in matches too */}
        <div className="mt-2 flex items-center gap-4 text-[0.85rem] text-dim">
          <Q3Name name={String(v["profile/name"] ?? "")} className="font-q3 text-[1.1rem] tracking-wider text-ink-hot" />
          <span className="ml-auto flex items-center gap-1.5">
            {Q3_COLORS.map((c, i) => (
              <span key={i} className="rounded-[2px] border border-white/10 bg-black/40 px-1 font-mono text-[0.75rem]" style={{ color: c }}>^{i}</span>
            ))}
          </span>
        </div>
        <SectionTitle>Character</SectionTitle>
        <div className="min-h-0 flex-1 overflow-y-auto pr-2">
          <div data-grid="8" className="grid grid-cols-8 gap-2">
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
        <SectionTitle>Player color</SectionTitle>
        <ColorField wide value={String(v["profile/team_color"] ?? "#db2410")} onChange={(c) => setValue("profile/team_color", c)} />
        <p className="mt-2 text-[0.85rem] leading-snug text-dim">Lights the menu showcase and travels with you online: friends see it in their list. Your card&apos;s own trim is its Accent.</p>
        <SectionTitle>Played as</SectionTitle>
        <p className="text-[0.85rem] leading-snug text-dim">
          25 matches as a character earns their banner, and 10 in an alternate skin earns that head for your emblem.
        </p>
      </div>
    </div>
  );
}

// ---------------------------------------------------------------- Achievements

const SHOW: [Value, string][] = [["all", "All"], ["earned", "Earned"], ["progress", "In progress"], ["todo", "Not started"]];

function AchievementsTab({ init, cat, profile }: { init: InitPayload; cat: CardsCatalog; profile: ProfileState }) {
  const [group, setGroup] = useState("All");
  const [show, setShow] = useState<Value>("all");
  const state = useMemo(() => Object.fromEntries(profile.achievements.map((a) => [a.id, a])), [profile]);
  const groups = ["All", ...ACH_GROUPS.filter((g) => cat.achievements.some((a) => achievementGroup(a) === g))];
  const list = cat.achievements.filter((a) => {
    const s = state[a.id];
    if (group !== "All" && achievementGroup(a) !== group) return false;
    if (show === "earned") return s?.done;
    if (show === "progress") return !s?.done && (s?.progress ?? 0) > 0;
    if (show === "todo") return !s?.done && !(s?.progress ?? 0);
    return true;
  });
  const done = profile.achievements.filter((a) => a.done);
  const medals = (["bronze", "silver", "gold"] as const).map((m) => [m, cat.achievements.filter((a) => a.medal === m && state[a.id]?.done).length, cat.achievements.filter((a) => a.medal === m).length] as const);
  const mapName = (m: string) => init.maps.find((x) => x.map === m)?.name ?? m;
  return (
    <div className="flex h-full flex-col px-10 py-5">
      <div className="flex items-center gap-6">
        <div>
          <div className="font-q3 text-[2rem] leading-none text-ink-hot">
            {done.length}
            <span className="text-[1rem] text-dim"> / {cat.achievements.length}</span>
          </div>
          <div className="mt-1 h-1.5 w-56 overflow-hidden rounded-full bg-white/[0.07]">
            <div className="h-full rounded-full bg-gradient-to-r from-red to-amber" style={{ width: `${(done.length / Math.max(1, cat.achievements.length)) * 100}%` }} />
          </div>
        </div>
        {medals.map(([m, n, of]) => (
          <div key={m} className="flex items-center gap-2">
            <span className="h-4 w-4 rounded-full border-2" style={{ borderColor: cat.medal_colors[m], background: `${cat.medal_colors[m]}33` }} />
            <span className="font-q3 text-[0.75rem] tracking-[0.15em] text-dim">
              <span className="text-ink-hot">{n}</span> / {of} {m}
            </span>
          </div>
        ))}
        <div className="ml-auto w-52">
          <Spinner wide value={show} options={SHOW} onChange={setShow} />
        </div>
      </div>
      <div data-row className="mt-4 flex flex-wrap gap-1.5">
        {groups.map((g) => (
          <button
            key={g}
            data-nav
            onClick={() => {
              if (g === group) return;
              sfx.move();
              setGroup(g);
            }}
            className={cn("rounded-sm border px-2.5 py-1 font-q3 text-[0.65rem] tracking-[0.14em] transition-colors", g === group ? "border-glow/70 bg-red/20 text-ink-hot" : "border-white/10 text-dim focus:border-ink/40 focus:text-ink")}
          >
            {g}
          </button>
        ))}
      </div>
      <div className="mt-4 min-h-0 flex-1 overflow-y-auto pr-2">
        {list.length === 0 ? (
          <EmptyState title="Nothing here yet" text="No achievements match this filter." />
        ) : (
          <div data-grid="2" className="grid grid-cols-2 gap-2">
            {list.map((a) => {
              const s = state[a.id];
              const secret = a.hidden && !s?.done;
              const pct = Math.min(1, (s?.progress ?? 0) / Math.max(1, a.target));
              const pays = (Object.keys(cat.parts) as PartKind[]).flatMap((k) => cat.parts[k].filter((p) => p.ach === a.id).map((p) => ({ k, p })));
              const color = cat.medal_colors[a.medal];
              return (
                <div key={a.id} data-nav tabIndex={-1} className={cn("flex gap-3 rounded-sm border bg-black/30 p-3 transition-colors focus:border-ink/40", s?.done ? "border-white/[0.1]" : "border-white/[0.05]")}>
                  <div
                    className={cn("grid h-14 w-14 shrink-0 place-items-center rounded-full border-2 bg-black/50", !s?.done && "grayscale")}
                    style={{ borderColor: s?.done ? color : "rgb(255 255 255 / 0.1)", boxShadow: s?.done ? `0 0 1rem ${color}55` : undefined }}
                  >
                    {secret ? (
                      <span className="font-q3 text-[1.3rem] text-faint">?</span>
                    ) : (
                      // eslint-disable-next-line @next/next/no-img-element
                      <img src={iconArt(cat, a.icon)} alt="" className={cn("h-11 w-11 object-contain", !s?.done && "opacity-50")} />
                    )}
                  </div>
                  <div className="min-w-0 flex-1">
                    <div className="flex items-center gap-2">
                      <span className={cn("truncate font-q3 text-[0.85rem] tracking-[0.1em]", s?.done ? "text-ink-hot" : "text-ink")}>{secret ? "Hidden achievement" : a.name}</span>
                      <span className="font-q3 text-[0.55rem] tracking-[0.2em]" style={{ color }}>{a.medal}</span>
                      {s?.done && <Tag>Earned</Tag>}
                    </div>
                    <div className="truncate text-[0.8rem] text-dim">{secret ? "Keep playing to find out." : a.desc}</div>
                    {!s?.done && !secret && a.target > 1 && (
                      <div className="mt-1.5 flex items-center gap-2">
                        <div className="h-1 flex-1 overflow-hidden rounded-full bg-white/[0.07]">
                          <div className="h-full rounded-full" style={{ width: `${pct * 100}%`, background: color }} />
                        </div>
                        <span className="font-mono text-[0.65rem] tabular-nums text-faint">
                          {fmt(Math.min(s?.progress ?? 0, a.target))} / {fmt(a.target)}
                        </span>
                      </div>
                    )}
                    {!s?.done && !secret && s?.missing.length > 0 && s.missing.length <= 8 && (
                      <div className="mt-1 truncate text-[0.7rem] text-faint">Still to go: {s.missing.map(mapName).join(", ")}</div>
                    )}
                    {pays.length > 0 && !secret && (
                      <div className="mt-1 truncate text-[0.7rem] text-amber/80">Pays: {[...new Set(pays.map(({ k, p }) => `${p.name} ${k.replace(/s$/, "")}`))].join(", ")}</div>
                    )}
                  </div>
                </div>
              );
            })}
          </div>
        )}
      </div>
    </div>
  );
}

// ---------------------------------------------------------------- Career

const WEAPONS = ["gauntlet", "machinegun", "shotgun", "grenade", "rocket", "lightning", "railgun", "plasma", "bfg"];
const MEDALS = ["excellent", "impressive", "gauntlet", "capture", "assist", "defend", "perfect"];
const MODE_NAMES: Record<string, string> = { ffa: "Free For All", duel: "Tournament", tdm: "Team DM", ctf: "CTF", ca: "Clan Arena", ft: "Freeze Tag", instagib: "Instagib" };

function CareerTab({ cat, profile }: { cat: CardsCatalog; profile: ProfileState }) {
  const c = profile.career;
  const next = useMemo(() => {
    const out: { k: PartKind; p: CardPart }[] = [];
    for (const k of Object.keys(cat.parts) as PartKind[]) for (const p of cat.parts[k]) if (p.level > profile.level) out.push({ k, p });
    return out.sort((a, b) => a.p.level - b.p.level).slice(0, 6);
  }, [cat, profile.level]);
  const topWeapon = Math.max(1, ...WEAPONS.map((w) => c.frags_weapon[w] ?? 0));
  const fav = Object.entries(c.matches_model).sort((a, b) => b[1] - a[1])[0];
  const hours = c.playtime / 3600;
  const rows: [string, string][] = [
    ["Matches", fmt(c.matches)],
    ["Wins", fmt(c.wins)],
    ["Win rate", c.matches ? `${Math.round((c.wins / c.matches) * 100)}%` : "-"],
    ["Frags", fmt(c.frags)],
    ["Deaths", fmt(c.deaths)],
    ["K/D", (c.frags / Math.max(1, c.deaths)).toFixed(2)],
    ["Time played", hours >= 1 ? `${hours.toFixed(1)} h` : `${Math.round(c.playtime / 60)} min`],
    ["Favorite character", fav ? `${fav[0]} (${fav[1]})` : "-"],
  ];
  const ranked = profile.level >= profile.ranked_level;
  return (
    <div className="grid h-full grid-cols-[24rem_1fr] gap-10 overflow-y-auto px-10 py-5">
      <div className="min-h-0">
        <SectionTitle>Level</SectionTitle>
        <div className="flex items-end gap-4">
          <div className="font-q3 text-[4rem] leading-none text-ink-hot [text-shadow:0_0_1.5rem_rgb(219_36_16/0.6)]">{profile.level}</div>
          <div className="pb-2 text-[0.85rem] text-dim">
            of {profile.max_level}
            <br />
            {fmt(profile.total)} XP total
          </div>
        </div>
        <div className="mt-3 h-2 overflow-hidden rounded-full bg-white/[0.07]">
          <motion.div className="h-full rounded-full bg-gradient-to-r from-red to-amber shadow-[0_0_0.8rem_rgb(255_120_30/0.7)]" initial={{ width: 0 }} animate={{ width: `${(profile.xp / Math.max(1, profile.next)) * 100}%` }} transition={{ duration: 0.9, ease: [0.2, 0.9, 0.1, 1] }} />
        </div>
        <div className="mt-1 flex justify-between font-mono text-[0.7rem] text-faint">
          <span>{fmt(profile.xp)} / {fmt(profile.next)}</span>
          <span>{fmt(profile.next - profile.xp)} to level {Math.min(profile.max_level, profile.level + 1)}</span>
        </div>
        <p className="mt-3 text-[0.8rem] leading-snug text-faint">Every match played to the end pays XP: 100 for finishing, 10 a frag, 150 a win, 25 a medal. Matches against bots count.</p>
        <SectionTitle>Next unlocks</SectionTitle>
        {next.length === 0 ? (
          <p className="text-[0.85rem] text-dim">The whole level track is yours.</p>
        ) : (
          <div className="flex flex-col gap-1.5">
            {next.map(({ k, p }) => (
              <div key={k + p.id} className="flex items-center gap-3 rounded-sm border border-white/[0.06] bg-black/30 px-2 py-1.5">
                <span className="w-10 shrink-0 text-center font-q3 text-[0.8rem] text-amber">L{p.level}</span>
                <NextSwatch k={k} p={p} cat={cat} />
                <span className="min-w-0 flex-1 truncate text-[0.85rem] text-ink">{p.name}</span>
                <span className="font-q3 text-[0.55rem] tracking-[0.18em] text-faint">{k.replace(/s$/, "").toUpperCase()}</span>
              </div>
            ))}
          </div>
        )}
        <SectionTitle>Ranked</SectionTitle>
        {ranked ? (
          <p className="text-[0.85rem] text-dim">Ranked is open to you. Every ranked match you finish moves your rating.</p>
        ) : (
          <>
            <p className="text-[0.85rem] text-dim">Ranked opens at level {profile.ranked_level}.</p>
            <div className="mt-2 h-1.5 overflow-hidden rounded-full bg-white/[0.07]">
              <div className="h-full rounded-full bg-dim" style={{ width: `${(profile.level / profile.ranked_level) * 100}%` }} />
            </div>
          </>
        )}
      </div>
      <div className="min-h-0">
        <SectionTitle>Career</SectionTitle>
        <div className="grid grid-cols-4 gap-2">
          {rows.map(([k, n]) => (
            <div key={k} className="rounded-sm border border-white/[0.07] bg-black/30 px-3 py-2.5">
              <div className="truncate font-q3 text-[0.55rem] tracking-[0.2em] text-dim">{k}</div>
              <div className="truncate font-q3 text-[1.3rem] capitalize text-ink-hot">{n}</div>
            </div>
          ))}
        </div>
        <SectionTitle>Weapons</SectionTitle>
        <div className="grid grid-cols-3 gap-x-6 gap-y-2">
          {WEAPONS.map((w) => (
            <div key={w} className="flex items-center gap-2">
              {/* eslint-disable-next-line @next/next/no-img-element */}
              <img src={`/q3/hud/iconw_${w}.png`} alt="" className="h-7 w-7 object-contain" />
              <div className="min-w-0 flex-1">
                <div className="flex justify-between text-[0.75rem]">
                  <span className="capitalize text-dim">{w === "bfg" ? "BFG" : w}</span>
                  <span className="font-mono tabular-nums text-ink">{fmt(c.frags_weapon[w] ?? 0)}</span>
                </div>
                <div className="mt-0.5 h-1 overflow-hidden rounded-full bg-white/[0.07]">
                  <div className="h-full rounded-full bg-gradient-to-r from-blood to-glow" style={{ width: `${((c.frags_weapon[w] ?? 0) / topWeapon) * 100}%` }} />
                </div>
              </div>
            </div>
          ))}
        </div>
        <SectionTitle>Medals</SectionTitle>
        <div className="flex flex-wrap gap-4">
          {MEDALS.map((m) => (
            <div key={m} className="flex items-center gap-2" title={m}>
              {/* eslint-disable-next-line @next/next/no-img-element */}
              <img src={`/q3/hud/medal_${m === "perfect" ? "victory" : m}.png`} alt="" className={cn("h-9 w-9 object-contain", !(c.medal[m] ?? 0) && "opacity-30 grayscale")} />
              <div className="leading-tight">
                <div className="font-q3 text-[1rem] text-ink-hot">{fmt(c.medal[m] ?? 0)}</div>
                <div className="font-q3 text-[0.5rem] tracking-[0.18em] text-dim">{m === "gauntlet" ? "HUMILIATION" : m.toUpperCase()}</div>
              </div>
            </div>
          ))}
        </div>
        <SectionTitle>Wins by mode</SectionTitle>
        <div className="flex flex-wrap gap-2">
          {Object.entries(MODE_NAMES).map(([id, n]) => (
            <div key={id} className="rounded-sm border border-white/[0.07] bg-black/30 px-3 py-1.5 text-[0.8rem] text-dim">
              {n} <span className="ml-1 font-q3 text-ink-hot">{fmt(c.wins_mode[id] ?? 0)}</span>
            </div>
          ))}
        </div>
        <p className="mt-4 text-[0.75rem] text-faint">Frags per weapon, pickups and movement records fill in once match tracking records them.</p>
      </div>
    </div>
  );
}

function NextSwatch({ k, p, cat }: { k: PartKind; p: CardPart; cat: CardsCatalog }) {
  if (k === "banners") return <Banner p={p} cat={cat} className="h-6 w-24 shrink-0 rounded-[2px] text-[0.3rem]" />;
  if (k === "emblems") {
    // eslint-disable-next-line @next/next/no-img-element
    return <img src={art(p.art)} alt="" className="h-6 w-6 shrink-0 rounded-[2px] object-cover" />;
  }
  if (k === "frames") return <span className="h-6 w-6 shrink-0 rounded-[2px]" style={{ ...frameStyle(p, "#db2410"), boxShadow: "none" }} />;
  if (k === "accents") return <span className="h-5 w-5 shrink-0 rounded-full" style={{ background: p.value }} />;
  if (k === "rails") return <span className="h-1 w-6 shrink-0 rounded-full" style={railStyle(p)} />;
  return <span className="w-6 shrink-0 text-center font-q3 text-[0.7rem] text-dim">T</span>;
}
