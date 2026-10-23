"use client";
import { useEffect, useRef } from "react";
import { sfx, tick } from "@/lib/sound";
import { cn } from "@/lib/cn";

// A slider: drag, click, or Left/Right by one step (Shift: ten). Pushing past an end buzzes,
// like q3_ui's Slider_Key.
export default function Slider({ value, min, max, step, onChange, format, wide }: {
  value: number; min: number; max: number; step: number; onChange: (v: number) => void; format?: (v: number) => string; wide?: boolean;
}) {
  const ref = useRef<HTMLDivElement>(null);
  const track = useRef<HTMLDivElement>(null);
  const decimals = Math.max(0, (String(step).split(".")[1] ?? "").length);
  const snap = (v: number) => Math.min(max, Math.max(min, Number((Math.round((v - min) / step) * step + min).toFixed(decimals))));
  const t = max > min ? (value - min) / (max - min) : 0;

  const stepBy = (d: number, big = false) => {
    const next = snap(value + d * step * (big ? 10 : 1));
    if (next === value) return sfx.buzz();
    tick();
    onChange(next);
  };
  useEffect(() => {
    const el = ref.current!;
    const h = (e: Event) => stepBy((e as CustomEvent).detail);
    el.addEventListener("nav-lr", h);
    return () => el.removeEventListener("nav-lr", h);
  });
  const fromPointer = (x: number) => {
    const r = track.current!.getBoundingClientRect();
    const v = snap(min + ((x - r.left) / r.width) * (max - min));
    if (v !== value) {
      tick();
      onChange(v);
    }
  };
  return (
    <div
      ref={ref}
      role="slider"
      aria-valuenow={value}
      tabIndex={0}
      data-nav
      data-lr
      className={cn("group flex items-center gap-4 rounded-sm px-1 py-1.5 focus:bg-white/5", wide ? "w-full" : "w-[22rem]")}
      onPointerDown={(e) => {
        (e.target as HTMLElement).setPointerCapture(e.pointerId);
        fromPointer(e.clientX);
      }}
      onPointerMove={(e) => e.buttons & 1 && fromPointer(e.clientX)}
    >
      <div ref={track} className="relative h-1.5 flex-1 cursor-pointer rounded-full bg-white/10">
        <div className="absolute inset-y-0 left-0 rounded-full bg-gradient-to-r from-blood to-red group-focus:to-glow" style={{ width: `${t * 100}%` }} />
        <div
          className="absolute top-1/2 h-4 w-4 -translate-x-1/2 -translate-y-1/2 rounded-full border-2 border-ink bg-[#1a1412] shadow-[0_0_0.6rem_rgb(255_74_28/0.0)] transition-shadow group-focus:border-ink-hot group-focus:shadow-[0_0_0.8rem_rgb(255_74_28/0.9)]"
          style={{ left: `${t * 100}%` }}
        />
      </div>
      <span className="w-20 text-right font-q3 text-sm tabular-nums tracking-wide text-ink">{format ? format(value) : value}</span>
    </div>
  );
}
