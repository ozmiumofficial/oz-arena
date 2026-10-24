"use client";

import { Player, PlayerRef } from "@remotion/player";
import { useEffect, useRef, useState } from "react";
import { Trailer } from "../remotion/Trailer";
import { FPS, HEIGHT, WIDTH, barFrame, beats, durationInFrames, edit } from "../remotion/lib/timeline";

const KIND_COLOR: Record<string, string> = {
  opener: "#7a5c3a", compare: "#db2410", clip: "#a8521f", black: "#2a2420", placeholder: "#3d3530",
  logo: "#ff4a1c", montage: "#ffad33", end: "#b3170a",
};

const clock = (frame: number) => {
  const s = frame / FPS;
  return `${Math.floor(s / 60)}:${(s % 60).toFixed(1).padStart(4, "0")}`;
};

/** The edit in a player, and a timeline of its segments over the track's bars (click to jump). */
export function Preview() {
  const player = useRef<PlayerRef>(null);
  const [frame, setFrame] = useState(0);
  useEffect(() => {
    const p = player.current;
    if (!p) return;
    const on = (e: { detail: { frame: number } }) => setFrame(e.detail.frame);
    p.addEventListener("frameupdate", on);
    return () => p.removeEventListener("frameupdate", on);
  }, []);
  const pct = (f: number) => `${(f / durationInFrames) * 100}%`;
  const now = edit.segments.find((s) => barFrame(s.from) <= frame && frame < barFrame(s.to));
  return (
    <main style={{ maxWidth: 1400, margin: "0 auto", padding: "24px 16px 40px" }}>
      <header style={{ display: "flex", alignItems: "baseline", gap: 16, flexWrap: "wrap", marginBottom: 14 }}>
        <h1 style={{ margin: 0, fontSize: 22, letterSpacing: "0.08em" }}>Trailer edit</h1>
        <span style={{ color: "var(--dim)", fontSize: 14 }}>
          {beats.audio} · {beats.tempo} BPM · {beats.bars.length} bars · drop at {beats.drop.toFixed(2)} s
        </span>
      </header>
      <Player
        ref={player}
        component={Trailer}
        durationInFrames={durationInFrames}
        fps={FPS}
        compositionWidth={WIDTH}
        compositionHeight={HEIGHT}
        controls
        style={{ width: "100%", aspectRatio: "16 / 9", background: "black" }}
      />
      <div style={{ display: "flex", justifyContent: "space-between", margin: "10px 0 6px", fontSize: 14, color: "var(--dim)" }}>
        <span>{clock(frame)} / {clock(durationInFrames)}</span>
        <span>{now ? `${now.id} · ${now.kind}` : ""}</span>
      </div>
      <div style={{ position: "relative", height: 54, background: "var(--panel)", borderRadius: 4, overflow: "hidden" }}>
        {beats.bars.map((t, i) => (
          <div key={i} style={{ position: "absolute", left: pct(t * FPS), top: 0, bottom: 0, width: 1,
            background: i % 4 === 0 ? "rgba(255,255,255,0.12)" : "rgba(255,255,255,0.04)" }} />
        ))}
        {edit.segments.map((s) => {
          const a = barFrame(s.from);
          const b = barFrame(s.to);
          return (
            <button key={s.id} title={`${s.id} (${s.kind}): bars ${s.from}-${s.to}`} onClick={() => player.current?.seekTo(a)}
              style={{ position: "absolute", left: pct(a), width: pct(b - a), top: 8, height: 38, border: "1px solid #000",
                background: KIND_COLOR[s.kind] ?? "#444", opacity: s.kind === "placeholder" ? 0.55 : 0.9, cursor: "pointer", padding: 0 }} />
          );
        })}
        <div style={{ position: "absolute", left: pct(Math.round(beats.drop * FPS)), top: 0, bottom: 0, width: 2, background: "var(--glow)" }} />
        <div style={{ position: "absolute", left: pct(frame), top: 0, bottom: 0, width: 2, background: "#fff" }} />
      </div>
      <p style={{ color: "var(--faint)", fontSize: 13, marginTop: 10 }}>
        Dim blocks are footage still to record. The orange line is the drop. Edit edit/trailer.json; the page reloads.
      </p>
    </main>
  );
}
