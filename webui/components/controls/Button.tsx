"use client";
import { sfx } from "@/lib/sound";
import { cn } from "@/lib/cn";

// Panel buttons. `refuse` is a reason the action can't run yet: clicking buzzes (menu4) and says why.
export default function Button({ children, onClick, refuse, onRefuse, primary, small, className, autoFocus }: {
  children: React.ReactNode;
  onClick?: () => void;
  refuse?: string;
  onRefuse?: (why: string) => void;
  primary?: boolean;
  small?: boolean;
  className?: string;
  autoFocus?: boolean;
}) {
  return (
    <button
      data-nav
      data-autofocus={autoFocus ? "" : undefined}
      title={refuse}
      onClick={() => {
        if (refuse) {
          sfx.buzz();
          onRefuse?.(refuse);
          return;
        }
        sfx.move();
        onClick?.();
      }}
      className={cn(
        "group relative overflow-hidden rounded-sm border font-q3 tracking-[0.12em] transition-all duration-150",
        small ? "px-4 py-1.5 text-[0.8rem]" : "px-6 py-2.5 text-[1rem]",
        primary
          ? "border-glow/70 bg-gradient-to-b from-red/90 to-blood/90 text-ink-hot shadow-[0_0_1.5rem_rgb(219_36_16/0.35)] focus:shadow-[0_0_2.2rem_rgb(255_74_28/0.7)] focus:brightness-125"
          : "border-white/10 bg-white/[0.04] text-ink focus:border-glow/70 focus:bg-red/15 focus:text-ink-hot",
        refuse && "opacity-60",
        className,
      )}
    >
      <span className="relative z-10">{children}</span>
      <span className="pointer-events-none absolute inset-0 -translate-x-full bg-gradient-to-r from-transparent via-white/15 to-transparent transition-transform duration-500 group-focus:translate-x-full" />
    </button>
  );
}
