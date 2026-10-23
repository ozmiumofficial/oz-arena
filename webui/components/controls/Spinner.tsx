"use client";
import { useEffect, useRef, useState } from "react";
import { AnimatePresence, motion } from "motion/react";
import { sfx } from "@/lib/sound";
import { useLayer } from "@/lib/nav";
import { cn } from "@/lib/cn";
import type { Value } from "@/lib/types";

// Q3's spin control made modern: Left/Right cycle (and wrap, like SpinControl_Key), clicking the
// value opens a list. Options marked disabled buzz when chosen.
export default function Spinner({ value, options, onChange, disabled, wide }: {
  value: Value; options: [Value, string][]; onChange: (v: Value) => void; disabled?: (v: Value) => string | false; wide?: boolean;
}) {
  const ref = useRef<HTMLDivElement>(null);
  const [open, setOpen] = useState(false);
  const i = Math.max(0, options.findIndex((o) => JSON.stringify(o[0]) === JSON.stringify(value)));
  const pick = (v: Value) => {
    const why = disabled?.(v);
    if (why) return sfx.buzz();
    sfx.move();
    onChange(v);
  };
  const cycle = (d: number) => {
    if (!options.length) return;
    pick(options[(i + d + options.length) % options.length][0]);
  };
  useEffect(() => {
    const el = ref.current!;
    const h = (e: Event) => cycle((e as CustomEvent).detail);
    el.addEventListener("nav-lr", h);
    return () => el.removeEventListener("nav-lr", h);
  });
  return (
    <div className={cn("relative", wide ? "w-full" : "w-[22rem]")}>
      <div
        ref={ref}
        role="button"
        tabIndex={0}
        data-nav
        data-lr
        onClick={() => {
          if (options.length > 4) {
            sfx.move();
            setOpen(true);
          } else cycle(1);
        }}
        onKeyDown={(e) => {
          if (e.key === "Enter" || e.key === " ") {
            e.preventDefault();
            (e.currentTarget as HTMLElement).click();
          }
        }}
        className="group flex h-10 cursor-pointer items-center justify-between rounded-sm border border-white/10 bg-black/40 px-2 transition-colors focus:border-glow/70 focus:bg-red/10"
      >
        <Arrow dir={-1} onClick={(e) => (e.stopPropagation(), cycle(-1))} />
        <AnimatePresence mode="popLayout" initial={false}>
          <motion.span
            key={String(options[i]?.[0])}
            initial={{ opacity: 0, y: 6 }}
            animate={{ opacity: 1, y: 0 }}
            exit={{ opacity: 0, y: -6 }}
            transition={{ duration: 0.14 }}
            className="truncate px-2 font-q3 text-[0.95rem] tracking-wider text-ink group-focus:text-ink-hot"
          >
            {options[i]?.[1] ?? String(value)}
          </motion.span>
        </AnimatePresence>
        <Arrow dir={1} onClick={(e) => (e.stopPropagation(), cycle(1))} />
      </div>
      <AnimatePresence>
        {open && (
          <OptionList
            options={options}
            current={i}
            disabled={disabled}
            onPick={(v) => {
              pick(v);
              setOpen(false);
            }}
            onClose={() => setOpen(false)}
          />
        )}
      </AnimatePresence>
    </div>
  );
}

function Arrow({ dir, onClick }: { dir: number; onClick: (e: React.MouseEvent) => void }) {
  return (
    <span onClick={onClick} className="grid h-7 w-7 cursor-pointer place-items-center text-dim transition-colors hover:text-glow group-focus:text-red">
      <svg viewBox="0 0 10 10" className="h-2.5 w-2.5 fill-current">{dir < 0 ? <path d="M7 0 2 5l5 5z" /> : <path d="m3 0 5 5-5 5z" />}</svg>
    </span>
  );
}

function OptionList({ options, current, disabled, onPick, onClose }: {
  options: [Value, string][]; current: number; disabled?: (v: Value) => string | false; onPick: (v: Value) => void; onClose: () => void;
}) {
  const ref = useRef<HTMLDivElement>(null);
  useLayer(ref, {
    onBack: () => {
      sfx.out();
      onClose();
    },
  });
  return (
    <motion.div
      ref={ref}
      initial={{ opacity: 0, scaleY: 0.9, y: -4 }}
      animate={{ opacity: 1, scaleY: 1, y: 0 }}
      exit={{ opacity: 0, scaleY: 0.95 }}
      transition={{ duration: 0.14 }}
      style={{ transformOrigin: "top" }}
      className="absolute left-0 right-0 top-11 z-50 max-h-[22rem] overflow-y-auto rounded-sm border border-metal/80 bg-[#0d0a09]/95 p-1 shadow-[0_1rem_3rem_rgb(0_0_0/0.8)]"
    >
      {options.map(([v, label], k) => {
        const why = disabled?.(v);
        return (
          <button
            key={String(v)}
            data-nav
            data-autofocus={k === current ? "" : undefined}
            onClick={() => onPick(v)}
            title={why || undefined}
            className={cn(
              "flex w-full items-center justify-between rounded-sm px-3 py-1.5 text-left text-[0.95rem] focus:bg-red/25 focus:text-ink-hot",
              why ? "text-faint" : "text-ink",
            )}
          >
            <span>{label}</span>
            {k === current && <span className="h-1.5 w-1.5 rounded-full bg-glow" />}
          </button>
        );
      })}
    </motion.div>
  );
}
