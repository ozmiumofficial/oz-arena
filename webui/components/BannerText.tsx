"use client";
// Quake 3's red banner font (font2_prop), drawn as CSS sprites from the 4x atlas make_ui.py
// builds. Capitals only, like UI_DrawBannerString.
import { useMenu } from "@/lib/store";
import { cn } from "@/lib/cn";

const ATLAS = "/q3/fonts/q3_banner.png";
const ATLAS_W = 2048;

export default function BannerText({ text, height = 2.5, tracking = 0, className }: { text: string; height?: number; tracking?: number; className?: string }) {
  const f = useMenu((s) => s.init?.banner);
  if (!f) return <span className={cn("font-q3 text-red", className)} style={{ fontSize: `${height}rem` }}>{text}</span>;
  const k = height / f.size; // rem per atlas pixel
  const m = f.margin * k;
  return (
    <span className={cn("inline-flex items-start whitespace-nowrap", className)} style={{ height: `${height}rem` }} aria-label={text}>
      {[...text.toUpperCase()].map((ch, i) => {
        const g = f.glyphs[String(ch.charCodeAt(0))];
        if (!g) return <span key={i} style={{ width: `${(f.space + tracking / k) * k}rem` }} />;
        return (
          <span
            key={i}
            className="shrink-0"
            style={{
              width: `${g[2] * k}rem`,
              height: `${g[3] * k}rem`,
              marginTop: `${-m}rem`,
              marginLeft: i === 0 ? `${-m}rem` : 0,
              marginRight: `${(f.gap - 2 * f.margin) * k + tracking}rem`,
              backgroundImage: `url(${ATLAS})`,
              backgroundPosition: `${-g[0] * k}rem ${-g[1] * k}rem`,
              backgroundSize: `${ATLAS_W * k}rem auto`,
              filter: "drop-shadow(0 0.15rem 0.35rem rgb(0 0 0 / 0.8))",
            }}
          />
        );
      })}
    </span>
  );
}
