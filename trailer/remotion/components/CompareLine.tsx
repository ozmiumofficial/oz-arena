import { AbsoluteFill, Easing, interpolate, useCurrentFrame, useVideoConfig } from "remotion";
import { ClipRef, HEIGHT, WIDTH } from "../lib/timeline";
import { color, font } from "../lib/theme";
import { Footage } from "./Footage";
import { Crt } from "./Opener";

const TILT = (6 * Math.PI) / 180;   // a few degrees off vertical, so it reads as motion, not a split screen

/**
 * The comparison line (TRAILER_PLAN.md, CompareLine): both games' captures of the same camera path,
 * one over the other, split by a glowing line that sweeps across so the remaster takes the frame.
 * dir 1: the remaster on the right, the line moving right to left; -1 the mirror image.
 * widen: first opens Quake 3's 4:3 frame (with its CRT look) to 16:9.
 */
export const CompareLine: React.FC<{
  retail: ClipRef;
  remaster: ClipRef;
  dir?: 1 | -1;
  widen?: boolean;
  map?: { name: string; code: string };
}> = ({ retail, remaster, dir = 1, widen = false, map }) => {
  const frame = useCurrentFrame();
  const { durationInFrames } = useVideoConfig();
  const open = widen ? Math.round(durationInFrames * 0.18) : 0;   // frames spent widening
  // pillars of a 4:3 frame (240 px each side at 1080p) closing as the frame widens
  const pillar = widen ? interpolate(frame, [0, open], [(WIDTH - (HEIGHT * 4) / 3) / 2, 0], {
    extrapolateLeft: "clamp", extrapolateRight: "clamp", easing: Easing.inOut(Easing.cubic),
  }) : 0;
  const crt = widen ? interpolate(frame, [0, open], [1, 0], { extrapolateLeft: "clamp", extrapolateRight: "clamp" }) : 0;
  const s0 = open + durationInFrames * 0.06;
  const s1 = durationInFrames * 0.88;
  const [x0, x1] = dir === 1 ? [1.06, -0.06] : [-0.06, 1.06];
  const x = interpolate(frame, [s0, s1], [x0, x1], {
    extrapolateLeft: "clamp", extrapolateRight: "clamp", easing: Easing.inOut(Easing.cubic),
  }) * WIDTH;
  const dx = Math.tan(TILT) * HEIGHT / 2;
  const top = x + dx;
  const bottom = x - dx;
  const remasterClip = dir === 1
    ? `polygon(${top}px 0, ${WIDTH}px 0, ${WIDTH}px ${HEIGHT}px, ${bottom}px ${HEIGHT}px)`
    : `polygon(0 0, ${top}px 0, ${bottom}px ${HEIGHT}px, 0 ${HEIGHT}px)`;
  const labelsIn = interpolate(frame, [s0, s0 + 12, s1 - 12, s1], [0, 1, 1, 0], {
    extrapolateLeft: "clamp", extrapolateRight: "clamp",
  });
  const mapIn = interpolate(frame, [6, 20, durationInFrames - 14, durationInFrames], [0, 1, 1, 0], {
    extrapolateLeft: "clamp", extrapolateRight: "clamp",
  });
  const label: React.CSSProperties = {
    position: "absolute", top: 34, fontFamily: font.body, fontWeight: 700, fontSize: 26,
    letterSpacing: "0.32em", color: color.ink, textShadow: "0 2px 12px rgba(0,0,0,0.9)", opacity: labelsIn,
  };
  const retailLeft = dir === 1;
  return (
    <AbsoluteFill style={{ backgroundColor: color.black }}>
      <AbsoluteFill style={{ left: pillar, width: WIDTH - 2 * pillar, overflow: "hidden" }}>
        <Footage clip={retail} />
        {crt > 0 && <Crt strength={crt} />}
      </AbsoluteFill>
      <AbsoluteFill style={{ clipPath: remasterClip }}>
        <Footage clip={remaster} />
      </AbsoluteFill>
      {x > -40 && x < WIDTH + 40 && (
        <div
          style={{
            position: "absolute", left: x - 2, top: -HEIGHT * 0.1, width: 4, height: HEIGHT * 1.2,
            transform: `rotate(${(TILT * 180) / Math.PI}deg)`, transformOrigin: "50% 50%",
            background: `linear-gradient(${color.inkHot}, ${color.glow} 50%, ${color.inkHot})`,
            boxShadow: `0 0 12px 3px ${color.glow}, 0 0 42px 10px ${color.red}aa`,
          }}
        />
      )}
      <div style={{ ...label, left: retailLeft ? top - 40 : top + 40, transform: retailLeft ? "translateX(-100%)" : undefined }}>
        1999
      </div>
      <div style={{ ...label, left: retailLeft ? top + 40 : top - 40, transform: retailLeft ? undefined : "translateX(-100%)", color: color.inkHot }}>
        REMASTERED
      </div>
      {map && (
        <div style={{ position: "absolute", right: 80, bottom: 70, textAlign: "right", opacity: mapIn, textShadow: "0 3px 18px rgba(0,0,0,0.95)" }}>
          <div style={{ fontFamily: font.head, fontWeight: font.headWeight, fontSize: 64, color: color.inkHot, letterSpacing: "0.04em" }}>{map.name}</div>
          <div style={{ fontFamily: font.body, fontWeight: 600, fontSize: 24, color: color.glow, letterSpacing: "0.3em", marginTop: 6 }}>
            {map.code.toUpperCase()}
          </div>
        </div>
      )}
    </AbsoluteFill>
  );
};
