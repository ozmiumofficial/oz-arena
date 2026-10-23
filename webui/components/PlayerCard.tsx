"use client";
// The player card (PLAN.md "The card, Quake 3 style"): banner across the top, the emblem
// overlapping it, the frame around everything, name in Quake 3 colors, title, level and XP bar,
// three showcase stats, three medal badges and the ID strip. Sizes are in em, so the caller sets
// the scale with font-size. Animated banners and frames stop under Reduced flashing (.reduced).
// Godot draws the same card natively for the scoreboard and results (scripts/ui/q3_card_view.gd).
import { cn } from "@/lib/cn";
import { accentOf, art, fmt, iconArt, part, rankTier, TIER_COLORS } from "@/lib/cards";
import type { CardData, CardPart, CardsCatalog, ProfileState, RankedState } from "@/lib/types";
import { Q3Name } from "./Social";
import RankIcon from "./RankIcon";

export interface CardProps {
  card: CardData;
  cat: CardsCatalog;
  name: string;
  level: number;
  /** XP into the level and XP needed for the next; no bar without it */
  xp?: [number, number];
  /** showcase stat id -> value */
  stats?: Record<string, string>;
  ctx?: ProfileState["banner_ctx"];
  /** LOCAL or OFFICIAL on the ID strip; hidden when unset */
  id?: string;
  /** ranked rating and tier, drawn beside the badges; no rank block without it */
  ranked?: RankedState;
  /** a slot the editor is pointing at, outlined on the card */
  focus?: string;
  className?: string;
  style?: React.CSSProperties;
}

export default function PlayerCard({ card, cat, name, level, xp, stats, ctx, id, ranked, focus, className, style }: CardProps) {
  const accent = accentOf(cat, card.accent);
  const title = part(cat, "titles", card.title)?.name ?? "";
  const emblem = part(cat, "emblems", card.emblem);
  const frame = part(cat, "frames", card.frame);
  const ring = (slot: string) => focus === slot && "outline outline-2 outline-offset-2 outline-amber/90";
  return (
    <div className={cn("relative w-[28em] rounded-[0.3em] transition-[outline] duration-200", cardFrameClass(frame), ring("frame"), className)} style={{ ...frameStyle(frame, accent), ...style }}>
      <div className="relative overflow-hidden rounded-[0.18em] bg-[#0b0908] shadow-[inset_0_0_0_1px_rgb(0_0_0/0.8)]">
        <Banner p={part(cat, "banners", card.banner)} cat={cat} ctx={ctx} className={cn("aspect-[4/1] w-full", focus === "banner" && "outline outline-2 -outline-offset-2 outline-amber/90")} />
        <div className="h-[0.14em]" style={{ background: `linear-gradient(90deg, ${accent}, ${accent}55 60%, transparent)` }} />
        <div
          className={cn("absolute left-[1em] top-[4.1em] h-[5.4em] w-[5.4em] overflow-hidden rounded-[0.2em] border-[0.14em] bg-[radial-gradient(circle_at_50%_35%,#2a211d,#060504)]", ring("emblem"))}
          style={{ borderColor: accent, boxShadow: `0 0 1.2em ${accent}66, 0 0.4em 1em rgb(0 0 0 / 0.8)` }}
        >
          {/* eslint-disable-next-line @next/next/no-img-element */}
          {emblem?.art && <img src={art(emblem.art)} alt="" className="h-full w-full object-cover" />}
        </div>
        <div className="flex min-h-[3.3em] items-start gap-[0.6em] pl-[7.2em] pr-[1em] pt-[0.45em]">
          <div className="min-w-0 flex-1 leading-tight">
            <Q3Name name={name} className="block truncate font-q3 text-[1.4em] tracking-[0.06em] text-ink-hot [text-shadow:0_0.1em_0.4em_rgb(0_0_0/0.9)]" />
            <div className={cn("truncate font-q3 text-[0.7em] tracking-[0.22em]", ring("title"))} style={{ color: lighten(accent) }}>{title}</div>
          </div>
          <LevelBadge level={level} accent={accent} />
        </div>
        {xp && (
          <div className="mx-[1em] mt-[0.55em] flex items-center gap-[0.6em]">
            <div className="relative h-[0.42em] flex-1 overflow-hidden rounded-full bg-white/[0.07]">
              <div className="absolute inset-y-0 left-0 rounded-full transition-[width] duration-700" style={{ width: `${Math.min(100, (xp[0] / Math.max(1, xp[1])) * 100)}%`, background: `linear-gradient(90deg, ${accent}aa, ${accent})`, boxShadow: `0 0 0.6em ${accent}` }} />
            </div>
            <span className="font-mono text-[0.62em] tabular-nums text-dim">{fmt(xp[0])} / {fmt(xp[1])} XP</span>
          </div>
        )}
        <div className={cn("mx-[1em] mt-[0.7em] grid grid-cols-3 gap-[0.4em]", ring("stats"))}>
          {[0, 1, 2].map((i) => {
            const sid = card.stats[i];
            const label = cat.card_stats.find((s) => s.id === sid)?.label;
            return (
              <div key={i} className="rounded-[0.15em] border border-white/[0.06] bg-black/35 px-[0.5em] py-[0.35em] text-center">
                <div className="truncate font-q3 text-[1.15em] leading-tight text-ink-hot">{sid ? (stats?.[sid] ?? "-") : ""}</div>
                <div className="truncate font-q3 text-[0.5em] tracking-[0.18em] text-dim">{label ?? " "}</div>
              </div>
            );
          })}
        </div>
        <div className={cn("mx-[1em] mb-[0.2em] mt-[0.55em] flex items-center gap-[0.5em]", ring("badges"))}>
          {[0, 1, 2].map((i) => (
            <Badge key={i} cat={cat} id={card.badges[i]} />
          ))}
          <div className="ml-auto flex items-center gap-[0.45em] text-right leading-none">
            {ranked && <RankBlock ranked={ranked} />}
            {id && !ranked && <div className={cn("font-q3 text-[0.5em] tracking-[0.25em]", id === "OFFICIAL" ? "text-amber" : "text-faint")}>{id}</div>}
          </div>
        </div>
        <div className="h-[0.6em]" />
      </div>
    </div>
  );
}

