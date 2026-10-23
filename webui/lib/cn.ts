export function cn(...c: (string | false | null | undefined)[]) {
  return c.filter(Boolean).join(" ");
}
