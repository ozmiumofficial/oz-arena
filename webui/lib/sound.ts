// Quake 3's menu sounds, played by Godot (UiSound) when the menu asks. The rules follow
// q3_ui (ui_qmenu.c / ui_atoms.c):
//   move  the cursor lands on a different item (keys or mouse), an item with an action is
//         activated, a spinner/toggle/slider value changes
//   in    a menu opens, once its first frame has drawn
//   out   a menu closes (Escape, Backspace, right mouse button)
//   buzz  a slider pushed past its end, a list scrolled past its end, a refused action
//   change  the showcase changes subject (Godot plays that one itself)
import { send } from "./bridge";

let quietUntil = 0;

function play(name: string) {
  void send("sound", { name });
}

export const sfx = {
  move() {
    if (performance.now() >= quietUntil) play("move");
  },
  in() {
    requestAnimationFrame(() => requestAnimationFrame(() => play("in")));
  },
  out() {
    play("out");
  },
  buzz() {
    play("buzz");
  },
  // programmatic focus (a page opening on its first item) is silent, as in q3_ui
  quiet(ms = 120) {
    quietUntil = performance.now() + ms;
  },
};

// Sliders: q3_ui plays one move per step; dragging is throttled so it stays a tick, not a buzz saw.
let lastTick = 0;
export function tick() {
  const t = performance.now();
  if (t - lastTick > 70) {
    lastTick = t;
    sfx.move();
  }
}