// The rank beside the badges: tier icon, tier and division, MMR; placements or Unranked before that
function RankBlock({ ranked }: { ranked: RankedState }) {
  const placed = ranked.placed && ranked.rank?.id;
  return (
    <>
      <div className="leading-tight">
        <div className="font-q3 text-[0.62em] tracking-[0.14em]" style={{ color: placed ? TIER_COLORS[ranked.rank.id] : undefined }}>
          {placed ? ranked.rank.label.toUpperCase() : "UNRANKED"}
        </div>
        <div className="font-mono text-[0.55em] tabular-nums text-dim">
          {placed ? `${fmt(ranked.rating)} MMR` : ranked.open ? `PLACEMENT ${ranked.matches}/${ranked.placements}` : "NOT YET OPEN"}
        </div>
      </div>
      <RankIcon tier={placed ? ranked.rank.id : undefined} division={placed ? ranked.rank.division : 0} className="h-[2.4em] w-[1.9em]" />
    </>
  );
}

function LevelBadge({ level, accent }: { level: number; accent: string }) {
  return (
    <div className="relative grid h-[2.7em] w-[2.7em] shrink-0 place-items-center" title={`Level ${level}`}>
      <svg viewBox="0 0 40 40" className="absolute inset-0 h-full w-full">
        <path d="M20 1 L37 10 L37 30 L20 39 L3 30 L3 10 Z" fill="rgb(0 0 0 / 0.55)" stroke={accent} strokeWidth="2" />
        <path d="M20 5 L33.5 12.3 L33.5 27.7 L20 35 L6.5 27.7 L6.5 12.3 Z" fill="none" stroke="rgb(255 255 255 / 0.08)" strokeWidth="1" />
      </svg>
      <span className="relative font-q3 text-[1.05em] leading-none text-ink-hot">{level}</span>
    </div>
  );
}

