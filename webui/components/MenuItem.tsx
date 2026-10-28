"use client";
import { useState } from "react";
import { AnimatePresence, motion } from "motion/react";
import { cn } from "@/lib/cn";

// A main or sub-menu entry: Quake 3's prop font, a red glow that pulses while focused
// (QMF_PULSEIFFOCUS), an accent bar, a light sweep across, and the item's hint underneath:
// opening under the focused item, or always shown (sub-menus, so every choice explains itself).
// A lock badge says what a locked item needs without hovering it.
export default function MenuItem({ id, label, hint, index, size = "lg", current, dim, primary, hintAlways, lock, onActivate, onFocusItem }: {
  id?: string;
  label: string;
  hint?: string;
  index?: number;
  size?: "lg" | "md";
  current?: boolean;
  dim?: boolean;
  primary?: boolean;
  hintAlways?: boolean;
  lock?: string;
  onActivate: () => void;
  onFocusItem?: () => void;
}) {
  const [focused, setFocused] = useState(false);
  const hintClass = size === "lg" ? "pl-[4.25rem] text-[0.95rem]" : "pl-6 text-[0.85rem]";
  return (
    <div className="relative">
      <button
        data-nav
        data-id={id}
        data-current={current ? "" : undefined}
        onFocus={() => {
          setFocused(true);
          onFocusItem?.();
        }}
        onBlur={() => setFocused(false)}
        onClick={onActivate}
        className={cn("group relative flex w-full items-center text-left", size === "lg" ? "py-[0.45rem] pl-7" : "py-[0.4rem] pl-6")}
      >
        <motion.span
          className="absolute left-0 top-1/2 h-[70%] w-[3px] -translate-y-1/2 bg-glow shadow-[0_0_0.8rem_rgb(255_74_28)]"
          initial={false}
          animate={{ scaleY: focused ? 1 : current ? 0.5 : 0, opacity: focused ? 1 : current ? 0.6 : 0 }}
          transition={{ duration: 0.18 }}
        />
        <motion.span
          className="absolute inset-y-0.5 left-0 right-0 origin-left bg-gradient-to-r from-red/40 via-red/10 to-transparent"
          initial={false}
          animate={{ scaleX: focused ? 1 : 0, opacity: focused ? 1 : 0 }}
          transition={{ duration: 0.28, ease: [0.2, 0.9, 0.1, 1] }}
        />
        {index !== undefined && (
          <span className={cn("relative mr-4 w-6 font-q3 text-[0.7rem] tabular-nums tracking-widest transition-colors", focused ? "text-glow" : "text-faint")}>
            {String(index + 1).padStart(2, "0")}
          </span>
        )}
        <span
          className={cn(
            "relative font-q3 leading-none tracking-[0.06em] transition-[color,transform] duration-200",
            size === "lg" ? "text-[2.55rem]" : "text-[1.75rem]",
            focused
              ? "q3-glow translate-x-1 text-ink-hot"
              : dim
                ? "text-ink/40"
                : primary
                  ? "text-ink-hot [text-shadow:0_0_0.7em_rgb(219_36_16/0.55)]"
                  : "text-ink/85",
          )}
        >
          {label}
        </span>
        {lock && (
          <span className="relative ml-4 flex items-center gap-1.5 rounded-sm border border-amber/50 bg-black/50 px-2 py-0.5 font-q3 text-[0.7rem] tracking-[0.15em] text-amber">
            <svg viewBox="0 0 24 24" className="h-3 w-3 fill-none stroke-current stroke-[2.5]"><rect x="5" y="11" width="14" height="10" rx="1.5" /><path d="M8 11V7a4 4 0 0 1 8 0v4" /></svg>
            {lock}
          </span>
        )}
        {focused && <span key={label} className="q3-sweep pointer-events-none absolute inset-0 overflow-hidden" />}
      </button>
      {/* the hint, under the item; clicking it activates the item too */}
      {hintAlways && hint ? (
        <div onClick={onActivate} className={cn("-mt-0.5 cursor-pointer pb-1.5 transition-colors", hintClass, focused ? "text-ink/80" : "text-dim")}>
          {hint}
        </div>
      ) : (
        <AnimatePresence initial={false}>
          {focused && hint && (
            <motion.div
              initial={{ height: 0, opacity: 0 }}
              animate={{ height: "auto", opacity: 1 }}
              exit={{ height: 0, opacity: 0 }}
              transition={{ duration: 0.2 }}
              onClick={onActivate}
              className={cn("cursor-pointer overflow-hidden text-dim", hintClass)}
            >
              <div className="pb-1">{hint}</div>
            </motion.div>
          )}
        </AnimatePresence>
      )}
    </div>
  );
}
