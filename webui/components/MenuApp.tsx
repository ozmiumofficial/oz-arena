"use client";
// The main menu (AppRoot picks it outside a match). Godot draws the 3D showcase and the banner logo underneath; this page is a
// transparent layer on top (godot_wry). Menu tree: lib/menu.json.
import { useCallback, useEffect, useLayoutEffect, useRef, useState } from "react";
import { AnimatePresence, motion } from "motion/react";
import menu from "@/lib/menu.json";
import { connect, useMenu, toast, getState, setSocialOpen } from "@/lib/store";
import { inGodot, on, send } from "@/lib/bridge";
import { installNav, nav, setSocialToggle, useLayer, type NavDir } from "@/lib/nav";
import { sfx } from "@/lib/sound";
import { cn } from "@/lib/cn";
import type { MenuItemDef } from "@/lib/types";
import MenuItem from "./MenuItem";
import BannerText from "./BannerText";
import { BottomBar, DevBackdrop, Shade, SubjectCaption, Toasts, TopBar } from "./Chrome";
import { SocialPanel } from "./Social";
import { PAGES } from "./pages";

const SUBMENUS: Record<string, { title: string; items: MenuItemDef[] }> = {
  single: { title: "Single Player", items: menu.single as MenuItemDef[] },
  play: { title: "Play", items: menu.play as MenuItemDef[] },
};
const NOT_WIDE = new Set(["main", "quit", ...Object.keys(SUBMENUS)]);

export default function MenuApp() {
  const [stack, setStack] = useState<string[]>(["main"]);
  const [dev, setDev] = useState(false);
  const [shown, setShown] = useState(false);
  const ready = useMenu((s) => !!s.init);
  const socialOpen = useMenu((s) => s.socialOpen);
  const top = stack[stack.length - 1];

  useEffect(() => {
    setDev(!inGodot());
    installNav();
    connect();
    setSocialToggle(() => {
      const open = !getState().socialOpen;
      if (open) sfx.in();
      else sfx.out();
      setSocialOpen(open);
    });
    const offNav = on("nav", (m) => nav(m.dir as NavDir));
    // Godot's capture option page=<id> opens a page directly
    const offOpen = on("open", (m) => {
      const id = String(m.page);
      if (id === "friends") return setSocialOpen(true);
      const sub = Object.keys(SUBMENUS).find((k) => SUBMENUS[k].items.some((it) => it.page === id));
      setStack(id === "main" ? ["main"] : sub ? ["main", sub, id] : ["main", id]);
    });
    return () => {
      offNav();
      offOpen();
    };
  }, []);
  useEffect(() => {
    if (!ready) return;
    // no requestAnimationFrame here: Chromium pauses it while the window is covered, and the menu
    // must never stay invisible because of that
    const t = setTimeout(() => setShown(true), 30);
    setTimeout(() => void send("log", { text: `menu shown, page ${document.visibilityState}` }), 1500);
    return () => clearTimeout(t);
  }, [ready]);

  useEffect(() => {
    void send("page", { id: top, wide: !NOT_WIDE.has(top) });
  }, [top]);

  const open = useCallback((id: string) => {
    setStack((s) => [...s, id]);
    sfx.in();
  }, []);
  const back = useCallback(() => {
    setStack((s) => {
      if (s.length <= 1) return s;
      sfx.out();
      return s.slice(0, -1);
    });
  }, []);
  const activate = useCallback(
    (it: MenuItemDef, from: string) => {
      if (it.submenu) {
        setStack(["main", it.submenu]);
        sfx.in();
      } else if (it.page) {
        // a main-menu item opened while something else is up replaces it
        setStack((s) => (from === "main" ? ["main", it.page!] : [...s, it.page!]));
        sfx.in();
      } else if (it.action) {
        void send(it.action);
      }
    },
    [],
  );

  const wide = !NOT_WIDE.has(top);
  const Page = wide ? PAGES[top] : null;
  const hints: [string, string][] = [["Enter", "Select"], ["Esc", "Back"], ["F", "Friends"]];
  if (top === "settings") hints.push(["Q/E", "Tabs"]);
  return (
    <main className="fixed inset-0 select-none overflow-hidden">
      {dev && <DevBackdrop />}
      <Shade dim={wide ? 0.38 : top === "quit" ? 0.5 : 0} />
      <motion.div className="absolute inset-0" initial={{ opacity: 0 }} animate={{ opacity: shown ? 1 : 0 }} transition={{ duration: 0.6 }}>
        <TopBar />
        <MainColumn stack={stack} activate={activate} open={open} dev={dev} />
        <AnimatePresence>
          {stack[1] in SUBMENUS && <SubMenu key={stack[1]} id={stack[1]} stack={stack} activate={activate} back={back} />}
        </AnimatePresence>
        <AnimatePresence mode="wait">{Page && <Page key={top} back={back} open={open} />}</AnimatePresence>
        <AnimatePresence>{top === "quit" && <QuitConfirm key="quit" back={back} />}</AnimatePresence>
        <SubjectCaption hidden={wide || socialOpen} />
        <BottomBar hints={hints} />
        <SocialPanel />
        <Toasts />
        <CaptureVeil />
      </motion.div>
    </main>
  );
}