export function Badge({ cat, id, size = "2.1em" }: { cat: CardsCatalog; id?: string; size?: string }) {
  const a = id ? cat.achievements.find((x) => x.id === id) : undefined;
  const color = a ? cat.medal_colors[a.medal] : undefined;
  return (
    <div
      className="relative grid shrink-0 place-items-center overflow-hidden rounded-full border-[0.12em] bg-black/40"
      style={{ width: size, height: size, borderColor: color ?? "rgb(255 255 255 / 0.08)", boxShadow: color ? `0 0 0.7em ${color}55` : undefined }}
      title={a ? `${a.name}: ${a.desc}` : "Empty badge slot"}
    >
      {/* eslint-disable-next-line @next/next/no-img-element */}
      {a && <img src={iconArt(cat, a.icon)} alt="" className="h-[82%] w-[82%] object-contain" />}
    </div>
  );
}

// ---------------------------------------------------------------- banners

export function Banner({ p, cat, ctx, className }: { p?: CardPart; cat: CardsCatalog; ctx?: ProfileState["banner_ctx"]; className?: string }) {
  const base = p?.base ? part(cat, "banners", p.base) : undefined;
  const src = art(p?.art ?? base?.art);
  return (
    <div className={cn("relative overflow-hidden bg-[#070605]", className)}>
      {p?.kind === "stat" ? (
        <StatBanner p={p} cat={cat} ctx={ctx} />
      ) : src ? (
        // skies pan slowly on the live card
        // eslint-disable-next-line @next/next/no-img-element
        <img src={src} alt="" className={cn("absolute inset-0 h-full object-cover", p?.fx === "pan" ? "q3-anim q3-pan w-[130%] max-w-none" : "w-full")} />
      ) : (
        <Smoke />
      )}
      {p?.kind === "prestige" && <Prestige id={p.id} />}
      <div className="pointer-events-none absolute inset-0 bg-[linear-gradient(180deg,transparent_45%,rgb(0_0_0/0.55))]" />
    </div>
  );
}

// The Void: the menu showcase's smoke
function Smoke() {
  return (
    <div className="absolute inset-0 bg-[radial-gradient(ellipse_at_70%_60%,#2a1512,#070605_70%)]">
      <div className="q3-anim q3-smoke absolute -left-[10%] top-[10%] h-[140%] w-[60%] rounded-full bg-[radial-gradient(ellipse,rgb(150_135_125/0.22),transparent_65%)] blur-[0.6em]" />
      <div className="q3-anim q3-smoke absolute left-[40%] -top-[30%] h-[150%] w-[70%] rounded-full bg-[radial-gradient(ellipse,rgb(120_100_95/0.18),transparent_65%)] blur-[0.8em] [animation-delay:-6s]" />
      <div className="absolute right-[8%] top-[20%] h-[60%] w-[30%] rounded-full bg-[radial-gradient(ellipse,rgb(219_36_16/0.22),transparent_70%)] blur-[0.8em]" />
    </div>
  );
}

// Prestige banners' animation over their base art
function Prestige({ id }: { id: string }) {
  if (id === "quad_damage") return <div className="q3-anim q3-quad absolute inset-0 bg-[radial-gradient(ellipse_at_50%_50%,rgb(70_110_255/0.7),rgb(40_60_255/0.25)_60%,transparent)] mix-blend-screen" />;
  if (id === "lava_bath")
    return <div className="q3-anim q3-lava absolute inset-x-0 bottom-0 h-full bg-[linear-gradient(180deg,transparent,rgb(255_110_20/0.5)_45%,rgb(255_60_0/0.85)_70%,#3a0800)] mix-blend-screen" />;
  if (id === "longest_yard")
    return (
      <div className="absolute inset-0 overflow-hidden">
        {[0, 1, 2].map((i) => (
          <div key={i} className="q3-anim q3-rail absolute h-[0.18em] w-[70%] rounded-full" style={{ top: `${25 + i * 22}%`, animationDelay: `${-i * 1.3}s`, background: ["linear-gradient(90deg,transparent,#3060ff,#fff)", "linear-gradient(90deg,transparent,#ff3030,#fff)", "linear-gradient(90deg,transparent,#30ff60,#fff)"][i], boxShadow: "0 0 0.5em #fff" }} />
        ))}
      </div>
    );
  if (id === "gibbed")
    return (
      <div className="absolute inset-0 overflow-hidden">
        {Array.from({ length: 10 }, (_, i) => (
          <span key={i} className="q3-anim q3-gib absolute bottom-[10%] left-[50%] h-[0.45em] w-[0.45em] rounded-full bg-[#8a0a04]" style={{ ["--dx" as string]: `${(i - 4.5) * 2.4}em`, ["--dy" as string]: `-${3 + (i % 3)}em`, animationDelay: `${-i * 0.23}s` } as React.CSSProperties} />
        ))}
      </div>
    );
  if (id === "nightmare") return <div className="q3-anim q3-flicker absolute inset-0 bg-[radial-gradient(ellipse_at_50%_40%,transparent_30%,rgb(180_0_0/0.55))] mix-blend-multiply" />;
  return null;
}

