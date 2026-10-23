"use client";
// Settings: every entry of Godot's settings schema, ten tabs, search, tooltips and an info panel,
// reset per setting (R or the arrow) and per tab, config codes, a live crosshair preview, cm/360,
// and a 15 s "keep these display settings?" check.
import { useEffect, useMemo, useRef, useState } from "react";
import { AnimatePresence, motion } from "motion/react";
import { useMenu, setValue, resetValue, isDefault, toast } from "@/lib/store";
import { send, inGodot } from "@/lib/bridge";
import { bindLabel } from "@/lib/binds";
import { sfx } from "@/lib/sound";
import { useLayer, focusFirst } from "@/lib/nav";
import { cn } from "@/lib/cn";
import type { SettingEntry, Value } from "@/lib/types";
import PagePanel, { Badge, SectionTitle } from "../PagePanel";
import Toggle from "../controls/Toggle";
import Slider from "../controls/Slider";
import Spinner from "../controls/Spinner";
import ColorField from "../controls/ColorField";
import TextField from "../controls/TextField";
import BindKeys from "../controls/BindKeys";
import Button from "../controls/Button";
import Crosshair from "./Crosshair";
import type { PageProps } from "./index";

const DISPLAY = ["video/display_mode", "video/resolution", "video/monitor"];

