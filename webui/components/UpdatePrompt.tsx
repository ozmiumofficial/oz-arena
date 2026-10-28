"use client";
// "Update available": Godot's Q3UpdateCheck found a newer release on GitHub. Q3UpdateInstall downloads
// only the files that changed (by itself with Settings > Network > Update automatically, else on Update
// now), then Restart now installs it; otherwise it installs when the game quits (UPDATES_PLAN.md).
// Later hides the offer until the next launch (a download keeps going), Skip hides that version for good.
// A build without the updater (or the editor) can only open the release page.
import { useRef } from "react";
import { AnimatePresence, motion } from "motion/react";
import { dismissUpdate, useMenu, type UpdateInfo, type UpdateStatus } from "@/lib/store";
import { send } from "@/lib/bridge";
import { useLayer } from "@/lib/nav";
import { sfx } from "@/lib/sound";
import BannerText from "./BannerText";

function size(bytes: number) {
  if (bytes <= 0) return "";
  return bytes >= 1e9 ? `${(bytes / 1e9).toFixed(1)} GB` : `${Math.round(bytes / 1e6)} MB`;
}

// The release notes are GitHub markdown; only headings and bullets are worth drawing.
function Notes({ text }: { text: string }) {
  return (
    <div className="max-h-[16rem] w-full overflow-y-auto pr-2 text-left text-[0.95rem] leading-relaxed text-ink">
      {text.split("\n").map((raw, i) => {
        const line = raw.trim();
        if (!line) return <div key={i} className="h-2" />;
        if (line.startsWith("#")) return <div key={i} className="mt-1 font-q3 text-[1.15rem] tracking-[0.08em] text-ink-hot">{line.replace(/^#+\s*/, "")}</div>;
        if (/^[-*]\s/.test(line)) return <div key={i} className="pl-4 -indent-4 before:mr-2 before:text-glow before:content-['•']">{line.slice(2)}</div>;
        return <div key={i}>{line}</div>;
      })}
    </div>
  );
}

function Progress({ s }: { s: UpdateStatus }) {
  const frac = s.total > 0 ? Math.min(1, s.done / s.total) : 0;
  return (
    <div className="flex w-full flex-col gap-2">
      <div className="h-2 w-full overflow-hidden rounded-sm bg-white/[0.08]">
        <motion.div className="h-full bg-glow shadow-[0_0_0.8em_rgb(255_74_28)]" animate={{ width: `${(frac * 100).toFixed(1)}%` }} transition={{ ease: "linear", duration: 0.4 }} />
      </div>
      <div className="flex justify-between text-[0.85rem] text-ink/70">
        <span>{s.state === "checking" ? "Checking the update's signature…" : `Downloading ${s.file}`}</span>
        <span>{s.total > 0 ? `${size(s.done) || "0 MB"} / ${size(s.total)}` : ""}</span>
      </div>
    </div>
  );
}

function Prompt({ u, s, later, skip }: { u: UpdateInfo; s: UpdateStatus | null; later: () => void; skip: () => void }) {
  const ref = useRef<HTMLDivElement>(null);
  useLayer(ref, { onBack: later });
  const view = () => {
    void send("open_url", { url: u.url });
    later();
  };
  const st = s && s.tag === u.tag ? s.state : "idle";
  const busy = st === "checking" || st === "downloading";
  let line = "";
  if (st === "ready") line = "Downloaded. Restart now to play it, or it installs when you quit.";
  else if (st === "applying") line = "Installing…";
  else if (st === "failed") line = s?.error || "The update stopped.";
  else if (st === "current") line = "You already have every file of this version.";
  let buttons: [string, () => void][];
  if (st === "ready") buttons = [["Restart now", () => void send("update_apply")], ["Later", later]];
  else if (busy || st === "applying") buttons = [["Keep playing", later]];
  else if (u.can_install) buttons = [[st === "failed" ? "Try again" : "Update now", () => void send("update_start")], ["Later", later], ["Skip this version", skip]];
  else buttons = [["View release", view], ["Later", later], ["Skip this version", skip]];
  return (
    <motion.div ref={ref} className="absolute inset-0 z-40 grid place-items-center bg-black/50" initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }}>
      <motion.div
        initial={{ scale: 0.92, y: 10 }}
        animate={{ scale: 1, y: 0 }}
        transition={{ type: "spring", stiffness: 380, damping: 28 }}
        className="q3-grain relative flex w-[40rem] flex-col items-center gap-5 rounded-sm border border-white/10 bg-glass px-12 py-9 shadow-[0_2rem_6rem_rgb(0_0_0/0.8)]"
      >
        <div className="q3-corners pointer-events-none absolute inset-0" />
        <BannerText text="Update available" height={2.4} />
        <div className="text-center">
          <div className="font-q3 text-[1.6rem] tracking-[0.08em] text-ink-hot">{u.version}</div>
          <div className="text-[0.85rem] text-ink/70">
            {/* an in-game update fetches only what changed, so the zip's size would mislead */}
            {[u.beta ? "Beta" : "", u.published, u.can_install ? "" : size(u.size)].filter(Boolean).join("  ·  ")}
          </div>
        </div>
        {u.notes && <Notes text={u.notes} />}
        {busy && s && <Progress s={s} />}
        {line && <div className={`text-center text-[0.95rem] ${st === "failed" ? "text-red" : "text-ink"}`}>{line}</div>}
        <div data-row className="flex gap-3">
          {buttons.map(([label, fn], i) => (
            <Btn key={label} label={label} autoFocus={i === 0} onClick={fn} />
          ))}
        </div>
      </motion.div>
    </motion.div>
  );
}

function Btn({ label, onClick, autoFocus }: { label: string; onClick: () => void; autoFocus?: boolean }) {
  return (
    <button
      data-nav
      data-autofocus={autoFocus ? "" : undefined}
      onClick={() => {
        sfx.move();
        onClick();
      }}
      className="rounded-sm border border-white/10 bg-white/[0.04] px-6 py-3 font-q3 text-[1.3rem] tracking-[0.1em] text-ink transition-all focus:border-glow/70 focus:bg-red/20 focus:text-ink-hot focus:[text-shadow:0_0_0.6em_rgb(255_74_28)]"
    >
      {label}
    </button>
  );
}

export default function UpdatePrompt({ hidden }: { hidden: boolean }) {
  const update = useMenu((s) => s.update);
  const status = useMenu((s) => s.updateStatus);
  // the store clears the offer as the prompt closes; the exit animation still needs what it showed
  const last = useRef<UpdateInfo | null>(null);
  if (update) last.current = update;
  return (
    <AnimatePresence>
      {update && !hidden && last.current && <Prompt key="update" u={last.current} s={status} later={() => dismissUpdate(false)} skip={() => dismissUpdate(true)} />}
    </AnimatePresence>
  );
}
