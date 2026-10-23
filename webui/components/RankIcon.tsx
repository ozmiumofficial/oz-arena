"use client";
// Ranked tier icons, Bronze to Godlike (cards.json "ranked_tiers"): a gothic shield that gains
// chevrons, blades, wings and a horned crown as the tiers climb. Pips under the shield count the
// division up (III one pip, I three); Godlike has none. No tier draws the Unranked shield.
import { useId } from "react";
import { TIER_COLORS } from "@/lib/cards";
import { cn } from "@/lib/cn";

const ORDER = ["bronze", "silver", "gold", "platinum", "diamond", "godlike"];
const SHIELD = "M32 6 L54 13 V33 C54 48 44 58 32 65 C20 58 10 48 10 33 V13 Z";

export default function RankIcon({ tier, division = 0, className, title }: { tier?: string; division?: number; className?: string; title?: string }) {
  const id = useId().replace(/:/g, "");
  const n = tier ? ORDER.indexOf(tier) : -1;
  const c = tier ? (TIER_COLORS[tier] ?? "#888") : "#5d554f";
  const pips = division > 0 ? 4 - division : 0;
  return (
    <svg viewBox="0 0 64 80" className={cn("shrink-0 overflow-visible", n === 5 && "q3-anim q3-godlike", className)} role="img" aria-label={title ?? (tier ? `${tier} ${division || ""}` : "Unranked")}>
      <defs>
        <linearGradient id={`m${id}`} x1="0" y1="0" x2="0.4" y2="1">
          <stop offset="0" stopColor={`color-mix(in srgb, ${c} 55%, white)`} />
          <stop offset="0.45" stopColor={c} />
          <stop offset="1" stopColor={`color-mix(in srgb, ${c} 45%, black)`} />
        </linearGradient>
        <radialGradient id={`g${id}`} cx="0.5" cy="0.45" r="0.6">
          <stop offset="0" stopColor={c} stopOpacity={n >= 3 ? 0.55 : 0.3} />
          <stop offset="1" stopColor={c} stopOpacity="0" />
        </radialGradient>
      </defs>
      {n >= 0 && <circle cx="32" cy="36" r="34" fill={`url(#g${id})`} />}
      {/* wings (Diamond, Godlike) and blades (Platinum up) behind the shield */}
      {n >= 4 && (
        <g fill={`url(#m${id})`} stroke="#0a0807" strokeWidth="1.2" strokeLinejoin="round">
          <path d="M12 22 C2 20 -6 30 -8 40 C-2 36 3 37 6 40 C2 44 2 50 4 54 C7 48 10 46 13 46 Z" />
          <path d="M52 22 C62 20 70 30 72 40 C66 36 61 37 58 40 C62 44 62 50 60 54 C57 48 54 46 51 46 Z" />
        </g>
      )}
      {n >= 3 && (
        <g fill={`url(#m${id})`} stroke="#0a0807" strokeWidth="1.2" strokeLinejoin="round">
          <path d="M11 18 L1 12 L7 28 Z" />
          <path d="M53 18 L63 12 L57 28 Z" />
        </g>
      )}
      {/* horned crown (Godlike), spike (Gold and Platinum) */}
      {n === 5 && (
        <g fill={`url(#m${id})`} stroke="#0a0807" strokeWidth="1.2" strokeLinejoin="round">
          <path d="M20 12 C14 4 15 -4 19 -8 C19 -1 22 4 27 8 Z" />
          <path d="M44 12 C50 4 49 -4 45 -8 C45 -1 42 4 37 8 Z" />
          <path d="M26 9 L32 -2 L38 9 Z" />
        </g>
      )}
      {(n === 2 || n === 3 || n === 4) && <path d="M27 9 L32 0 L37 9 Z" fill={`url(#m${id})`} stroke="#0a0807" strokeWidth="1.2" strokeLinejoin="round" />}
      <path d={SHIELD} fill={n >= 0 ? `url(#m${id})` : "#1a1614"} stroke="#0a0807" strokeWidth="2" strokeLinejoin="round" />
      <path d="M32 11 L49 16.5 V33 C49 45 41 53.5 32 59.5 C23 53.5 15 45 15 33 V16.5 Z" fill="rgb(0 0 0 / 0.35)" stroke="rgb(255 255 255 / 0.18)" strokeWidth="1" />
      {n < 0 && <text x="32" y="44" textAnchor="middle" fontSize="22" fontWeight="900" fill="#8d847c" fontFamily="var(--font-q3), sans-serif">?</text>}
      {/* the emblem inside: chevrons for the metals, a star for Platinum, a gem for Diamond, an eye for Godlike */}
      {n >= 0 && n <= 2 && (
        <g fill="none" stroke={`color-mix(in srgb, ${c} 40%, white)`} strokeWidth="4" strokeLinejoin="miter" strokeLinecap="square">
          {Array.from({ length: n + 1 }, (_, i) => (
            <path key={i} d={`M21 ${27 + i * 9 + (2 - n) * 4.5} L32 ${35 + i * 9 + (2 - n) * 4.5} L43 ${27 + i * 9 + (2 - n) * 4.5}`} />
          ))}
        </g>
      )}
      {n === 3 && <path d="M32 21 L35.5 31 L46 31.5 L37.8 38 L40.8 48 L32 42.2 L23.2 48 L26.2 38 L18 31.5 L28.5 31 Z" fill={`color-mix(in srgb, ${c} 35%, white)`} stroke="#0a0807" strokeWidth="1" />}
      {n === 4 && (
        <g stroke="#0a0807" strokeWidth="1" strokeLinejoin="round">
          <path d="M32 20 L44 32 L32 50 L20 32 Z" fill={`color-mix(in srgb, ${c} 40%, white)`} />
          <path d="M20 32 L44 32 M32 20 L27 32 L32 50 L37 32 Z" fill="rgb(255 255 255 / 0.25)" />
        </g>
      )}
      {n === 5 && (
        <g>
          <path d="M18 34 C24 24 40 24 46 34 C40 44 24 44 18 34 Z" fill="#1a0503" stroke="#ffcf6a" strokeWidth="1.5" />
          <circle cx="32" cy="34" r="6" fill="#ffcf6a" />
          <circle cx="32" cy="34" r="2.6" fill="#1a0503" />
          <path d="M24 48 L32 54 L40 48" fill="none" stroke="#ffcf6a" strokeWidth="2.5" />
        </g>
      )}
      {pips > 0 && (
        <g fill={c} stroke="#0a0807" strokeWidth="1">
          {Array.from({ length: pips }, (_, i) => (
            <path key={i} d={`M${32 + (i - (pips - 1) / 2) * 10} 70 l4 4 l-4 4 l-4 -4 Z`} />
          ))}
        </g>
      )}
    </svg>
  );
}
