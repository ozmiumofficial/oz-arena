// Bind strings use Godot's names, the same as scripts/settings/settings.gd:
// "key:W", "mouse:1" (1 left, 2 right, 3 middle, 4/5 wheel up/down, 8/9 side), "joy:0", "joyaxis:5:1".

const MOUSE: Record<number, string> = { 1: "Mouse 1", 2: "Mouse 2", 3: "Mouse 3", 4: "Wheel up", 5: "Wheel down", 6: "Wheel left", 7: "Wheel right", 8: "Mouse 4", 9: "Mouse 5" };
const PAD: Record<number, string> = { 0: "Pad A", 1: "Pad B", 2: "Pad X", 3: "Pad Y", 4: "Back", 5: "Guide", 6: "Start", 7: "Left stick", 8: "Right stick", 9: "Left bumper", 10: "Right bumper", 11: "D-pad up", 12: "D-pad down", 13: "D-pad left", 14: "D-pad right" };
const AXES: Record<number, string> = { 0: "Left stick X", 1: "Left stick Y", 2: "Right stick X", 3: "Right stick Y", 4: "Left trigger", 5: "Right trigger" };

export function bindLabel(s: string): string {
  if (!s) return "";
  const [kind, a, b] = s.split(":");
  switch (kind) {
    case "key":
      return a;
    case "mouse":
      return MOUSE[+a] ?? `Mouse ${a}`;
    case "joy":
      return PAD[+a] ?? `Pad ${a}`;
    case "joyaxis": {
      const n = AXES[+a] ?? `Axis ${a}`;
      return +a >= 4 ? n : `${n} ${+b > 0 ? "+" : "-"}`;
    }
  }
  return s;
}

export function isPadBind(s: string) {
  return s.startsWith("joy");
}

// Browser preview only: KeyboardEvent.code -> Godot key name.
const CODES: Record<string, string> = {
  Space: "Space", Enter: "Enter", Tab: "Tab", Backquote: "QuoteLeft", Minus: "Minus", Equal: "Equal",
  BracketLeft: "BracketLeft", BracketRight: "BracketRight", Semicolon: "Semicolon", Quote: "Apostrophe",
  Comma: "Comma", Period: "Period", Slash: "Slash", Backslash: "BackSlash", CapsLock: "CapsLock",
  ArrowUp: "Up", ArrowDown: "Down", ArrowLeft: "Left", ArrowRight: "Right",
  ShiftLeft: "Shift", ShiftRight: "Shift", ControlLeft: "Ctrl", ControlRight: "Ctrl", AltLeft: "Alt", AltRight: "Alt",
  Insert: "Insert", Home: "Home", End: "End", PageUp: "PageUp", PageDown: "PageDown",
};

export function codeToGodot(code: string): string {
  if (code.startsWith("Key")) return code.slice(3);
  if (code.startsWith("Digit")) return code.slice(5);
  if (/^F\d+$/.test(code)) return code;
  if (code.startsWith("Numpad")) return "Kp " + code.slice(6);
  return CODES[code] ?? code;
}

export function mouseToGodot(button: number): number {
  return [1, 3, 2, 8, 9][button] ?? 1;
}