// Stat-driven banners, drawn from the career (Q3Profile._banner_ctx)
function StatBanner({ p, cat, ctx }: { p: CardPart; cat: CardsCatalog; ctx?: ProfileState["banner_ctx"] }) {
  const plate = "absolute inset-0 bg-[repeating-linear-gradient(135deg,#1a1412_0_0.5em,#14100e_0.5em_1em)]";
  const label = (t: string, sub?: string) => (
    <div className="absolute bottom-[0.6em] right-[0.9em] text-right leading-tight [text-shadow:0_0.1em_0.4em_#000]">
      <div className="font-q3 text-[0.5em] tracking-[0.3em] text-dim">{t}</div>
      {sub && <div className="font-q3 text-[0.95em] tracking-[0.08em] text-ink-hot">{sub}</div>}
    </div>
  );
  if (p.id === "favorite_arena") {
    const m = ctx?.favorite_map || "q3dm17";
    return (
      <>
        {/* eslint-disable-next-line @next/next/no-img-element */}
        <img src={art(part(cat, "banners", `map_${m}`)?.art)} alt="" className="absolute inset-0 h-full w-full object-cover" />
        {label("FAVORITE ARENA", part(cat, "banners", `map_${m}`)?.name ?? m)}
      </>
    );
  }
  if (p.id === "weapon_mastery") {
    const w = ctx?.best_weapon || "railgun";
    return (
      <>
        <div className={plate} />
        <div className="absolute inset-0 bg-[radial-gradient(ellipse_at_30%_50%,rgb(219_36_16/0.35),transparent_60%)]" />
        {/* eslint-disable-next-line @next/next/no-img-element */}
        <img src={art(part(cat, "emblems", `iconw_${w}`)?.art)} alt="" className="absolute left-[8%] top-[8%] h-[84%] object-contain drop-shadow-[0_0_0.6em_rgb(255_74_28/0.6)]" />
        {label("WEAPON MASTERY", w.toUpperCase())}
      </>
    );
  }
  if (p.id === "trophy_wall") {
    const medals = ctx?.medals.length ? ctx.medals : [{ id: "excellent", n: 0 }, { id: "impressive", n: 0 }, { id: "gauntlet", n: 0 }];
    return (
      <>
        <div className={plate} />
        <div className="absolute inset-y-0 left-[6%] flex items-center gap-[1.2em]">
          {medals.map((m) => (
            <div key={m.id} className="flex items-center gap-[0.3em]">
              {/* eslint-disable-next-line @next/next/no-img-element */}
              <img src={art(part(cat, "emblems", `medal_${m.id}`)?.art)} alt="" className="h-[3.6em] w-[3.6em] object-contain drop-shadow-[0_0_0.5em_rgb(255_200_80/0.4)]" />
              <span className="font-q3 text-[1.1em] text-ink-hot">{fmt(m.n)}</span>
            </div>
          ))}
        </div>
        {label("TROPHY WALL")}
      </>
    );
  }
  if (p.id === "rank_crest") {
    const t = rankTier(cat, ctx?.ranked_peak ?? 0);
    const c = TIER_COLORS[t?.id ?? "bronze"];
    return (
      <>
        <div className="absolute inset-0" style={{ background: `linear-gradient(110deg, #0a0807 30%, ${c}55 55%, #0a0807 56%, #0a0807 62%, ${c}33 63%, #0a0807 80%)` }} />
        <svg viewBox="0 0 40 46" className="absolute left-[6%] top-[12%] h-[76%]">
          <path d="M20 2 L37 9 V24 C37 34 29 41 20 44 C11 41 3 34 3 24 V9 Z" fill="#0a0807" stroke={c} strokeWidth="2.5" />
          <path d="M20 11 L24 19 L33 20 L26 26 L28 35 L20 30 L12 35 L14 26 L7 20 L16 19 Z" fill={c} />
        </svg>
        {label("RANKED PEAK", t?.name ?? "Unranked")}
      </>
    );
  }
  if (p.id === "campaign_map") {
    const won = ctx?.campaign_won ?? 0;
    return (
      <>
        <div className={plate} />
        <div className="absolute inset-y-0 left-[5%] flex items-center gap-[0.35em]">
          {Array.from({ length: 26 }, (_, i) => (
            <span key={i} className="h-[1.6em] w-[0.5em] rounded-[0.1em]" style={{ background: i < won ? "linear-gradient(180deg,#ffad33,#db2410)" : "rgb(255 255 255 / 0.07)", boxShadow: i < won ? "0 0 0.4em rgb(255 120 30 / 0.6)" : undefined }} />
          ))}
        </div>
        {label("CAMPAIGN", `${won} / 26 ARENAS`)}
      </>
    );
  }
  // season: a new look each quarter
  const now = new Date();
  const q = Math.floor(now.getMonth() / 3) + 1;
  const hue = [8, 210, 140, 280][q - 1];
  return (
    <>
      <div className="absolute inset-0" style={{ background: `linear-gradient(120deg, hsl(${hue} 70% 8%), hsl(${hue} 80% 22%) 50%, hsl(${hue} 70% 6%))` }} />
      <div className="absolute inset-0 bg-[repeating-linear-gradient(120deg,transparent_0_1.2em,rgb(255_255_255/0.04)_1.2em_1.3em)]" />
      {label("SEASON", `${now.getFullYear()} Q${q}`)}
    </>
  );
}

