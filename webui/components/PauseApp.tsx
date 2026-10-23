"use client";
// The in-match pause menu: the same web app, shown by Gameplay over the paused match (Escape).
// The match stays visible behind, darkened on the left where the menu sits; the crosshair editor
// leaves the screen's center clear so the real crosshair previews live. Escape on the menu
// itself resumes. Quake 3's in-game menu (Resume, Restart Arena, Leave Arena, Exit Game),
// modernized: Resume, Ready up (warmup), Crosshair, Settings, Restart match, Leave match, Quit.
import { useCallback, useEffect, useRef, useState } from "react";
import { AnimatePresence, motion } from "motion/react";
import { useMenu } from "@/lib/store";
import { on, send } from "@/lib/bridge";
import { nav, useLayer, type NavDir } from "@/lib/nav";
import { sfx } from "@/lib/sound";
import { cn } from "@/lib/cn";
import MenuItem from "./MenuItem";
import BannerText from "./BannerText";
import { Toasts } from "./Chrome";
import { PAGES } from "./pages";

type Item = { id: string; label: string; hint: string; page?: string; act?: () => void };

export default function PauseApp() {
  const [stack, setStack] = useState<string[]>([]);
  const [confirm, setConfirm] = useState<"" | "leave" | "quit">("");
  const match = useMenu((s) => s.match);
  const opened = useMenu((s) => s.pauseOpen);
  const top = stack[stack.length - 1] ?? "";
  // every time Godot opens the pause menu it starts at the top
  useEffect(() => {
    setStack([]);
    setConfirm("");
  }, [opened]);
  useEffect(() => {
    void send("page", { id: top || "pause", wide: !!top });
  }, [top]);
  // Godot (tests, captures) can open a page directly
  useEffect(() => on("open", (m) => setStack(PAGES[String(m.page)] ? [String(m.page)] : [])), []);
  // keys Godot got while the page didn't have the keyboard (Escape: back a page, or resume)
  useEffect(() => on("nav", (m) => nav(m.dir as NavDir)), []);

  const open = useCallback((id: string) => {
    setStack((s) => [...s, id]);
    sfx.in();
  }, []);
  const back = useCallback(() => {
    setStack((s) => {
      sfx.out();
      return s.slice(0, -1);
    });
  }, []);
  const resume = () => {
    sfx.out();
    void send("resume");
  };

  const items: Item[] = [
    { id: "resume", label: "Resume", hint: "Back to the match", act: resume },
  ];
  if (match?.warmup && match.ready_up)
    items.push({ id: "ready", label: match.ready ? "Not ready" : "Ready up", hint: "Every player readies up to start the countdown (F3)", act: () => (sfx.move(), send("ready_up")) });
  items.push(
    { id: "crosshair", label: "Crosshair", hint: "Edit it live over your match", page: "crosshair" },
    { id: "settings", label: "Settings", hint: "Every setting, applied as you change it", page: "settings" },
  );
  if (match?.can_restart) items.push({ id: "restart", label: "Restart match", hint: "Start this match over", act: () => (sfx.move(), send("restart_match")) });
  items.push(
    { id: "leave", label: "Leave match", hint: "Back to the main menu", act: () => (sfx.in(), setConfirm("leave")) },
    { id: "quit", label: "Quit game", hint: "Close the game", act: () => (sfx.in(), setConfirm("quit")) },
  );

  const Page = top ? PAGES[top] : null;
  const clear = top === "crosshair";
  return (
    <main className="fixed inset-0 select-none overflow-hidden">
      {/* the match shows through: darker under the menu, the center kept clear for the crosshair editor */}
      <div className="pointer-events-none absolute inset-0 bg-[linear-gradient(90deg,rgb(0_0_0/0.85)_0%,rgb(0_0_0/0.6)_28%,rgb(0_0_0/0.15)_50%,transparent_62%)]" />
      <motion.div className="pointer-events-none absolute inset-0 bg-black" animate={{ opacity: clear ? 0 : top ? 0.35 : 0.18 }} transition={{ duration: 0.35 }} />
      <AnimatePresence>{!clear && <PauseColumn key="col" items={items} covered={!!top || !!confirm} onBack={resume} open={open} />}</AnimatePresence>
      <AnimatePresence mode="wait">{Page && <Page key={top} back={back} open={open} />}</AnimatePresence>
      <AnimatePresence>
        {confirm && (
          <Confirm
            key={confirm}
            title={confirm === "leave" ? "Leave the match?" : "Quit the game?"}
            onYes={() => void send(confirm === "leave" ? "leave_match" : "quit")}
            onNo={() => (sfx.out(), setConfirm(""))}
          />
        )}
      </AnimatePresence>
      <div className="absolute bottom-6 left-[4.5rem] flex items-center gap-5 font-q3 text-[0.7rem] tracking-[0.2em] text-dim">
        {[["Esc", top ? "Back" : "Resume"], ["Enter", "Select"]].map(([k, l]) => (
          <span key={k} className="flex items-center gap-2"><kbd className="rounded-sm border border-white/15 px-1.5 py-0.5 text-[0.65rem]">{k}</kbd>{l}</span>
        ))}
      </div>
      <Toasts />
    </main>
  );
}