function MainColumn({ stack, activate, open, dev }: { stack: string[]; activate: (it: MenuItemDef, from: string) => void; open: (id: string) => void; dev: boolean }) {
  const ref = useRef<HTMLDivElement>(null);
  const logo = useRef<HTMLDivElement>(null);
  const deep = stack.length > 1;
  useLayer(ref, {
    onSide: (d) => {
      const el = document.activeElement as HTMLElement | null;
      const sub = (menu.main as MenuItemDef[]).find((it) => it.id === el?.dataset.id)?.submenu;
      if (d > 0 && sub && !deep) {
        open(sub);
        return true;
      }
      return false;
    },
  });
  // tell Godot where to draw the 3D logo (window pixels)
  useLayoutEffect(() => {
    const report = () => {
      const r = logo.current?.getBoundingClientRect();
      const W = window.innerWidth;
      const H = window.innerHeight;
      // fractions of the page, so Godot can place the logo whatever the display scaling
      if (r) void send("layout", { logo: { x: r.left / W, y: r.top / H, w: r.width / W, h: r.height / H } });
    };
    report();
    const ro = new ResizeObserver(report);
    ro.observe(document.documentElement);
    return () => ro.disconnect();
  }, []);
  return (
    <div ref={ref} className="absolute bottom-24 left-[3.5rem] top-8 flex w-[30rem] flex-col">
      {/* the banner5 logo: Godot renders it into this box (q3_ui's 640x120 strip, 60 degree fov) */}
      <div ref={logo} className={cn("relative h-[7.5rem] w-[30rem] shrink-0", dev && "rounded-sm border border-dashed border-white/10")}>
        {dev && <div className="absolute inset-0 grid place-items-center font-q3 text-[0.75rem] tracking-[0.3em] text-white/25">3D logo (Godot)</div>}
      </div>
      <motion.div className="mt-3 flex justify-center" initial={{ opacity: 0, letterSpacing: "0.6em" }} animate={{ opacity: 1, letterSpacing: "0em" }} transition={{ delay: 0.3, duration: 0.9 }}>
        <BannerText text="Remastered" height={1.9} tracking={0.35} />
      </motion.div>
      <nav className={cn("mt-auto mb-6 flex flex-col transition-opacity duration-300", deep && "opacity-55")}>
        {(menu.main as MenuItemDef[]).map((it, i) => (
          <motion.div key={it.id} initial={{ opacity: 0, x: -30 }} animate={{ opacity: 1, x: 0 }} transition={{ delay: 0.35 + i * 0.05, duration: 0.45, ease: [0.2, 0.9, 0.1, 1] }}>
            <MenuItem
              id={it.id}
              label={it.label}
              hint={it.hint}
              index={i}
              current={deep && stack[1] === (it.submenu ?? it.page)}
              onFocusItem={() => void send("hover", { id: it.id })}
              onActivate={() => {
                sfx.move();
                activate(it, "main");
              }}
            />
          </motion.div>
        ))}
      </nav>
    </div>
  );
}

