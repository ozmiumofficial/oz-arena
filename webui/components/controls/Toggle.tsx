"use client";
import { useEffect, useRef } from "react";
import { motion } from "motion/react";
import { sfx } from "@/lib/sound";
import { cn } from "@/lib/cn";

// A switch. Enter/click or Left/Right flips it (q3_ui's RadioButton_Key), with the move sound.
export default function Toggle({ value, onChange, disabled }: { value: boolean; onChange: (v: boolean) => void; disabled?: boolean }) {
  const ref = useRef<HTMLButtonElement>(null);
  const flip = () => {
    if (disabled) return sfx.buzz();
    sfx.move();
    onChange(!value);
  };
  useEffect(() => {
    const el = ref.current!;
    const h = () => flip();
    el.addEventListener("nav-lr", h);
    return () => el.removeEventListener("nav-lr", h);
  });
  return (
    <button ref={ref} data-nav data-lr onClick={flip} className="group flex items-center gap-3 rounded-sm px-1 py-1 focus:bg-white/5">
      <span className={cn("relative h-6 w-12 rounded-full border transition-colors duration-200", value ? "border-glow/80 bg-red/70" : "border-metal bg-black/60")}>
        <motion.span
          className={cn("absolute top-0.5 h-[1.125rem] w-[1.125rem] rounded-full shadow", value ? "bg-ink-hot" : "bg-dim")}
          animate={{ left: value ? "1.55rem" : "0.2rem" }}
          transition={{ type: "spring", stiffness: 600, damping: 32 }}
        />
      </span>
      <span className={cn("font-q3 text-sm tracking-wider", value ? "text-ink-hot" : "text-dim")}>{value ? "On" : "Off"}</span>
    </button>
  );
}
