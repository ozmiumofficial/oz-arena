// Keyboard, mouse and controller navigation, one model for all three (like q3_ui's cursor):
// - Each open menu or page is a layer; only the top layer takes input.
// - Items are elements with data-nav. Hovering an item focuses it, so the mouse cursor and the
//   keyboard/controller cursor are the same thing. Focus moving to a different item plays "move".
// - Up/Down walk the items in order and wrap (Menu_AdjustCursor wraps). Inside a data-grid
//   container they move by rows and stop at the edges with a buzz.
// - Left/Right go to the focused item if it takes them (data-lr: sliders, spinners), else to the
//   grid, else to the layer (the main menu uses Right to enter the Play sub-menu).
// - Escape, Backspace and the right mouse button go back; Q/E (or the bumpers) switch tabs.
// Godot sends the same commands for controllers as {"type": "nav", "dir": ...}.
"use client";

import { useEffect, type RefObject } from "react";
import { sfx } from "./sound";
import { getState } from "./store";

export type NavDir = "up" | "down" | "left" | "right" | "accept" | "back" | "tab_prev" | "tab_next" | "social";

interface Layer {
  el: HTMLElement;
  onBack?: () => void;
  onTab?: (d: number) => void;
  onSide?: (d: number) => boolean;
}

const layers: Layer[] = [];
let lastFocused: Element | null = null;

function top() {
  return layers[layers.length - 1];
}

function items(root: HTMLElement): HTMLElement[] {
  return [...root.querySelectorAll<HTMLElement>("[data-nav]")].filter(
    (e) => !e.closest("[data-nav-off]") && e.offsetParent !== null && !(e as HTMLButtonElement).disabled,
  );
}

// Items side by side in a data-row (bind slots, footer buttons): Left/Right move along the row;
// Up/Down only see the row's first item.
function vertical(list: HTMLElement[]) {
  return list.filter((e) => {
    const row = e.closest("[data-row]");
    return !row || items(row as HTMLElement)[0] === e;
  });
}

export function focusEl(el: HTMLElement | null | undefined, quiet = false) {
  if (!el) return;
  if (quiet) sfx.quiet();
  el.focus({ preventScroll: true });
  el.scrollIntoView({ block: "nearest", behavior: "smooth" });
}

export function focusFirst(root: HTMLElement | null) {
  if (!root) return;
  // a page opens on its first control, not on a text field (a search box would swallow keys)
  const list = items(root);
  const first = root.querySelector<HTMLElement>("[data-autofocus]") ?? list.find((e) => !typing(e)) ?? list[0];
  focusEl(first, true);
}