function SubMenu({ id, stack, activate, back }: { id: string; stack: string[]; activate: (it: MenuItemDef, from: string) => void; back: () => void }) {
  const ref = useRef<HTMLDivElement>(null);
  const cont = useMenu((s) => s.init?.continue ?? "");
  const progress = useMenu((s) => s.init?.progress);
  const rankedLocked = !!progress && progress.level < progress.ranked_level;
  const covered = stack[stack.length - 1] !== id;
  useLayer(ref, { onBack: back, onSide: (d) => (d < 0 ? (back(), true) : false) });
  const { title, items: all } = SUBMENUS[id];
  const items = all.filter((it) => it.when !== "continue" || cont);
  return (
    <motion.div
      ref={ref}
      data-nav-off={covered ? "" : undefined}
      initial={{ opacity: 0, x: "-2rem" }}
      animate={{ opacity: covered ? 0 : 1, x: covered ? "-1rem" : 0 }}
      exit={{ opacity: 0, x: "-2rem" }}
      transition={{ duration: 0.28, ease: [0.2, 0.9, 0.1, 1] }}
      className={cn("absolute bottom-[8.5rem] left-[35rem] w-[30rem]", covered && "pointer-events-none")}
    >
      <div className="mb-3 flex items-center gap-4 pl-6">
        <BannerText text={title} height={1.7} />
        <span className="h-px flex-1 bg-gradient-to-r from-red/60 to-transparent" />
      </div>
      <div className="border-l border-white/10">
        {items.map((it, i) => (
          <motion.div key={it.id} initial={{ opacity: 0, x: -16 }} animate={{ opacity: 1, x: 0 }} transition={{ delay: 0.04 * i, duration: 0.3 }}>
            <MenuItem
              size="md"
              label={it.label}
              hint={it.id === "continue" ? cont.split("\n")[0] : it.id === "ranked" && rankedLocked ? `Locked: reach level ${progress!.ranked_level} (you're level ${progress!.level})` : it.hint}
              dim={it.id === "ranked" && rankedLocked}
              current={stack.includes(it.page ?? "-")}
              onFocusItem={() => void send("hover", { id: it.id })}
              onActivate={() => {
                sfx.move();
                activate(it, id);
              }}
            />
          </motion.div>
        ))}
      </div>
    </motion.div>
  );
}

// Quake 3's "EXIT GAME?" confirmation.
function QuitConfirm({ back }: { back: () => void }) {
  const ref = useRef<HTMLDivElement>(null);
  useLayer(ref, { onBack: back });
  return (
    <motion.div ref={ref} className="absolute inset-0 z-40 grid place-items-center" initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }}>
      <motion.div
        initial={{ scale: 0.92, y: 10 }}
        animate={{ scale: 1, y: 0 }}
        transition={{ type: "spring", stiffness: 380, damping: 28 }}
        className="q3-grain relative flex w-[34rem] flex-col items-center gap-7 rounded-sm border border-white/10 bg-glass px-12 py-10 shadow-[0_2rem_6rem_rgb(0_0_0/0.8)]"
      >
        <div className="q3-corners pointer-events-none absolute inset-0" />
        <BannerText text="Exit game?" height={2.4} />
        <div data-row className="flex gap-4">
          <ConfirmButton label="Yes" onClick={() => void send("quit")} />
          <ConfirmButton label="No" autoFocus onClick={back} />
        </div>
      </motion.div>
    </motion.div>
  );
}

function ConfirmButton({ label, onClick, autoFocus }: { label: string; onClick: () => void; autoFocus?: boolean }) {
  return (
    <button
      data-nav
      data-autofocus={autoFocus ? "" : undefined}
      onClick={() => {
        sfx.move();
        onClick();
      }}
      className="min-w-[9rem] rounded-sm border border-white/10 bg-white/[0.04] px-8 py-3 font-q3 text-[1.6rem] tracking-[0.1em] text-ink transition-all focus:border-glow/70 focus:bg-red/20 focus:text-ink-hot focus:[text-shadow:0_0_0.6em_rgb(255_74_28)]"
    >
      {label}
    </button>
  );
}

// While Godot captures a bind, the whole menu waits for the key.
function CaptureVeil() {
  const capture = useMenu((s) => s.capture);
  const label = useMenu((s) => (s.capture ? s.init?.schema.find((e) => e.key === s.capture!.key)?.label : ""));
  useEffect(() => {
    if (!capture) return;
    const t = setTimeout(() => {
      toast("Bind capture timed out.", "warn");
      void send("cancel_capture");
    }, 8000);
    return () => clearTimeout(t);
  }, [capture]);
  return (
    <AnimatePresence>
      {capture && (
        <motion.div className="absolute inset-0 z-50 grid place-items-center bg-black/60" initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }}>
          <motion.div initial={{ scale: 0.95 }} animate={{ scale: 1 }} className="rounded-sm border border-glow/50 bg-glass px-14 py-10 text-center shadow-[0_0_4rem_rgb(219_36_16/0.35)]">
            <div className="font-q3 text-[0.8rem] tracking-[0.3em] text-dim">{capture.slot === 2 ? "Controller bind" : "Bind"}</div>
            <div className="mt-2 font-q3 text-[2rem] tracking-[0.06em] text-ink-hot">{label}</div>
            <div className="mt-5 animate-pulse font-q3 text-[1rem] tracking-[0.2em] text-glow">Press a key, button or wheel</div>
            <div className="mt-4 text-[0.9rem] text-dim">Esc cancels, Backspace clears</div>
          </motion.div>
        </motion.div>
      )}
    </AnimatePresence>
  );
}