function PauseColumn({ items, covered, onBack, open }: { items: Item[]; covered: boolean; onBack: () => void; open: (id: string) => void }) {
  const ref = useRef<HTMLDivElement>(null);
  const match = useMenu((s) => s.match);
  useLayer(ref, { onBack });
  return (
    <motion.div
      ref={ref}
      data-nav-off={covered ? "" : undefined}
      initial={{ opacity: 0, x: "-2rem" }}
      animate={{ opacity: covered ? 0.45 : 1, x: 0 }}
      exit={{ opacity: 0, x: "-2rem" }}
      transition={{ duration: 0.28, ease: [0.2, 0.9, 0.1, 1] }}
      className={cn("absolute bottom-24 left-[3.5rem] top-12 flex w-[30rem] flex-col", covered && "pointer-events-none")}
    >
      <BannerText text="Paused" height={3.2} />
      {match && (
        <div className="mt-4 border-l-2 border-red/70 pl-4">
          <div className="font-q3 text-[1.05rem] tracking-[0.08em] text-ink-hot">{match.title}</div>
          <div className="mt-1 flex items-center gap-3 text-[0.95rem] text-dim">
            <span className="tabular-nums text-ink">{match.clock}</span>
            {match.warmup && <span className="rounded-[2px] border border-amber/40 bg-amber/10 px-1.5 font-q3 text-[0.6rem] tracking-[0.15em] text-amber">Warmup</span>}
          </div>
          <div className="mt-3 flex flex-col gap-1">
            {(match.standings ?? []).slice(0, 4).map((s, i) => (
              <div key={i} className={cn("flex w-[18rem] items-center justify-between text-[0.95rem]", s.local ? "text-amber" : "text-ink/80")}>
                <span className="truncate"><span className="mr-2 text-faint tabular-nums">{i + 1}</span>{s.name}</span>
                <span className="tabular-nums">{s.score}</span>
              </div>
            ))}
          </div>
        </div>
      )}
      <nav className="mt-auto flex flex-col">
        {items.map((it, i) => (
          <motion.div key={it.id} initial={{ opacity: 0, x: -24 }} animate={{ opacity: 1, x: 0 }} transition={{ delay: 0.04 * i, duration: 0.3 }}>
            <MenuItem
              id={it.id}
              label={it.label}
              hint={it.hint}
              size="md"
              onActivate={() => {
                if (it.page) open(it.page);
                else it.act?.();
              }}
            />
          </motion.div>
        ))}
      </nav>
    </motion.div>
  );
}

function Confirm({ title, onYes, onNo }: { title: string; onYes: () => void; onNo: () => void }) {
  const ref = useRef<HTMLDivElement>(null);
  useLayer(ref, { onBack: onNo });
  return (
    <motion.div ref={ref} className="absolute inset-0 z-40 grid place-items-center bg-black/50" initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }}>
      <motion.div
        initial={{ scale: 0.92, y: 10 }}
        animate={{ scale: 1, y: 0 }}
        transition={{ type: "spring", stiffness: 380, damping: 28 }}
        className="q3-grain relative flex w-[34rem] flex-col items-center gap-7 rounded-sm border border-white/10 bg-glass px-12 py-10 shadow-[0_2rem_6rem_rgb(0_0_0/0.8)]"
      >
        <div className="q3-corners pointer-events-none absolute inset-0" />
        <BannerText text={title} height={2.2} />
        <div data-row className="flex gap-4">
          {[["Yes", onYes, false], ["No", onNo, true]].map(([label, f, auto]) => (
            <button
              key={label as string}
              data-nav
              data-autofocus={auto ? "" : undefined}
              onClick={() => {
                sfx.move();
                (f as () => void)();
              }}
              className="min-w-[9rem] rounded-sm border border-white/10 bg-white/[0.04] px-8 py-3 font-q3 text-[1.6rem] tracking-[0.1em] text-ink transition-all focus:border-glow/70 focus:bg-red/20 focus:text-ink-hot focus:[text-shadow:0_0_0.6em_rgb(255_74_28)]"
            >
              {label as string}
            </button>
          ))}
        </div>
      </motion.div>
    </motion.div>
  );
}