export default function SettingsPage({ back, open }: PageProps) {
  const init = useMenu((s) => s.init);
  const values = useMenu((s) => s.values);
  const [tab, setTab] = useState("video");
  const [query, setQuery] = useState("");
  const [focus, setFocus] = useState<string>("");
  const [confirm, setConfirm] = useState<Record<string, Value> | null>(null);
  const [importing, setImporting] = useState(false);
  const list = useRef<HTMLDivElement>(null);
  const tabs = init?.tabs ?? [];
  const schema = useMemo(() => init?.schema ?? [], [init]);

  const shown = useMemo(() => {
    const q = query.trim().toLowerCase();
    if (!q) return schema.filter((e) => e.tab === tab);
    const words = q.split(/\s+/);
    return schema.filter((e) => e.tab !== "profile" && words.every((w) => `${e.label} ${e.tip} ${e.group} ${e.key}`.toLowerCase().includes(w)));
  }, [schema, tab, query]);
  const groups = useMemo(() => {
    const g: [string, SettingEntry[]][] = [];
    for (const e of shown) {
      const name = query ? `${tabs.find((t) => t[0] === e.tab)?.[1]} / ${e.group}` : e.group;
      const last = g[g.length - 1];
      if (last && last[0] === name) last[1].push(e);
      else g.push([name, [e]]);
    }
    return g;
  }, [shown, query, tabs]);

  const switchTab = (d: number) => {
    const i = tabs.findIndex((t) => t[0] === tab);
    const next = tabs[(i + d + tabs.length) % tabs.length][0];
    sfx.move();
    setQuery("");
    setTab(next);
    requestAnimationFrame(() => requestAnimationFrame(() => focusFirst(list.current)));
  };

  const change = (e: SettingEntry, v: Value) => {
    if (DISPLAY.includes(e.key) && inGodot() && !confirm) {
      setConfirm(Object.fromEntries(DISPLAY.map((k) => [k, values[k]])));
    }
    setValue(e.key, v);
  };

  // R resets the focused setting
  useEffect(() => {
    const h = (ev: KeyboardEvent) => {
      if ((ev.key === "r" || ev.key === "R") && focus && !(ev.target as HTMLElement).matches("input")) {
        if (!isDefault(focus)) {
          sfx.move();
          resetValue(focus);
        } else sfx.buzz();
      }
    };
    window.addEventListener("keydown", h);
    return () => window.removeEventListener("keydown", h);
  }, [focus]);

  const focused = schema.find((e) => e.key === focus);
  return (
    <PagePanel
      title="Settings"
      back={back}
      onTab={switchTab}
      footer={
        <div data-row className="flex w-full items-center gap-3">
          <Button
            small
            onClick={() => {
              void send("reset_tab", { tab });
              toast(`${tabs.find((t) => t[0] === tab)?.[1]} reset to defaults.`);
            }}
          >
            Reset tab
          </Button>
          <Button small onClick={() => void send("export_code", { prefix: tab === "crosshair" ? "crosshair/" : "" })}>
            {tab === "crosshair" ? "Copy crosshair code" : "Copy config code"}
          </Button>
          <Button small onClick={() => setImporting(true)}>
            Paste code
          </Button>
          {tab === "crosshair" && !query && (
            <Button small primary onClick={() => open("crosshair")}>
              Crosshair editor
            </Button>
          )}
          <span className="ml-auto text-[0.85rem] text-faint">Changes apply and save as you make them.</span>
        </div>
      }
    >
      <div className="flex h-full flex-col">
        <div className="flex items-center gap-1 px-10 pt-3">
          {tabs.map(([id, label]) => (
            <button
              key={id}
              onClick={() => {
                if (id === tab && !query) return;
                sfx.move();
                setQuery("");
                setTab(id);
              }}
              className={cn("relative px-2.5 py-2 font-q3 text-[0.78rem] tracking-[0.12em] transition-colors", id === tab && !query ? "text-ink-hot" : "text-dim hover:text-ink")}
            >
              {label}
              {id === tab && !query && <motion.span layoutId="settings-tab" className="absolute inset-x-2 -bottom-px h-[2px] bg-glow shadow-[0_0_0.6rem_rgb(255_74_28)]" />}
            </button>
          ))}
          <div className="ml-auto shrink-0 pl-3">
            <input
              data-nav
              value={query}
              onChange={(e) => setQuery(e.target.value)}
              placeholder="Search settings"
              className="h-9 w-[12rem] rounded-sm border border-white/10 bg-black/50 px-3 text-[0.95rem] text-ink-hot placeholder:text-faint focus:border-glow/70"
            />
          </div>
        </div>
        <div className="flex min-h-0 flex-1 gap-8 px-10 pb-4 pt-4">
          <div ref={list} className="min-h-0 flex-1 overflow-y-auto pr-3">
            <motion.div key={query ? "search" : tab} initial={{ opacity: 0, y: 8 }} animate={{ opacity: 1, y: 0 }} transition={{ duration: 0.18 }}>
                {tab === "graphics" && !query && <PresetBar />}
                {groups.map(([name, entries]) => (
                  <div key={name}>
                    <SectionTitle className="mt-7">{name}</SectionTitle>
                    {entries.map((e) => (
                      <Row key={e.key} e={e} value={values[e.key]} active={focus === e.key} onFocus={() => setFocus(e.key)} onChange={(v) => change(e, v)} />
                    ))}
                  </div>
                ))}
                {!shown.length && <div className="py-16 text-center text-dim">No settings match &ldquo;{query}&rdquo;.</div>}
            </motion.div>
          </div>
          <InfoPanel e={focused} tab={query ? "" : tab} />
        </div>
      </div>
      <AnimatePresence>{confirm && <DisplayConfirm previous={confirm} onDone={() => setConfirm(null)} />}</AnimatePresence>
      <AnimatePresence>{importing && <ImportCode onDone={() => setImporting(false)} />}</AnimatePresence>
    </PagePanel>
  );
}

function Row({ e, value, active, onFocus, onChange }: { e: SettingEntry; value: Value; active: boolean; onFocus: () => void; onChange: (v: Value) => void }) {
  const modified = JSON.stringify(value) !== JSON.stringify(e.default);
  return (
    <div
      onFocus={onFocus}
      onMouseEnter={onFocus}
      className={cn("group relative flex min-h-[3.25rem] items-center gap-4 rounded-sm px-3 py-1.5 transition-colors", active ? "bg-white/[0.045]" : "hover:bg-white/[0.02]")}
      title={e.tip}
    >
      {active && <motion.span layoutId="settings-row" className="absolute inset-y-1 left-0 w-[3px] bg-glow shadow-[0_0_0.6rem_rgb(255_74_28)]" transition={{ duration: 0.18 }} />}
      <div className="flex min-w-0 flex-1 items-center gap-2.5">
        <span className={cn("truncate text-[1.05rem] font-medium", active ? "text-ink-hot" : "text-ink")}>{e.label}</span>
        {modified && <span className="h-1.5 w-1.5 shrink-0 rounded-full bg-glow" title="Changed from the default" />}
        {!e.wired && <Badge>Not yet wired</Badge>}
        {e.restart && <Badge tone="dim">Restart</Badge>}
      </div>
      <Control e={e} value={value} onChange={onChange} />
      <button
        onClick={() => {
          sfx.move();
          resetValue(e.key);
        }}
        title="Reset to default (R)"
        className={cn("grid h-8 w-8 place-items-center rounded-sm text-dim transition-all hover:bg-white/5 hover:text-glow", modified ? "opacity-100" : "pointer-events-none opacity-0")}
      >
        <svg viewBox="0 0 24 24" className="h-4 w-4 fill-none stroke-current stroke-2"><path d="M3 12a9 9 0 1 0 3-6.7M3 4v5h5" /></svg>
      </button>
    </div>
  );
}

