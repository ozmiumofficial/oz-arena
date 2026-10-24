import { AbsoluteFill, Easing, interpolate, useCurrentFrame, useVideoConfig } from "remotion";
import { ClipRef } from "../lib/timeline";
import { color, font } from "../lib/theme";
import { Footage } from "./Footage";

/**
 * The title forming out of the menu's red smoke: a showcase capture blurred into pure mist behind
 * it. `sub` under the title (a fan project).
 */
export const Logo: React.FC<{ clip?: ClipRef; blur?: number; sub?: string }> = ({ clip, blur = 22, sub }) => {
  const frame = useCurrentFrame();
  const { durationInFrames } = useVideoConfig();
  const form = interpolate(frame, [0, 40], [0, 1], { extrapolateRight: "clamp", easing: Easing.out(Easing.cubic) });
  const out = interpolate(frame, [durationInFrames - 10, durationInFrames], [1, 0], { extrapolateLeft: "clamp" });
  const spacing = interpolate(form, [0, 1], [0.5, 0.06]);
  const glow = 0.6 + 0.4 * Math.sin(frame / 9);
  return (
    <AbsoluteFill style={{ backgroundColor: color.black }}>
      {clip && (
        <AbsoluteFill style={{ filter: `blur(${blur}px) brightness(0.75) saturate(1.3)`, transform: "scale(1.15)" }}>
          <Footage clip={clip} />
        </AbsoluteFill>
      )}
      <AbsoluteFill style={{ justifyContent: "center", alignItems: "center", textAlign: "center", opacity: Math.min(form, out) }}>
        <div style={{ fontFamily: font.head, fontWeight: font.headWeight, fontSize: 150, color: color.inkHot, letterSpacing: `${spacing}em`, filter: `blur(${(1 - form) * 10}px)`,
          textShadow: `0 0 ${30 + 20 * glow}px ${color.red}, 0 8px 40px rgba(0,0,0,0.9)` }}>
          QUAKE III ARENA
        </div>
        <div style={{ fontFamily: font.body, fontWeight: 700, fontSize: 62, color: color.glow, letterSpacing: `${0.2 + spacing}em`, marginTop: 6,
          textShadow: `0 0 24px ${color.red}` }}>
          REMASTERED
        </div>
        {sub && (
          <div style={{ fontFamily: font.body, fontWeight: 500, fontStyle: "italic", fontSize: 28, color: color.dim, marginTop: 26, letterSpacing: "0.1em" }}>{sub}</div>
        )}
      </AbsoluteFill>
    </AbsoluteFill>
  );
};
