"use client";
// The crosshair editor. In the pause menu the panel sits on the left and the preview is the real
// crosshair in the match behind it (Godot's HUD, under this transparent page): every change shows
// there at once, a weapon tab switches the HUD to that weapon's crosshair, and "Test hit" flashes
// the hit color on it. From the main menu (Settings > Crosshair) it previews over a level shot.
// Shapes, size, thickness, gap, dot, outline, colors, color by health, presets, per-weapon
// crosshairs and the crosshair code (copy / paste) are all Settings' crosshair/* entries.
import { useEffect, useRef, useState } from "react";
import { AnimatePresence, motion } from "motion/react";
import { useMenu, setValue, toast } from "@/lib/store";
import { send } from "@/lib/bridge";
import { sfx } from "@/lib/sound";
import { cn } from "@/lib/cn";
import type { Value } from "@/lib/types";
import PagePanel, { SectionTitle } from "../PagePanel";
import Slider from "../controls/Slider";
import Toggle from "../controls/Toggle";
import ColorField from "../controls/ColorField";
import Button from "../controls/Button";
import Crosshair, { CrosshairGlyph, glyphFrom, isQ3Shape } from "./Crosshair";
import { ImportCode } from "./SettingsPage";
import type { PageProps } from "./index";

const SHAPES = ["none", "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "cross", "dot", "circle", "t"];
// weapon number, settings key, HUD icon, name
const WEAPONS: [number, string, string, string][] = [
  [1, "gauntlet", "gauntlet", "Gauntlet"], [2, "machinegun", "machinegun", "Machine gun"], [3, "shotgun", "shotgun", "Shotgun"],
  [4, "grenadel", "grenade", "Grenade launcher"], [5, "rocketl", "rocket", "Rocket launcher"], [6, "lightning", "lightning", "Lightning gun"],
  [7, "railgun", "railgun", "Railgun"], [8, "plasma", "plasma", "Plasma gun"], [9, "bfg", "bfg", "BFG10K"],
];
const PRESETS: [string, Record<string, Value>][] = [
  ["Quake 3", { "crosshair/shape": "c", "crosshair/size": 24, "crosshair/color": "#ffffff", "crosshair/outline": false, "crosshair/dot": false }],
  ["Arena cross", { "crosshair/shape": "cross", "crosshair/size": 20, "crosshair/thickness": 2, "crosshair/gap": 4, "crosshair/color": "#ffffff", "crosshair/outline": true, "crosshair/dot": false }],
  ["Sharp dot", { "crosshair/shape": "dot", "crosshair/size": 10, "crosshair/thickness": 2, "crosshair/color": "#39ff6a", "crosshair/outline": true, "crosshair/dot": false }],
  ["Ring", { "crosshair/shape": "circle", "crosshair/size": 28, "crosshair/thickness": 2, "crosshair/color": "#ffad33", "crosshair/outline": true, "crosshair/dot": true }],
  ["Rail T", { "crosshair/shape": "t", "crosshair/size": 22, "crosshair/thickness": 2, "crosshair/gap": 3, "crosshair/color": "#4fc3ff", "crosshair/outline": true, "crosshair/dot": false }],
];

export default function CrosshairEditor({ back }: PageProps) {
  const live = useMenu((s) => s.init?.context === "pause");
  const values = useMenu((s) => s.values);
  const schema = useMenu((s) => s.init?.schema ?? []);
  const held = useMenu((s) => s.match?.weapon ?? 0);
  const [weapon, setWeapon] = useState(0);          // 0: every weapon (the main crosshair)
  const [importing, setImporting] = useState(false);
  const [hit, setHit] = useState(false);
  const range = (key: string) => schema.find((e) => e.key === key);
  const perWeapon = !!values["crosshair/per_weapon"];
  const wkey = WEAPONS.find((w) => w[0] === weapon)?.[1] ?? "";
  const weaponShape = wkey ? String(values[`crosshair/weapon_${wkey}`] ?? "default") : "default";
  const shownShape = wkey && perWeapon && weaponShape !== "default" ? weaponShape : String(values["crosshair/shape"] ?? "c");

  // the HUD behind shows the tab's weapon (0: the weapon in hand); leaving puts it back
  useEffect(() => {
    if (live) void send("crosshair_preview", { weapon: weapon || held });
  }, [live, weapon, held]);
  useEffect(() => () => void (live && send("crosshair_preview", { weapon: 0 })), [live]);

  const pickShape = (s: string) => {
    sfx.move();
    if (!wkey) return setValue("crosshair/shape", s);
    setValue(`crosshair/weapon_${wkey}`, s);
    if (!perWeapon) setValue("crosshair/per_weapon", true);
  };
  const testHit = () => {
    setHit(true);
    setTimeout(() => setHit(false), 220);
    if (live) void send("crosshair_preview", { weapon: weapon || held, hit: true });
  };

  const body = (
    <div className="flex h-full min-h-0 gap-7 px-10 pb-5 pt-4">
      <div className="min-h-0 flex-1 overflow-y-auto pr-2">
        {/* every weapon, or one weapon's own crosshair */}
        <div data-row className="mb-1 flex flex-wrap gap-1.5">
          <WeaponTab active={weapon === 0} onClick={() => setWeapon(0)} label="All" />
          {WEAPONS.map(([n, key, icon, name]) => (
            <WeaponTab
              key={n}
              active={weapon === n}
              onClick={() => setWeapon(n)}
              icon={`/q3/hud/iconw_${icon}.png`}
              title={name}
              marked={perWeapon && String(values[`crosshair/weapon_${key}`] ?? "default") !== "default"}
            />
          ))}
        </div>
        <p className="mb-2 min-h-[1.4rem] text-[0.9rem] text-dim">
          {weapon === 0
            ? "The crosshair for every weapon without its own."
            : `${WEAPONS[weapon - 1][3]}: pick a shape to give it its own${perWeapon ? "" : " (turns per-weapon crosshairs on)"}.`}
        </p>

        <SectionTitle className="mt-4">Shape</SectionTitle>
        <div data-grid="5" className="grid grid-cols-5 gap-2">
          {wkey && (
            <ShapeTile
              label="Same as all"
              active={weaponShape === "default"}
              onClick={() => (sfx.move(), setValue(`crosshair/weapon_${wkey}`, "default"))}
            >
              <span className="font-q3 text-[0.7rem] tracking-[0.12em] text-dim">All</span>
            </ShapeTile>
          )}
          {SHAPES.map((s) => (
            <ShapeTile key={s} label={isQ3Shape(s) ? `Quake 3 ${s.toUpperCase()}` : s === "t" ? "T" : s} active={(wkey ? weaponShape : values["crosshair/shape"]) === s} onClick={() => pickShape(s)}>
              {s === "none" ? <span className="font-q3 text-[0.7rem] tracking-[0.12em] text-dim">None</span> : <CrosshairGlyph {...glyphFrom(values, s)} size={Math.min(Number(values["crosshair/size"] ?? 24), 34)} />}
            </ShapeTile>
          ))}
        </div>

        <SectionTitle className="mt-7">Look</SectionTitle>
        <Field label="Size">
          <Slider value={Number(values["crosshair/size"])} min={range("crosshair/size")?.min ?? 8} max={range("crosshair/size")?.max ?? 96} step={1} onChange={(v) => setValue("crosshair/size", v)} format={(v) => `${v} px`} />
        </Field>
        <Field label="Thickness" dim={isQ3Shape(shownShape)}>
          <Slider value={Number(values["crosshair/thickness"])} min={range("crosshair/thickness")?.min ?? 1} max={range("crosshair/thickness")?.max ?? 8} step={1} onChange={(v) => setValue("crosshair/thickness", v)} format={(v) => `${v} px`} />
        </Field>
        <Field label="Gap" dim={!["cross", "t"].includes(shownShape)}>
          <Slider value={Number(values["crosshair/gap"])} min={range("crosshair/gap")?.min ?? 0} max={range("crosshair/gap")?.max ?? 32} step={1} onChange={(v) => setValue("crosshair/gap", v)} format={(v) => `${v} px`} />
        </Field>
        <Field label="Center dot">
          <Toggle value={!!values["crosshair/dot"]} onChange={(v) => setValue("crosshair/dot", v)} />
        </Field>
        <Field label="Outline">
          <Toggle value={!!values["crosshair/outline"]} onChange={(v) => setValue("crosshair/outline", v)} />
        </Field>

        <SectionTitle className="mt-7">Color</SectionTitle>
        <Field label="Color">
          <ColorField value={String(values["crosshair/color"])} onChange={(v) => setValue("crosshair/color", v)} />
        </Field>
        <Field label="Hit color">
          <div data-row className="flex w-[22rem] items-center gap-2">
            <div className="flex-1"><ColorField wide value={String(values["crosshair/hit_color"])} onChange={(v) => setValue("crosshair/hit_color", v)} /></div>
            <Button small onClick={testHit}>Test hit</Button>
          </div>
        </Field>
        <Field label="Color by health">
          <Toggle value={!!values["crosshair/health_color"]} onChange={(v) => setValue("crosshair/health_color", v)} />
        </Field>
        <Field label="Per-weapon crosshairs">
          <Toggle value={perWeapon} onChange={(v) => setValue("crosshair/per_weapon", v)} />
        </Field>

        <SectionTitle className="mt-7">Presets</SectionTitle>
        <div data-row className="grid grid-cols-5 gap-2">
          {PRESETS.map(([name, set]) => (
            <button
              key={name}
              data-nav
              onClick={() => {
                sfx.move();
                Object.entries(set).forEach(([k, v]) => setValue(k, v));
                toast(`${name} crosshair.`);
              }}
              className="flex flex-col items-center gap-2 rounded-sm border border-white/10 bg-black/30 px-2 pb-2 pt-3 transition-all focus:border-glow/70 focus:bg-red/15"
            >
              <div className="grid h-12 place-items-center">
                <CrosshairGlyph
                  shape={String(set["crosshair/shape"])}
                  size={Number(set["crosshair/size"])}
                  color={String(set["crosshair/color"])}
                  thickness={Number(set["crosshair/thickness"] ?? 2)}
                  gap={Number(set["crosshair/gap"] ?? 4)}
                  dot={!!set["crosshair/dot"]}
                  outline={!!set["crosshair/outline"]}
                />
              </div>
              <span className="font-q3 text-[0.65rem] tracking-[0.12em] text-dim">{name}</span>
            </button>
          ))}
        </div>
      </div>
      {!live && (
        <aside className="flex w-[19rem] shrink-0 flex-col gap-3">
          <Crosshair shape={shownShape} />
          <p className="text-[0.85rem] text-dim">In a match, the pause menu edits the real crosshair over your game.</p>
        </aside>
      )}
    </div>
  );

  return (
    <>
      <PagePanel
        title="Crosshair"
        back={back}
        placement={live ? "bottom-[6.5rem] left-[3.5rem] top-[6rem] w-[46rem]" : undefined}
        footer={
          <div data-row className="flex w-full items-center gap-3">
            <Button small onClick={() => void send("export_code", { prefix: "crosshair/" })}>Copy crosshair code</Button>
            <Button small onClick={() => setImporting(true)}>Paste code</Button>
            <Button small onClick={() => (send("reset_tab", { tab: "crosshair" }), toast("Crosshair reset to Quake 3's."))}>Reset</Button>
            <span className="ml-auto text-[0.85rem] text-faint">Saved as you go.</span>
          </div>
        }
      >
        {body}
        <AnimatePresence>{importing && <ImportCode onDone={() => setImporting(false)} />}</AnimatePresence>
      </PagePanel>
      {live && <LiveMarker hit={hit} weapon={weapon ? WEAPONS[weapon - 1][3] : ""} />}
    </>
  );
}

// A frame around the screen's center, where the real crosshair sits in the match behind.
function LiveMarker({ hit, weapon }: { hit: boolean; weapon: string }) {
  return (
    <motion.div className="pointer-events-none absolute left-1/2 top-1/2 h-[11rem] w-[11rem] -translate-x-1/2 -translate-y-1/2" initial={{ opacity: 0, scale: 1.15 }} animate={{ opacity: 1, scale: 1 }} exit={{ opacity: 0 }}>
      {["left-0 top-0 border-l-2 border-t-2", "right-0 top-0 border-r-2 border-t-2", "bottom-0 left-0 border-b-2 border-l-2", "bottom-0 right-0 border-b-2 border-r-2"].map((c) => (
        <span key={c} className={cn("absolute h-6 w-6 transition-colors", hit ? "border-glow" : "border-white/45", c)} />
      ))}
      <div className="absolute -bottom-8 left-1/2 -translate-x-1/2 whitespace-nowrap font-q3 text-[0.65rem] tracking-[0.3em] text-white/60">
        Live {weapon && <span className="text-amber">· {weapon}</span>}
      </div>
    </motion.div>
  );
}

function WeaponTab({ active, onClick, label, icon, title, marked }: { active: boolean; onClick: () => void; label?: string; icon?: string; title?: string; marked?: boolean }) {
  return (
    <button
      data-nav
      title={title}
      onClick={() => {
        sfx.move();
        onClick();
      }}
      className={cn(
        "relative grid h-11 min-w-[2.9rem] place-items-center rounded-sm border px-2 transition-all",
        active ? "border-glow/80 bg-red/25 shadow-[0_0_1rem_rgb(219_36_16/0.35)]" : "border-white/10 bg-black/30 focus:border-glow/50",
      )}
    >
      {icon ? (
        // eslint-disable-next-line @next/next/no-img-element
        <img src={icon} alt={title} className={cn("h-7 w-7", !active && "opacity-70")} />
      ) : (
        <span className={cn("font-q3 text-[0.8rem] tracking-[0.12em]", active ? "text-ink-hot" : "text-dim")}>{label}</span>
      )}
      {marked && <span className="absolute right-1 top-1 h-1.5 w-1.5 rounded-full bg-amber" title="Has its own crosshair" />}
    </button>
  );
}

function ShapeTile({ children, label, active, onClick }: { children: React.ReactNode; label: string; active: boolean; onClick: () => void }) {
  const ref = useRef<HTMLButtonElement>(null);
  return (
    <button
      ref={ref}
      data-nav
      title={label}
      onClick={onClick}
      className={cn(
        "relative grid aspect-square place-items-center overflow-hidden rounded-sm border bg-black/40 transition-all",
        active ? "border-glow/80 shadow-[inset_0_0_1.2rem_rgb(219_36_16/0.35)]" : "border-white/10 focus:border-glow/50 focus:bg-white/[0.04]",
      )}
    >
      {children}
      {active && <span className="absolute bottom-1 right-1 h-1.5 w-1.5 rounded-full bg-glow" />}
    </button>
  );
}

function Field({ label, children, dim }: { label: string; children: React.ReactNode; dim?: boolean }) {
  return (
    <div className={cn("flex min-h-[3rem] items-center gap-4 rounded-sm px-3 py-1 transition-opacity", dim && "opacity-45")}>
      <span className="flex-1 text-[1.02rem] font-medium text-ink">{label}</span>
      {children}
    </div>
  );
}