function Control({ e, value, onChange }: { e: SettingEntry; value: Value; onChange: (v: Value) => void }) {
  switch (e.type) {
    case "bool":
      return <div className="w-[22rem]"><Toggle value={!!value} onChange={onChange} /></div>;
    case "int":
    case "float": {
      const fmt = (v: number) => {
        const d = e.type === "float" ? Math.max(0, (String(e.step ?? 0.01).split(".")[1] ?? "").length) : 0;
        if (e.key === "mouse/sensitivity") return v.toFixed(2);
        if (e.key === "video/max_fps" && v === 0) return "Uncapped";
        return `${v.toFixed(d)}${e.unit && e.unit !== "x" ? " " + e.unit : e.unit === "x" ? "x" : ""}`;
      };
      return <Slider value={Number(value)} min={e.min ?? 0} max={e.max ?? 100} step={e.step ?? 1} onChange={onChange} format={fmt} />;
    }
    case "enum":
      return <Spinner value={value} options={e.options ?? []} onChange={onChange} />;
    case "color":
      return <ColorField value={String(value)} onChange={onChange} />;
    case "string":
      return <TextField value={String(value)} onCommit={onChange} max={e.max} />;
    case "bind":
      return <BindKeys k={e.key} value={value as string[]} />;
  }
}

function PresetBar() {
  const preset = useMenu((s) => String(s.values["graphics/preset"] ?? "high"));
  return (
    <div data-row className="mb-2 grid grid-cols-4 gap-2">
      {["low", "medium", "high", "ultra"].map((p) => (
        <button
          key={p}
          data-nav
          onClick={() => {
            sfx.move();
            setValue("graphics/preset", p);
          }}
          className={cn(
            "rounded-sm border py-3 font-q3 text-[0.95rem] tracking-[0.15em] transition-all",
            preset === p ? "border-glow/80 bg-red/25 text-ink-hot shadow-[0_0_1.2rem_rgb(219_36_16/0.35)]" : "border-white/10 bg-black/30 text-dim focus:border-glow/50 focus:text-ink",
          )}
        >
          {p}
        </button>
      ))}
    </div>
  );
}