export function useLayer(ref: RefObject<HTMLElement | null>, opts: Omit<Layer, "el"> & { autofocus?: boolean } = {}) {
  useEffect(() => {
    const el = ref.current;
    if (!el) return;
    const layer: Layer = { el, onBack: opts.onBack, onTab: opts.onTab, onSide: opts.onSide };
    layers.push(layer);
    if (opts.autofocus !== false) requestAnimationFrame(() => focusFirst(el));
    return () => {
      const i = layers.indexOf(layer);
      if (i >= 0) layers.splice(i, 1);
      const below = top();
      if (below && !below.el.contains(document.activeElement)) {
        const prev = below.el.querySelector<HTMLElement>("[data-nav][data-current]");
        requestAnimationFrame(() => (prev ? focusEl(prev, true) : focusFirst(below.el)));
      }
    };
    // the handlers are read through the layer object; re-registering on every render would reorder layers
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [ref]);
  // keep the latest handlers without re-registering the layer
  useEffect(() => {
    const l = layers.find((x) => x.el === ref.current);
    if (l) Object.assign(l, { onBack: opts.onBack, onTab: opts.onTab, onSide: opts.onSide });
  });
}

// The Friends panel opens from anywhere (F, or Y on a controller), whatever layer is on top.
let socialToggle: (() => void) | null = null;
export function setSocialToggle(f: (() => void) | null) {
  socialToggle = f;
}

export function nav(dir: NavDir) {
  if (dir === "social") {
    if (!getState().capture) socialToggle?.();
    return;
  }
  const layer = top();
  if (!layer || getState().capture) return;
  const cur = document.activeElement as HTMLElement | null;
  const list = items(layer.el);
  const inLayer = cur && layer.el.contains(cur) && cur.hasAttribute("data-nav");
  switch (dir) {
    case "back":
      layer.onBack?.();
      return;
    case "tab_prev":
    case "tab_next":
      layer.onTab?.(dir === "tab_next" ? 1 : -1);
      return;
    case "accept":
      if (inLayer) cur!.click();
      return;
  }
  if (!inLayer) {
    focusFirst(layer.el);
    return;
  }
  const grid = cur!.closest<HTMLElement>("[data-grid]");
  const row = cur!.closest<HTMLElement>("[data-row]");
  if (dir === "left" || dir === "right") {
    const d = dir === "left" ? -1 : 1;
    if (cur!.hasAttribute("data-lr")) {
      cur!.dispatchEvent(new CustomEvent("nav-lr", { detail: d }));
      return;
    }
    if (row) {
      const r = items(row);
      const i = r.indexOf(cur!) + d;
      if (i >= 0 && i < r.length) return focusEl(r[i]);
    }
    if (grid) {
      const g = items(grid);
      const i = g.indexOf(cur!) + d;
      if (i < 0 || i >= g.length) sfx.buzz();
      else focusEl(g[i]);
      return;
    }
    layer.onSide?.(d);
    return;
  }
  const d = dir === "up" ? -1 : 1;
  if (grid) {
    const g = items(grid);
    const cols = Number(grid.dataset.grid) || 1;
    const i = g.indexOf(cur!) + d * cols;
    if (i >= 0 && i < g.length) return focusEl(g[i]);
    // leaving the grid by its top or bottom edge: continue with the items before/after it
    const outside = list.filter((e) => !grid.contains(e));
    const firstIdx = list.indexOf(g[0]);
    const before = outside.filter((e) => list.indexOf(e) < firstIdx);
    const after = outside.filter((e) => list.indexOf(e) > firstIdx);
    const t = d < 0 ? before[before.length - 1] : after[0];
    if (t) focusEl(t);
    else sfx.buzz();
    return;
  }
  const vlist = vertical(list);
  const from = row ? items(row)[0] : cur!;
  const i = vlist.indexOf(from) + d;
  let target = vlist[(i + vlist.length) % vlist.length];
  const tg = target?.closest<HTMLElement>("[data-grid]");
  if (tg) {
    // entering a grid from above lands on its first item, from below on its last row's first
    const g = items(tg);
    target = d > 0 ? g[0] : g[Math.max(0, g.length - (Number(tg.dataset.grid) || 1))];
  }
  focusEl(target);
}

function typing(t: EventTarget | null) {
  const el = t as HTMLElement | null;
  return !!el && (el.tagName === "INPUT" || el.tagName === "TEXTAREA") && (el as HTMLInputElement).type !== "range";
}

// Bind capture takes a right click as Mouse 2; the context menu that follows as the button comes
// up must not go back a page as well.
let backHeldUntil = 0;
export function holdBack(ms: number) {
  backHeldUntil = performance.now() + ms;
}

let installed = false;
export function installNav() {
  if (installed) return;
  installed = true;
  window.addEventListener("keydown", (e) => {
    if (getState().capture) return;
    const t = typing(e.target);
    const map: Record<string, NavDir> = {
      ArrowUp: "up", ArrowDown: "down", ArrowLeft: "left", ArrowRight: "right", Escape: "back",
      PageUp: "tab_prev", PageDown: "tab_next",
    };
    let dir = map[e.key];
    if (!t && (e.key === "q" || e.key === "Q")) dir = "tab_prev";
    if (!t && (e.key === "e" || e.key === "E")) dir = "tab_next";
    if (!t && e.key === "Backspace") dir = "back";
    if (!t && (e.key === "f" || e.key === "F") && !e.ctrlKey && !e.altKey && !e.metaKey) dir = "social";
    if (!dir) return;
    if (t && (dir === "left" || dir === "right")) return; // caret movement in text fields
    e.preventDefault();
    nav(dir);
  });
  window.addEventListener("contextmenu", (e) => {
    e.preventDefault();
    if (!getState().capture && performance.now() > backHeldUntil) nav("back");
  });
  // the mouse cursor is the menu cursor: hovering an item focuses it
  document.addEventListener("mouseover", (e) => {
    const el = (e.target as HTMLElement).closest<HTMLElement>("[data-nav]");
    const layer = top();
    if (!el || !layer || !layer.el.contains(el) || el === document.activeElement) return;
    if (typing(document.activeElement)) return;
    el.focus({ preventScroll: true });
  });
  document.addEventListener("focusin", (e) => {
    const el = e.target as HTMLElement;
    if (!el.hasAttribute?.("data-nav")) return;
    if (el !== lastFocused) {
      lastFocused = el;
      sfx.move();
    }
  });
}
