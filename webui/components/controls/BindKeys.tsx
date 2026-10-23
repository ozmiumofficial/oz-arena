"use client";
import { send } from "@/lib/bridge";
import { bindLabel } from "@/lib/binds";
import { useMenu } from "@/lib/store";
import { sfx } from "@/lib/sound";
import { cn } from "@/lib/cn";

// Two keyboard/mouse binds and one controller bind. Clicking a slot asks Godot to capture the next
// input (key, mouse button, wheel or pad button), so every device works the same way.
export default function BindKeys({ k, value }: { k: string; value: string[] }) {
  const capture = useMenu((s) => s.capture);
  const conflict = useMenu((s) => s.conflicts[k]);
  return (
    <div data-row className="flex w-[22rem] gap-2">
      {[0, 1, 2].map((slot) => {
        const live = capture?.key === k && capture.slot === slot;
        const label = bindLabel(value[slot] ?? "");
        return (
          <button
            key={slot}
            data-nav
            title={slot === 2 ? "Controller" : slot === 0 ? "Primary" : "Secondary"}
            onClick={() => {
              sfx.move();
              void send("capture_bind", { key: k, slot });
            }}
            className={cn(
              "relative h-10 flex-1 truncate rounded-sm border px-2 font-q3 text-[0.8rem] tracking-wider transition-colors",
              live ? "animate-pulse border-glow bg-red/30 text-ink-hot" : "border-white/10 bg-black/40 focus:border-glow/70 focus:bg-red/10",
              !label && !live && "text-faint",
              conflict && label && !live && "text-amber",
            )}
          >
            {live ? "Press..." : label || (slot === 2 ? "Pad" : "-")}
          </button>
        );
      })}
    </div>
  );
}
