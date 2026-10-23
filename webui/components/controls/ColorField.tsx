"use client";
import { useRef, useState } from "react";
import { AnimatePresence, motion } from "motion/react";
import { useLayer } from "@/lib/nav";
import { sfx } from "@/lib/sound";
import { cn } from "@/lib/cn";

const PRESETS = ["#db2410", "#ff6a00", "#ffcc00", "#7dff3a", "#22e0c8", "#2f8cff", "#5a3bff", "#d23cff", "#ff4f9a", "#ffffff", "#a0a0a0", "#000000"];

// A swatch that opens a palette of presets and a hex field.
export default function ColorField({ value, onChange, wide }: { value: string; onChange: (v: string) => void; wide?: boolean }) {
  const [open, setOpen] = useState(false);
  return (
    <div className={cn("relative", wide ? "w-full" : "w-[22rem]")}>
      <button
        data-nav
        onClick={() => {
          sfx.move();
          setOpen(true);
        }}
        className="flex h-10 w-full items-center gap-3 rounded-sm border border-white/10 bg-black/40 px-2 focus:border-glow/70 focus:bg-red/10"
      >
        <span className="h-6 w-10 rounded-sm border border-white/30 shadow-inner" style={{ background: value }} />
        <span className="font-q3 text-sm tracking-widest text-ink">{value.toUpperCase()}</span>
      </button>
      <AnimatePresence>{open && <Palette value={value} onPick={onChange} onClose={() => setOpen(false)} />}</AnimatePresence>
    </div>
  );
}

function Palette({ value, onPick, onClose }: { value: string; onPick: (c: string) => void; onClose: () => void }) {
  const ref = useRef<HTMLDivElement>(null);
  const [hex, setHex] = useState(value);
  useLayer(ref, {
    onBack: () => {
      sfx.out();
      onClose();
    },
  });
  return (
    <motion.div
      ref={ref}
      initial={{ opacity: 0, y: -4 }}
      animate={{ opacity: 1, y: 0 }}
      exit={{ opacity: 0 }}
      className="absolute left-0 top-11 z-50 w-[22rem] rounded-sm border border-metal/80 bg-[#0d0a09]/95 p-3 shadow-[0_1rem_3rem_rgb(0_0_0/0.8)]"
    >
      <div data-grid="6" className="grid grid-cols-6 gap-2">
        {PRESETS.map((c) => (
          <button
            key={c}
            data-nav
            data-autofocus={c === value.toLowerCase() ? "" : undefined}
            onClick={() => {
              sfx.move();
              onPick(c);
              onClose();
            }}
            className={cn("h-9 rounded-sm border-2 transition-transform focus:scale-110", c === value.toLowerCase() ? "border-ink-hot" : "border-transparent focus:border-glow")}
            style={{ background: c }}
          />
        ))}
      </div>
      <div className="mt-3 flex gap-2">
        <input
          data-nav
          value={hex}
          onChange={(e) => setHex(e.target.value)}
          onKeyDown={(e) => {
            if (e.key === "Enter" && /^#?[0-9a-f]{6}$/i.test(hex)) {
              onPick(hex.startsWith("#") ? hex : "#" + hex);
              onClose();
            }
          }}
          className="h-9 flex-1 rounded-sm border border-white/10 bg-black/60 px-2 font-q3 text-sm tracking-widest text-ink-hot focus:border-glow/70"
        />
        <input type="color" value={value} onChange={(e) => onPick(e.target.value)} className="h-9 w-12 cursor-pointer rounded-sm border border-white/10 bg-transparent" />
      </div>
    </motion.div>
  );
}
