"use client";
import { useRef } from "react";
import { motion } from "motion/react";
import { useLayer } from "@/lib/nav";
import BannerText from "./BannerText";
import { cn } from "@/lib/cn";

// The dark glass panel the larger pages open in, right of the menu column. `placement` replaces
// the position (the crosshair editor in the pause menu sits left, clear of the screen's center).
export default function PagePanel({ title, subtitle, back, onTab, children, footer, className, placement }: {
  title: string;
  subtitle?: string;
  back: () => void;
  onTab?: (d: number) => void;
  children: React.ReactNode;
  footer?: React.ReactNode;
  className?: string;
  placement?: string;
}) {
  const ref = useRef<HTMLDivElement>(null);
  useLayer(ref, { onBack: back, onTab });
  return (
    <motion.section
      ref={ref}
      initial={{ opacity: 0, x: "3rem", filter: "blur(6px)" }}
      animate={{ opacity: 1, x: 0, filter: "blur(0px)" }}
      exit={{ opacity: 0, x: "2rem", filter: "blur(4px)" }}
      transition={{ duration: 0.32, ease: [0.2, 0.9, 0.1, 1] }}
      className={cn("q3-grain absolute flex flex-col", placement ?? "bottom-[6.5rem] left-[35rem] right-[4.5rem] top-[7.5rem]", "overflow-hidden rounded-sm border border-white/[0.07] bg-glass shadow-[0_2rem_6rem_rgb(0_0_0/0.75)]", className)}
    >
      <div className="q3-corners pointer-events-none absolute inset-0" />
      <motion.div
        className="absolute left-0 right-0 top-0 h-[2px] origin-left bg-gradient-to-r from-glow via-red to-transparent"
        initial={{ scaleX: 0 }}
        animate={{ scaleX: 1 }}
        transition={{ duration: 0.6, delay: 0.1, ease: [0.2, 0.9, 0.1, 1] }}
      />
      <header className="flex items-end justify-between gap-6 px-10 pb-4 pt-8">
        <div>
          <BannerText text={title} height={2.6} />
          {subtitle && <p className="mt-2 max-w-[48rem] text-[1rem] text-dim">{subtitle}</p>}
        </div>
        <button onClick={back} className="mb-1 flex items-center gap-2 font-q3 text-[0.75rem] tracking-[0.2em] text-dim transition-colors hover:text-ink-hot">
          <kbd className="rounded-sm border border-white/15 px-1.5 py-0.5 font-q3 text-[0.65rem]">ESC</kbd> BACK
        </button>
      </header>
      <div className="mx-10 h-px bg-gradient-to-r from-white/15 via-white/5 to-transparent" />
      <div className="relative min-h-0 flex-1">{children}</div>
      {footer && <footer className="flex items-center gap-3 border-t border-white/[0.06] bg-black/30 px-10 py-4">{footer}</footer>}
    </motion.section>
  );
}

export function EmptyState({ title, text }: { title: string; text: string }) {
  return (
    <div className="flex flex-col items-center justify-center gap-3 rounded-sm border border-dashed border-white/10 bg-black/20 px-10 py-14 text-center">
      <div className="font-q3 text-[1.3rem] tracking-[0.1em] text-ink">{title}</div>
      <p className="max-w-[34rem] text-[1rem] text-dim">{text}</p>
    </div>
  );
}

export function Badge({ children, tone = "amber" }: { children: React.ReactNode; tone?: "amber" | "red" | "dim" }) {
  return (
    <span
      className={cn(
        "rounded-[2px] border px-1.5 py-[1px] font-q3 text-[0.6rem] tracking-[0.15em]",
        tone === "amber" && "border-amber/40 bg-amber/10 text-amber",
        tone === "red" && "border-glow/50 bg-red/15 text-glow",
        tone === "dim" && "border-white/15 text-dim",
      )}
    >
      {children}
    </span>
  );
}

export function SectionTitle({ children, className }: { children: React.ReactNode; className?: string }) {
  return (
    <div className={cn("mb-3 mt-6 flex items-center gap-3 first:mt-0", className)}>
      <span className="font-q3 text-[0.85rem] tracking-[0.22em] text-red">{children}</span>
      <span className="h-px flex-1 bg-gradient-to-r from-red/40 to-transparent" />
    </div>
  );
}
