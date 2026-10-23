"use client";
import { useEffect, useState } from "react";

// Text input; commits on Enter or when focus leaves.
export default function TextField({ value, onCommit, max = 64, placeholder, wide }: {
  value: string;
  onCommit: (v: string) => void;
  max?: number;
  placeholder?: string;
  wide?: boolean;
}) {
  const [v, setV] = useState(value);
  useEffect(() => setV(value), [value]);
  return (
    <input
      data-nav
      value={v}
      maxLength={max}
      placeholder={placeholder}
      spellCheck={false}
      onChange={(e) => setV(e.target.value)}
      onBlur={() => v !== value && onCommit(v)}
      onKeyDown={(e) => e.key === "Enter" && (e.currentTarget as HTMLInputElement).blur()}
      className={`${wide ? "w-full" : "w-[22rem]"} h-10 rounded-sm border border-white/10 bg-black/50 px-3 text-[1rem] text-ink-hot placeholder:text-faint focus:border-glow/70 focus:bg-black/70`}
    />
  );
}
