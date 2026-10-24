import { AbsoluteFill, interpolate, useCurrentFrame, useVideoConfig } from "remotion";
import { ClipRef, HEIGHT, WIDTH } from "../lib/timeline";
import { color } from "../lib/theme";
import { Footage } from "./Footage";

/** A light CRT look over the 1999 footage: scanlines, a soft vignette, a little grain. */
export const Crt: React.FC<{ strength?: number }> = ({ strength = 1 }) => {
  const frame = useCurrentFrame();
  return (
    <AbsoluteFill style={{ opacity: strength, pointerEvents: "none" }}>
      <AbsoluteFill style={{ background: "repeating-linear-gradient(0deg, rgba(0,0,0,0.28) 0px, rgba(0,0,0,0.28) 1px, transparent 2px, transparent 4px)" }} />
      <AbsoluteFill style={{ background: "radial-gradient(ellipse at center, transparent 55%, rgba(0,0,0,0.55) 100%)" }} />
      <AbsoluteFill style={{ backgroundColor: `rgba(255,240,220,${0.02 + 0.015 * Math.sin(frame * 1.7)})`, mixBlendMode: "overlay" }} />
    </AbsoluteFill>
  );
};

/** The cold open: Quake 3 as it was, 4:3 inside black pillars, a CRT look, fading up from black. */
export const Opener: React.FC<{ clip: ClipRef }> = ({ clip }) => {
  const frame = useCurrentFrame();
  const { durationInFrames } = useVideoConfig();
  const fade = interpolate(frame, [0, 50], [0, 1], { extrapolateRight: "clamp" });
  const w43 = (HEIGHT * 4) / 3;
  const push = interpolate(frame, [0, durationInFrames], [1, 1.04]);
  return (
    <AbsoluteFill style={{ backgroundColor: color.black }}>
      {/* AbsoluteFill is width 100%, so the 4:3 window sets its own width */}
      <AbsoluteFill style={{ left: (WIDTH - w43) / 2, width: w43, opacity: fade, overflow: "hidden" }}>
        <AbsoluteFill style={{ transform: `scale(${push})`, filter: "saturate(0.85) contrast(1.06)" }}>
          <Footage clip={clip} />
        </AbsoluteFill>
        <Crt />
      </AbsoluteFill>
    </AbsoluteFill>
  );
};
