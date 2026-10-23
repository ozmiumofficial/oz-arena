"use client";
// Live crosshair preview over a level shot: Quake 3's ten crosshairs (tinted with a CSS mask) or
// vector shapes, at the size it would have at 1080p, flashing the hit color now and then.
// CrosshairGlyph draws one crosshair; the editor's shape tiles use it too.
import { useEffect, useState } from "react";
import { useMenu } from "@/lib/store";
import type { Value } from "@/lib/types";

export interface GlyphProps {
  shape: string;
  size: number;      // 1080p pixels
  color: string;
  thickness: number;
  gap: number;
  dot: boolean;
  outline: boolean;
}

// The settings' values as glyph props; `shape` overrides the main shape (per-weapon tabs).
export function glyphFrom(v: Record<string, Value>, shape?: string, hit = false): GlyphProps {
  return {
    shape: shape ?? String(v["crosshair/shape"] ?? "c"),
    size: Number(v["crosshair/size"] ?? 24),
    color: hit ? String(v["crosshair/hit_color"]) : String(v["crosshair/color"] ?? "#ffffff"),
    thickness: Number(v["crosshair/thickness"] ?? 2),
    gap: Number(v["crosshair/gap"] ?? 4),
    dot: !!v["crosshair/dot"],
    outline: !!v["crosshair/outline"],
  };
}

// Quake 3's own crosshairs are a-j (gfx/2d/crosshaira-j); every other id is a vector shape.
export const isQ3Shape = (s: string) => /^[a-j]$/.test(s);

export function CrosshairGlyph({ shape, size, color, thickness: th, gap, dot, outline }: GlyphProps) {
  const px = (n: number) => `${n / 16}rem`; // 1080p pixels -> rem
  const shadow = outline ? "drop-shadow(0 0 1px #000) drop-shadow(0 0 1px #000)" : "none";
  if (shape === "none") return null;
  return (
    <div className="relative grid place-items-center" style={{ filter: shadow }}>
      {isQ3Shape(shape) ? (
        <div
          style={{
            width: px(size),
            height: px(size),
            background: color,
            WebkitMaskImage: `url(/q3/crosshairs/crosshair_${shape}.png)`,
            maskImage: `url(/q3/crosshairs/crosshair_${shape}.png)`,
            WebkitMaskSize: "100% 100%",
            maskSize: "100% 100%",
            maskMode: "luminance",
          }}
        />
      ) : (
        <svg width={px(size)} height={px(size)} viewBox={`${-size / 2} ${-size / 2} ${size} ${size}`} style={{ overflow: "visible" }}>
          {shape === "cross" &&
            [0, 90, 180, 270].map((a) => <rect key={a} x={gap} y={-th / 2} width={Math.max(size / 2 - gap, 0)} height={th} fill={color} transform={`rotate(${a})`} />)}
          {shape === "t" &&
            [0, 90, 180].map((a) => <rect key={a} x={gap} y={-th / 2} width={Math.max(size / 2 - gap, 0)} height={th} fill={color} transform={`rotate(${a})`} />)}
          {shape === "circle" && <circle r={size / 2 - th} fill="none" stroke={color} strokeWidth={th} />}
          {shape === "dot" && <circle r={Math.max(1.5, th) * 1.6} fill={color} />}
        </svg>
      )}
      {dot && shape !== "dot" && <div className="absolute rounded-full" style={{ width: px(Math.max(th, 2) * 2), height: px(Math.max(th, 2) * 2), background: color }} />}
    </div>
  );
}

export default function Crosshair({ shape }: { shape?: string }) {
  const v = useMenu((s) => s.values);
  const [hit, setHit] = useState(false);
  useEffect(() => {
    const t = setInterval(() => {
      setHit(true);
      setTimeout(() => setHit(false), 180);
    }, 2200);
    return () => clearInterval(t);
  }, []);
  return (
    <div className="relative aspect-square w-full overflow-hidden rounded-sm border border-white/10">
      {/* eslint-disable-next-line @next/next/no-img-element */}
      <img src="/q3/levelshots/q3dm17.jpg" alt="" className="absolute inset-0 h-full w-full scale-[2.2] object-cover brightness-75" />
      <div className="absolute inset-0 grid place-items-center">
        <CrosshairGlyph {...glyphFrom(v, shape, hit)} />
      </div>
      <div className="absolute bottom-2 left-3 font-q3 text-[0.6rem] tracking-[0.25em] text-white/50">Preview</div>
    </div>
  );
}