// ---------------------------------------------------------------- frames

const CAMPAIGN_FRAME: Record<string, string> = {
  frame_training: "#6b625b", frame_tier1: "#7a5a3a", frame_tier2: "#8a6a3a", frame_tier3: "#9a7a3a", frame_tier4: "#a8562a", frame_tier5: "#b8402a", frame_tier6: "#c02a1a", frame_final: "#e0b040", frame_nightmare: "#d01010",
};
const SHELL: Record<string, string> = { frame_quad: "#4a6bff", frame_battlesuit: "#ffcc33", frame_regen: "#ff3030", frame_haste: "#ff9a30", frame_invis: "#b8d8ff" };

export function frameStyle(f: CardPart | undefined, accent: string): React.CSSProperties {
  const bevel = "inset 0 0.08em 0 rgb(255 255 255 / 0.18), inset 0 -0.1em 0 rgb(0 0 0 / 0.6), 0 0.8em 2.4em rgb(0 0 0 / 0.75)";
  if (!f || f.style === "plain") return { padding: "0.14em", background: `linear-gradient(160deg, ${accent}aa, #2a2320 40%, #141110)`, boxShadow: bevel };
  if (f.art) {
    const glow = SHELL[f.id];
    return {
      padding: "0.42em",
      background: `url(${art(f.art)}) center / 7em repeat`,
      boxShadow: bevel,
      ...(glow ? ({ ["--glow" as string]: glow } as React.CSSProperties) : {}),
    };
  }
  const c = f.style === "ranked" ? TIER_COLORS[f.id.replace("frame_ranked_", "")] : CAMPAIGN_FRAME[f.id];
  return { padding: "0.36em", background: `linear-gradient(160deg, ${c ?? accent}, #1a1412 45%, ${c ?? accent}88 100%)`, boxShadow: bevel, ...(f.fx === "animated" ? ({ ["--glow" as string]: c } as React.CSSProperties) : {}) };
}

// frames with --glow pulse it (PlayerCard adds the class through cardFrameClass)
export function cardFrameClass(f: CardPart | undefined) {
  return f?.fx === "animated" ? "q3-anim q3-card-glow" : "";
}

function lighten(hex: string) {
  return `color-mix(in srgb, ${hex} 70%, white)`;
}