function InfoPanel({ e, tab }: { e?: SettingEntry; tab: string }) {
  const values = useMenu((s) => s.values);
  const mYaw = useMenu((s) => s.init?.m_yaw ?? 0.022);
  const conflicts = useMenu((s) => (e ? s.conflicts[e.key] : undefined));
  const schema = useMenu((s) => s.init?.schema ?? []);
  const cm = 360 / (Number(values["mouse/sensitivity"]) * mYaw) / Number(values["mouse/dpi"] || 800) * 2.54;
  const optLabel = (en: SettingEntry, v: Value) => en.options?.find((o) => JSON.stringify(o[0]) === JSON.stringify(v))?.[1] ?? (Array.isArray(v) ? v.map(bindLabel).filter(Boolean).join(", ") || "Unbound" : String(v));
  return (
    <aside className="flex w-[21rem] shrink-0 flex-col gap-4">
      {tab === "crosshair" && <Crosshair />}
      {tab === "mouse" && (
        <div className="rounded-sm border border-white/10 bg-black/40 p-5">
          <div className="font-q3 text-[0.7rem] tracking-[0.3em] text-dim">One full turn</div>
          <div className="mt-1 font-q3 text-[3rem] leading-none text-ink-hot">{isFinite(cm) ? cm.toFixed(1) : "-"}<span className="ml-2 text-[1.2rem] text-dim">cm</span></div>
          <div className="mt-2 text-[0.9rem] text-dim">{Number(values["mouse/sensitivity"]).toFixed(2)} sensitivity x 0.022 m_yaw at {String(values["mouse/dpi"])} DPI</div>
        </div>
      )}
      <AnimatePresence mode="wait">
        {e && (
          <motion.div key={e.key} initial={{ opacity: 0, x: 8 }} animate={{ opacity: 1, x: 0 }} exit={{ opacity: 0 }} transition={{ duration: 0.15 }} className="rounded-sm border border-white/[0.07] bg-black/30 p-5">
            <div className="font-q3 text-[1.1rem] tracking-[0.06em] text-ink-hot">{e.label}</div>
            <p className="mt-2 text-[0.98rem] leading-relaxed text-ink/85">{e.tip}</p>
            {!e.wired && <p className="mt-3 text-[0.9rem] text-amber">Not yet wired: stored and saved now, used once the system it drives exists.</p>}
            {e.restart && <p className="mt-3 text-[0.9rem] text-dim">Takes effect after a restart.</p>}
            {conflicts && conflicts.length > 0 && (
              <p className="mt-3 text-[0.9rem] text-amber">Also bound to: {conflicts.map((k) => schema.find((s) => s.key === k)?.label ?? k).join(", ")}</p>
            )}
            <div className="mt-4 flex items-center justify-between border-t border-white/[0.06] pt-3 text-[0.85rem] text-dim">
              <span>Default: <span className="text-ink">{optLabel(e, e.default)}</span></span>
              <span className="font-q3 text-[0.65rem] tracking-[0.2em]">R reset</span>
            </div>
          </motion.div>
        )}
      </AnimatePresence>
    </aside>
  );
}

function DisplayConfirm({ previous, onDone }: { previous: Record<string, Value>; onDone: () => void }) {
  const ref = useRef<HTMLDivElement>(null);
  const [left, setLeft] = useState(15);
  const revert = () => {
    Object.entries(previous).forEach(([k, v]) => setValue(k, v));
    onDone();
  };
  useLayer(ref, { onBack: () => (sfx.out(), revert()) });
  useEffect(() => {
    const t = setInterval(() => setLeft((x) => x - 1), 1000);
    return () => clearInterval(t);
  }, []);
  useEffect(() => {
    if (left <= 0) revert();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [left]);
  return (
    <motion.div ref={ref} className="absolute inset-0 z-40 grid place-items-center bg-black/70" initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }}>
      <div className="flex w-[30rem] flex-col items-center gap-5 rounded-sm border border-white/10 bg-glass p-9 text-center">
        <div className="font-q3 text-[1.4rem] tracking-[0.08em] text-ink-hot">Keep these display settings?</div>
        <div className="font-q3 text-[3rem] leading-none text-glow tabular-nums">{left}</div>
        <div className="text-dim">Reverting automatically if you don&apos;t confirm.</div>
        <div data-row className="flex gap-3">
          <Button primary autoFocus onClick={onDone}>Keep</Button>
          <Button onClick={revert}>Revert</Button>
        </div>
      </div>
    </motion.div>
  );
}

export function ImportCode({ onDone }: { onDone: () => void }) {
  const ref = useRef<HTMLDivElement>(null);
  const [code, setCode] = useState("");
  useLayer(ref, { onBack: () => (sfx.out(), onDone()) });
  return (
    <motion.div ref={ref} className="absolute inset-0 z-40 grid place-items-center bg-black/70" initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }}>
      <div className="flex w-[36rem] flex-col gap-4 rounded-sm border border-white/10 bg-glass p-8">
        <div className="font-q3 text-[1.3rem] tracking-[0.08em] text-ink-hot">Paste a config or crosshair code</div>
        <input
          data-nav
          data-autofocus
          value={code}
          onChange={(e) => setCode(e.target.value)}
          className="h-11 rounded-sm border border-white/10 bg-black/60 px-3 font-mono text-[0.9rem] text-ink-hot focus:border-glow/70"
        />
        <div data-row className="flex gap-3">
          <Button
            primary
            onClick={() => {
              void send("import_code", { code });
              onDone();
            }}
          >
            Import
          </Button>
          <Button onClick={onDone}>Cancel</Button>
        </div>
      </div>
    </motion.div>
  );
}
