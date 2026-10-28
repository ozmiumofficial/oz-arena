"use client";
// While a bind is being captured, the whole menu waits for the input (main menu and pause menu).
// The page captures keys, mouse buttons and the wheel itself and sends them as bind_input; Godot
// captures controllers. Keys forwarded to Godot by the web view arrived late (the Enter that opened
// the slot bound itself) and the pause menu forwards none, so keyboard binds there never took.
import { useEffect } from "react";
import { AnimatePresence, motion } from "motion/react";
import { useMenu, toast } from "@/lib/store";
import { send } from "@/lib/bridge";
import { codeToGodot, mouseToGodot } from "@/lib/binds";
import { holdBack } from "@/lib/nav";
import { sfx } from "@/lib/sound";

const TIMEOUT_MS = 10000;

export default function CaptureVeil() {
  const capture = useMenu((s) => s.capture);
  const label = useMenu((s) => (s.capture ? s.init?.schema.find((e) => e.key === s.capture!.key)?.label : ""));
  const pad = capture?.slot === 2;
  useEffect(() => {
    if (!capture) return;
    let sent = false;
    let byMouse = false;
    const finish = (type: string, extra: Record<string, unknown> = {}) => {
      if (sent) return;
      sent = true;
      void send(type, extra);
    };
    const stop = (e: Event) => {
      e.preventDefault();
      e.stopImmediatePropagation();
    };
    const onKey = (e: KeyboardEvent) => {
      stop(e);
      if (e.repeat) return;
      if (e.code === "Escape") return finish("cancel_capture");
      if (e.code === "Backspace" || e.code === "Delete") return finish("bind_input", { value: "" });
      if (pad) return sfx.buzz();
      const name = codeToGodot(e.code);
      if (!name) return sfx.buzz();
      finish("bind_input", { value: "key:" + name });
    };
    const onMouse = (e: MouseEvent) => {
      stop(e);
      // a right click opens the context menu as the button comes up, which would go back a page
      if (e.button === 2) holdBack(1000);
      if (pad) return sfx.buzz();
      byMouse = true;
      finish("bind_input", { value: "mouse:" + mouseToGodot(e.button) });
    };
    const onWheel = (e: WheelEvent) => {
      stop(e);
      if (pad || (!e.deltaY && !e.deltaX)) return;
      const n = e.deltaY ? (e.deltaY < 0 ? 4 : 5) : e.deltaX < 0 ? 6 : 7;
      finish("bind_input", { value: "mouse:" + n });
    };
    // side buttons would send the web view back or forward, and the click would land under the veil
    const swallow = (e: Event) => stop(e);
    const opts = { capture: true, passive: false };
    // the click that opened the slot is still finishing; listen from the next event on
    const t0 = setTimeout(() => {
      window.addEventListener("keydown", onKey, opts);
      window.addEventListener("keyup", swallow, opts);
      window.addEventListener("mousedown", onMouse, opts);
      window.addEventListener("wheel", onWheel, opts);
      for (const ev of ["mouseup", "click", "auxclick", "pointerdown", "pointerup"]) window.addEventListener(ev, swallow, opts);
    }, 0);
    const t1 = setTimeout(() => {
      toast("Bind capture timed out.", "warn");
      finish("cancel_capture");
    }, TIMEOUT_MS);
    return () => {
      clearTimeout(t0);
      clearTimeout(t1);
      window.removeEventListener("keydown", onKey, opts);
      window.removeEventListener("keyup", swallow, opts);
      window.removeEventListener("mousedown", onMouse, opts);
      window.removeEventListener("wheel", onWheel, opts);
      for (const ev of ["mouseup", "click", "auxclick", "pointerdown", "pointerup"]) window.removeEventListener(ev, swallow, opts);
      // the button still coming up after a mouse bind (mouseup, click) must not press what's under it
      if (!byMouse) return;
      const late = (e: Event) => stop(e);
      for (const ev of ["mouseup", "click", "auxclick"]) window.addEventListener(ev, late, { capture: true, once: true });
      setTimeout(() => {
        for (const ev of ["mouseup", "click", "auxclick"]) window.removeEventListener(ev, late, { capture: true });
      }, 400);
    };
  }, [capture, pad]);
  return (
    <AnimatePresence>
      {capture && (
        <motion.div className="absolute inset-0 z-50 grid place-items-center bg-black/60" initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }}>
          <motion.div initial={{ scale: 0.95 }} animate={{ scale: 1 }} className="rounded-sm border border-glow/50 bg-glass px-14 py-10 text-center shadow-[0_0_4rem_rgb(219_36_16/0.35)]">
            <div className="font-q3 text-[0.8rem] tracking-[0.3em] text-dim">{pad ? "Controller bind" : "Bind"}</div>
            <div className="mt-2 font-q3 text-[2rem] tracking-[0.06em] text-ink-hot">{label}</div>
            <div className="mt-5 animate-pulse font-q3 text-[1rem] tracking-[0.2em] text-glow">{pad ? "Press a controller button or trigger" : "Press a key, mouse button or wheel"}</div>
            <div className="mt-4 text-[0.9rem] text-dim">Esc cancels, Backspace clears</div>
          </motion.div>
        </motion.div>
      )}
    </AnimatePresence>
  );
}
